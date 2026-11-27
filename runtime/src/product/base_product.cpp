#include "runtime_product.h"

#ifndef NSMBW_GAME_ID
#define NSMBW_GAME_ID "SMNE01"  // set by runtime/CMakeLists.txt
#endif
#ifndef NSMBW_PRODUCT
#define NSMBW_PRODUCT "vanilla"  // set by runtime/CMakeLists.txt
#endif

namespace RuntimeProduct {
namespace {

constexpr std::string_view kGameId = NSMBW_GAME_ID;
constexpr std::string_view kProduct = NSMBW_PRODUCT;
// Newer Super Mario Bros. Wii (projects/nsmbw/recomp-newer.yml): translated from v1.30's code for
// the USA, whose DLCodeUS_1.bin is this size; another version runs other code.
constexpr bool kNewer = kProduct == "newer";
// Newer Super Mario Bros. Wii Holiday Special: a pack over the vanilla USA base with its own small
// DLCode. It does not use Newer's disc ID or save.
constexpr bool kHoliday = kProduct == "holiday";
// Newer: Falling Leaf, a standalone Newer 1.30-based pack over the vanilla USA base.
constexpr bool kFallingLeaf = kProduct == "fallingleaf";
// Another Super Mario Bros. Wii, a standalone pack over the vanilla USA base.
constexpr bool kAnother = kProduct == "another";
#if defined(MKW_PLATFORM_SWITCH)
// The launcher gives each game its own folder, so a mod's user data can live
// directly in data/ like the base game's.
constexpr std::string_view kNewerUserDataFolder = {};
constexpr std::string_view kHolidayUserDataFolder = {};
constexpr std::string_view kFallingLeafUserDataFolder = {};
constexpr std::string_view kAnotherUserDataFolder = {};
#else
constexpr std::string_view kNewerUserDataFolder = "NewerWiiSaveData";
constexpr std::string_view kHolidayUserDataFolder = "HolidayWiiSaveData";
constexpr std::string_view kFallingLeafUserDataFolder = "FallingLeafWiiSaveData";
constexpr std::string_view kAnotherUserDataFolder = "AnotherWiiSaveData";
#endif

constexpr Descriptor MakeDescriptor() {
    if constexpr (kNewer) {
        return Descriptor{
            Kind::BaseGame,
            "Newer Super Mario Bros. Wii",
            kGameId,
            "Newer Super Mario Bros. Wii v1.30 for the USA (NTSC, SMNE03)",
            "NewerRes/DLCodeUS_1.bin",
            463252,
            "NewerSMBW",
            "title/00010000/534d4e45/data/NerSMBW.sav",
            kNewerUserDataFolder,
            "game-newer",
            true,
        };
    } else if constexpr (kHoliday) {
        return Descriptor{
            Kind::BaseGame,
            "Newer: Holiday Special",
            kGameId,
            "Newer: Holiday Special for the USA (SMNE01 base)",
            "NewerRes/DLCodeUS_1.bin",
            1860,
            "XmasNewer",
            "title/00010000/534d4e45/data/XmasNer.sav",
            kHolidayUserDataFolder,
            "game-holiday",
        };
    } else if constexpr (kFallingLeaf) {
        return Descriptor{
            Kind::BaseGame,
            "Newer: Falling Leaf",
            kGameId,
            "Newer: Falling Leaf for the USA (SMNE01 base)",
            "NewerRes/DLCodeUS_1.bin",
            496356,
            "NewerFL",
            "title/00010000/534d4e45/data/OerSMBW.sav",
            kFallingLeafUserDataFolder,
            "game-fallingleaf",
            true,
        };
    } else if constexpr (kAnother) {
        return Descriptor{
            Kind::BaseGame,
            "Another Super Mario Bros. Wii",
            kGameId,
            "Another Super Mario Bros. Wii for the USA (SMNE01 base)",
            "NewerRes/DLCodeUS_1.bin",
            8116,
            "Another",
            "title/00010000/534d4e45/data/another.sav",
            kAnotherUserDataFolder,
            "game-another",
        };
    } else {
        return Descriptor{
            Kind::BaseGame,
            "New Super Mario Bros. Wii",
            kGameId,
        };
    }
}

} // namespace

const Descriptor& Active() noexcept {
    static constexpr Descriptor descriptor = MakeDescriptor();
    return descriptor;
}

} // namespace RuntimeProduct
