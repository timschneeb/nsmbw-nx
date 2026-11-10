#include "wii_disc.h"

#include <cryptopp/aes.h>
#include <cryptopp/cpu.h>
#include <cryptopp/modes.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <vector>

#if defined(__has_include)
#if __has_include(<bzlib.h>)
#include <bzlib.h>
#define WiiDisc_HAVE_BZIP2 1
#endif
#if __has_include(<zstd.h>)
#include <zstd.h>
#define WiiDisc_HAVE_ZSTD 1
#endif
#endif

#include "Lzma2Dec.h"
#include "LzmaDec.h"

namespace WiiDisc {
namespace {

namespace fs = std::filesystem;

constexpr uint32_t kWiiMagic = 0x5D1C9EA3;
constexpr uint32_t kGameCubeMagic = 0xC2339F3D;
constexpr uint64_t kCluster = 0x8000;      // on the disc: 0x400 of hashes, then the data
constexpr uint64_t kClusterData = 0x7C00;
constexpr uint64_t kBatch = 64;            // clusters read and decrypted at a time (2 MiB)
constexpr size_t kChunk = 4 << 20;         // bytes written at a time

// The Wii's common keys, as WiiDisc.java and Dolphin hold them: a partition's title key is encrypted
// with one of these, as on the console.
constexpr uint8_t kCommonKeys[2][16] = {
    {0xeb, 0xe4, 0x2a, 0x22, 0x5e, 0x85, 0x93, 0xe4, 0x48, 0xd9, 0xc5, 0x45, 0x73, 0x81, 0xaa, 0xf7}, // retail
    {0x63, 0xb8, 0x2b, 0xb4, 0xf4, 0x61, 0x4e, 0x2e, 0x13, 0xf2, 0xfe, 0xfb, 0xba, 0x4c, 0x9b, 0x7e}, // Korean
};

const char* const kConvert = ". Dolphin can turn it into an .iso: right-click the game, Convert File..., Format: ISO";

uint32_t Be32(const uint8_t* b) {
    return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | uint32_t(b[3]);
}

uint16_t Be16(const uint8_t* b) {
    return uint16_t(b[0] << 8 | b[1]);
}

std::string Utf8(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

// One file or several in a row (a .wbfs split into .wbf1, .wbf2... for FAT32), read as one.
class Parts {
public:
    bool Open(const fs::path& first) {
        Add(first);
        if (m_parts.empty()) return false;
        // Dolphin's naming: the extension's last letter becomes 1, 2, 3...
        fs::path::string_type name = first.native();
        if (name.size() > 1 && (name.back() == 's' || name.back() == 'S')) {
            for (char part = '1'; part <= '9'; ++part) {
                name.back() = static_cast<fs::path::value_type>(part);
                const fs::path next(name);
                std::error_code ec;
                if (!fs::is_regular_file(next, ec) || !Add(next)) break;
            }
        }
        return true;
    }

    bool Read(uint64_t offset, uint8_t* out, size_t size) {
        for (Part& part : m_parts) {
            if (size == 0) break;
            if (offset >= part.size) {
                offset -= part.size;
                continue;
            }
            const size_t n = size_t(std::min<uint64_t>(size, part.size - offset));
            part.file.clear();
            part.file.seekg(std::streamoff(offset));
            part.file.read(reinterpret_cast<char*>(out), std::streamsize(n));
            if (size_t(part.file.gcount()) != n) return false;
            out += n;
            size -= n;
            offset = 0;
        }
        return size == 0;
    }

private:
    struct Part {
        std::ifstream file;
        uint64_t size = 0;
    };

    bool Add(const fs::path& path) {
        std::error_code ec;
        const uint64_t size = fs::file_size(path, ec);
        if (ec) return false;
        Part part;
        part.file.open(path, std::ios::binary);
        if (!part.file) return false;
        part.size = size;
        m_parts.push_back(std::move(part));
        return true;
    }

    std::vector<Part> m_parts;
};

// The disc's bytes, from whatever the image stores them in.
class Reader {
public:
    virtual ~Reader() = default;
    // `size` bytes at `offset` of the disc; false when the image ends first.
    virtual bool Read(uint64_t offset, uint8_t* out, size_t size) = 0;
    // True when the container keeps the game partition decrypted and without hashes (.rvz, .wia),
    // in which case ReadStoredPartition() answers for it and no key or decryption is needed.
    virtual bool StoredPartition() const { return false; }
    virtual bool ReadStoredPartition(uint64_t dataStart, uint64_t offset, uint8_t* out, size_t size) {
        (void)dataStart;
        (void)offset;
        (void)out;
        (void)size;
        return false;
    }
};

class IsoReader final : public Reader {
public:
    explicit IsoReader(Parts parts) : m_parts(std::move(parts)) {}
    bool Read(uint64_t offset, uint8_t* out, size_t size) override { return m_parts.Read(offset, out, size); }

private:
    Parts m_parts;
};

// A .wbfs file: the disc in blocks, with a table saying where each block of the disc is stored (0 for a
// block that isn't stored, which reads as zeros).
class WbfsReader final : public Reader {
public:
    static std::unique_ptr<Reader> Open(Parts parts, std::string& error) {
        uint8_t head[12];
        if (!parts.Read(0, head, sizeof(head))) {
            error = "the disc image is cut short";
            return nullptr;
        }
        const unsigned sectorShift = head[8];   // the drive's sector size, log2
        const unsigned blockShift = head[9];    // the image's block size, log2
        if (sectorShift < 9 || sectorShift > 16 || blockShift < 15 || blockShift > 30) {
            error = "that .wbfs file is damaged";
            return nullptr;
        }
        const uint64_t sector = uint64_t(1) << sectorShift;
        // A dual-layer Wii disc has 143432 * 2 sectors of 0x8000 bytes.
        const size_t blocks = size_t((uint64_t(143432) * 2) >> (blockShift - 15));
        const uint64_t infoSize = (0x100 + blocks * 2 + sector - 1) / sector * sector;
        std::vector<uint8_t> slots(size_t(sector - 12));
        if (!parts.Read(12, slots.data(), slots.size())) {
            error = "the disc image is cut short";
            return nullptr;
        }
        const auto used = std::find_if(slots.begin(), slots.end(), [](uint8_t slot) { return slot != 0; });
        if (used == slots.end()) {
            error = "that .wbfs file holds no game";
            return nullptr;
        }
        const uint64_t info = sector + uint64_t(used - slots.begin()) * infoSize;
        std::vector<uint8_t> table(blocks * 2);
        if (!parts.Read(info + 0x100, table.data(), table.size())) {
            error = "the disc image is cut short";
            return nullptr;
        }
        auto reader = std::unique_ptr<WbfsReader>(new WbfsReader(std::move(parts), blockShift));
        reader->m_table.resize(blocks);
        for (size_t i = 0; i < blocks; ++i) reader->m_table[i] = Be16(&table[i * 2]);
        return reader;
    }

    bool Read(uint64_t offset, uint8_t* out, size_t size) override {
        const uint64_t block = uint64_t(1) << m_shift;
        while (size > 0) {
            const uint64_t index = offset >> m_shift;
            const uint64_t within = offset & (block - 1);
            const size_t n = size_t(std::min<uint64_t>(size, block - within));
            if (index >= m_table.size()) return false;
            if (m_table[index] == 0) {
                std::memset(out, 0, n);
            } else if (!m_parts.Read((uint64_t(m_table[index]) << m_shift) + within, out, n)) {
                return false;
            }
            offset += n;
            out += n;
            size -= n;
        }
        return true;
    }

private:
    WbfsReader(Parts parts, unsigned shift) : m_parts(std::move(parts)), m_shift(shift) {}

    Parts m_parts;
    unsigned m_shift;
    std::vector<uint16_t> m_table;
};

// A .ciso file: the disc split into blocks with a table at 0x8000 saying which blocks are stored at
// all; the holes read as zeroes. Dolphin's and wit's Compact ISO, which keep the blocks themselves
// uncompressed (the compression is in the holes being dropped).
class CisoReader final : public Reader {
public:
    static std::unique_ptr<Reader> Open(Parts parts, std::string& error) {
        uint8_t head[8];
        if (!parts.Read(0, head, sizeof(head))) {
            error = "the disc image is cut short";
            return nullptr;
        }
        // The block size is the one field that is little endian.
        const uint64_t blockSize = uint32_t(head[4]) | uint32_t(head[5]) << 8 | uint32_t(head[6]) << 16 |
                                   uint32_t(head[7]) << 24;
        if (blockSize < 0x8000 || blockSize > (64u << 20) || (blockSize & (blockSize - 1)) != 0) {
            error = "that .ciso file is damaged";
            return nullptr;
        }
        std::vector<uint8_t> map(kHeaderSize - sizeof(head));
        if (!parts.Read(sizeof(head), map.data(), map.size())) {
            error = "the disc image is cut short";
            return nullptr;
        }
        auto reader = std::unique_ptr<CisoReader>(new CisoReader(std::move(parts), blockSize));
        reader->m_slots.resize(map.size());
        uint32_t used = 0;
        for (size_t i = 0; i < map.size(); ++i) {
            reader->m_slots[i] = map[i] == 1 ? used++ : kUnused;
        }
        return reader;
    }

    bool Read(uint64_t offset, uint8_t* out, size_t size) override {
        while (size > 0) {
            const uint64_t block = offset / m_blockSize;
            const uint64_t within = offset % m_blockSize;
            const size_t n = size_t(std::min<uint64_t>(size, m_blockSize - within));
            if (block >= m_slots.size() || m_slots[block] == kUnused) {
                std::memset(out, 0, n);
            } else if (!m_parts.Read(kHeaderSize + uint64_t(m_slots[block]) * m_blockSize + within, out, n)) {
                return false;
            }
            offset += n;
            out += n;
            size -= n;
        }
        return true;
    }

private:
    static constexpr uint64_t kHeaderSize = 0x8000;
    static constexpr uint32_t kUnused = 0xFFFFFFFFu;

    CisoReader(Parts parts, uint64_t blockSize) : m_parts(std::move(parts)), m_blockSize(blockSize) {}

    Parts m_parts;
    uint64_t m_blockSize;
    std::vector<uint32_t> m_slots;
};

// ---------------------------------------------------------------------------
// .wia and .rvz: the disc in chunks, each compressed on its own. RVZ adds Zstandard and an encoding
// for the pseudo-random padding Wii discs are full of. A Wii game partition is kept decrypted, with
// the 0x400-byte hash area of every 0x8000-byte sector dropped, so reading it needs neither the disc
// key nor any decryption. The layout is documented in Dolphin's docs/WiaAndRvz.md.
// ---------------------------------------------------------------------------

constexpr uint32_t kWiaMagic = 0x57494101;  // "WIA\x1", read as a big-endian word
constexpr uint32_t kRvzMagic = 0x52565A01;  // "RVZ\x1", likewise
// One hash group: 64 sectors of 0x7C00 data bytes. Exception lists are counted in these, not in the
// 2 MiB the group occupies on the disc.
constexpr uint64_t kWiaGroupDataSize = kClusterData * 64;
constexpr uint32_t kWiaChunkUnit = 0x200000;  // chunk sizes are multiples of this (RVZ: powers of two)
// A hash exception is a 2-byte offset and a 20-byte SHA-1. A list holds at most one entry per
// block hash plus the padding (52 per block of 64), which is the size reserved per list.
constexpr size_t kExceptionSize = 22;
constexpr size_t kMaxExceptionList = 52 * 64 * kExceptionSize + 2;

enum class WiaCodec : uint32_t { None = 0, Purge = 1, Bzip2 = 2, Lzma = 3, Lzma2 = 4, Zstd = 5 };

void* SzAllocImpl(ISzAllocPtr, size_t size) { return size == 0 ? nullptr : std::malloc(size); }
void SzFreeImpl(ISzAllocPtr, void* address) { std::free(address); }
const ISzAlloc kSzAlloc = {SzAllocImpl, SzFreeImpl};

uint32_t Swap32(uint32_t value) {
    return (value >> 24) | ((value >> 8) & 0xFF00u) | ((value << 8) & 0xFF0000u) | (value << 24);
}

uint64_t Be64(const uint8_t* b) {
    return uint64_t(Be32(b)) << 32 | Be32(b + 4);
}

// The pseudo-random filler Wii discs put in unused areas, generated from a 68-byte seed. This is
// Dolphin's LaggedFibonacciGenerator, with its "shift by 18 instead of 16" quirk folded into the
// words so that output is a plain copy.
class JunkGenerator {
public:
    void Seed(const uint8_t* seed) {
        for (size_t i = 0; i < kSeedWords; ++i) {
            m_buffer[i] = Be32(seed + i * 4);
        }
        for (size_t i = kSeedWords; i < kWords; ++i) {
            m_buffer[i] = (m_buffer[i - 17] << 23) ^ (m_buffer[i - 16] >> 9) ^ m_buffer[i - 1];
        }
        for (uint32_t& word : m_buffer) {
            word = Swap32((word & 0xFF00FFFFu) | ((word >> 2) & 0x00FF0000u));
        }
        for (int i = 0; i < 4; ++i) {
            Advance();
        }
        m_position = 0;
    }

    void Skip(uint64_t bytes) { Consume(bytes, nullptr); }
    void Generate(uint8_t* out, size_t bytes) { Consume(bytes, out); }

private:
    static constexpr size_t kWords = 521;
    static constexpr size_t kSeedWords = 17;
    static constexpr size_t kWindow = kWords * sizeof(uint32_t);

    void Consume(uint64_t bytes, uint8_t* out) {
        const auto* raw = reinterpret_cast<const uint8_t*>(m_buffer);
        while (bytes > 0) {
            const size_t step = size_t(std::min<uint64_t>(bytes, kWindow - m_position));
            if (out != nullptr) {
                std::memcpy(out, raw + m_position, step);
                out += step;
            }
            m_position += step;
            bytes -= step;
            if (m_position == kWindow) {
                Advance();
                m_position = 0;
            }
        }
    }

    void Advance() {
        for (size_t i = 0; i < 32; ++i) {
            m_buffer[i] ^= m_buffer[i + kWords - 32];
        }
        for (size_t i = 32; i < kWords; ++i) {
            m_buffer[i] ^= m_buffer[i - 32];
        }
    }

    uint32_t m_buffer[kWords] = {};
    size_t m_position = 0;
};

// A run of RVZ-packed data: literal bytes, and runs of padding regenerated from a seed.
bool UnpackRvz(const uint8_t* in, size_t inSize, uint64_t dataOffset, uint8_t* out, size_t outSize) {
    JunkGenerator junk;
    size_t at = 0;
    size_t done = 0;
    while (done < outSize) {
        if (at + 4 > inSize) {
            return false;
        }
        uint32_t size = Be32(in + at);
        at += 4;
        const bool filler = (size & 0x80000000u) != 0;
        size &= 0x7FFFFFFFu;
        if (size > outSize - done) {
            return false;
        }
        if (filler) {
            if (at + 68 > inSize) {
                return false;
            }
            junk.Seed(in + at);
            at += 68;
            junk.Skip(dataOffset % 0x8000);
            junk.Generate(out + done, size);
        } else {
            if (at + size > inSize) {
                return false;
            }
            std::memcpy(out + done, in + at, size);
            at += size;
        }
        done += size;
        dataOffset += size;
    }
    return true;
}

// PURGE stores runs of non-zero data as segments, and a SHA-1 of them at the end that reading does
// not need. `out` must already be zeroed.
bool DecodePurge(const uint8_t* in, size_t inSize, size_t segments, uint8_t* out, size_t outSize) {
    if (inSize < 20) {
        return false;
    }
    size_t at = segments;
    const size_t end = inSize - 20;
    while (at + 8 <= end) {
        const uint32_t offset = Be32(in + at);
        const uint32_t size = Be32(in + at + 4);
        at += 8;
        if (offset > outSize || size > outSize - offset || at + size > end) {
            return false;
        }
        std::memcpy(out + offset, in + at, size);
        at += size;
    }
    return true;
}

class WiaReader final : public Reader {
public:
    static std::unique_ptr<Reader> Open(Parts parts, bool rvz, std::string& error);

    bool Read(uint64_t offset, uint8_t* out, size_t size) override;
    bool StoredPartition() const override { return !m_partitions.empty(); }
    bool ReadStoredPartition(uint64_t dataStart, uint64_t offset, uint8_t* out, size_t size) override;

private:
    struct RawEntry {
        uint64_t offset = 0, size = 0;
        uint32_t group = 0, groups = 0;
    };
    struct Group {
        uint64_t offset = 0;
        uint32_t size = 0;
        bool compressed = false;
        uint32_t packed = 0;
    };
    struct Segment {
        uint64_t dataOffset = 0, dataSize = 0;
        uint32_t group = 0, groups = 0;
    };
    struct Partition {
        uint64_t dataStart = 0;
        std::vector<Segment> segments;
    };

    WiaReader(Parts parts, bool rvz) : m_parts(std::move(parts)), m_rvz(rvz) {}

    bool Parse(std::string& error);
    bool ReadTable(uint64_t offset, uint32_t storedSize, size_t plainSize, std::vector<uint8_t>& out);
    bool Decode(WiaCodec codec, const uint8_t* in, size_t inSize, uint8_t* out, size_t outCapacity,
                size_t& produced);
    bool Load(uint32_t index, uint64_t dataOffset, size_t groupBytes, bool partition, const uint8_t** data);

    Parts m_parts;
    bool m_rvz = false;
    WiaCodec m_codec = WiaCodec::None;
    uint8_t m_props[7] = {};
    uint8_t m_propsSize = 0;
    uint32_t m_chunkSize = 0;       // disc bytes per group
    size_t m_chunkData = 0;         // partition-data bytes per group, hashes left out
    uint32_t m_partitionLists = 1;  // exception lists per partition group
    uint64_t m_isoSize = 0;
    std::array<uint8_t, 0x80> m_discHeader{};
    std::vector<RawEntry> m_raw;
    std::vector<Group> m_groups;
    std::vector<Partition> m_partitions;
    // Decompressed groups, least recently used first. The budget keeps a couple of them for a .wia
    // with 40 MiB chunks and many more for an .rvz with 128 KiB ones.
    struct CachedGroup {
        uint32_t index = 0;
        uint64_t use = 0;
        std::vector<uint8_t> bytes;
    };
    std::vector<CachedGroup> m_cache;
    uint64_t m_cacheUse = 0;
    static constexpr size_t kCacheLimitBytes = 192ull << 20;
};

std::unique_ptr<Reader> WiaReader::Open(Parts parts, bool rvz, std::string& error) {
    auto reader = std::unique_ptr<WiaReader>(new WiaReader(std::move(parts), rvz));
    if (!reader->Parse(error)) {
        return nullptr;
    }
    return reader;
}

bool WiaReader::Parse(std::string& error) {
    uint8_t head[0x48];
    if (!m_parts.Read(0, head, sizeof(head))) {
        error = "the disc image is cut short";
        return false;
    }
    const uint32_t magic = Be32(head);
    if (magic != kWiaMagic && magic != kRvzMagic) {
        error = "that isn't a .wia or .rvz image";
        return false;
    }
    if ((magic == kRvzMagic) != m_rvz) {
        error = "the disc image's header and extension disagree";
        return false;
    }
    m_isoSize = Be64(head + 0x24);
    const uint32_t header2Size = Be32(head + 0x0C);
    if (header2Size < 0xD5 || header2Size > 0x1000) {
        error = "that disc image's header is damaged";
        return false;
    }
    std::vector<uint8_t> header2(header2Size);
    if (!m_parts.Read(sizeof(head), header2.data(), header2.size())) {
        error = "the disc image is cut short";
        return false;
    }
    if (Be32(header2.data()) != 2) {
        error = "that disc image holds no Wii disc";
        return false;
    }
    m_codec = static_cast<WiaCodec>(Be32(header2.data() + 0x04));
    const uint32_t maxCodec = m_rvz ? static_cast<uint32_t>(WiaCodec::Zstd) : static_cast<uint32_t>(WiaCodec::Lzma2);
    if (static_cast<uint32_t>(m_codec) > maxCodec || (m_rvz && m_codec == WiaCodec::Purge)) {
        error = "that disc image uses a compression this build doesn't read";
        return false;
    }
    m_chunkSize = Be32(header2.data() + 0x0C);
    const bool powerOfTwo = (m_chunkSize & (m_chunkSize - 1)) == 0;
    if (m_chunkSize < 0x8000 || (m_rvz ? !powerOfTwo : m_chunkSize % kWiaChunkUnit != 0)) {
        error = "that disc image's chunk size is damaged";
        return false;
    }
    m_propsSize = header2[0xD4];
    if (m_propsSize > sizeof(m_props) || header2Size < 0xD5 + m_propsSize) {
        error = "that disc image's header is damaged";
        return false;
    }
    std::memcpy(m_props, header2.data() + 0xD5, m_propsSize);
    std::memcpy(m_discHeader.data(), header2.data() + 0x10, m_discHeader.size());
    m_chunkData = size_t(uint64_t(m_chunkSize) * kClusterData / kCluster);
    m_partitionLists = std::max<uint32_t>(1, static_cast<uint32_t>(m_chunkData / kWiaGroupDataSize));

    const uint32_t partitions = Be32(header2.data() + 0x90);
    const uint32_t partitionEntrySize = Be32(header2.data() + 0x94);
    const uint64_t partitionOffset = Be64(header2.data() + 0x98);
    if (partitions > 64 || (partitions != 0 && (partitionEntrySize < 0x30 || partitionEntrySize > 0x1000))) {
        error = "that disc image's partition table is damaged";
        return false;
    }
    std::vector<uint8_t> entries(size_t(partitions) * partitionEntrySize);
    if (!entries.empty() && !m_parts.Read(partitionOffset, entries.data(), entries.size())) {
        error = "the disc image is cut short";
        return false;
    }
    for (uint32_t i = 0; i < partitions; ++i) {
        const uint8_t* entry = entries.data() + size_t(i) * partitionEntrySize;
        const uint32_t baseSector = Be32(entry + 0x10);
        Partition partition;
        partition.dataStart = uint64_t(baseSector) * kCluster;
        for (int j = 0; j < 2; ++j) {
            const uint8_t* data = entry + 0x10 + j * 0x10;
            const uint32_t firstSector = Be32(data);
            const uint32_t sectors = Be32(data + 4);
            if (sectors == 0 || firstSector < baseSector) {
                continue;
            }
            Segment segment;
            segment.dataOffset = uint64_t(firstSector - baseSector) * kClusterData;
            segment.dataSize = uint64_t(sectors) * kClusterData;
            segment.group = Be32(data + 8);
            segment.groups = Be32(data + 12);
            partition.segments.push_back(segment);
        }
        if (!partition.segments.empty()) {
            m_partitions.push_back(std::move(partition));
        }
    }

    const uint32_t rawCount = Be32(header2.data() + 0xB4);
    const uint64_t rawOffset = Be64(header2.data() + 0xB8);
    const uint32_t rawSize = Be32(header2.data() + 0xC0);
    if (rawCount > 0) {
        std::vector<uint8_t> table;
        if (!ReadTable(rawOffset, rawSize, size_t(rawCount) * 0x18, table)) {
            error = "that disc image's raw data table is damaged";
            return false;
        }
        m_raw.resize(rawCount);
        for (uint32_t i = 0; i < rawCount; ++i) {
            const uint8_t* entry = table.data() + size_t(i) * 0x18;
            // A raw range can start inside a sector (the first one starts at 0x80); its groups begin
            // at the sector it starts in, so round the start down and give the difference to the size.
            const uint64_t offset = Be64(entry);
            const uint64_t start = offset & ~uint64_t(kCluster - 1);
            m_raw[i].offset = start;
            m_raw[i].size = Be64(entry + 8) + (offset - start);
            m_raw[i].group = Be32(entry + 0x10);
            m_raw[i].groups = Be32(entry + 0x14);
        }
    }

    const uint32_t groupCount = Be32(header2.data() + 0xC4);
    const uint64_t groupOffset = Be64(header2.data() + 0xC8);
    const uint32_t groupSize = Be32(header2.data() + 0xD0);
    const size_t entrySize = m_rvz ? 0x0C : 0x08;
    if (groupCount > 0) {
        std::vector<uint8_t> table;
        if (!ReadTable(groupOffset, groupSize, size_t(groupCount) * entrySize, table)) {
            error = "that disc image's chunk table is damaged";
            return false;
        }
        m_groups.resize(groupCount);
        for (uint32_t i = 0; i < groupCount; ++i) {
            const uint8_t* entry = table.data() + size_t(i) * entrySize;
            Group& group = m_groups[i];
            group.offset = uint64_t(Be32(entry)) * 4;
            uint32_t size = Be32(entry + 4);
            if (m_rvz) {
                group.compressed = (size & 0x80000000u) != 0;
                size &= 0x7FFFFFFFu;
                group.packed = Be32(entry + 8);
            } else {
                group.compressed = m_codec != WiaCodec::None && m_codec != WiaCodec::Purge;
            }
            group.size = size;
        }
    }
    if (m_partitions.empty() || m_groups.empty()) {
        error = "that disc image has no game partition";
        return false;
    }
    return true;
}

bool WiaReader::ReadTable(uint64_t offset, uint32_t storedSize, size_t plainSize, std::vector<uint8_t>& out) {
    if (storedSize == 0) {
        return plainSize == 0;
    }
    std::vector<uint8_t> stored(storedSize);
    if (!m_parts.Read(offset, stored.data(), stored.size())) {
        return false;
    }
    if (m_codec == WiaCodec::None) {
        if (stored.size() != plainSize) {
            return false;
        }
        out = std::move(stored);
        return true;
    }
    out.assign(plainSize, 0);
    if (m_codec == WiaCodec::Purge) {
        return DecodePurge(stored.data(), stored.size(), 0, out.data(), out.size());
    }
    size_t produced = 0;
    return Decode(m_codec, stored.data(), stored.size(), out.data(), out.size(), produced) && produced == plainSize;
}

bool WiaReader::Decode(WiaCodec codec, const uint8_t* in, size_t inSize, uint8_t* out, size_t outCapacity,
                       size_t& produced) {
    produced = 0;
    switch (codec) {
    case WiaCodec::None:
        if (inSize > outCapacity) {
            return false;
        }
        std::memcpy(out, in, inSize);
        produced = inSize;
        return true;
    case WiaCodec::Zstd:
#if defined(WiiDisc_HAVE_ZSTD)
    {
        const size_t got = ZSTD_decompress(out, outCapacity, in, inSize);
        if (ZSTD_isError(got)) {
            return false;
        }
        produced = got;
        return true;
    }
#else
        return false;
#endif
    case WiaCodec::Bzip2:
#if defined(WiiDisc_HAVE_BZIP2)
    {
        unsigned int destLen = static_cast<unsigned int>(outCapacity);
        const int result = BZ2_bzBuffToBuffDecompress(reinterpret_cast<char*>(out), &destLen,
                                                      const_cast<char*>(reinterpret_cast<const char*>(in)),
                                                      static_cast<unsigned int>(inSize), 0, 0);
        if (result != 0) {
            return false;
        }
        produced = destLen;
        return true;
    }
#else
        return false;
#endif
    case WiaCodec::Lzma: {
        CLzmaDec state;
        LzmaDec_Construct(&state);
        if (LzmaDec_Allocate(&state, m_props, m_propsSize, &kSzAlloc) != SZ_OK) {
            return false;
        }
        LzmaDec_Init(&state);
        SizeT destLen = outCapacity;
        SizeT srcLen = inSize;
        ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
        const SRes result = LzmaDec_DecodeToBuf(&state, out, &destLen, in, &srcLen, LZMA_FINISH_END, &status);
        LzmaDec_Free(&state, &kSzAlloc);
        if (result != SZ_OK) {
            return false;
        }
        produced = destLen;
        return true;
    }
    case WiaCodec::Lzma2: {
        CLzma2Dec state;
        Lzma2Dec_Construct(&state);
        if (Lzma2Dec_Allocate(&state, m_props[0], &kSzAlloc) != SZ_OK) {
            return false;
        }
        Lzma2Dec_Init(&state);
        SizeT destLen = outCapacity;
        SizeT srcLen = inSize;
        ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
        const SRes result = Lzma2Dec_DecodeToBuf(&state, out, &destLen, in, &srcLen, LZMA_FINISH_END, &status);
        Lzma2Dec_Free(&state, &kSzAlloc);
        if (result != SZ_OK) {
            return false;
        }
        produced = destLen;
        return true;
    }
    case WiaCodec::Purge:
        return false;  // the segment layout is handled by Load
    }
    return false;
}

bool WiaReader::Load(uint32_t index, uint64_t dataOffset, size_t groupBytes, bool partition,
                     const uint8_t** data) {
    for (auto it = m_cache.rbegin(); it != m_cache.rend(); ++it) {
        if (it->index == index) {
            it->use = ++m_cacheUse;
            *data = it->bytes.data();
            return true;
        }
    }
    if (index >= m_groups.size()) {
        return false;
    }
    const Group& group = m_groups[index];
    // Make room: the least recently used groups go first.
    size_t total = 0;
    for (const CachedGroup& cached : m_cache) {
        total += cached.bytes.size();
    }
    while (!m_cache.empty() && total + groupBytes > kCacheLimitBytes) {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
            if (it->use < oldest->use) {
                oldest = it;
            }
        }
        total -= oldest->bytes.size();
        m_cache.erase(oldest);
    }
    m_cache.push_back(CachedGroup{index, ++m_cacheUse, std::vector<uint8_t>(groupBytes, 0)});
    std::vector<uint8_t>& cache = m_cache.back().bytes;
    if (group.size == 0) {  // a chunk of zeroes
        *data = cache.data();
        return true;
    }
    std::vector<uint8_t> stored(group.size);
    if (!m_parts.Read(group.offset, stored.data(), stored.size())) {
        return false;
    }

    const WiaCodec codec = m_rvz && !group.compressed ? WiaCodec::None : m_codec;
    const size_t lists = partition ? m_partitionLists : 0;
    if (codec == WiaCodec::Purge) {
        size_t at = 0;
        for (size_t i = 0; i < lists; ++i) {
            if (at + 2 > stored.size()) {
                return false;
            }
            at += 2 + size_t(Be16(stored.data() + at)) * kExceptionSize;
            if (at > stored.size()) {
                return false;
            }
        }
        if (at % 4 != 0) {
            at += 4 - at % 4;  // NONE and PURGE pad after the lists; the segments follow
        }
        if (!DecodePurge(stored.data(), stored.size(), at, cache.data(), groupBytes)) {
            return false;
        }
        *data = cache.data();
        return true;
    }

    std::vector<uint8_t> stream;
    if (codec == WiaCodec::None) {
        stream = std::move(stored);
    } else {
        // The stream holds the exception lists first, then the data (for RVZ, the packed form of it).
        const size_t dataBytes = m_rvz && group.packed != 0 ? group.packed : groupBytes;
        stream.resize(lists * kMaxExceptionList + dataBytes + 64);
        size_t produced = 0;
        if (!Decode(codec, stored.data(), stored.size(), stream.data(), stream.size(), produced)) {
            return false;
        }
        stream.resize(produced);
    }

    size_t at = 0;
    for (size_t i = 0; i < lists; ++i) {
        if (at + 2 > stream.size()) {
            return false;
        }
        at += 2 + size_t(Be16(stream.data() + at)) * kExceptionSize;
        if (at > stream.size()) {
            return false;
        }
    }
    if (codec == WiaCodec::None && at % 4 != 0) {
        at += 4 - at % 4;
    }
    if (at > stream.size()) {
        return false;
    }

    if (m_rvz && group.packed != 0) {
        if (!UnpackRvz(stream.data() + at, stream.size() - at, dataOffset, cache.data(), groupBytes)) {
            return false;
        }
    } else {
        if (stream.size() - at < groupBytes) {
            return false;
        }
        std::memcpy(cache.data(), stream.data() + at, groupBytes);
    }
    *data = cache.data();
    return true;
}

bool WiaReader::Read(uint64_t offset, uint8_t* out, size_t size) {
    if (offset > m_isoSize || size > m_isoSize - offset) {
        return false;
    }
    if (offset < m_discHeader.size()) {
        const size_t n = size_t(std::min<uint64_t>(size, m_discHeader.size() - offset));
        std::memcpy(out, m_discHeader.data() + offset, n);
        offset += n;
        out += n;
        size -= n;
    }
    while (size > 0) {
        const RawEntry* entry = nullptr;
        for (const RawEntry& candidate : m_raw) {
            if (candidate.size != 0 && offset >= candidate.offset && offset < candidate.offset + candidate.size) {
                entry = &candidate;
                break;
            }
        }
        if (entry == nullptr) {
            return false;
        }
        const uint64_t relative = offset - entry->offset;
        const uint64_t group = relative / m_chunkSize;
        const uint64_t groupStart = group * m_chunkSize;
        if (entry->size <= groupStart) {
            return false;
        }
        const size_t groupBytes = size_t(std::min<uint64_t>(m_chunkSize, entry->size - groupStart));
        const size_t within = size_t(relative - groupStart);
        const uint8_t* groupData = nullptr;
        if (group > 0xFFFFFFFFu || !Load(entry->group + static_cast<uint32_t>(group), entry->offset + groupStart,
                                         groupBytes, false, &groupData)) {
            return false;
        }
        const size_t n = std::min(size, groupBytes - within);
        std::memcpy(out, groupData + within, n);
        offset += n;
        out += n;
        size -= n;
    }
    return true;
}

bool WiaReader::ReadStoredPartition(uint64_t dataStart, uint64_t offset, uint8_t* out, size_t size) {
    for (const Partition& partition : m_partitions) {
        if (partition.dataStart != dataStart) {
            continue;
        }
        size_t done = 0;
        for (const Segment& segment : partition.segments) {
            if (done == size) {
                break;
            }
            if (offset + done < segment.dataOffset || offset + done >= segment.dataOffset + segment.dataSize) {
                continue;
            }
            // A read can be larger than what one group still holds, so keep going group by group
            // until this segment is used up.
            while (done < size) {
                const uint64_t relative = offset + done - segment.dataOffset;
                if (relative >= segment.dataSize) {
                    break;  // the rest of the read is in the next segment
                }
                const uint64_t group = relative / m_chunkData;
                const uint64_t groupStart = group * m_chunkData;
                if (segment.dataSize <= groupStart || group > 0xFFFFFFFFu) {
                    return false;
                }
                const size_t groupBytes = size_t(std::min<uint64_t>(m_chunkData, segment.dataSize - groupStart));
                const size_t within = size_t(relative - groupStart);
                const uint8_t* groupData = nullptr;
                if (!Load(segment.group + static_cast<uint32_t>(group), segment.dataOffset + groupStart,
                          groupBytes, true, &groupData)) {
                    return false;
                }
                const size_t n = std::min(size - done, groupBytes - within);
                std::memcpy(out + done, groupData + within, n);
                done += n;
            }
        }
        return done == size;
    }
    return false;
}

// An FST name as a path, refused when it could leave its folder or land elsewhere on Windows.
bool NameToPath(const std::string& name, fs::path& out) {
    if (name.empty() || name == "." || name == "..") return false;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || c == '/' || c == '\\') return false;
#if defined(_WIN32)
        if (std::strchr(":*?\"<>|", c) != nullptr) return false;
#endif
    }
#if defined(_WIN32)
    if (name.back() == '.' || name.back() == ' ') return false;
    std::string stem = name.substr(0, name.find('.'));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](char c) { return char(std::toupper(static_cast<unsigned char>(c))); });
    static const char* const reserved[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
                                           "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4",
                                           "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    for (const char* r : reserved) {
        if (stem == r) return false;
    }
    // Latin-1 is the first 256 code points, so each byte becomes the wide character of the same value.
    std::wstring wide(name.size(), L'\0');
    std::transform(name.begin(), name.end(), wide.begin(), [](char c) { return wchar_t(static_cast<unsigned char>(c)); });
    out = fs::path(wide);
