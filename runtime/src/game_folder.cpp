#include "game_folder.h"

#include "game_picker.h"
#include "runtime_config.h"
#include "runtime_log.h"
#include "runtime_product.h"
#include "ui_text.h"
#include "wii_disc.h"

#include <cryptopp/sha.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace GameFolder {
namespace {

namespace fs = std::filesystem;

bool IsExtractedDisc(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path / "files", ec) && fs::is_regular_file(path / "sys" / "fst.bin", ec);
}

// Why Switch found no game, for the report main.cpp shows before the boot; empty elsewhere and
// whenever one was found.
std::string g_startupProblem;
// True when the message above is the "nothing there" one rather than "something is there and unusable".
bool g_nothingFound = true;

// PlanStartup() checks the folder, then EnsureConfigured() checks the same folder a moment later.
// The scan stats all 1,189 files (~1.8 s), so the second call reuses the first result. Cleared after
// that use, and whenever the folder is about to change.
struct StartupCheck {
    std::filesystem::path root;
    std::string problem;
    bool incomplete = false;
    bool valid = false;
};
StartupCheck g_startupCheck;

// The game this build was translated from (runtime_product.h: the original, or a mod such as Newer).
std::string GameName() {
    return std::string(RuntimeProduct::Active().displayName);
}

// The one sentence a folder with any of the game's files left out gets, instead of starting and
// stopping in the boot (issue #29: part of Dolphin's extract got as far as the shaders, then closed).
// Translated in ui_text.cpp; the Android app's AppFiles.INCOMPLETE is the same sentence.
constexpr const char* kIncomplete = "The folder doesn't include all required files.";

// An FST name, or "" when it isn't plain ASCII (extractors spell other names differently, so such a
// file isn't looked for) or can't be a file name.
std::string AsciiName(const std::vector<char>& fst, size_t at) {
    std::string name;
    for (; at < fst.size() && fst[at] != 0; ++at) {
        const auto c = static_cast<unsigned char>(fst[at]);
        if (c < 0x20 || c >= 0x80 || c == '/' || c == '\\') {
            return {};
        }
        name += static_cast<char>(c);
    }
    return name == "." || name == ".." ? std::string() : name;
}

// The files the game needs that `root` doesn't have, as paths relative to it, at most `limit`:
// sys/boot.bin, sys/main.dol and sys/fst.bin, then every file the disc's own file table (fst.bin)
// lists under files/, so any region's or revision's disc passes as long as nothing was left out.
// Only whether each is there is checked (about 1200 files for the base game), never what's in it.
// The Android app does the same (GameFileList.java).
std::vector<std::string> MissingFiles(const fs::path& root, size_t limit) {
    std::vector<std::string> missing;
    std::error_code ec;
    for (const char* name : {"sys/boot.bin", "sys/main.dol", "sys/fst.bin"}) {
        if (!fs::is_regular_file(root / fs::u8path(name), ec)) {
            missing.emplace_back(name);
        }
    }
    if (!missing.empty()) {
        return missing;
    }
    std::ifstream in(root / "sys" / "fst.bin", std::ios::binary);
    const std::vector<char> fst((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto be32 = [&](size_t at) {
        return uint32_t(uint8_t(fst[at])) << 24 | uint32_t(uint8_t(fst[at + 1])) << 16 |
               uint32_t(uint8_t(fst[at + 2])) << 8 | uint32_t(uint8_t(fst[at + 3]));
    };
    // Entry 0 is the root: its third word is the number of entries, as a directory's is the index
    // past its last entry. The names follow the entries.
    const uint32_t entries = fst.size() >= 12 ? be32(8) : 0;
    if (entries == 0 || uint64_t(entries) * 12 > fst.size()) {
        return {"sys/fst.bin"};
    }
    const size_t strings = size_t(entries) * 12;
    std::vector<std::string> dirs(size_t(entries) + 1);  // "" below a folder that isn't looked in
    std::vector<uint32_t> ends(size_t(entries) + 1);
    size_t depth = 0;
    dirs[0] = "files";
    ends[0] = entries;
    for (uint32_t i = 1; i < entries && missing.size() < limit; ++i) {
        while (depth > 0 && i >= ends[depth]) {
            --depth;
        }
        const uint32_t word = be32(size_t(i) * 12);
        const std::string name = AsciiName(fst, strings + (word & 0xFFFFFF));
        const std::string path = dirs[depth].empty() || name.empty() ? std::string() : dirs[depth] + "/" + name;
        if ((word >> 24) != 0) {
            ++depth;
            dirs[depth] = path;
            ends[depth] = std::min(entries, be32(size_t(i) * 12 + 8));
        } else if (!path.empty() && !fs::is_regular_file(root / fs::u8path(path), ec)) {
            missing.push_back(path);
        }
    }
    return missing;
}

// Problem(), and whether the trouble is files left out (`incomplete`: the player gets kIncomplete
// in a message box too). Below.
std::string Check(const fs::path& root, bool* incomplete);

// The first disc image beside the program that passes CheckImage, or empty.
fs::path FirstUsableImage(const fs::path& program, std::string* problem, fs::path* failed);

std::string CheckImage(const fs::path& image, bool* incomplete);

// Why a disc with this ID and revision can't be used ("" when it can).
std::string IdProblem(const std::string& id, int revision) {
    const Disc kind = Identify(id, revision);
    if (kind == Disc::Supported) {
        return {};
    }
    if (kind == Disc::NeedsSupportModule) {
        // The PAL support module hooks in here: once For() offers one, the chooser asks to install it.
        if (const std::optional<SupportModule::Offer> offer = SupportModule::For(id, revision)) {
            return offer->message;
        }
        RT_LOGF(RT_TAG_RUNTIME, "game folder: %s revision %d needs a support module; none is available\n", id.c_str(),
                revision);
    }
    const std::string disc(RuntimeProduct::Active().discName);
    // The release's two programs: each points to the other's game.
    if (RuntimeProduct::Active().gameId == "SMNE01" && id == "SMNE03") {
        return "That is Newer Super Mario Bros. Wii (SMNE03): open it with NewerSMBW, the release's other "
               "program. This one runs only " + disc + ".";
    }
    if (RuntimeProduct::Active().gameId == "SMNE03" && id == "SMNE01") {
        return "That is New Super Mario Bros. Wii (SMNE01): open it with NSMBW, the release's other program. "
               "This one runs only " + disc + ".";
    }
    if (id != RuntimeProduct::Active().gameId) {
        return "That is " + id + ", not " + disc + ". This build runs only that disc: it was translated from it.";
    }
    return "That is " + id + " revision " + std::to_string(revision) + ". This build runs only " + disc +
           " (Dolphin shows the revision under Properties > Info).";
}

// A disc image is extracted into "<target>.importing" first, with this marker inside, and renamed to
// its target once complete. A folder that still has the marker was left by an import that never
// finished (a crash, or the program killed), so it can go.
constexpr const char* kImportingSuffix = ".importing";
constexpr const char* kImportingMarker = ".nsmbw-importing";

fs::path ProgramDirectory() {
    return RuntimeConfigFile::ExecutableDirectory().value_or(fs::path{});  // the compat build's too
}

// The folder beside the program that is used by itself: "game", or a mod's own (runtime_product.h).
std::string GameFolderName() {
    return std::string(RuntimeProduct::Active().programGameFolder);
}

// "that isn't a Wii disc image" -> "That isn't a Wii disc image."
std::string SentenceOf(std::string text) {
    if (!text.empty()) {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
        text += '.';
    }
    return text;
}

// Removes a folder this program made. Windows Defender may hold a file it has just been handed for a
// moment, so a failure is retried.
bool RemoveTree(const fs::path& path) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::error_code ec;
#if defined(MKW_PLATFORM_SWITCH)
        // remove_all() gives up on a tree this size on Horizon (a 1,189-file folder was left in place
        // with only an error code), so every entry is removed by name, contents before their folder.
        std::vector<fs::path> entries;
        for (fs::recursive_directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec)) {
            entries.push_back(it->path());
        }
        if (ec) {
            RT_LOGF(RT_TAG_RUNTIME, "couldn't list %s: %s\n", RuntimeConfigFile::PathToUtf8(path).c_str(),
                    ec.message().c_str());
        }
        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            std::error_code removeError;
            fs::remove(*it, removeError);
            if (removeError) {
                RT_LOGF(RT_TAG_RUNTIME, "couldn't remove %s: %s\n", RuntimeConfigFile::PathToUtf8(*it).c_str(),
                        removeError.message().c_str());
            }
        }
        std::error_code rootError;
        fs::remove(path, rootError);
        if (rootError) {
            RT_LOGF(RT_TAG_RUNTIME, "couldn't remove %s: %s\n", RuntimeConfigFile::PathToUtf8(path).c_str(),
                    rootError.message().c_str());
        }
