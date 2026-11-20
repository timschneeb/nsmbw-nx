#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <limits>
#include <string>
#include <filesystem>
#include <vector>

#include "../fs_helper.hpp"
#include "../internal.hpp"
#include "../sqlite_utils.hpp"
#include "gpu.hpp"

#include <sqlite3.h>
#include <fmt/format.h>
#if defined(AURORA_CACHE_USE_ZSTD)
#include <zstd.h>
#endif
#define XXH_STATIC_LINKING_ONLY
#include <xxhash.h>

namespace aurora::gfx {
// Defined in lib/gfx/pipeline_cache.cpp; the writable SDL-backed sqlite VFS.
const char* sdl_vfs_name();
bool register_sdl_vfs();
}  // namespace aurora::gfx

namespace aurora::webgpu {
static Module Log("aurora::gpu::cache");

static sqlite3* db;
static std::filesystem::path g_cachePath;
static sqlite3_stmt* load_stmt;
static sqlite3_stmt* store_stmt;
static sqlite3_stmt* touch_stmt;
static bool cache_broken;
static std::atomic_bool g_wasReset{false};
static std::mutex cache_mutex;
#if defined(AURORA_CACHE_USE_ZSTD)
static std::vector<uint8_t> compress_buffer;
#endif

// Dawn calls the store callback once per blob, several times per pipeline. A transaction per blob
// made the boot prewarm SD-bound: with a rollback journal each commit creates, fsyncs and deletes
// a journal file on the card. Buffer the rows, like the pipeline cache writer does for recipes, and
// commit them in batches.
constexpr size_t StoreBatchRows = 64;
constexpr size_t StoreBatchBytes = 8u << 20;
constexpr auto StoreBatchAge = std::chrono::milliseconds(500);
struct PendingStore {
  XXH128_hash_t key;
  std::vector<uint8_t> value;
  size_t valueSize;
  int compressed;
  int64_t day;
};
static std::vector<PendingStore> g_pendingStores;
static size_t g_pendingStoreBytes = 0;
static std::chrono::steady_clock::time_point g_pendingStoreSince{};

// Caller holds cache_mutex. Defined below; the load path flushes an overdue batch.
static void flush_stores();

// Schema 3 added last_used (whole days since the Unix epoch) so stale blobs can be
// pruned: config-version bumps and driver updates change every Dawn cache key, and
// without an age column the orphaned rows accumulate forever (observed 1.6 GB).
// Schema 4 is a content reset for the vulkan_monolithic_pipeline_cache switch, which
// obsoletes every per-pipeline blob at once.
constexpr int CACHE_SCHEMA = 4;
constexpr int64_t PruneAfterDays = 30;
// Rows whose last_used lags today are collected in memory and written in one batch at
// shutdown: the load path sits inside a read transaction that is always rolled back,
// and day granularity makes anything more eager pointless.

static std::atomic<uint64_t> g_lookups{0};
static std::atomic<uint64_t> g_hits{0};
static std::atomic<uint64_t> g_stores{0};
static std::atomic<uint64_t> g_hitBytes{0};
static std::vector<XXH128_hash_t> g_pendingTouches;

static int64_t days_now() { return static_cast<int64_t>(std::time(nullptr) / 86400); }

static void init_abort() {
  cache_broken = true;
  sqlite3_close(db);
  db = nullptr;
}

static int check(int ret) {
  if (ret != SQLITE_OK) {
    Log.error("SQLite operation failed: {}", sqlite3_errmsg(db));
  }

  return ret;
}

enum class SchemaState { Match, Mismatch, Error };

static SchemaState check_schema() {
  auto ret = sqlite::exec(db, "CREATE TABLE IF NOT EXISTS aurora_schema(value INTEGER);");
  if (ret != SQLITE_OK) {
    Log.error("Failed to create schema table: {}", sqlite3_errmsg(db));
    return SchemaState::Error;
  }

  bool match = false;
  const auto cmd = fmt::format("SELECT * FROM aurora_schema WHERE value = {}", CACHE_SCHEMA);
  ret = sqlite::exec(db, cmd.c_str(), [&match](int, char**, char**) { match = true; }, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to check schema table: {}", sqlite3_errmsg(db));
    return SchemaState::Error;
  }
  return match ? SchemaState::Match : SchemaState::Mismatch;
}

static bool create_schema() {
  sqlite::Transaction tx(db, Log, true);
  if (!tx) {
    Log.error("Failed to open schema transaction: {}", sqlite3_errmsg(db));
    return false;
  }
  const auto cmd = fmt::format(
      R"(CREATE TABLE IF NOT EXISTS aurora_schema(value INTEGER);
DROP TABLE IF EXISTS cache;
CREATE TABLE cache (
  key BLOB PRIMARY KEY NOT NULL,
  value BLOB NOT NULL,
  size INTEGER NOT NULL,
  compressed INTEGER NOT NULL,
  last_used INTEGER NOT NULL DEFAULT 0
);
DELETE FROM aurora_schema;
INSERT INTO aurora_schema VALUES ({});)",
      CACHE_SCHEMA);
  const auto ret = sqlite::exec(db, cmd.c_str());
  if (ret != SQLITE_OK) {
    Log.error("Failed to create schema: {}", sqlite3_errmsg(db));
    return false;
  }
  tx.commit();
  return true;
}