#else
    std::string utf8;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x80) {
            utf8 += c;
        } else {
            utf8 += char(0xC0 | (u >> 6));
            utf8 += char(0x80 | (u & 0x3F));
        }
    }
    out = fs::path(utf8);
#endif
    return true;
}

} // namespace

struct Disc::State {
    std::unique_ptr<Reader> reader;
    std::string id;
    int revision = 0;
    uint64_t dataStart = 0, dataSize = 0;
    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption cipher;
    std::vector<uint8_t> raw = std::vector<uint8_t>(kCluster * kBatch);
    std::vector<uint8_t> plain = std::vector<uint8_t>(kClusterData * kBatch);
    uint64_t cachedFirst = UINT64_MAX;
    uint64_t cachedCount = 0;
    // The partition's layout (boot.bin): where main.dol and the file table are, and their sizes.
    uint64_t dolOffset = 0, dolSize = 0, fstOffset = 0, apploaderSize = 0, totalSize = 0;
    std::vector<uint8_t> fst;
    // Every file of the partition, for reading the image in place. Reads are serialized because the
    // disc index, the game and the preload thread all come through here.
    std::vector<File> files;
    std::unordered_map<std::string, size_t> fileIndex;
    std::mutex readMutex;

    // A lookup key: no leading separators, '/' between the parts, case-folded.
    static std::string Key(std::string_view path) {
        size_t at = 0;
        while (at < path.size() && (path[at] == '/' || path[at] == '\\')) {
            ++at;
        }
        std::string key;
        key.reserve(path.size() - at);
        for (; at < path.size(); ++at) {
            char c = path[at];
            if (c == '\\') {
                c = '/';
            }
            key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return key;
    }

    void AddFile(std::string path, uint64_t offset, uint64_t size) {
        fileIndex.emplace(Key(path), files.size());
        files.push_back({std::move(path), offset, size});
    }

    // The FST's files, then the five system files that live in the partition's header area.
    void BuildIndex() {
        const uint32_t entries = Be32(&fst[8]);
        const size_t strings = size_t(entries) * 12;
        std::vector<std::string> dirs(entries + 1);
        std::vector<uint32_t> ends(entries + 1);
        size_t depth = 0;
        ends[0] = entries;
        for (uint32_t i = 1; i < entries; ++i) {
            while (depth > 0 && i >= ends[depth]) {
                --depth;
            }
            const uint8_t* entry = &fst[size_t(i) * 12];
            const uint32_t word = Be32(entry);
            const size_t at = strings + (word & 0xFFFFFF);
            if (at >= fst.size()) {
                break;
            }
            const char* begin = reinterpret_cast<const char*>(&fst[at]);
            const char* end = static_cast<const char*>(std::memchr(begin, 0, fst.size() - at));
            if (end == nullptr) {
                break;
            }
            std::string path = dirs[depth].empty() ? std::string(begin, end) : dirs[depth] + "/" + std::string(begin, end);
            if ((word >> 24) != 0) {
                ++depth;
                dirs[depth] = path;
                ends[depth] = std::min(entries, Be32(entry + 8));
            } else {
                AddFile(std::move(path), uint64_t(Be32(entry + 4)) << 2, Be32(entry + 8));
            }
        }
        AddFile("sys/boot.bin", 0, 0x440);
        AddFile("sys/bi2.bin", 0x440, 0x2000);
        AddFile("sys/apploader.img", 0x2440, apploaderSize);
        AddFile("sys/main.dol", dolOffset, dolSize);
        AddFile("sys/fst.bin", fstOffset, fst.size());
    }

    // Reads and decrypts up to kBatch clusters from `first`: each one's IV is in its hash area.
    bool Decrypt(uint64_t first) {
        const uint64_t clusters = (dataSize + kCluster - 1) / kCluster;
        if (first >= clusters) return false;
        const uint64_t count = std::min(kBatch, clusters - first);
        if (!reader->Read(dataStart + first * kCluster, raw.data(), size_t(count * kCluster))) return false;
        for (uint64_t c = 0; c < count; ++c) {
            const uint8_t* cluster = &raw[size_t(c * kCluster)];
            cipher.Resynchronize(cluster + 0x3D0, 16);
            cipher.ProcessData(&plain[size_t(c * kClusterData)], cluster + 0x400, size_t(kClusterData));
        }
        cachedFirst = first;
        cachedCount = count;
        return true;
    }

    // `size` bytes of the decrypted partition at `offset` (an offset in its data, hashes left out).
    bool ReadPartition(uint64_t offset, uint8_t* out, size_t size) {
        if (reader->StoredPartition()) {
            return reader->ReadStoredPartition(dataStart, offset, out, size);
        }
        for (size_t at = 0; at < size;) {
            const uint64_t cluster = (offset + at) / kClusterData;
            const uint64_t within = (offset + at) % kClusterData;
            if (cluster < cachedFirst || cluster >= cachedFirst + cachedCount) {
                if (!Decrypt(cluster)) return false;
            }
            const uint64_t start = (cluster - cachedFirst) * kClusterData + within;
            const size_t n = size_t(std::min<uint64_t>(size - at, cachedCount * kClusterData - start));
            std::memcpy(out + at, &plain[size_t(start)], n);
            at += n;
        }
        return true;
    }

    std::string ReadLayout() {
        uint8_t boot[0x440];
        uint8_t apploader[0x20];
        uint8_t dol[0x100];
        if (!ReadPartition(0, boot, sizeof(boot))) return "the disc image is cut short";
        dolOffset = uint64_t(Be32(boot + 0x420)) << 2;
        fstOffset = uint64_t(Be32(boot + 0x424)) << 2;
        const uint64_t fstSize = uint64_t(Be32(boot + 0x428)) << 2;
        if (fstSize < 12 || fstSize > (64u << 20)) return "the disc's file table is damaged";
        if (!ReadPartition(0x2440, apploader, sizeof(apploader))) return "the disc image is cut short";
        apploaderSize = 0x20 + uint64_t(Be32(apploader + 0x14)) + Be32(apploader + 0x18);
        if (!ReadPartition(dolOffset, dol, sizeof(dol))) return "the disc image is cut short";
        dolSize = 0x100;
        for (int i = 0; i < 18; ++i) {
            dolSize = std::max<uint64_t>(dolSize, uint64_t(Be32(dol + i * 4)) + Be32(dol + 0x90 + i * 4));
        }
        fst.resize(size_t(fstSize));
        if (!ReadPartition(fstOffset, fst.data(), fst.size())) return "the disc image is cut short";
        const uint32_t entries = Be32(&fst[8]);
        if (entries == 0 || uint64_t(entries) * 12 > fstSize) return "the disc's file table is damaged";
        totalSize = 0x440 + 0x2000 + apploaderSize + dolSize + fstSize;
        for (uint32_t i = 1; i < entries; ++i) {
            if (fst[i * 12] == 0) totalSize += Be32(&fst[i * 12 + 8]);
        }
        BuildIndex();
        return "";
    }

    // Extract()'s time split, for the startup log (see ExtractStats).
    uint64_t extractReadNanos = 0, extractWriteNanos = 0, extractFsNanos = 0, extractProgressNanos = 0;

    static uint64_t NanosSince(const std::chrono::steady_clock::time_point& start) {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now() - start)
                                         .count());
    }

    std::string Write(const fs::path& target, uint64_t offset, uint64_t size, uint64_t& done,
                      const Progress& progress, const std::atomic<bool>& cancel) {
        if (offset + size > dataSize / kCluster * kClusterData) return "the disc image is cut short";
        auto start = std::chrono::steady_clock::now();
        std::vector<uint8_t> chunk(size_t(std::min<uint64_t>(kChunk, std::max<uint64_t>(size, 1))));
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        extractFsNanos += NanosSince(start);
        if (!out) return "couldn't create " + Utf8(target);
        for (uint64_t at = 0; at < size;) {
            if (cancel.load(std::memory_order_relaxed)) return "cancelled";
            const size_t n = size_t(std::min<uint64_t>(chunk.size(), size - at));
            start = std::chrono::steady_clock::now();
            const bool read = ReadPartition(offset + at, chunk.data(), n);
            extractReadNanos += NanosSince(start);
            if (!read) return "the disc image is cut short";
            start = std::chrono::steady_clock::now();
            out.write(reinterpret_cast<const char*>(chunk.data()), std::streamsize(n));
            extractWriteNanos += NanosSince(start);
            if (!out) return "couldn't write " + Utf8(target) + " (is the drive full?)";
            at += n;
            done += n;
            if (progress) {
                const auto progressStart = std::chrono::steady_clock::now();
                progress(done, totalSize);
                extractProgressNanos += NanosSince(progressStart);
            }
        }
        start = std::chrono::steady_clock::now();
        out.close();
        extractFsNanos += NanosSince(start);
        if (!out) return "couldn't write " + Utf8(target) + " (is the drive full?)";
        return "";
    }
};

