#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// The player's own copy of New Super Mario Bros. Wii, extracted with Dolphin ("Extract Entire Disc"):
// a folder holding files/ and sys/. This port runs only the USA disc, revision 1 (SMNE01 rev 1);
// other revisions place the game's code elsewhere.
namespace GameFolder {

// The extracted game at `path` (or its DATA subfolder); empty when there is none.
std::filesystem::path Find(const std::filesystem::path& path);

// Why the game at `root` can't be used ("" when it can): not an extracted disc, another game or
// revision (read from sys/boot.bin), or files left out ("The folder doesn't include all required
// files.": any file its sys/fst.bin lists that isn't there, or sys/boot.bin, main.dol, fst.bin).
std::string Problem(const std::filesystem::path& root);

// What a disc is to this build, from its ID and revision (boot.bin bytes 0-5 and 7).
enum class Disc { Supported, NeedsSupportModule, Unsupported };
Disc Identify(std::string_view id, int revision);

// A support module lets this build run a disc it wasn't translated from (the PAL disc, SMNP01
// revision 1, for the base game). Not built yet: For() finds none, so such a disc gets the same
// "That is SMNP01, not ..." answer as any other.
namespace SupportModule {
struct Offer {
    std::string message;         // what the player is told
    uint64_t downloadBytes = 0;  // 0 when there's nothing to download
};
std::optional<Offer> For(std::string_view id, int revision);
} // namespace SupportModule

// At startup, before anything reads the disc. Windows and Linux: the game is asked for when it is
// missing or wrong (Open Disc Image, which is read in place, or Open Extracted Disc Folder), and
// saved to Config.toml. A "game" folder next to the program ("game-newer" for Newer) is picked up by
// itself, and so is one disc image next to it. Files left out are also said in a message box.
// Switch has no chooser: the beside-program folder, then one beside-program image.
// Returns false when the player gave up (the program then quits), or when Switch found nothing at
// all, in which case StartupProblem() says what to do and main.cpp shows it before the boot.
bool EnsureConfigured();

// Why Switch found no game, in words for the player (the detected folder, what goes there, where
// Config.toml is); empty on every other platform and whenever EnsureConfigured() succeeded.
const std::string& StartupProblem();

// Switch start-up, before the boot: the game is played from an extracted folder, so an image beside
// the program is extracted into one, and a folder that can no longer be used is replaced by a fresh
// extraction. The plan changes nothing; main.cpp shows what it says and carries it out.
struct StartupPlan {
    enum class Kind { Ready, Repair, Extract } kind = Kind::Ready;
    // Repair: the folder to delete (always the place this program extracts to).
    std::filesystem::path folder;
    // Repair and Extract: the image to extract. Empty when there is nothing to extract from, in which
    // case a repair does not happen and the folder is left as it is.
    std::filesystem::path image;
    // Repair: why the folder can't be used, in words for the player.
    std::string problem;
};

// Decides what has to happen before EnsureConfigured() runs. A folder that is merely modded or from
// another disc is never deleted; only one that is empty, incomplete or has no usable sys/boot.bin,
// and only when an image can rebuild it. On other platforms, and with NSMBW_READ_IMAGE set, this is
// always Ready (the image is then read in place, as before).
StartupPlan PlanStartup();

// Removes the folder a Repair plan named. False with `problem` set when it can't.
bool DeleteStartupFolder(const std::filesystem::path& folder, std::string& problem);

// Extracts `image` into the game folder next to the program. `progress` receives 0..1 and a status
// line for the boot splash. False with `problem` set when it can't.
bool ExtractStartupImage(const std::filesystem::path& image,
                         const std::function<void(float fraction, const char* status)>& progress,
                         std::string& problem);

// Whether the game being played is the original, unchanged: a retail disc ID (SMNE01, SMNP01,
// SMNJ01) and the main.dol that disc came with. A modded disc (Newer and the like) isn't. Worked out
// once, at the first call (main.cpp makes it at startup, once the folder is known).
bool RetailDisc();

} // namespace GameFolder
