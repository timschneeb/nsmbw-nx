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
#if defined(MKW_PLATFORM_SWITCH)
// The launcher gives each game its own folder, so a mod's user data can live
// directly in data/ like the base game's.
constexpr std::string_view kNewerUserDataFolder = {};
constexpr std::string_view kHolidayUserDataFolder = {};
#else
constexpr std::string_view kNewerUserDataFolder = "NewerWiiSaveData";
constexpr std::string_view kHolidayUserDataFolder = "HolidayWiiSaveData";
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
        };
    } else if constexpr (kHoliday) {
        return Descriptor{
            Kind::BaseGame,
            "Newer Super Mario Bros. Wii Holiday Special",
            kGameId,
            "Newer Super Mario Bros. Wii Holiday Special for the USA (SMNE01 base)",
            "NewerRes/DLCodeUS_1.bin",
            1860,
            "XmasNewer",
            "title/00010000/534d4e45/data/XmasNer.sav",
            kHolidayUserDataFolder,
            "game-holiday",
            true,
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
