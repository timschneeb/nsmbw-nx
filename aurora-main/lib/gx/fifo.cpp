#include "fifo.hpp"
#include "command_processor.hpp"
#include "../gfx/common.hpp"
#include "../internal.hpp"
#include "../thread_utils.hpp"
#include "../webgpu/gpu.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "tracy/Tracy.hpp"

#include <aurora/switch_hooks.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(__linux__)
#include <pthread.h>
#endif
namespace aurora::gx::fifo {
static Module Log("aurora::gx::fifo");

namespace detail {
uint8_t* sBufferData = nullptr;
uint32_t sBufferSize = 0;
uint32_t sBufferCapacity = 0;
// NSMBW: the producer's command buffer. hand_off() swaps ownership with a decoded job buffer
// instead of copying it; a background display list is hundreds of KiB and the copy cost 0.5 ms a
// frame.
std::unique_ptr<uint8_t[]> sProducerOwned;
bool sInDisplayList = false;
uint8_t* sDlBuffer = nullptr;
uint32_t sDlSize = 0;
uint32_t sDlWritePos = 0;
} // namespace detail

void init() {
  constexpr uint32_t initialCapacity = 64 * 1024;
  reset_cp_register_cache();
  detail::sProducerOwned = std::unique_ptr<uint8_t[]>(new uint8_t[initialCapacity]);
  detail::sBufferData = detail::sProducerOwned.get();
  detail::sBufferSize = 0;
  detail::sBufferCapacity = initialCapacity;
  detail::sInDisplayList = false;
  detail::sDlBuffer = nullptr;
  detail::sDlSize = 0;
  detail::sDlWritePos = 0;
}

void write_data_grow(const void* data, uint32_t length) {
  const uint32_t needed = detail::sBufferSize + length;
  const uint32_t newCap = std::max(detail::sBufferCapacity * 2, needed);
  auto grown = std::unique_ptr<uint8_t[]>(new uint8_t[newCap]);
  std::memcpy(grown.get(), detail::sBufferData, detail::sBufferSize);
  detail::sProducerOwned = std::move(grown);
  detail::sBufferData = detail::sProducerOwned.get();
  detail::sBufferCapacity = newCap;
  std::memcpy(detail::sBufferData + detail::sBufferSize, data, length);
  detail::sBufferSize = needed;
}

void begin_display_list(uint8_t* buf, uint32_t size) {
  detail::sInDisplayList = true;
  detail::sDlBuffer = buf;
  detail::sDlSize = size;
  detail::sDlWritePos = 0;
}

uint32_t end_display_list() {
  detail::sInDisplayList = false;
  uint32_t bytesWritten = detail::sDlWritePos;
  uint32_t padded = (bytesWritten + 31) & ~31u;
  while (detail::sDlWritePos < padded && detail::sDlWritePos < detail::sDlSize) {
    detail::sDlBuffer[detail::sDlWritePos++] = 0;
  }
  detail::sDlBuffer = nullptr;
  detail::sDlSize = 0;
  detail::sDlWritePos = 0;
  return padded;
}

// How much of the producer's frame is spent blocked before it may decode the next batch of GX commands.
std::atomic<uint64_t> g_statSealedWaitNanos{0};
std::atomic<uint64_t> g_statSyncWaitNanos{0};
// Time this thread spent inside the command decoder. Two clock reads per handed-off buffer, so it
// stays on regardless of the stats mode and shows how loaded the decode core is.
std::atomic<uint64_t> g_statDecodeNanos{0};

static void note_drain_wait(uint64_t nanos) noexcept {
  ZoneScopedN("FIFO drain wait");
  TracyPlot("aurora: fifoDrainWaitUs", static_cast<int64_t>(nanos / 1000));
  g_statSealedWaitNanos.fetch_add(nanos, std::memory_order_relaxed);
}

// Decodes a whole command buffer, splitting the staging batch whenever it fills.
static void process_buffer(const uint8_t* data, uint32_t size) {
  // SEALED, not DONE.
  const auto waited = aurora::wait_for_frame_worker_sealed();
  if (waited.count() > 0) UNLIKELY {
    note_drain_wait(static_cast<uint64_t>(waited.count()));
  }
  const auto decodeStart = std::chrono::steady_clock::now();
  uint32_t consumed = 0;
  bool retried = false;
  while (consumed < size) {
    const auto count = process(data + consumed, size - consumed, true);
    if (count == 0 && retried)
      throw gfx::StagingCapacityError("FIFO draw does not fit after capacity submission");
    consumed += count;
    if (consumed == size) break;
    // process returned with its renderer lock released. No recursive drain.
    gfx::split_staging_batch();
    retried = true;
  }
  g_statDecodeNanos.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - decodeStart).count()), std::memory_order_relaxed);
}