static void prune_stale_rows() {
  const auto cmd = fmt::format("DELETE FROM cache WHERE last_used < {}", days_now() - PruneAfterDays);
  auto ret = sqlite::exec(db, cmd.c_str());
  if (ret != SQLITE_OK) {
    Log.error("Failed to prune stale cache rows: {}", sqlite3_errmsg(db));
    return;
  }
  const auto pruned = sqlite3_changes(db);
  if (pruned > 0) {
    Log.info("Pruned {} stale Dawn cache blobs (unused for {}+ days)", pruned, PruneAfterDays);
  }
  // Freed pages are only reused, never returned to the filesystem, so compact when a
  // meaningful amount was dropped. Blobs run hundreds of KB each, making even a few
  // hundred rows a noticeable slice of the file.
  if (pruned > 256) {
    ret = sqlite::exec(db, "VACUUM;");
    if (ret != SQLITE_OK) {
      Log.warn("Failed to vacuum Dawn cache after pruning: {}", sqlite3_errmsg(db));
    }
  }
}

static std::filesystem::path cache_path() {
  return fs_path_from_string(g_config.cachePath) / "dawn_cache.db";
}

static bool open_cache_db(const std::string& file) {
  aurora::gfx::register_sdl_vfs();
  const auto ret = sqlite3_open_v2(file.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
                                   aurora::gfx::sdl_vfs_name());
  if (ret != SQLITE_OK) {
    Log.error("Failed to open database: {}", sqlite3_errmsg(db));
    if (db != nullptr) {
      sqlite3_close(db);
      db = nullptr;
    }
    return false;
  }
  if (sqlite::exec(db, "PRAGMA journal_mode=DELETE; PRAGMA synchronous=NORMAL;") != SQLITE_OK) {
    Log.error("Failed to set pragmas: {}", sqlite3_errmsg(db));
    sqlite3_close(db);
    db = nullptr;
    return false;
  }
  return true;
}

static bool cache_integrity_ok() {
  bool ok = false;
  const auto ret = sqlite::exec(
      db, "PRAGMA quick_check;",
      [&ok](int argc, char** argv, char**) {
        if (argc > 0 && argv[0] != nullptr && std::strcmp(argv[0], "ok") == 0) {
          ok = true;
        }
      },
      nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Dawn cache integrity check failed: {}", sqlite3_errmsg(db));
  }
  return ret == SQLITE_OK && ok;
}

static void remove_cache_files(const std::string& path) {
  std::remove(path.c_str());
  for (const char* suffix : {"-journal", "-wal", "-shm"}) {
    std::remove((path + suffix).c_str());
  }
}

// Journal-only recovery: journal_mode=DELETE with synchronous=NORMAL already rolls back a crash
// mid-write, so a failed open or quick_check means the file itself is unusable. Delete it and
// start from an empty schema; the caller then forces a blocking shader rebuild.
static bool recover_cache() {
  const std::string path = cache_path().string();
  if (db != nullptr) {
    sqlite3_close(db);
    db = nullptr;
  }
  remove_cache_files(path);
  if (!open_cache_db(path)) {
    return false;
  }
  if (!create_schema()) {
    return false;
  }
  g_wasReset.store(true, std::memory_order_relaxed);
  Log.warn("Dawn cache was corrupt; deleted and recreated empty");
  return true;
}