#else
        fs::remove_all(path, ec);
        if (ec) {
            RT_LOGF(RT_TAG_RUNTIME, "couldn't remove %s: %s\n", RuntimeConfigFile::PathToUtf8(path).c_str(),
                    ec.message().c_str());
        }
#endif
        if (!fs::exists(fs::symlink_status(path, ec))) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}

bool RenameTree(const fs::path& from, const fs::path& to, std::error_code& ec) {
    for (int attempt = 0; attempt < 3; ++attempt) {
        fs::rename(from, to, ec);
        if (!ec) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}

// Where a disc image is extracted, best first: "game" next to the program (picked up there at every
// start, so two installs keep two games; a mod has a name of its own, so its program can sit beside
// this one), then "game" in the data folder (already the mod's own). A portable install keeps
// everything in its data folder.
std::vector<fs::path> ExtractTargets() {
    const fs::path data = RuntimeConfigFile::ApplicationDataDirectory() / "game";
    const fs::path program = ProgramDirectory();
    if (RuntimeConfigFile::PortableRootDirectory() || program.empty()) {
        return {data};
    }
    return {program / fs::u8path(GameFolderName()), data};
}

fs::path Staging(const fs::path& target) {
    fs::path staging = target;
    staging += kImportingSuffix;
    return staging;
}

void CleanStaleImports() {
    for (const fs::path& target : ExtractTargets()) {
        const fs::path staging = Staging(target);
        std::error_code ec;
        if (fs::is_regular_file(staging / kImportingMarker, ec)) {
            RT_LOGF(RT_TAG_RUNTIME, "disc image: removing an unfinished import at %s\n",
                    RuntimeConfigFile::PathToUtf8(staging).c_str());
            RemoveTree(staging);
        }
    }
}

std::string Megabytes(uint64_t bytes) {
    return std::to_string((bytes + (1u << 20) - 1) >> 20);
}

// What writes the disc's files into the staging folder: "" when done, "cancelled" when that was
// asked for, or why it failed. The picker drives it with its progress window, the Switch with its
// boot splash.
using ExtractStep = std::function<std::string(WiiDisc::Disc& disc, const std::filesystem::path& staging,
                                              const std::atomic<bool>* cancel, int& files,
                                              WiiDisc::Disc::ExtractStats* stats)>;

// Opens `image`, checks it, and writes it into the first free folder among `targets`: "<target>.importing"
// with a marker, the free-space check, `extract`, the same folder check every extracted game gets, then
// the rename into place. False when it can't be done: `problem` then says why, or is empty when the
// extraction was cancelled; `incomplete` as Check() sets it.
bool ExtractImageTo(const fs::path& image, const std::vector<fs::path>& targets, const ExtractStep& extract,
                    std::string& problem, bool* incomplete, fs::path* extractedTo = nullptr) {
    std::string error;
    const std::unique_ptr<WiiDisc::Disc> disc = WiiDisc::Disc::Open(image, error);
    if (!disc) {
        problem = SentenceOf(error);
        return false;
    }
    // The ID and revision first, before anything is written.
    problem = IdProblem(disc->Id(), disc->Revision());
    if (!problem.empty()) {
        return false;
    }

    fs::path target, staging, taken;
    for (const fs::path& candidate : targets) {
        const fs::path folder = Staging(candidate);
        std::error_code ec;
        // Never write over a folder this program didn't make: a "game" next to the program may be a
        // link to the player's own Dolphin extract.
        if (fs::exists(fs::symlink_status(candidate, ec))) {
            taken = candidate;
            continue;
        }
        if (fs::exists(fs::symlink_status(folder, ec))) {
            taken = folder;
            continue;
        }
        fs::create_directories(candidate.parent_path(), ec);
        if (!fs::create_directory(folder, ec)) {
            continue;  // can't write here (Program Files, read-only media, /opt)
        }
        std::ofstream marker(folder / kImportingMarker);
        marker << "An unfinished import of a disc image; removed at the next start.\n";
        marker.close();
        if (!marker) {
            RemoveTree(folder);
            continue;
        }
        target = candidate;
        staging = folder;
        break;
    }
    if (target.empty()) {
        if (!taken.empty()) {
            problem = "There's already a folder named " + RuntimeConfigFile::PathToUtf8(taken.filename()) + " in " +
                      RuntimeConfigFile::PathToUtf8(taken.parent_path()) + ". Move or delete it, then try again.";
        } else {
            problem = "Couldn't make a folder for the game next to this program or in " +
                      RuntimeConfigFile::PathToUtf8(RuntimeConfigFile::ApplicationDataDirectory()) + ".";
        }
        return false;
    }

    const uint64_t size = disc->ExtractedSize();
    std::error_code spaceError;
    const fs::space_info space = fs::space(target.parent_path(), spaceError);
    if (!spaceError && space.available < size + (64u << 20)) {
        RemoveTree(staging);
        problem = "There isn't enough free space in " + RuntimeConfigFile::PathToUtf8(target.parent_path()) +
                  ": the game takes " + Megabytes(size) + " MB.";
        return false;
    }

    RT_LOGF(RT_TAG_RUNTIME, "disc image: extracting %s (%s revision %d, aes %s) to %s\n",
            RuntimeConfigFile::PathToUtf8(image).c_str(), disc->Id().c_str(), disc->Revision(),
            disc->AesBackend(), RuntimeConfigFile::PathToUtf8(staging).c_str());
    const auto start = std::chrono::steady_clock::now();
    int files = 0;
    WiiDisc::Disc::ExtractStats stats;
    const std::string result = extract(*disc, staging, nullptr, files, &stats);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    if (result == "cancelled") {
        RT_LOGF(RT_TAG_RUNTIME, "disc image: cancelled after %.1f s\n", seconds);
        RemoveTree(staging);
        problem.clear();
        return false;
    }
    if (!result.empty()) {
        RT_LOGF(RT_TAG_RUNTIME, "disc image: failed after %.1f s: %s\n", seconds, result.c_str());
        RemoveTree(staging);
        problem = "Couldn't extract the disc image: " + result + ". Nothing was changed.";
        return false;
    }
    // What a folder is checked for: every file there, the code modules, and a mod's version.
    bool incompleteFlag = false;
    problem = Check(staging, incomplete != nullptr ? incomplete : &incompleteFlag);
    if (!problem.empty()) {
        RemoveTree(staging);
        return false;
    }
    std::error_code ec;
    fs::remove(staging / kImportingMarker, ec);
    if (!RenameTree(staging, target, ec)) {
        RemoveTree(staging);
        problem = "Couldn't extract the disc image: couldn't rename " + RuntimeConfigFile::PathToUtf8(staging) +
                  " (" + ec.message() + "). Nothing was changed.";
        return false;
    }
    RT_LOGF(RT_TAG_RUNTIME, "disc image: %s -> %s (%llu bytes, %d files, %.1f s: read %.1f s, write %.1f s, "
                            "file system %.1f s, progress %.1f s)\n",
            RuntimeConfigFile::PathToUtf8(image).c_str(), RuntimeConfigFile::PathToUtf8(target).c_str(),
            static_cast<unsigned long long>(size), files, seconds, stats.readNanos / 1e9, stats.writeNanos / 1e9,
            stats.fsNanos / 1e9, stats.progressNanos / 1e9);
    g_startupCheck.valid = false;

    if (target.parent_path() == RuntimeConfigFile::ResolveConfigPath().parent_path()) {
        // Beside Config.toml (the data folder): saved as relative, so a portable install can move.
        RuntimeConfigFile::SetDvdRoot(fs::path("game"));
    } else if (RuntimeConfigFile::DvdRoot().empty()) {
        RuntimeConfigFile::SetDvdRoot(target);
    } else {
        RuntimeConfigFile::UseDvdRootForThisRun(target);  // next to the program: found there every start
    }
    if (extractedTo != nullptr) {
        *extractedTo = target;
    }
    return true;
}

#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
// What to extract, and how: the chooser's Help.
std::string Steps(const char* openDolphin) {
    return "This port runs your own copy of " + std::string(RuntimeProduct::Active().discName) +
           ", from a disc you own. Choose one of these:\n\n"
           "Open Disc Image: the image you dumped from your disc. .iso, .wbfs, .ciso, .wia and .rvz all "
           "work, and the game reads the image itself, so nothing is extracted and nothing is copied.\n\n"
           "Open Extracted Disc Folder: a folder Dolphin extracted the disc to.\n"
           "1. " + openDolphin + " and find " + GameName() + " in the game list.\n"
           "2. Right-click it, choose Properties, then the Filesystem tab.\n"
           "3. Right-click the disc at the top and choose Extract Entire Disc, then pick an empty folder.\n"
           "4. Choose that folder here. Keep it: the game reads it every time.\n\n"
           "(Dolphin shows the revision under Properties > Info. Or put the extracted folder next to this "
           "program and name it \"" + GameFolderName() + "\", or put one disc image next to it.)";
}

GamePicker::Text PickerText() {
    GamePicker::Text text;
    text.title = GameName();
    text.body = "This port runs your own copy of " + std::string(RuntimeProduct::Active().discName) +
                ". Open your disc image, or the folder Dolphin's Extract Entire Disc made from it.";
#if defined(_WIN32)
    text.help = Steps("Open Dolphin on this computer");
#else
    text.help = Steps("Open Dolphin");
#endif
    text.isoTitle = "Choose your disc image of " + GameName();
    text.folderTitle = "Choose the folder Dolphin extracted " + GameName() + " to";
    text.mods = RuntimeProduct::Active().gameId == "SMNE01";  // the original game's build only
    return text;
}

// Extracts the disc image at `image` and makes it the game folder. False when it can't: `problem`
// then says why, or is empty when the player cancelled; `incomplete` as Check() sets it.
bool ImportDiscImage(const fs::path& image, std::string& problem, bool& incomplete) {
    int files = 0;
    return ExtractImageTo(
        image, ExtractTargets(),
        [&](WiiDisc::Disc& disc, const fs::path& staging, const std::atomic<bool>*, int& files,
            WiiDisc::Disc::ExtractStats*) {
            return GamePicker::RunWithProgress(
                PickerText(), "Extracting the disc image",
                [&](const GamePicker::Progress& progress, const std::atomic<bool>& cancel) {
                    return disc.Extract(staging, progress, cancel, files);
                });
        },
        problem, &incomplete);
}
#endif

#if defined(MKW_PLATFORM_SWITCH)
// No folder picker on Switch: the beside-program game folder, or one disc image beside it.
namespace {
// What a player is told when nothing usable was found. The NRO's folder is whatever getcwd() said
// (host_platform.cpp); nothing here is a fixed path.
std::string NoGameProblem(const fs::path& program, const fs::path& image, const std::string& imageProblem,
                          const std::string& folderProblem, const std::string& configuredProblem) {
    const std::string where = program.empty() ? std::string("next to NSMBW.nro")
                                              : RuntimeConfigFile::PathToUtf8(program);
    // What to do comes last, after whatever was found and rejected: leading with "No game was found"
    // while naming a folder that is plainly there reads as a contradiction.
    std::string reasons;
    if (!folderProblem.empty()) {
        reasons += "The game folder can't be used: " + folderProblem + "\n\n";
    }
    if (!image.empty() && !imageProblem.empty()) {
        reasons += "The disc image " + RuntimeConfigFile::PathToUtf8(image.filename()) + " can't be used: " +
                   imageProblem + "\n\n";
    }
    if (!configuredProblem.empty()) {
        reasons += "The [paths] dvd_root entry can't be used: " + configuredProblem + "\n\n";
    }
    const std::string guidance =
        "Put your own copy of " + std::string(RuntimeProduct::Active().discName) + " next to NSMBW.nro (" +
        where + "): the extracted game folder named \"game\", or a disc image (.iso, .wbfs, .ciso, .wia or "
        ".rvz), which is extracted into that folder on the next start.";
    return reasons.empty() ? "No game was found. " + guidance : reasons + guidance;
}
// Whether the folder is unusable in a way an extraction repairs: not an extracted disc at all,
// no readable sys/boot.bin, or files its sys/fst.bin lists that are missing. A changed module or
// another disc's folder is not one of these: that may be the player's own mod, so it is left alone.
bool StructurallyBroken(const fs::path& root, std::string& problem) {
    if (!IsExtractedDisc(root)) {
        problem = Check(root, nullptr);
        return true;
    }
    std::error_code ec;
    if (!fs::is_regular_file(root / "sys" / "boot.bin", ec)) {
        problem = Check(root, nullptr);
        return true;
    }
    std::ifstream boot(root / "sys" / "boot.bin", std::ios::binary);
    char header[8] = {};
    if (!boot.read(header, sizeof(header))) {
        problem = Check(root, nullptr);
        return true;
    }
    bool incomplete = false;
    problem = Check(root, &incomplete);
    g_startupCheck = {root, problem, incomplete, true};
    if (incomplete) {
        return true;
    }
    problem.clear();
    return false;
}

// The place an image is extracted to that holds `folder` (the folder itself, or its parent for the
// disc's DATA subtree), so deleting it also clears the way for the extraction.
fs::path ExtractionTargetFor(const fs::path& folder) {
    for (const fs::path& target : ExtractTargets()) {
        if (target == folder || target == folder.parent_path()) {
            return target;
        }
    }
    return ExtractTargets().front();
}
} // namespace

#endif

} // namespace