namespace {
#if defined(CRYPTOPP_ARM_AES_AVAILABLE) && defined(MKW_PLATFORM_SWITCH)
// Crypto++ is compiled here with CRYPTOPP_DISABLE_ASM, which also compiles out its ARM feature
// probe, so HasAES() would stay false even though the ARMv8 AES block functions are built in (see
// runtime/CMakeLists.txt). The A57 always has the instructions, but the flag is only set after one
// block has been decrypted through both paths and compared: it decides the round-key byte order, so
// enabling it wrongly corrupts every read rather than being a little slower.
bool g_armAes = false;

void SetArmAes(bool on) {
    CryptoPP::g_ArmDetectionDone = true;
    CryptoPP::g_hasAES = on;
}

bool ArmAesMatchesTable() {
    uint8_t key[16], iv[16], data[32];
    for (size_t i = 0; i < sizeof(key); ++i) key[i] = uint8_t(i * 7 + 1);
    for (size_t i = 0; i < sizeof(iv); ++i) iv[i] = uint8_t(i * 5 + 3);
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = uint8_t(i * 11 + 7);
    auto decrypt = [&](bool arm, uint8_t* out) {
        SetArmAes(arm);
        CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption cipher;
        cipher.SetKeyWithIV(key, sizeof(key), iv, sizeof(iv));
        cipher.ProcessData(out, data, sizeof(data));
    };
    uint8_t arm[sizeof(data)], table[sizeof(data)];
    decrypt(true, arm);
    decrypt(false, table);
    return std::memcmp(arm, table, sizeof(data)) == 0;
}

// Runs before the first SetKey of the process.
void EnableArmAes() {
    static const bool once = [] {
        g_armAes = ArmAesMatchesTable();
        SetArmAes(g_armAes);
        return true;
    }();
    (void)once;
}
#else
void EnableArmAes() {}
#endif
} // namespace