// Older builds kept a 54 MB .bak and a .corrupt copy beside the database. Recovery no longer
// reads them, so reclaim whatever an older install left behind.
static void remove_legacy_backups(const std::string& path) {
  for (const char* suffix : {".bak", ".bak.tmp", ".corrupt"}) {
    const std::string file = path + suffix;
    if (std::remove(file.c_str()) == 0) {
      Log.info("Dawn cache: removed legacy {}", file);
    }
  }
}

static bool cache_init_core() {
  Log.debug("SQLite version {}", sqlite3_libversion());

  g_cachePath = cache_path();
  std::string file = fs_path_to_string(g_cachePath);
  Log.debug("Using dawn cache at {}", file);

  if (!open_cache_db(file)) {
    Log.error("Dawn cache open failed; attempting recovery");
    if (!recover_cache()) {
      return false;
    }
  } else if (!cache_integrity_ok()) {
    Log.error("Dawn cache is malformed; attempting recovery");
    if (!recover_cache()) {
      return false;
    }
  }

  Log.info("Dawn cache: checking schema");
  switch (check_schema()) {
  case SchemaState::Match:
    Log.info("Dawn cache: pruning stale rows");
    prune_stale_rows();
    Log.info("Dawn cache: prune done");
    break;
  case SchemaState::Mismatch: {
    // Dropping the table would leave the freed pages inside the file, so a schema
    // change deletes the database outright; pre-schema-3 files had grown unbounded.
    Log.info("Dawn cache schema changed; recreating '{}'", file);
    sqlite3_close(db);
    db = nullptr;
    remove_cache_files(g_cachePath.string());
    if (!open_cache_db(file)) {
      Log.error("Failed to recreate database");
      return false;
    }
    if (!create_schema()) {
      return false;
    }
    break;
  }
  case SchemaState::Error:
    return false;
  }

  Log.info("Dawn cache: schema ok, preparing statements");
  int ret = sqlite3_prepare_v3(db, "SELECT value, size, compressed, last_used FROM cache WHERE key = ?", -1,
                               SQLITE_PREPARE_PERSISTENT, &load_stmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare statement: {}", sqlite3_errmsg(db));
    return false;
  }

  ret = sqlite3_prepare_v3(db, "REPLACE INTO cache (key, value, size, compressed, last_used) VALUES (?, ?, ?, ?, ?)",
                           -1, SQLITE_PREPARE_PERSISTENT, &store_stmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare statement: {}", sqlite3_errmsg(db));
    return false;
  }

  ret = sqlite3_prepare_v3(db, "UPDATE cache SET last_used = ? WHERE key = ?", -1, SQLITE_PREPARE_PERSISTENT,
                           &touch_stmt, nullptr);
  if (ret != SQLITE_OK) {
    Log.error("Failed to prepare statement: {}", sqlite3_errmsg(db));
    return false;
  }

  remove_legacy_backups(file);
  return true;
}

// Caller holds cache_mutex. Writes the queued last_used refreshes in one transaction.
static void flush_touches() {
  if (g_pendingTouches.empty() || db == nullptr || touch_stmt == nullptr) {
    return;
  }

  sqlite::Transaction tx(db, Log, true);
  if (!tx) {
    Log.error("Failed to open touch transaction");
    g_pendingTouches.clear();
    return;
  }
  const auto today = days_now();
  for (const auto& keyHash : g_pendingTouches) {
    check(sqlite3_bind_int64(touch_stmt, 1, today));
    check(sqlite3_bind_blob(touch_stmt, 2, &keyHash, sizeof(keyHash), SQLITE_TRANSIENT));
    if (sqlite3_step(touch_stmt) != SQLITE_DONE) {
      Log.error("Failed to refresh cache row age: {}", sqlite3_errmsg(db));
    }
    check(sqlite3_reset(touch_stmt));
  }
  g_pendingTouches.clear();
  tx.commit();
}

static bool cache_init() {
  if (cache_broken) {
    return false;
  }

  if (db) {
    return true;
  }

  if (!cache_init_core()) {
    Log.error("SQLite DB init failed");
    init_abort();
    return false;
  }

  Log.info("Dawn cache ready");

  return true;
}