#if defined(MKW_PLATFORM_SWITCH)
StartupPlan PlanStartup() {
    StartupPlan plan;
    const fs::path program = ProgramDirectory();
    if (program.empty()) {
        return plan;  // EnsureConfigured() decides
    }
    CleanStaleImports();
    // The image to extract from: beside the program first, then one named in [paths] dvd_root.
    const auto startupImage = [&] {
        if (const fs::path image = FirstUsableImage(program, nullptr, nullptr); !image.empty()) {
            return image;
        }
        const fs::path configured = RuntimeConfigFile::ResolvedDvdRoot();
        std::error_code ec;
        if (!configured.empty() && fs::is_regular_file(configured, ec) && CheckImage(configured, nullptr).empty()) {
            return configured;
        }
        return fs::path{};
    };
    // Anything at the game folder's own path counts, including an empty or half-deleted folder that
    // Find() doesn't recognize: that is exactly what a repair replaces.
    const fs::path root = program / fs::u8path(GameFolderName());
    std::error_code dirError;
    if (!fs::is_directory(root, dirError)) {
        if (const fs::path image = startupImage(); !image.empty()) {
            g_startupCheck.valid = false;
            plan.kind = StartupPlan::Kind::Extract;
            plan.image = image;
        }
        return plan;
    }
    const fs::path found = Find(root);
    const fs::path beside = found.empty() ? root : found;
    std::string structural;
    if (!StructurallyBroken(beside, structural)) {
        return plan;  // usable, or a problem that deleting the folder would not fix
    }
    const fs::path image = startupImage();
    if (image.empty()) {
        return plan;  // nothing to rebuild it from: the folder stays, EnsureConfigured explains
    }
    std::error_code ec;
    if (fs::is_symlink(fs::symlink_status(beside, ec))) {
        return plan;  // not this program's folder
    }
    g_startupCheck.valid = false;
    plan.kind = StartupPlan::Kind::Repair;
    plan.folder = ExtractionTargetFor(beside);
    plan.image = image;
    plan.problem = structural;
    RT_LOGF(RT_TAG_RUNTIME, "game folder: %s can't be used: %s\n", RuntimeConfigFile::PathToUtf8(beside).c_str(),
            structural.c_str());
    return plan;
}

