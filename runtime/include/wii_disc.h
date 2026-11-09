#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A Wii disc image: the game partition's sys/ (boot.bin, bi2.bin, apploader.img, main.dol, fst.bin),
// its files/, and the same copy extracted the way Dolphin's Extract Entire Disc makes it. A port of
// the Android app's WiiDisc.java.
//
// Supported containers: raw .iso/.gcm, .wbfs (including the .wbf1... parts of a split one), .ciso,
// .wia and .rvz. NKit images and GameCube discs are recognised and refused with a sentence.
//
// Extraction is what the PC chooser used to do; images are normally read in place instead, through
// Files() and Read().
namespace WiiDisc {

class Disc {
public:
    // A file of the partition: `path` is "Stage/01_01.arc" (the FST's own names) or, for the five
    // system files, "sys/boot.bin" and so on. `offset` is in the decrypted partition, hashes not
    // counted, so it is both the FST's own value and what Read() takes.
    struct File {
        std::string path;
        uint64_t offset = 0;
        uint64_t size = 0;
    };

    // Opens the image at `path`. On failure returns null and sets `error` to why, in words for the
    // player ("that isn't a Wii disc image").
    static std::unique_ptr<Disc> Open(const std::filesystem::path& path, std::string& error);

    // True when the file looks like one of the containers this reads, by extension. Cheap enough to
    // answer before the image is opened; Open() still has the final word.
    static bool LooksLikeImage(const std::filesystem::path& path);

    ~Disc();

    // The disc header's game ID (6 characters, such as SMNE01) and revision.
    const std::string& Id() const;
    int Revision() const;

    // Every file the disc has (the FST's, then the five system files), in FST order.
    const std::vector<File>& Files() const;

    // Size of `path` ("Stage/01_01.arc", "/Stage/01_01.arc" and "sys/main.dol" all work), or
    // UINT64_MAX when the disc has no such file.
    uint64_t FileSize(std::string_view path) const;

    // `size` bytes of `path` at `offset`, false when the disc has no such file or range. Reads are
    // serialized: the disc index and the preload thread both come through here.
    bool Read(std::string_view path, uint64_t offset, uint8_t* out, size_t size);
    bool Read(std::string_view path, std::vector<uint8_t>& out);

    // `size` bytes of the decrypted partition at `offset` (an offset in its data, hashes left out).
    // This is the space the FST's own file offsets and the DVD layer's disc offsets live in.
    bool ReadAt(uint64_t offset, uint8_t* out, size_t size);

    // How many bytes Extract() writes.
    uint64_t ExtractedSize() const;

    using Progress = std::function<void(uint64_t done, uint64_t total)>;

    // Writes sys/ and files/ into `root`, and sets `files` to how many files it wrote. Returns "" when
    // done, or why it stopped ("cancelled" once `cancel` is set). Whatever was written stays: the
    // caller extracts into a folder of its own and removes that folder on failure.
    std::string Extract(const std::filesystem::path& root, const Progress& progress,
                        const std::atomic<bool>& cancel, int& files);

    struct State;

private:
    explicit Disc(std::unique_ptr<State> state);
    std::unique_ptr<State> m_state;
};

} // namespace WiiDisc