size_t load_from_cache(void const* key, size_t keySize, void* value, size_t valueSize, void*) {
  std::lock_guard lock(cache_mutex);

  if (!cache_init()) {
    return 0;
  }

  // Do not let a partial batch sit in memory for a whole session.
  if (!g_pendingStores.empty() && std::chrono::steady_clock::now() - g_pendingStoreSince >= StoreBatchAge) {
    flush_stores();
  }

  sqlite::Transaction tx(db, Log);
  if (!tx) {
    Log.error("Failed to open load transaction");
    return 0;
  }

  // Dawn probes with value == nullptr for the size first, then fetches; count each
  // probe as one logical lookup so the hit rate reads per-entry.
  if (value == nullptr) {
    g_lookups.fetch_add(1, std::memory_order_relaxed);
  }

  const auto keyHash = XXH128(key, keySize, 0);
  check(sqlite3_bind_blob(load_stmt, 1, &keyHash, sizeof(keyHash), SQLITE_TRANSIENT));

  const auto ret = sqlite3_step(load_stmt);
  size_t foundSize;
  if (ret == SQLITE_ROW) {
    // Hit
    const auto foundPtr = sqlite3_column_blob(load_stmt, 0);
    const auto declaredSize = sqlite3_column_int64(load_stmt, 1);
    const auto storedSize = sqlite3_column_bytes(load_stmt, 0);
    const auto compression = sqlite3_column_int(load_stmt, 2);
    const bool compressed = compression == 1;
    // Dawn asks for the size before allocating its destination. Validate here,
    // not only during the copy: corrupt metadata must become a cache miss.
    bool valid = declaredSize > 0 &&
                 static_cast<uint64_t>(declaredSize) <= std::numeric_limits<size_t>::max() &&
                 foundPtr != nullptr && storedSize > 0 && (compression == 0 || compression == 1);
    if (valid && compressed) {
#if defined(AURORA_CACHE_USE_ZSTD)
      // Our writer uses ZSTD_compress, which records the original content size.
      const auto frameSize = ZSTD_getFrameContentSize(foundPtr, static_cast<size_t>(storedSize));
      valid = frameSize != ZSTD_CONTENTSIZE_ERROR && frameSize != ZSTD_CONTENTSIZE_UNKNOWN &&
              frameSize == static_cast<uint64_t>(declaredSize);
#else
      valid = false;
#endif
    } else if (valid) {
      valid = declaredSize == storedSize;
    }
    if (!valid) {
      Log.error("Ignoring cache entry with inconsistent size or compression metadata");
      check(sqlite3_reset(load_stmt));
      return 0;
    }
    foundSize = static_cast<size_t>(declaredSize);
    if (value == nullptr) {
      g_hits.fetch_add(1, std::memory_order_relaxed);
    } else {
      g_hitBytes.fetch_add(static_cast<uint64_t>(foundSize), std::memory_order_relaxed);
    }
    if (sqlite3_column_int64(load_stmt, 3) != days_now()) {
      g_pendingTouches.push_back(keyHash);
    }

    if (value && valueSize == foundSize) {
      if (compressed) {
#if defined(AURORA_CACHE_USE_ZSTD)
        const auto compSize = sqlite3_column_bytes(load_stmt, 0);
        const auto zstdRet = ZSTD_decompress(value, valueSize, foundPtr, compSize);
        if (ZSTD_isError(zstdRet)) {
          Log.error("zstd decompression error: {}", ZSTD_getErrorName(zstdRet));
          foundSize = 0;
        } else if (zstdRet != foundSize) {
          Log.error("zstd decompression size mismatch: expected {}, got {}", foundSize, zstdRet);
          foundSize = 0;
        }
#else
        Log.error("Cache entry is zstd-compressed but zstd support is disabled");
        foundSize = 0;
#endif
      } else {
        if (foundSize != 0 && !foundPtr) {
          Log.error("Cache entry is missing raw value data");
          foundSize = 0;
        } else if (foundSize != 0) {
          std::memcpy(value, foundPtr, foundSize);
        }
      }
    }
  } else if (ret == SQLITE_DONE) {
    // Miss
    foundSize = 0;
  } else {
    Log.error("Looking up cache key failed: {}", sqlite3_errmsg(db));
    return 0;
  }

  check(sqlite3_reset(load_stmt));

  return foundSize;
}

