#include "disc_files.h"

#include "runtime_config.h"
#include "runtime_log.h"

#include <filesystem>
#include <mutex>

namespace DiscFiles {
namespace {

namespace fs = std::filesystem;

std::once_flag g_openOnce;
std::unique_ptr<WiiDisc::Disc> g_disc;
std::string g_error;

bool ConfiguredPathIsImage() {
    const fs::path path = RuntimeConfigFile::ResolvedDvdRoot();
    if (path.empty()) {
        return false;
    }
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec;
}

void OpenOnce() {
    if (!ConfiguredPathIsImage()) {
        return;
    }
    const fs::path path = RuntimeConfigFile::ResolvedDvdRoot();
    g_disc = WiiDisc::Disc::Open(path, g_error);
    if (g_disc) {
        RT_LOGF(RT_TAG_RUNTIME, "disc image: %s (%s revision %d, %zu file(s), aes %s)\n",
                RuntimeConfigFile::PathToUtf8(path).c_str(), g_disc->Id().c_str(), g_disc->Revision(),
                g_disc->Files().size(), WiiDisc::Disc::AesBackend());
    }
}

} // namespace

bool ImageMode() {
    return ConfiguredPathIsImage();
}

bool Open(std::string& error) {
    std::call_once(g_openOnce, OpenOnce);
    if (!g_disc) {
        error = g_error.empty() ? "No disc image is configured" : g_error;
        return false;
    }
    error.clear();
    return true;
}

const std::vector<WiiDisc::Disc::File>& Files() {
    static const std::vector<WiiDisc::Disc::File> kNone;
    return g_disc ? g_disc->Files() : kNone;
}

const std::string& Id() {
    static const std::string kNone;
    return g_disc ? g_disc->Id() : kNone;
}

int Revision() {
    return g_disc ? g_disc->Revision() : 0;
}

bool ReadAt(uint64_t offset, uint32_t length, std::vector<uint8_t>& out) {
    if (!g_disc || length == 0) {
        return g_disc != nullptr;
    }
    out.resize(length);
    return g_disc->ReadAt(offset, out.data(), out.size());
}

bool ReadWhole(std::string_view path, std::vector<uint8_t>& out) {
    return g_disc && g_disc->Read(path, out);
}

uint64_t Size(std::string_view path) {
    return g_disc ? g_disc->FileSize(path) : UINT64_MAX;
}

} // namespace DiscFiles
