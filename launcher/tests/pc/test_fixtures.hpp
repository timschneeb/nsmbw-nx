#pragma once

#include "sha.hpp"

#include <switch.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace launcher::test {

inline auto ReadFile(const std::filesystem::path& path) -> std::string {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

inline void WriteFile(const std::filesystem::path& path, const std::string& data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
}

inline void WriteFakeNro(const std::filesystem::path& path, const std::string& version,
                         char marker = '\0', size_t padding = 0) {
    NroHeader header{};
    header.magic = NROHEADER_MAGIC;
    header.size = sizeof(NroHeader);
    NroAssetHeader asset{};
    asset.magic = NROASSETHEADER_MAGIC;
    asset.nacp.offset = sizeof(NroAssetHeader);
    asset.nacp.size = offsetof(NacpStruct, display_version) + 0x10;
    NacpStruct nacp{};
    std::strncpy(nacp.display_version, version.c_str(), sizeof(nacp.display_version));
    nacp.lang[0].name[0] = marker;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(&header), sizeof(header));
    file.write(reinterpret_cast<const char*>(&asset), sizeof(asset));
    file.write(reinterpret_cast<const char*>(&nacp), sizeof(nacp));
    if (padding > 0) {
        std::vector<char> zeros(padding, '\0');
        file.write(zeros.data(), static_cast<std::streamsize>(zeros.size()));
    }
}

inline auto FileSha(const std::filesystem::path& path) -> std::string {
    std::string sha;
    FileSha256(path.string(), sha);
    return sha;
}

inline auto FileSize(const std::filesystem::path& path) -> s64 {
    std::error_code ec;
    return static_cast<s64>(std::filesystem::file_size(path, ec));
}

inline auto ShaBytes(const std::string& data) -> std::string {
    Sha256Context ctx;
    sha256ContextCreate(&ctx);
    sha256ContextUpdate(&ctx, data.data(), data.size());
    u8 hash[32];
    sha256ContextGetHash(&ctx, hash);
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (u8 byte : hash) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0xF]);
    }
    return out;
}

inline auto GameJson(const std::string& id, const std::string& name, const std::string& author,
                     const std::filesystem::path& nro, const std::string& version = "1.0.0",
                     const std::string& banner = {}, const std::string& game_version = {},
                     const std::string& region = "U") -> std::string {
    const std::string game = game_version.empty() ? version : game_version;
    std::ostringstream out;
    out << "\"" << id << "\": {\"name\": \"" << name << "\", \"author\": \"" << author
        << "\", \"components\": {\"nro\": {\"recomp_version\": \"" << version
        << "\", \"game_version\": \"" << game << "\", \"region\": \"" << region
        << "\", \"sha256\": \"" << FileSha(nro) << "\", \"size\": " << FileSize(nro)
        << ", \"path\": \"" << nro.string() << "\"}}";
    if (!banner.empty()) {
        out << ", \"banner\": {\"path\": \"" << banner << "\"}";
    }
    out << "}";
    return out.str();
}

inline void WriteCatalogue(const std::filesystem::path& path,
                           const std::vector<std::string>& games,
                           const std::string& launcher_version = "",
                           const std::string& launcher_url = "",
                           const std::filesystem::path& launcher_payload = {},
                           const std::string& recomp_version = "") {
    std::string launcher_sha;
    s64 launcher_size = 0;
    if (!launcher_url.empty()) {
        const auto payload = launcher_payload.empty()
                                 ? path.parent_path() / "src" / "launcher.nro"
                                 : launcher_payload;
        launcher_sha = FileSha(payload);
        launcher_size = FileSize(payload);
    }
    std::ofstream file(path);
    file << "{\"schema\":1,\"recomp_version\":\"" << recomp_version
         << "\",\"launcher\":{\"version\":\"" << launcher_version << "\",\"url\":\""
         << launcher_url << "\",\"sha256\":\"" << launcher_sha << "\",\"size\":" << launcher_size
         << "},\"games\":{";
    for (size_t i = 0; i < games.size(); ++i) {
        if (i != 0) file << ",";
        file << games[i];
    }
    file << "}}\n";
}

} // namespace launcher::test