// Caller holds cache_mutex. Writes every buffered blob in one transaction.
static void flush_stores() {
  if (g_pendingStores.empty()) {
    return;
  }
  std::vector<PendingStore> pending;
  pending.swap(g_pendingStores);
  g_pendingStoreBytes = 0;
  g_pendingStoreSince = {};
  if (db == nullptr || store_stmt == nullptr) {
    return;
  }

  sqlite::Transaction tx(db, Log, true);
  if (!tx) {
    Log.error("Failed to open store transaction");
    return;
  }

  for (const auto& entry : pending) {
    check(sqlite3_bind_blob64(store_stmt, 1, &entry.key, sizeof(entry.key), SQLITE_TRANSIENT));
    check(sqlite3_bind_blob64(store_stmt, 2, entry.value.data(), entry.value.size(), SQLITE_TRANSIENT));
    check(sqlite3_bind_int64(store_stmt, 3, static_cast<sqlite3_int64>(entry.valueSize)));
    check(sqlite3_bind_int(store_stmt, 4, entry.compressed));
    check(sqlite3_bind_int64(store_stmt, 5, entry.day));
    if (sqlite3_step(store_stmt) != SQLITE_DONE) {
      Log.error("Failed to insert row: {}", sqlite3_errmsg(db));
      check(sqlite3_reset(store_stmt));
      continue;
    }
    check(sqlite3_reset(store_stmt));
  }

  tx.commit();
}

void store_to_cache(void const* key, size_t keySize, void const* value, size_t valueSize, void*) {
  std::lock_guard lock(cache_mutex);

  if (!cache_init()) {
    return;
  }

  PendingStore entry{
      .key = XXH128(key, keySize, 0),
      .valueSize = valueSize,
      .compressed = 0,
      .day = days_now(),
  };
#if defined(AURORA_CACHE_USE_ZSTD)
  const auto bound = ZSTD_compressBound(valueSize);
  if (ZSTD_isError(bound)) {
    Log.error("Failed to calculate ZSTD_compressBound: {}", ZSTD_getErrorName(bound));
    return;
  }

  if (compress_buffer.size() < bound) {
    compress_buffer.resize(bound);
  }

  const auto compressRet = ZSTD_compress(compress_buffer.data(), compress_buffer.size(), value, valueSize, 0);
  if (ZSTD_isError(compressRet)) {
    Log.error("ZSTD compression error: {}", ZSTD_getErrorName(compressRet));
    return;
  }

  if (compressRet < valueSize) {
    entry.value.assign(compress_buffer.begin(), compress_buffer.begin() + static_cast<ptrdiff_t>(compressRet));
    entry.compressed = 1;
  } else {
    const auto* bytes = static_cast<const uint8_t*>(value);
    entry.value.assign(bytes, bytes + valueSize);
  }
#else
  const auto* bytes = static_cast<const uint8_t*>(value);
  entry.value.assign(bytes, bytes + valueSize);
#endif

  g_stores.fetch_add(1, std::memory_order_relaxed);
  g_pendingStoreBytes += entry.value.size();
  const auto now = std::chrono::steady_clock::now();
  if (g_pendingStores.empty()) {
    g_pendingStoreSince = now;
  }
  g_pendingStores.emplace_back(std::move(entry));
  if (g_pendingStores.size() >= StoreBatchRows || g_pendingStoreBytes >= StoreBatchBytes ||
      now - g_pendingStoreSince >= StoreBatchAge) {
    flush_stores();
  }
}

void cache_shutdown() {
  std::lock_guard lock(cache_mutex);
#if defined(AURORA_CACHE_USE_ZSTD)
  compress_buffer.clear();
#endif
  flush_stores();
  flush_touches();
  check(sqlite3_finalize(load_stmt));
  check(sqlite3_finalize(store_stmt));
  check(sqlite3_finalize(touch_stmt));
  load_stmt = nullptr;
  store_stmt = nullptr;
  touch_stmt = nullptr;
  check(sqlite3_close(db));
  db = nullptr;
}

bool dawn_cache_was_reset() noexcept { return g_wasReset.load(std::memory_order_relaxed); }

BlobCacheStats blob_cache_stats() noexcept {
  return BlobCacheStats{
      .lookups = g_lookups.load(std::memory_order_relaxed),
      .hits = g_hits.load(std::memory_order_relaxed),
      .stores = g_stores.load(std::memory_order_relaxed),
      .hitBytes = g_hitBytes.load(std::memory_order_relaxed),
  };
}

} // namespace aurora::webgpu