bool DeleteStartupFolder(const fs::path& folder, std::string& problem) {
    bool allowed = false;
    for (const fs::path& target : ExtractTargets()) {
        if (target == folder) {
            allowed = true;
        }
    }
    if (!allowed) {
        problem = "That folder isn't one this program extracts to: " + RuntimeConfigFile::PathToUtf8(folder) + ".";
        return false;
    }
    RT_LOGF(RT_TAG_RUNTIME, "game folder: deleting %s\n", RuntimeConfigFile::PathToUtf8(folder).c_str());
    if (!RemoveTree(folder)) {
        problem = "Couldn't delete " + RuntimeConfigFile::PathToUtf8(folder) + ".";
        return false;
    }
    return true;
}

bool ExtractStartupImage(const fs::path& image, const std::function<void(float, const char*)>& progress,
                         std::string& problem) {
    if (progress) {
        progress(0.0f, "Extracting the disc image");
    }
    const auto step = [&](WiiDisc::Disc& disc, const fs::path& staging, const std::atomic<bool>*, int& files,
                          WiiDisc::Disc::ExtractStats* stats) {
        std::atomic<bool> never{false};
        // The splash redraw is not free, so the bar advances in whole percents, not per chunk.
        float shown = -1.0f;
        const auto bytes = [&](uint64_t done, uint64_t total) {
            const float fraction =
                total != 0 ? static_cast<float>(static_cast<double>(done) / static_cast<double>(total)) : 0.0f;
            if (progress == nullptr || (fraction < 1.0f && fraction * 100.0f < shown + 1.0f)) {
                return;
            }
            shown = fraction * 100.0f;
            char status[96];
            std::snprintf(status, sizeof(status), "Extracting the disc image, %d%%",
                          static_cast<int>(shown));
            progress(fraction, status);
        };
        return disc.Extract(staging, bytes, never, files, stats);
    };
    return ExtractImageTo(image, ExtractTargets(), step, problem, nullptr);
}
#endif