// NSMBW: the GX thread (see threaded()). Buffers of commands are decoded on it in the order the
// game thread queued them; the game thread waits for it only where it needs decoded state.
namespace {
// Queue a buffer once this much has built up. The guest's frame ends with a drain that waits
// for the decode of everything still queued, so this is the size of the serial tail: smaller
// batches decode earlier and in parallel with the producer. NSMBW_GX_HANDOFF_KB overrides it
// (0 disables the size gate and hands off at every flush point).
uint32_t handOffBytes() {
  static const uint32_t bytes = [] {
    const char* value = std::getenv("NSMBW_GX_HANDOFF_KB");
    if (value == nullptr || value[0] == '\0') {
      return 4u * 1024u;
    }
    const long parsed = std::strtol(value, nullptr, 10);
    if (parsed <= 0) {
      return 0u;
    }
    return static_cast<uint32_t>(std::min<long>(parsed * 1024, 1024 * 1024));
  }();
  return bytes;
}

struct FifoJob {
  std::unique_ptr<uint8_t[]> data;
  uint32_t size = 0;
  uint32_t capacity = 0;
};

struct GxThread {
  std::thread thread;
  std::mutex mutex;
  std::condition_variable work;
  std::condition_variable idle;
  std::deque<FifoJob> jobs;
  std::vector<FifoJob> spare;
  bool busy = false;
};

GxThread& gx_thread() {
  static GxThread* t = [] {
    auto* state = new GxThread();
    state->thread = std::thread([state] {
      // Decode and encode are ~10 ms each on the Switch: on one core they serialize. The frame
      // worker keeps core 1; decode shares core 2 with audio (~1-2 ms) unless NSMBW_GX_CORE says
      // otherwise.
#if defined(MKW_PLATFORM_SWITCH)
      // Switch: the runtime picks the core and the priority (main.cpp), because it knows which
      // core the guest owns.
      MkwConfigureGxWorkerThread();
#else
      uint32_t core = 2;
      if (const char* value = std::getenv("NSMBW_GX_CORE")) {
        const int parsed = std::atoi(value);
        if (parsed >= 0 && parsed <= 2) {
          core = static_cast<uint32_t>(parsed);
        }
      }
      if (!aurora::pin_current_thread(core)) {
        Log.warn("GX thread could not be pinned to core {}", core);
      }
      // Named for profilers (NSMBW_PROFILE_THREAD=GXThread). SetThreadDescription is looked up,
      // as Windows 10 1607 brought it.
#if defined(_WIN32)
      using SetDescription = HRESULT(WINAPI*)(HANDLE, PCWSTR);
      if (const auto set = reinterpret_cast<SetDescription>(reinterpret_cast<void*>(
              GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")))) {
        set(GetCurrentThread(), L"GXThread");
      }
#elif defined(__linux__)
      pthread_setname_np(pthread_self(), "GXThread");
#endif
#endif
      for (;;) {
        FifoJob job;
        {
          std::unique_lock lock(state->mutex);
          state->work.wait(lock, [state] { return !state->jobs.empty(); });
          job = std::move(state->jobs.front());
          state->jobs.pop_front();
          state->busy = true;
        }
        process_buffer(job.data.get(), job.size);
        {
          std::lock_guard lock(state->mutex);
          state->busy = false;
          job.size = 0;
          state->spare.push_back(std::move(job));
          if (state->jobs.empty()) {
            state->idle.notify_all();
          }
        }
      }
    });
    state->thread.detach();
    Log.info("GX thread started ({} hardware threads), hand-off {} KiB",
             aurora::hardware_thread_count(), handOffBytes() / 1024u);
    return state;
  }();
  return *t;
}

void hand_off() {
  if (detail::sBufferSize == 0) {
    return;
  }
  auto& t = gx_thread();
  FifoJob next;
  {
    std::lock_guard lock(t.mutex);
    if (!t.spare.empty()) {
      next = std::move(t.spare.back());
      t.spare.pop_back();
    }
  }
  if (!next.data) {
    next.data = std::unique_ptr<uint8_t[]>(new uint8_t[64 * 1024]);
    next.capacity = 64 * 1024;
  }
  FifoJob job{std::move(detail::sProducerOwned), detail::sBufferSize, detail::sBufferCapacity};
  detail::sProducerOwned = std::move(next.data);
  detail::sBufferData = detail::sProducerOwned.get();
  detail::sBufferCapacity = next.capacity;
  detail::sBufferSize = 0;
  {
    std::lock_guard lock(t.mutex);
    t.jobs.push_back(std::move(job));
  }
  t.work.notify_one();
}
} // namespace

namespace {
bool threaded_for(wgpu::BackendType backend) {
  // On by default when there's a core to spare for it; NSMBW_GX_THREAD=0 or 1 forces it.
  // Two hardware threads count: on a two-core Celeron N2840 (Bay Trail) the game thread ran 8-15%
  // faster in courses and on the world map with it, and menus up to 30%, though there it shares its
  // core with the frame worker and the audio mix.
  static const bool wanted = [] {
    const char* value = std::getenv("NSMBW_GX_THREAD");
    if (value != nullptr && value[0] != '\0') {
      return value[0] == '1';
    }
#if defined(MKW_PLATFORM_SWITCH)
    return true;
#else
    return std::thread::hardware_concurrency() >= 2;
#endif
  }();
  // Never on OpenGL (ES): its context belongs to one thread, so that renderer runs single-threaded.
  // Off until the renderer is up, too; turning on later is safe, as nothing is left queued then.
  return wanted && backend != wgpu::BackendType::Undefined && backend != wgpu::BackendType::Null &&
         backend != wgpu::BackendType::OpenGL && backend != wgpu::BackendType::OpenGLES;
}

} // namespace

namespace detail {
// The answer for the backend it was last worked out for: (backend << 1) | on. Read inline from
// fifo.hpp; a backend change (a renderer re-created after a lost device) works it out again.
std::atomic<uint32_t> sThreadedFor{UINT32_MAX};
} // namespace detail

bool threaded_slow(wgpu::BackendType backend) {
  const auto backendValue = static_cast<uint32_t>(backend);
  const bool on = threaded_for(backend);
  detail::sThreadedFor.store((backendValue << 1) | (on ? 1u : 0u), std::memory_order_relaxed);
  return on;
}

bool async_copies() {
  // On with the GX thread on 4+ hardware threads, where it has a core of its own. Below that the
  // game thread decodes the tail itself whenever the GX thread is idle (sync_if_threaded), and a
  // two-core CPU's frame worker is the critical path, so copies stay as they were there.
  // NSMBW_ASYNC_COPY=0 or 1 forces it (still only with the GX thread).
  static const bool wanted = [] {
    const char* value = std::getenv("NSMBW_ASYNC_COPY");
    if (value != nullptr && value[0] != '\0') {
      return value[0] == '1';
    }
#if defined(MKW_PLATFORM_SWITCH)
    // libstdc++ reports 0 hardware threads on Switch, so the old rule left this off. The GX
    // thread has its own core there, and queuing the copy keeps the game thread moving.
    return true;
#else
    return std::thread::hardware_concurrency() >= 4;
#endif
  }();
  return wanted && threaded();
}

void flush_async() {
  if (!threaded()) {
    drain();
    return;
  }
  const uint32_t threshold = handOffBytes();
  if (detail::sBufferSize != 0 && (threshold == 0 || detail::sBufferSize >= threshold)) {
    hand_off();
  }
}

void sync_if_threaded() {
  if (!threaded()) {
    return;
  }
  auto& t = gx_thread();
  // With fewer than four hardware threads the GX thread shares a core with the frame worker and the
  // audio mix, and waking it for the last few commands, then being woken back, cost more than those
  // commands: on a two-core Celeron N2840 a third of the game thread's time went to these waits
  // while the GX thread sat idle. So when it has nothing queued and nothing in hand, decode the rest
  // here, in order, as the single-threaded path does. NSMBW_GX_INLINE_SYNC=0 or 1 forces it.
  static const bool inlineWhenIdle = [] {
    const char* value = std::getenv("NSMBW_GX_INLINE_SYNC");
    if (value != nullptr && value[0] != '\0') {
      return value[0] == '1';
    }
#if defined(MKW_PLATFORM_SWITCH)
    // libstdc++ reports 0 hardware threads on Switch, so the old rule moved the GX tail back onto
    // the game thread. The game thread is the critical path there; keep the decode on the worker.
    return false;
#else
    return std::thread::hardware_concurrency() < 4;
#endif
  }();
  if (inlineWhenIdle) {
    std::unique_lock lock(t.mutex);
    if (t.jobs.empty() && !t.busy) {
      lock.unlock();  // only this thread queues jobs, so the GX thread stays idle meanwhile
      if (detail::sBufferSize != 0) {
        process_buffer(detail::sBufferData, detail::sBufferSize);
        detail::sBufferSize = 0;
      }
      return;
    }
  }
  hand_off();
  const auto waitStart = std::chrono::steady_clock::now();
  std::unique_lock lock(t.mutex);
  t.idle.wait(lock, [&t] { return t.jobs.empty() && !t.busy; });
  g_statSyncWaitNanos.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now() - waitStart).count()), std::memory_order_relaxed);
}

void drain() {
  if (threaded()) {
    sync_if_threaded();
    return;
  }
  if (detail::sBufferSize == 0) {
    // Still waits for the frame worker to seal, as it always has.
    process_buffer(nullptr, 0);
    return;
  }
  process_buffer(detail::sBufferData, detail::sBufferSize);
  detail::sBufferSize = 0;
}

const uint8_t* get_buffer_data() { return detail::sBufferData; }
uint32_t get_buffer_size() { return detail::sBufferSize; }
void clear_buffer() {
  detail::sBufferSize = 0;
}

} // namespace aurora::gx::fifo

extern "C" void AuroraTakeFifoWaits(uint64_t* sealedWait, uint64_t* syncWait) {
  *sealedWait = aurora::gx::fifo::g_statSealedWaitNanos.exchange(0, std::memory_order_relaxed);
  *syncWait = aurora::gx::fifo::g_statSyncWaitNanos.exchange(0, std::memory_order_relaxed);
}

extern "C" uint64_t AuroraTakeFifoDecodeNanos() {
  return aurora::gx::fifo::g_statDecodeNanos.exchange(0, std::memory_order_relaxed);
}
