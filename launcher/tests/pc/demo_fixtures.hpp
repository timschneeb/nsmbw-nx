#pragma once

#include <filesystem>
#include <string>

namespace launcher::test {

struct DemoData {
    std::filesystem::path root;
    std::filesystem::path test_nro;
    std::filesystem::path newer_nro;
    std::filesystem::path extra_nro;
    std::filesystem::path launcher_nro;
    std::filesystem::path newer_banner;
    std::string launcher_url;
};

// Creates the catalogue, NROs, banner and launcher used by the PC renderer and viewer.
auto CreateDemoData(const std::filesystem::path& root, const std::string& banner_source)
    -> DemoData;

} // namespace launcher::test
