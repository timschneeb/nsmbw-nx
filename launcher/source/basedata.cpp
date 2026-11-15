#include "basedata.hpp"

#include "app.hpp"
#include "games.hpp"
#include "install.hpp"
#include "json_util.hpp"
#include "log.hpp"
#include "paths.hpp"
#include "ui/progress_box.hpp"

#include "wii_disc.h"

#include <switch/runtime/devices/fs_dev.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace launcher {
namespace {

namespace fs = std::filesystem;

constexpr const char* kImportingSuffix = ".importing";
constexpr const char* kImportingMarker = ".nsmbw-importing";
constexpr s64 kExtractHeadroom = 64 * 1024 * 1024;

auto IsExtractedDisc(const fs::path& path) -> bool {
    std::error_code ec;
    return fs::is_directory(path / "files", ec) && !ec &&
           fs::is_regular_file(path / "sys" / "fst.bin", ec) && !ec;
}

auto StripComment(std::string line) -> std::string {
    bool single = false;
    bool double_quote = false;
    bool escaped = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (double_quote && c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '\'' && !double_quote) {
            single = !single;
        } else if (c == '"' && !single) {
            double_quote = !double_quote;
        } else if (c == '#' && !single && !double_quote) {
            return line.substr(0, i);
        }
    }
    return line;
}

