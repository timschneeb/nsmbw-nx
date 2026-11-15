#pragma once

// Minimal libnx stand-in for the PC launcher tests.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using s8 = std::int8_t;
using s16 = std::int16_t;
using s32 = std::int32_t;
using s64 = std::int64_t;
using Result = u32;

constexpr u32 Module_Libnx = 0;

enum {
    LibnxError_BadInput = 1,
    LibnxError_NotFound = 2,
    LibnxError_IoError = 3,
    LibnxError_OutOfMemory = 4,
    LibnxError_NotInitialized = 5,
};

#define MAKERESULT(module, description) ((((module) & 0x1FFu) << 9) | ((description) & 0x1FFu))
#define R_SUCCEEDED(result) ((result) == 0)
#define R_FAILED(result) ((result) != 0)

inline u64 svcGetSystemTick() {
    return static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
}

#define NROHEADER_MAGIC 0x304f524e
#define NROASSETHEADER_MAGIC 0x54455341

typedef struct {
    u32 file_off;
    u32 size;
} NroSegment;

typedef struct {
    u32 magic;
    u32 unk1;
    u32 size;
    u32 unk2;
    NroSegment segments[3];
    u32 bss_size;
    u32 unk3;
    u8 build_id[0x20];
    u8 padding[0x20];
} NroHeader;

typedef struct {
    u64 offset;
    u64 size;
} NroAssetSection;

typedef struct {
    u32 magic;
    u32 version;
    NroAssetSection icon;
    NroAssetSection nacp;
    NroAssetSection romfs;
} NroAssetHeader;

typedef struct {
    char name[0x200];
    char author[0x100];
} NacpLanguageEntry;

// Only the prefix needed by ReadNroVersion; the layout matches libnx.
typedef struct {
    NacpLanguageEntry lang[16];
    u8 isbn[0x25];
    u8 startup_user_account;
    u8 user_account_switch_lock;
    u8 add_on_content_registration_type;
    u32 attribute_flag;
    u32 supported_language_flag;
    u32 parental_control_flag;
    u8 screenshot;
    u8 video_capture;
    u8 data_loss_confirmation;
    u8 play_log_policy;
    u64 presence_group_id;
    s8 rating_age[0x20];
    char display_version[0x10];
} NacpStruct;

// libnx SHA-256 context used by sha.cpp and romfs.cpp.
struct Sha256Context {
    struct evp_md_ctx_st* ctx;
};

void sha256ContextCreate(Sha256Context* ctx);
void sha256ContextUpdate(Sha256Context* ctx, const void* data, size_t size);
void sha256ContextGetHash(Sha256Context* ctx, u8 hash[32]);
