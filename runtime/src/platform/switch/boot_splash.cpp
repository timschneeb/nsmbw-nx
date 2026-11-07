#include <switch.h>
#include <switch/runtime/util/utf.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <cmath>
#include <cstdio>
#include <cstring>

extern "C" const unsigned char nsmbw_logo_rgba[];

namespace {
constexpr u32 kFbWidth = 1280;
constexpr u32 kFbHeight = 720;

Framebuffer g_framebuffer{};
bool g_framebufferReady = false;
PrintConsole* g_console = nullptr;

FT_Library g_library = nullptr;
FT_Face g_face = nullptr;
bool g_plReady = false;

char g_status[96] = "Starting up...";

inline u32 PackColor(u8 r, u8 g, u8 b) { return RGBA8(r, g, b, 0xFF); }

void BlendPixel(u32* pixel, u8 r, u8 g, u8 b, u8 alpha) {
    if (alpha == 0) {
        return;
    }
    if (alpha == 0xFF) {
        *pixel = PackColor(r, g, b);
        return;
    }
    const u32 dst = *pixel;
    const u32 inverse = 0xFFu - alpha;
    const u32 dr = dst & 0xFFu;
    const u32 dg = (dst >> 8) & 0xFFu;
    const u32 db = (dst >> 16) & 0xFFu;
    *pixel = RGBA8((r * alpha + dr * inverse) / 255, (g * alpha + dg * inverse) / 255,
                   (b * alpha + db * inverse) / 255, 0xFF);
}

void FillBackground(u32* framebuffer, u32 stridePixels) {
    for (u32 y = 0; y < kFbHeight; ++y) {
        const float t = static_cast<float>(y) / static_cast<float>(kFbHeight - 1);
        const u8 r = static_cast<u8>(18.0f + (4.0f - 18.0f) * t);
        const u8 g = static_cast<u8>(24.0f + (6.0f - 24.0f) * t);
        const u8 b = static_cast<u8>(44.0f + (12.0f - 44.0f) * t);
        const u32 color = PackColor(r, g, b);
        u32* row = framebuffer + static_cast<size_t>(y) * stridePixels;
        for (u32 x = 0; x < kFbWidth; ++x) {
            row[x] = color;
        }
    }

    // A soft glow behind the logo keeps the flat gradient from feeling empty.
    constexpr int kCenterX = 640;
    constexpr int kCenterY = 285;
    constexpr int kRadius = 420;
    for (int y = 0; y < static_cast<int>(kFbHeight); ++y) {
        const int dy = y - kCenterY;
        u32* row = framebuffer + static_cast<size_t>(y) * stridePixels;
        for (int x = 0; x < static_cast<int>(kFbWidth); ++x) {
            const int dx = x - kCenterX;
            const int distanceSq = dx * dx + dy * dy;
            if (distanceSq >= kRadius * kRadius) {
                continue;
            }
            const float falloff =
                1.0f - std::sqrt(static_cast<float>(distanceSq)) / static_cast<float>(kRadius);
            const u8 alpha = static_cast<u8>(falloff * falloff * 26.0f);
            BlendPixel(&row[x], 120, 150, 220, alpha);
        }
    }
}

int TextWidth(FT_Face face, const char* text) {
    int width = 0;
    const size_t length = std::strlen(text);
    for (size_t i = 0; i < length;) {
        uint32_t codepoint = 0;
        const ssize_t units = decode_utf8(&codepoint, reinterpret_cast<const uint8_t*>(&text[i]));
        if (units <= 0) {
            break;
        }
        i += static_cast<size_t>(units);
        if (codepoint == '\n') {
            continue;
        }
        const FT_UInt glyph = FT_Get_Char_Index(face, codepoint);
        if (FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT) == 0) {
            width += static_cast<int>(face->glyph->advance.x >> 6);
        }
    }
    return width;
}

void DrawGlyph(u32* framebuffer, u32 stridePixels, const FT_Bitmap& bitmap, int x, int y, u8 r, u8 g,
               u8 b) {
    if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) {
        return;
    }
    for (u32 row = 0; row < bitmap.rows; ++row) {
        const int targetY = y + static_cast<int>(row);
        if (targetY < 0 || targetY >= static_cast<int>(kFbHeight)) {
            continue;
        }
        const u8* source = bitmap.buffer + row * bitmap.pitch;
        u32* target = framebuffer + static_cast<size_t>(targetY) * stridePixels;
        for (u32 column = 0; column < bitmap.width; ++column) {
            const int targetX = x + static_cast<int>(column);
            if (targetX < 0 || targetX >= static_cast<int>(kFbWidth)) {
                continue;
            }
            BlendPixel(&target[targetX], r, g, b, source[column]);
        }
    }
}

void DrawText(FT_Face face, u32* framebuffer, u32 stridePixels, int x, int baseline, const char* text,
              u8 r, u8 g, u8 b) {
    int penX = x;
    const size_t length = std::strlen(text);
    for (size_t i = 0; i < length;) {
        uint32_t codepoint = 0;
        const ssize_t units = decode_utf8(&codepoint, reinterpret_cast<const uint8_t*>(&text[i]));
        if (units <= 0) {
            break;
        }
        i += static_cast<size_t>(units);
        if (codepoint == '\n') {
            continue;
        }
        const FT_UInt glyph = FT_Get_Char_Index(face, codepoint);
        if (FT_Load_Glyph(face, glyph, FT_LOAD_DEFAULT) != 0 ||
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0) {
            continue;
        }
        const FT_GlyphSlot slot = face->glyph;
        DrawGlyph(framebuffer, stridePixels, slot->bitmap, penX + slot->bitmap_left,
                  baseline - slot->bitmap_top, r, g, b);
        penX += static_cast<int>(slot->advance.x >> 6);
    }
}