const char* Disc::AesBackend() {
#if defined(CRYPTOPP_ARM_AES_AVAILABLE) && defined(MKW_PLATFORM_SWITCH)
    return g_armAes ? "armv8" : "table";
#else
    return "table";
#endif
}

std::unique_ptr<Disc> Disc::Open(const fs::path& path, std::string& error) {
    EnableArmAes();
    Parts parts;
    if (!parts.Open(path)) {
        error = "couldn't open " + Utf8(path);
        return nullptr;
    }
    uint8_t magic[4] = {};
    if (!parts.Read(0, magic, sizeof(magic))) {
        error = "that isn't a Wii disc image";
        return nullptr;
    }
    auto state = std::make_unique<State>();
    if (std::memcmp(magic, "WBFS", 4) == 0) {
        state->reader = WbfsReader::Open(std::move(parts), error);
        if (!state->reader) return nullptr;
    } else if (std::memcmp(magic, "RVZ\x01", 4) == 0 || std::memcmp(magic, "WIA\x01", 4) == 0) {
        state->reader = WiaReader::Open(std::move(parts), magic[0] == 'R', error);
        if (!state->reader) return nullptr;
    } else if (std::memcmp(magic, "\x01\xC0\x0B\xB1", 4) == 0) {
        error = std::string("that's a compressed disc image (.gcz)") + kConvert;
        return nullptr;
    } else if (std::memcmp(magic, "CISO", 4) == 0) {
        state->reader = CisoReader::Open(std::move(parts), error);
        if (!state->reader) return nullptr;
    } else {
        state->reader = std::make_unique<IsoReader>(std::move(parts));
    }

    uint8_t head[0x80];
    if (!state->reader->Read(0, head, sizeof(head))) {
        error = "that isn't a Wii disc image";
        return nullptr;
    }
    if (Be32(head + 0x1C) == kGameCubeMagic) {
        error = "that's a GameCube disc image, not a Wii one";
        return nullptr;
    }
    if (Be32(head + 0x18) != kWiiMagic) {
        error = "that isn't a Wii disc image";
        return nullptr;
    }
    uint8_t nkit[4] = {};
    if (state->reader->Read(0x200, nkit, sizeof(nkit)) && std::memcmp(nkit, "NKIT", 4) == 0) {
        error = "that's an NKit image; restore it to an .iso with NKit or Dolphin first";
        return nullptr;
    }
    if (head[0x61] != 0) {
        error = "that disc image isn't encrypted the way a Wii disc is";
        return nullptr;
    }
    state->id.assign(reinterpret_cast<const char*>(head), 6);
    state->revision = head[7];

    // The data partition (type 0) from the partition table at 0x40000.
    uint64_t partition = 0;
    uint8_t groups[32];
    if (!state->reader->Read(0x40000, groups, sizeof(groups))) {
        error = "the disc image is cut short";
        return nullptr;
    }
    for (int g = 0; g < 4 && partition == 0; ++g) {
        const uint32_t count = Be32(groups + g * 8);
        const uint64_t table = uint64_t(Be32(groups + g * 8 + 4)) << 2;
        if (count == 0 || count > 64) continue;
        std::vector<uint8_t> entries(count * 8);
        if (!state->reader->Read(table, entries.data(), entries.size())) continue;
        for (uint32_t i = 0; i < count; ++i) {
            if (Be32(&entries[i * 8 + 4]) == 0) {
                partition = uint64_t(Be32(&entries[i * 8])) << 2;
                break;
            }
        }
    }
    if (partition == 0) {
        error = "that disc image has no game partition";
        return nullptr;
    }

    uint8_t ticket[0x2C0];
    if (!state->reader->Read(partition, ticket, sizeof(ticket))) {
        error = "the disc image is cut short";
        return nullptr;
    }
    const unsigned keyIndex = ticket[0x1F1];
    if (keyIndex >= 2) {
        error = "that disc image isn't from a retail Wii disc";
        return nullptr;
    }
    state->dataStart = partition + (uint64_t(Be32(ticket + 0x2B8)) << 2);
    state->dataSize = uint64_t(Be32(ticket + 0x2BC)) << 2;
    uint8_t iv[16] = {};
    std::memcpy(iv, ticket + 0x1DC, 8); // the title ID
    uint8_t titleKey[16];
    CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption keyCipher;
    keyCipher.SetKeyWithIV(kCommonKeys[keyIndex], 16, iv, 16);
    keyCipher.ProcessData(titleKey, ticket + 0x1BF, 16);
    state->cipher.SetKeyWithIV(titleKey, 16, iv, 16);

    error = state->ReadLayout();
    if (!error.empty()) return nullptr;
    return std::unique_ptr<Disc>(new Disc(std::move(state)));
}