auto Unquote(const std::string& value) -> std::string {
    if (value.size() < 2 ||
        (value.front() != '"' && value.front() != '\'') ||
        value.back() != value.front()) {
        return value;
    }

    const char quote = value.front();
    std::string out;
    out.reserve(value.size() - 2);
    for (size_t i = 1; i + 1 < value.size(); ++i) {
        char c = value[i];
        if (quote == '"' && c == '\\' && i + 2 < value.size()) {
            const char next = value[++i];
            switch (next) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                default: out.push_back(next); break;
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

auto IsDevicePath(const std::string& value) -> bool {
    const auto colon = value.find(':');
    if (colon == std::string::npos || colon == 0) {
        return false;
    }
    for (size_t i = 0; i < colon; ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (i > 0 && ((c >= '0' && c <= '9') || c == '_' || c == '-'));
        if (!ok) {
            return false;
        }
    }
    return true;
}

auto ReadDvdRoot(const fs::path& config, std::string& value) -> bool {
    std::ifstream file(config);
    if (!file) {
        return false;
    }

    bool in_paths = false;
    std::string line;
    while (std::getline(file, line)) {
        line = Trim(StripComment(line));
        if (line.empty()) {
            continue;
        }
        if (line.front() == '[') {
            const auto end = line.find(']');
            in_paths = end != std::string::npos && Trim(line.substr(1, end - 1)) == "paths";
            continue;
        }
        if (!in_paths) {
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        if (Trim(line.substr(0, equals)) != "dvd_root") {
            continue;
        }
        value = Unquote(Trim(line.substr(equals + 1)));
        return !value.empty();
    }
    return false;
}

auto GameDvdRoot(const GameEntry& entry) -> std::optional<fs::path> {
    const fs::path config = fs::path(entry.dir) / "data" / "Config.toml";
    std::string value;
    if (!ReadDvdRoot(config, value)) {
        return std::nullopt;
    }

    fs::path path(value);
    if (!IsDevicePath(value) && path.is_relative()) {
        path = config.parent_path() / path;
    }
    path = path.lexically_normal();
    if (!IsExtractedDisc(path)) {
        return std::nullopt;
    }
    return path;
}

void RemoveTree(const fs::path& path) {
    const Result rc = fsdevDeleteDirectoryRecursively(path.c_str());
    if (R_FAILED(rc)) {
        log_write("[basedata] could not remove %s: 0x%08X\n", path.c_str(), static_cast<unsigned>(rc));
    }
}

auto Fail(std::string& error, std::string message) -> Result {
    error = std::move(message);
    log_write("[basedata] %s\n", error.c_str());
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
}

auto ImageProblem(const WiiDisc::Disc& disc) -> std::string {
    if (disc.Id() == "SMNE03") {
        return "The Newer disc image is not supported. Newer is installed as an overlay from the launcher.";
    }
    if (disc.Id() == "SMNP01") {
        return "The PAL disc is not supported. Use the USA SMNE01 revision 1 disc.";
    }
    if (disc.Id() != "SMNE01") {
        return "That is " + disc.Id() + ", not the USA New Super Mario Bros. Wii disc (SMNE01).";
    }
    if (disc.Revision() != 1) {
        return "That is SMNE01 revision " + std::to_string(disc.Revision()) + ". Use revision 1.";
    }
    for (const auto& file : disc.Files()) {
        if (file.path.rfind("NewerRes/", 0) == 0 || file.path.rfind("NewerSMBW/", 0) == 0) {
            return "That is a Newer Super Mario Bros. Wii disc image. Newer is installed as an overlay from "
                   "the launcher. Use a retail SMNE01 disc.";
        }
    }
    return {};
}

} // namespace

auto CheckBaseData(std::string& problem) -> BaseDataState {
    problem.clear();
    const fs::path root = BaseDiscDirectory();
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        if (ec) {
            problem = "Could not read " + root.string() + ": " + ec.message();
            return BaseDataState::Incomplete;
        }
        return BaseDataState::Missing;
    }
    if (!IsExtractedDisc(root)) {
        problem = "The folder exists but does not contain files/ and sys/.";
        return BaseDataState::Incomplete;
    }

    for (const char* name : {"sys/boot.bin", "sys/main.dol", "sys/fst.bin"}) {
        if (!fs::is_regular_file(root / name, ec) || ec) {
            problem = std::string("Missing ") + name + ".";
            return BaseDataState::Incomplete;
        }
    }

    std::ifstream boot(root / "sys" / "boot.bin", std::ios::binary);
    char header[8]{};
    if (!boot.read(header, sizeof(header))) {
        problem = "sys/boot.bin is too short to identify the disc.";
        return BaseDataState::Incomplete;
    }
    const std::string id(header, 6);
    const int revision = static_cast<unsigned char>(header[7]);
    if (id != "SMNE01" || revision != 1) {
        problem = "The extracted game data is " + id + " revision " + std::to_string(revision) +
                  ", not the USA SMNE01 revision 1.";
        return BaseDataState::Incomplete;
    }
    if (fs::is_directory(root / "files" / "NewerRes", ec) ||
        fs::is_directory(root / "files" / "NewerSMBW", ec)) {
        problem = "The extracted folder is a Newer Super Mario Bros. Wii disc image. Use a retail SMNE01 disc.";
        return BaseDataState::Incomplete;
    }

    return BaseDataState::Ready;
}

auto BaseDataReady() -> bool {
    std::string problem;
    return CheckBaseData(problem) == BaseDataState::Ready;
}

auto BaseDataReadyFor(const GameEntry& entry) -> bool {
    if (BaseDataReady()) {
        return true;
    }
    return GameDvdRoot(entry).has_value();
}

auto FindBaseImage(std::string& problem) -> std::optional<BaseImage> {
    problem.clear();
    std::vector<fs::path> candidates;
    std::error_code ec;
    for (fs::directory_iterator it(RootDirectory(), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || ec) {
            continue;
        }
        if (WiiDisc::Disc::LooksLikeImage(it->path())) {
            candidates.push_back(it->path());
        }
    }
    std::sort(candidates.begin(), candidates.end());

    log_write("[basedata] %zu image candidate(s) in %s\n", candidates.size(), RootDirectory().c_str());
    std::string first_problem;
    for (const auto& path : candidates) {
        std::string error;
        const std::unique_ptr<WiiDisc::Disc> disc = WiiDisc::Disc::Open(path, error);
        if (!disc) {
            log_write("[basedata] %s: %s\n", path.filename().string().c_str(), error.c_str());
            if (first_problem.empty()) {
                first_problem = path.filename().string() + ": " + error;
            }
            continue;
        }
        if (const std::string reason = ImageProblem(*disc); !reason.empty()) {
            log_write("[basedata] %s: %s\n", path.filename().string().c_str(), reason.c_str());
            if (first_problem.empty()) {
                first_problem = path.filename().string() + ": " + reason;
            }
            continue;
        }

        std::error_code size_ec;
        const auto image_size = fs::file_size(path, size_ec);
        if (size_ec) {
            if (first_problem.empty()) {
                first_problem = path.filename().string() + ": " + size_ec.message();
            }
            continue;
        }

        BaseImage result;
        result.path = path.string();
        result.id = disc->Id();
        result.revision = disc->Revision();
        result.image_size = static_cast<s64>(image_size);
        result.extracted_size = static_cast<s64>(disc->ExtractedSize());
        if (result.extracted_size <= 0) {
            if (first_problem.empty()) {
                first_problem = path.filename().string() + ": the disc reports no extractable size.";
            }
            continue;
        }
        return result;
    }

    problem = first_problem;
    return std::nullopt;
}

Result InstallBaseImage(const BaseImage& image, ui::ProgressBox& pbox, std::string& error,
                        bool replace_existing) {
    error.clear();
    const fs::path target = BaseDiscDirectory();
    const fs::path staging = std::string(target) + kImportingSuffix;
    std::error_code ec;

    if (replace_existing) {
        pbox.SetTransfer("Removing the incomplete base data");
        if (fs::exists(target, ec)) {
            const Result rc = fsdevDeleteDirectoryRecursively(target.c_str());
            if (R_FAILED(rc)) {
                return Fail(error, "Could not remove the incomplete base game data.");
            }
        }
    } else if (fs::exists(target, ec)) {
        return Fail(error, "The base game data folder already exists.");
    }

    RemoveTree(staging);
    fs::create_directories(target.parent_path(), ec);
    if (!fs::create_directory(staging, ec)) {
        return Fail(error, "Could not create " + staging.string() + ".");
    }

    std::ofstream marker(staging / kImportingMarker);
    marker << "An unfinished extraction; removed at the next launcher start.\n";
    marker.close();
    if (!marker) {
        RemoveTree(staging);
        return Fail(error, "Could not write the extraction marker.");
    }

    log_write("[basedata] extracting %s to %s (%lld bytes)\n", image.path.c_str(), target.c_str(),
              static_cast<long long>(image.extracted_size));

    s64 free = 0;
    s64 total = 0;
    if (R_FAILED(App::GetSdSize(&free, &total))) {
        RemoveTree(staging);
        return Fail(error, "Could not read the SD card free space.");
    }
    const s64 needed = image.extracted_size + kExtractHeadroom;
    if (needed > free) {
        RemoveTree(staging);
        return Fail(error, "Not enough space. The extracted game needs " + FormatSize(needed) + ", " +
                               FormatSize(free) + " free.");
    }

    std::string open_error;
    const std::unique_ptr<WiiDisc::Disc> disc = WiiDisc::Disc::Open(image.path, open_error);
    if (!disc) {
        RemoveTree(staging);
        return Fail(error, "Could not open " + fs::path(image.path).filename().string() + ": " + open_error);
    }

    pbox.NewTransfer("Extracting " + fs::path(image.path).filename().string());
    pbox.UpdateTransfer(0, image.extracted_size);

    std::atomic<bool> cancel{false};
    const auto progress = [&pbox, &cancel](uint64_t done, uint64_t total) {
        if (pbox.ShouldExit()) {
            cancel = true;
        }
        if (total > 0) {
            pbox.UpdateTransfer(static_cast<s64>(done), static_cast<s64>(total));
        }
    };

    int files = 0;
    const std::string result = disc->Extract(staging, progress, cancel, files);
    if (result == "cancelled" || pbox.ShouldExit()) {
        RemoveTree(staging);
        return Fail(error, "Base data extraction cancelled.");
    }
    if (!result.empty()) {
        RemoveTree(staging);
        return Fail(error, "Could not extract the disc image: " + result + ".");
    }

    if (!fs::is_directory(staging / "files", ec) ||
        !fs::is_regular_file(staging / "sys" / "boot.bin", ec) ||
        !fs::is_regular_file(staging / "sys" / "main.dol", ec) ||
        !fs::is_regular_file(staging / "sys" / "fst.bin", ec)) {
        RemoveTree(staging);
        return Fail(error, "The extracted game data is incomplete.");
    }

    fs::remove(staging / kImportingMarker, ec);
    std::error_code rename_ec;
    bool renamed = false;
    for (int attempt = 0; attempt < 3 && !renamed; ++attempt) {
        rename_ec.clear();
        fs::rename(staging, target, rename_ec);
        renamed = !rename_ec;
        if (!renamed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    if (!renamed) {
        RemoveTree(staging);
        return Fail(error, "Could not move the extracted game data into place: " + rename_ec.message());
    }

    log_write("[basedata] extracted %s to %s (%d files)\n", image.path.c_str(), target.c_str(), files);
    return 0;
}

void CleanupBaseImport() {
    const fs::path staging = std::string(BaseDiscDirectory()) + kImportingSuffix;
    std::error_code ec;
    if (fs::is_regular_file(staging / kImportingMarker, ec) && !ec) {
        log_write("[basedata] removing unfinished import %s\n", staging.c_str());
        fsdevDeleteDirectoryRecursively(staging.c_str());
    }
}

} // namespace launcher