fs::path Find(const fs::path& path) {
    if (path.empty()) {
        return {};
    }
    if (IsExtractedDisc(path)) {
        return path;
    }
    if (IsExtractedDisc(path / "DATA")) {
        return path / "DATA";
    }
    std::error_code ec;
    if (fs::is_regular_file(path, ec) && !ec) {
        return path;  // a disc image: Problem() opens it
    }
    return {};
}

Disc Identify(std::string_view id, int revision) {
    const RuntimeProduct::Descriptor& product = RuntimeProduct::Active();
    if (id == product.gameId) {
        return revision == 1 ? Disc::Supported : Disc::Unsupported;
    }
    // The PAL disc, revision 1, for the base game.
    if (product.gameId == "SMNE01" && id == "SMNP01" && revision == 1) {
        return Disc::NeedsSupportModule;
    }
    return Disc::Unsupported;
}

namespace SupportModule {
std::optional<Offer> For(std::string_view, int) {
    return std::nullopt;  // none exists yet
}
} // namespace SupportModule

namespace {
// The game's code modules were translated with the rest: a disc whose modules were changed (a
// repacked or modded image keeps the disc ID) would link nothing and stop at the first scene.
// Each is the original's LZ file or, decompressed, the original's .rel. Newer's disc has the same.
struct Module {
    const char* name;
    uintmax_t lzSize, relSize;
};
constexpr Module kModules[] = {
    {"d_profileNP", 8791, 24244},
    {"d_basesNP", 1190805, 3019336},
    {"d_enemiesNP", 779805, 2137424},
    {"d_en_bossNP", 229984, 605552},
};

// `sizeOf` answers for a path inside files/ ("rels/d_profileNP.rel.LZ") with UINT64_MAX when the
// disc has no such file. The same checks therefore run for an extracted folder and for an image.
template <typename SizeOf>
std::string CheckModules(const std::string& id, const std::string& disc, SizeOf sizeOf) {
    for (const Module& module : kModules) {
        const std::string lz = std::string("rels/") + module.name + ".rel.LZ";
        const std::string rel = std::string("rels/") + module.name + ".rel";
        const uintmax_t lzSize = sizeOf(lz);
        const uintmax_t relSize = sizeOf(rel);
        if ((lzSize != UINT64_MAX && lzSize == module.lzSize) ||
            (relSize != UINT64_MAX && relSize == module.relSize)) {
            continue;
        }
        if (lzSize == UINT64_MAX && relSize == UINT64_MAX) {
            return "That copy of " + id + " has no files/" + lz + ". This build needs " + disc +
                   " as Dolphin's Extract Entire Disc makes it, with nothing changed or left out.";
        }
        const bool lzMissing = lzSize == UINT64_MAX;
        return "That copy of " + id + " has a changed files/" + (lzMissing ? rel : lz) + " (" +
               std::to_string(lzMissing ? relSize : lzSize) + " bytes, not " +
               std::to_string(lzMissing ? module.relSize : module.lzSize) +
               "): it isn't the original disc (a modded or repacked image?). This build runs only " + disc +
               ", unchanged: it was translated from that disc's code.";
    }
    return {};
}

// A mod keeps its disc ID from version to version; its own code tells them apart.
template <typename SizeOf>
std::string CheckVersionFile(const std::string& id, const std::string& disc, const std::string& versionFile,
                             uintmax_t expected, SizeOf sizeOf) {
    if (versionFile.empty()) {
        return {};
    }
    const uintmax_t size = sizeOf(versionFile);
    if (size == UINT64_MAX) {
        return "That is " + id + ", but it has no files/" + versionFile + ". This build runs only " + disc + ".";
    }
    if (size != expected) {
        return "That is another version of " + GameName() + " (its files/" + versionFile + " is " +
               std::to_string(size) + " bytes, not " + std::to_string(expected) + "). This build runs only " +
               disc + ": it was translated from that version's code.";
    }
    return {};
}

std::string Check(const fs::path& root, bool* incomplete) {
    if (g_startupCheck.valid && g_startupCheck.root == root) {
        g_startupCheck.valid = false;
        if (incomplete != nullptr) {
            *incomplete = g_startupCheck.incomplete;
        }
        return g_startupCheck.problem;
    }
    // Files left out: kIncomplete (in the player's language) and which ones, for the chooser's red line.
    const auto leftOut = [&](const std::string& which) {
        if (incomplete != nullptr) {
            *incomplete = true;
        }
        RT_LOGF(RT_TAG_RUNTIME, "game folder %s: %s\n", RuntimeConfigFile::PathToUtf8(root).c_str(), which.c_str());
        return UiText::Tr(kIncomplete) + " " + which;
    };
    if (!IsExtractedDisc(root)) {
        return leftOut("It needs the files and sys folders Dolphin's Extract Entire Disc makes: choose the folder "
                       "that holds both.");
    }
    std::error_code bootError;
    if (!fs::is_regular_file(root / "sys" / "boot.bin", bootError)) {
        return leftOut("It has no sys/boot.bin.");
    }
    std::ifstream boot(root / "sys" / "boot.bin", std::ios::binary);
    char header[8] = {};
    if (!boot.read(header, sizeof(header))) {
        return "That folder's sys/boot.bin is too short, so the game can't be identified.";
    }
    const std::string id(header, 6);
    const int revision = static_cast<unsigned char>(header[7]);
    const RuntimeProduct::Descriptor& product = RuntimeProduct::Active();
    const std::string disc(product.discName);
    if (std::string problem = IdProblem(id, revision); !problem.empty()) {
        return problem;
    }
    // Every file the game reads (its own file table's list): a folder copied in part would start,
    // build its shaders and stop at whatever file isn't there.
    const auto start = std::chrono::steady_clock::now();
    const std::vector<std::string> missing = MissingFiles(root, SIZE_MAX);
    RT_LOGF(RT_TAG_RUNTIME, "game folder: %zu required file(s) missing (%.1f ms)\n", missing.size(),
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    if (!missing.empty()) {
        return leftOut(missing.size() == 1 ? "Missing: " + missing[0] + "."
                                           : "Missing: " + missing[0] + " and " + std::to_string(missing.size() - 1) +
                                                 " more files.");
    }
    const auto sizeOf = [&](std::string_view relative) -> uintmax_t {
        std::error_code ec;
        const uintmax_t size = fs::file_size(root / fs::u8path("files/" + std::string(relative)), ec);
        return ec ? UINT64_MAX : size;
    };
    if (std::string problem = CheckModules(id, disc, sizeOf); !problem.empty()) {
        return problem;
    }
    return CheckVersionFile(id, disc, std::string(product.versionFile), product.versionFileSize, sizeOf);
}
} // namespace

namespace {
// A disc image is checked by opening it and asking the same questions a folder is asked: the disc ID
// and revision, the code modules the translated build needs, and a mod's version file.
std::string CheckImage(const fs::path& image, bool* incomplete) {
    (void)incomplete;
    std::string error;
    const std::unique_ptr<WiiDisc::Disc> disc = WiiDisc::Disc::Open(image, error);
    if (!disc) {
        RT_LOGF(RT_TAG_RUNTIME, "disc image %s: %s\n", RuntimeConfigFile::PathToUtf8(image).c_str(), error.c_str());
        return SentenceOf(error);
    }
    const std::string id = disc->Id();
    const RuntimeProduct::Descriptor& product = RuntimeProduct::Active();
    const std::string discName(product.discName);
    if (std::string problem = IdProblem(id, disc->Revision()); !problem.empty()) {
        return problem;
    }
    const auto sizeOf = [&](std::string_view relative) -> uintmax_t { return disc->FileSize(relative); };
    if (std::string problem = CheckModules(id, discName, sizeOf); !problem.empty()) {
        return problem;
    }
    if (std::string problem =
            CheckVersionFile(id, discName, std::string(product.versionFile), product.versionFileSize, sizeOf);
        !problem.empty()) {
        return problem;
    }
    return {};
}
} // namespace

const std::string& StartupProblem() {
    return g_startupProblem;
}

const char* StartupProblemTitle() {
    return g_nothingFound ? "Unable to start because the game files are missing."
                          : "Unable to start because the game files can't be used.";
}

std::string Problem(const fs::path& root) {
    std::error_code ec;
    if (!IsExtractedDisc(root) && fs::is_regular_file(root, ec) && !ec) {
        return CheckImage(root, nullptr);
    }
    return Check(root, nullptr);
}

namespace {
// Each retail disc's main.dol, as projects/nsmbw/variants/variants.toml pins it (main_dol_sha256).
// The Japanese disc has none pinned yet, so it never counts as retail until one is added here.
struct RetailDol {
    const char* id;
    const char* sha256;
};
constexpr RetailDol kRetailDols[] = {
    {"SMNE01", "6d755ec24c395fee600a856a85785e0e99c0b7ecb680d573a6cdd1aa6f7beee7"},  // USA rev 1
    {"SMNP01", "51a2a8b0941e8fe3390404a9b2094e3c78246a999efa58fcc8a7c534c11d7b5b"},  // Europe rev 1
};

// SHA-256 in lowercase hex, the spelling kRetailDols uses.
std::string Sha256Hex(const uint8_t* data, size_t size) {
    CryptoPP::SHA256 hash;
    hash.Update(data, size);
    CryptoPP::byte digest[CryptoPP::SHA256::DIGESTSIZE];
    hash.Final(digest);
    static const char* hex = "0123456789abcdef";
    std::string text;
    for (const auto b : digest) {
        text += hex[b >> 4];
        text += hex[b & 15];
    }
    return text;
}

// Every supported image beside the program, by name, so which one is "first" does not depend on the
// order the directory happens to hand out.
std::vector<fs::path> FindProgramImages(const fs::path& program) {
    std::vector<fs::path> images;
    if (program.empty()) {
        return images;
    }
    std::error_code ec;
    for (fs::directory_iterator it(program, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path& path = it->path();
        // A macOS ._ sidecar is not an image.
        if (path.filename().native().rfind(fs::path("._").native(), 0) == 0) {
            continue;
        }
        if (WiiDisc::Disc::LooksLikeImage(path)) {
            images.push_back(path);
        }
    }
    std::sort(images.begin(), images.end());
    return images;
}

// The first image beside the program that passes the checks. `problem` and `failed` describe the
// first one that did not, for the message when none does.
fs::path FirstUsableImage(const fs::path& program, std::string* problem, fs::path* failed) {
    for (const fs::path& image : FindProgramImages(program)) {
        const std::string reason = Problem(image);
        if (reason.empty()) {
            return image;
        }
        RT_LOGF(RT_TAG_RUNTIME, "disc image: %s can't be used: %s\n",
                RuntimeConfigFile::PathToUtf8(image).c_str(), reason.c_str());
        if (problem != nullptr && problem->empty()) {
            *problem = reason;
        }
        if (failed != nullptr && failed->empty()) {
            *failed = image;
        }
    }
    return {};
}
// A file's SHA-256 in lowercase hex; empty when it can't be read.
std::string MainDolSha256(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    CryptoPP::SHA256 hash;
    std::vector<char> buffer(1 << 16);
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (in.gcount() > 0) {
            hash.Update(reinterpret_cast<const CryptoPP::byte*>(buffer.data()), static_cast<size_t>(in.gcount()));
        }
    }
    CryptoPP::byte digest[CryptoPP::SHA256::DIGESTSIZE];
    hash.Final(digest);
    static const char* hex = "0123456789abcdef";
    std::string text;
    for (const auto b : digest) {
        text += hex[b >> 4];
        text += hex[b & 15];
    }
    return text;
}
} // namespace

bool RetailDisc() {
    static const bool retail = [] {
        const fs::path root = Find(RuntimeConfigFile::ResolvedDvdRoot());
        if (root.empty()) {
            return false;
        }
        std::string id;
        std::string dolSha256;
        if (IsExtractedDisc(root)) {
            std::ifstream boot(root / "sys" / "boot.bin", std::ios::binary);
            char header[6] = {};
            if (!boot.read(header, sizeof(header))) {
                return false;
            }
            id.assign(header, sizeof(header));
            dolSha256 = MainDolSha256(root / "sys" / "main.dol");
        } else {
            std::string error;
            const std::unique_ptr<WiiDisc::Disc> disc = WiiDisc::Disc::Open(root, error);
            if (!disc) {
                return false;
            }
            id = disc->Id();
            std::vector<uint8_t> dol;
            if (disc->Read("sys/main.dol", dol)) {
                dolSha256 = Sha256Hex(dol.data(), dol.size());
            }
        }
        for (const RetailDol& dol : kRetailDols) {
            if (id == dol.id) {
                const bool same = dolSha256 == dol.sha256;
                RT_LOGF(RT_TAG_RUNTIME, "game folder: %s, %s main.dol\n", id.c_str(), same ? "the retail" : "a changed");
                return same;
            }
        }
        return false;
    }();
    return retail;
}

bool EnsureConfigured() {
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    CleanStaleImports();
    // A "game" folder next to the program needs no questions, and wins over the saved folder: every
    // install shares Documents\MarioWiiSaveData, so this is how two installs play two games (say the
    // original and a mod), each with its own saves (nand_path.h keeps saves per game). A mod's program
    // looks for its own name ("game-newer"), so the release's two programs keep their games apart.
    std::string besideProblem;
    bool besideIncomplete = false;
    std::error_code besideError;
    if (const fs::path besidePath = ProgramDirectory() / fs::u8path(GameFolderName());
        !ProgramDirectory().empty() && fs::is_directory(besidePath, besideError)) {
        const fs::path beside = Find(besidePath);
        besideProblem = Check(beside.empty() ? besidePath : beside, &besideIncomplete);
        if (besideProblem.empty()) {
            if (RuntimeConfigFile::DvdRoot().empty()) {
                RuntimeConfigFile::SetDvdRoot(beside);
            } else {
                RuntimeConfigFile::UseDvdRootForThisRun(beside);
            }
            RT_LOGF(RT_TAG_RUNTIME, "game folder: %s\n", RuntimeConfigFile::PathToUtf8(beside).c_str());
            return true;
        }
        RT_LOGF(RT_TAG_RUNTIME, "game folder: %s can't be used: %s\n", RuntimeConfigFile::PathToUtf8(besidePath).c_str(),
                besideProblem.c_str());
    }
    // A disc image beside the program is used by itself too, as a folder is.
    if (const fs::path image = FirstUsableImage(ProgramDirectory(), nullptr, nullptr); !image.empty()) {
        if (RuntimeConfigFile::DvdRoot().empty()) {
            RuntimeConfigFile::SetDvdRoot(image);
        } else {
            RuntimeConfigFile::UseDvdRootForThisRun(image);
        }
        RT_LOGF(RT_TAG_RUNTIME, "disc image: %s\n", RuntimeConfigFile::PathToUtf8(image).c_str());
        return true;
    }
#elif defined(MKW_PLATFORM_SWITCH)
    // No folder picker on Switch: the game folder beside the program, then one disc image beside it,
    // then [paths] dvd_root. Finding nothing is reported by StartupProblem(), before the boot.
    {
        const fs::path program = ProgramDirectory();
        std::string folderProblem;
        fs::path besideFolder;
        if (const fs::path beside = Find(program / "game"); !beside.empty()) {
            besideFolder = beside;
            folderProblem = Problem(beside);
            if (folderProblem.empty()) {
                if (RuntimeConfigFile::DvdRoot().empty()) {
                    RuntimeConfigFile::SetDvdRoot(beside);
                } else {
                    RuntimeConfigFile::UseDvdRootForThisRun(beside);
                }
                RT_LOGF(RT_TAG_RUNTIME, "game folder: %s\n", RuntimeConfigFile::PathToUtf8(beside).c_str());
                return true;
            }
            RT_LOGF(RT_TAG_RUNTIME, "game folder: %s can't be used: %s\n",
                    RuntimeConfigFile::PathToUtf8(beside).c_str(), folderProblem.c_str());
        }
        std::string imageProblem;
        fs::path rejectedImage;
        // An image is only a source for the extraction (game_folder.cpp's PlanStartup), never read in
        // place, so here it only has a message: a folder that is there but can't be used is reported
        // rather than silently skipped in favour of the vanilla image.
        FirstUsableImage(program, &imageProblem, &rejectedImage);
        std::string configuredProblem;
        const fs::path configuredPath = Find(RuntimeConfigFile::ResolvedDvdRoot());
        if (configuredPath.empty()) {
            if (!RuntimeConfigFile::DvdRoot().empty()) {
                const fs::path saved = RuntimeConfigFile::ResolvedDvdRoot();
                std::error_code existsError;
                configuredProblem = fs::exists(saved, existsError)
                                        ? Problem(saved)
                                        : "it isn't there any more (" + RuntimeConfigFile::PathToUtf8(saved) + ")";
            }
        } else if (!besideFolder.empty() &&
                   configuredPath.lexically_normal() == besideFolder.lexically_normal()) {
            // The configured path is the folder checked above; its problem is already in the message.
        } else if (Problem(configuredPath).empty()) {
            return true;  // this is what ResolvedDvdRoot() already answers with
        } else {
            configuredProblem = Problem(configuredPath);
        }
        g_startupProblem = NoGameProblem(program, rejectedImage, imageProblem, folderProblem, configuredProblem);
        g_nothingFound = folderProblem.empty() && imageProblem.empty() && configuredProblem.empty();
        RT_LOGF(RT_TAG_RUNTIME, "ERROR: %s\n", g_startupProblem.c_str());
        return false;
    }
#endif
    const fs::path configured = Find(RuntimeConfigFile::ResolvedDvdRoot());
    if (!configured.empty() && Problem(configured).empty()) {
        return true;
    }
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__))
    std::string problem;
    bool incomplete = false;
    if (!RuntimeConfigFile::DvdRoot().empty()) {
        const fs::path saved = RuntimeConfigFile::ResolvedDvdRoot();
        std::error_code ec;
        problem = fs::exists(saved, ec) ? Check(configured.empty() ? saved : configured, &incomplete)
                                        : "The game folder you chose before (" + RuntimeConfigFile::PathToUtf8(saved) +
                                              ") isn't there any more.";
    }
    if (problem.empty() && !besideProblem.empty()) {
        problem = besideProblem;
        incomplete = besideIncomplete;
    }
    const GamePicker::Text text = PickerText();
    for (;;) {
        // Files left out: said in a message box as well as in the chooser's red line.
        if (incomplete) {
            GamePicker::ShowError(text, UiText::Tr(kIncomplete));
            incomplete = false;
        }
        const GamePicker::Picked picked = GamePicker::Show(text, problem);
        if (picked.choice == GamePicker::Choice::Cancel) {
            GamePicker::Close();
            return false;
        }
        if (picked.choice == GamePicker::Choice::Iso) {
            // The image is read in place. NSMBW_IMPORT_DISC=1 keeps the old "extract once" path, for
            // testing the extractor itself; nothing else uses it.
            if (std::getenv("NSMBW_IMPORT_DISC") != nullptr) {
                if (!ImportDiscImage(picked.path, problem, incomplete)) {
                    continue;
                }
            } else {
                problem = CheckImage(picked.path, &incomplete);
                if (!problem.empty()) {
                    continue;
                }
                RuntimeConfigFile::SetDvdRoot(picked.path);
            }
        } else {
            const fs::path root = Find(picked.path);
            problem = Check(root.empty() ? picked.path : root, &incomplete);
            if (!problem.empty()) {
                continue;
            }
            RuntimeConfigFile::SetDvdRoot(root);
        }
        GamePicker::Close();
        RT_LOGF(RT_TAG_RUNTIME, "game folder: %s\n",
                RuntimeConfigFile::PathToUtf8(RuntimeConfigFile::ResolvedDvdRoot()).c_str());
        return true;
    }
#else
    // The app chose and checked the folder; what this side finds still goes into the log.
    if (const std::string problem = Problem(configured.empty() ? RuntimeConfigFile::ResolvedDvdRoot() : configured);
        !problem.empty()) {
        RT_LOGF(RT_TAG_RUNTIME, "game folder problem: %s\n", problem.c_str());
    }
    return true;
#endif
}

} // namespace GameFolder