Disc::Disc(std::unique_ptr<State> state) : m_state(std::move(state)) {}
Disc::~Disc() = default;

const std::string& Disc::Id() const {
    return m_state->id;
}

int Disc::Revision() const {
    return m_state->revision;
}

bool Disc::LooksLikeImage(const fs::path& path) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        return false;
    }
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".iso" || extension == ".gcm" || extension == ".wbfs" || extension == ".ciso" ||
           extension == ".wia" || extension == ".rvz";
}

const std::vector<Disc::File>& Disc::Files() const {
    return m_state->files;
}

uint64_t Disc::FileSize(std::string_view path) const {
    const State& s = *m_state;
    const auto it = s.fileIndex.find(State::Key(path));
    return it != s.fileIndex.end() ? s.files[it->second].size : UINT64_MAX;
}

bool Disc::Read(std::string_view path, uint64_t offset, uint8_t* out, size_t size) {
    const State& s = *m_state;
    const auto it = s.fileIndex.find(State::Key(path));
    if (it == s.fileIndex.end()) {
        return false;
    }
    const File& file = s.files[it->second];
    if (offset > file.size || size > file.size - offset) {
        return false;
    }
    return ReadAt(file.offset + offset, out, size);
}

bool Disc::ReadAt(uint64_t offset, uint8_t* out, size_t size) {
    State& s = *m_state;
    std::lock_guard<std::mutex> lock(s.readMutex);
    return size == 0 || s.ReadPartition(offset, out, size);
}