void SetFontSize(FT_Face face, int pixels) { (void)FT_Set_Pixel_Sizes(face, 0, pixels); }

void PrintCentered(const PrintConsole* console, const char* text) {
    const int width = console != nullptr ? console->consoleWidth : 80;
    const int length = static_cast<int>(std::strlen(text));
    int padding = (width - length) / 2;
    if (padding < 0) {
        padding = 0;
    }
    std::printf("%*s%s\n", padding, "", text);
}

void ShowConsoleFallback() {
    g_console = consoleInit(nullptr);
    if (g_console == nullptr) {
        return;
    }
    consoleClear();
    std::printf("\n\n\n");
    PrintCentered(g_console, "New Super Mario Bros. Wii");
    std::printf("\n");
    PrintCentered(g_console, g_status);
    consoleUpdate(g_console);
}

void DrawLogo(u32* framebuffer, u32 stridePixels) {
    constexpr int kLogoWidth = 820;
    constexpr int kLogoHeight = 339;
    constexpr int kLogoX = (static_cast<int>(kFbWidth) - kLogoWidth) / 2;
    constexpr int kLogoY = 148;
    for (int y = 0; y < kLogoHeight; ++y) {
        const int targetY = kLogoY + y;
        if (targetY < 0 || targetY >= static_cast<int>(kFbHeight)) {
            continue;
        }
        u32* row = framebuffer + static_cast<size_t>(targetY) * stridePixels;
        const unsigned char* source = nsmbw_logo_rgba + y * kLogoWidth * 4;
        for (int x = 0; x < kLogoWidth; ++x) {
            const int targetX = kLogoX + x;
            if (targetX < 0 || targetX >= static_cast<int>(kFbWidth)) {
                continue;
            }
            const unsigned char* pixel = source + x * 4;
            BlendPixel(&row[targetX], pixel[0], pixel[1], pixel[2], pixel[3]);
        }
    }
}

void DrawSplash(FT_Face face, u32* framebuffer, u32 stridePixels) {
    FillBackground(framebuffer, stridePixels);
    DrawLogo(framebuffer, stridePixels);

    SetFontSize(face, 30);
    DrawText(face, framebuffer, stridePixels,
             (static_cast<int>(kFbWidth) - TextWidth(face, g_status)) / 2, 590, g_status, 226, 232,
             244);
}

bool RedrawSplash() {
    if (!g_framebufferReady || g_face == nullptr) {
        return false;
    }
    u32 stride = 0;
    if (u32* framebuffer = static_cast<u32*>(framebufferBegin(&g_framebuffer, &stride))) {
        DrawSplash(g_face, framebuffer, stride / sizeof(u32));
        framebufferEnd(&g_framebuffer);
        return true;
    }
    return false;
}
} // namespace

// Shown before the process transcript takes stdout. The framebuffer stays up until Aurora claims
// the default NWindow; the shared system font gives the boot screen a real sans-serif face.
extern "C" void NsmbwBootSplashShow() {
    if (g_framebufferReady || g_console != nullptr) {
        return;
    }

    if (R_FAILED(plInitialize(PlServiceType_User))) {
        ShowConsoleFallback();
        return;
    }
    g_plReady = true;

    PlFontData font{};
    bool framebufferCreated = false;

    if (R_SUCCEEDED(plGetSharedFontByType(&font, PlSharedFontType_Standard)) &&
        FT_Init_FreeType(&g_library) == 0 &&
        FT_New_Memory_Face(g_library, static_cast<const FT_Byte*>(font.address),
                           static_cast<FT_Long>(font.size), 0, &g_face) == 0) {
        if (R_SUCCEEDED(framebufferCreate(&g_framebuffer, nwindowGetDefault(), kFbWidth, kFbHeight,
                                          PIXEL_FORMAT_RGBA_8888, 2))) {
            framebufferCreated = true;
            framebufferMakeLinear(&g_framebuffer);

            u32 stride = 0;
            if (u32* framebuffer = static_cast<u32*>(framebufferBegin(&g_framebuffer, &stride))) {
                DrawSplash(g_face, framebuffer, stride / sizeof(u32));
                framebufferEnd(&g_framebuffer);
                g_framebufferReady = true;
            }
        }
    }

    if (!g_framebufferReady) {
        if (framebufferCreated) {
            framebufferClose(&g_framebuffer);
        }
        if (g_face != nullptr) {
            FT_Done_Face(g_face);
            g_face = nullptr;
        }
        if (g_library != nullptr) {
            FT_Done_FreeType(g_library);
            g_library = nullptr;
        }
        plExit();
        g_plReady = false;
        ShowConsoleFallback();
    }
}

extern "C" void NsmbwBootSplashSetStatus(const char* status) {
    if (status == nullptr || status[0] == '\0' || std::strcmp(status, g_status) == 0) {
        return;
    }
    std::snprintf(g_status, sizeof(g_status), "%s", status);

    if (g_console != nullptr) {
        PrintCentered(g_console, g_status);
        consoleUpdate(g_console);
        return;
    }
    RedrawSplash();
}

// Called right before the first swapchain configuration, which takes over the default NWindow.
extern "C" void NsmbwBootSplashHide() {
    if (g_framebufferReady) {
        framebufferClose(&g_framebuffer);
        g_framebufferReady = false;
    }
    if (g_face != nullptr) {
        FT_Done_Face(g_face);
        g_face = nullptr;
    }
    if (g_library != nullptr) {
        FT_Done_FreeType(g_library);
        g_library = nullptr;
    }
    if (g_plReady) {
        plExit();
        g_plReady = false;
    }
    if (g_console != nullptr) {
        PrintConsole* console = g_console;
        g_console = nullptr;
        consoleExit(console);
    }
}
