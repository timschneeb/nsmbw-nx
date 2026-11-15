#pragma once

#include <switch.h>

#include <optional>
#include <string>

namespace launcher {

struct GameEntry;

namespace ui { struct ProgressBox; }

struct BaseImage {
    std::string path;
    std::string id;
    int revision{};
    s64 image_size{};
    s64 extracted_size{};
};

enum class BaseDataState {
    Ready,
    Missing,
    Incomplete,
};

auto BaseDataReady() -> bool;
// True when discs/SMNE01 or the game's [paths] dvd_root is a usable extracted folder.
auto BaseDataReadyFor(const GameEntry& entry) -> bool;
auto CheckBaseData(std::string& problem) -> BaseDataState;

// Finds the first valid USA SMNE01 revision 1 image in the launcher directory.
// `problem` is set when candidate images exist but none can be used.
auto FindBaseImage(std::string& problem) -> std::optional<BaseImage>;

// Extracts into discs/SMNE01. replace_existing removes an incomplete folder first.
Result InstallBaseImage(const BaseImage& image, ui::ProgressBox& pbox, std::string& error,
                        bool replace_existing);

// Removes an unfinished discs/SMNE01.importing left by an interrupted extraction.
void CleanupBaseImport();

} // namespace launcher