bool Disc::Read(std::string_view path, std::vector<uint8_t>& out) {
    const uint64_t size = FileSize(path);
    if (size == UINT64_MAX) {
        return false;
    }
    out.resize(size_t(size));
    return Read(path, 0, out.data(), out.size());
}

uint64_t Disc::ExtractedSize() const {
    return m_state->totalSize;
}

std::string Disc::Extract(const fs::path& root, const Progress& progress, const std::atomic<bool>& cancel,
                          int& files, ExtractStats* stats) {
    State& s = *m_state;
    files = 0;
    s.extractReadNanos = s.extractWriteNanos = s.extractFsNanos = s.extractProgressNanos = 0;
    uint64_t done = 0;
    const fs::path sys = root / "sys";
    std::error_code sysError;
    fs::create_directories(sys, sysError);
    if (!fs::is_directory(sys, sysError)) return "couldn't create " + Utf8(sys);
    struct SysFile {
        const char* name;
        uint64_t offset, size;
    };
    const SysFile sysFiles[] = {
        {"boot.bin", 0, 0x440},
        {"bi2.bin", 0x440, 0x2000},
        {"apploader.img", 0x2440, s.apploaderSize},
        {"main.dol", s.dolOffset, s.dolSize},
        {"fst.bin", s.fstOffset, s.fst.size()},
    };
    for (const SysFile& file : sysFiles) {
        std::string error = s.Write(sys / file.name, file.offset, file.size, done, progress, cancel);
        if (!error.empty()) return error;
        ++files;
    }

    // The FST: entry 0 is the root; a directory's third word is the index past its last entry.
    const uint32_t entries = Be32(&s.fst[8]);
    const size_t strings = size_t(entries) * 12;
    std::vector<fs::path> dirs(entries + 1);
    std::vector<uint32_t> ends(entries + 1);
    size_t depth = 0;
    dirs[0] = root / "files";
    ends[0] = entries;
    std::error_code ec;
    fs::create_directories(dirs[0], ec);
    for (uint32_t i = 1; i < entries; ++i) {
        while (depth > 0 && i >= ends[depth]) --depth;
        const uint8_t* entry = &s.fst[size_t(i) * 12];
        const uint32_t word = Be32(entry);
        const size_t at = strings + (word & 0xFFFFFF);
        if (at >= s.fst.size()) return "the disc's file table is damaged";
        const char* begin = reinterpret_cast<const char*>(&s.fst[at]);
        const char* end = static_cast<const char*>(std::memchr(begin, 0, s.fst.size() - at));
        fs::path name;
        if (end == nullptr || !NameToPath(std::string(begin, end), name)) return "the disc's file table is damaged";
        const fs::path path = dirs[depth] / name;
        if ((word >> 24) != 0) {
            ++depth;
            dirs[depth] = path;
            ends[depth] = std::min(entries, Be32(entry + 8));
            fs::create_directories(path, ec);
            if (!fs::is_directory(path, ec)) return "couldn't create " + Utf8(path);
        } else {
            std::string error = s.Write(path, uint64_t(Be32(entry + 4)) << 2, Be32(entry + 8), done, progress, cancel);
            if (!error.empty()) return error;
            ++files;
        }
    }
    if (stats != nullptr) {
        stats->readNanos = s.extractReadNanos;
        stats->writeNanos = s.extractWriteNanos;
        stats->fsNanos = s.extractFsNanos;
        stats->progressNanos = s.extractProgressNanos;
    }
    return "";
}

} // namespace WiiDisc
