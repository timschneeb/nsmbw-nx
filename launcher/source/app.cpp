#include "app.hpp"

#include <nanovg_dk.h>

#include "http.hpp"
#include "install.hpp"
#include "log.hpp"
#include "menu_main.hpp"
#include "paths.hpp"
#include "ui/nvg_util.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

extern "C" {
u32 __nx_applet_exit_mode = 0;
}

namespace launcher {
namespace {
App* g_app{};

void DekoError(void*, const char*, DkResult, const char* message) {
    log_write("[deko3d] %s\n", message != nullptr ? message : "");
}

NVGcolor Colour(uint32_t rgba) {
    return nvgRGBA((rgba >> 24) & 0xff, (rgba >> 16) & 0xff, (rgba >> 8) & 0xff, rgba & 0xff);
}

void SetColour(Theme& theme, ThemeEntryID id, uint32_t rgba) {
    theme.elements[id] = {ElementType::Colour, 0, Colour(rgba)};
}
} // namespace

auto App::Get() -> App& { return *g_app; }
auto App::GetApp() -> App* { return g_app; }
auto App::GetVg() -> NVGcontext* { return g_app != nullptr ? g_app->m_vg : nullptr; }
auto App::PlaySoundEffect(SoundEffect) -> void {}
auto App::GetTextScrollSpeed() -> int { return 1; }
auto App::IsApplication() -> bool { return true; }

auto App::Exit() -> void {
    if (g_app != nullptr) {
        g_app->Quit();
    }
}

auto App::GetSdSize(s64* free, s64* total) -> Result {
    Result rc = nsGetFreeSpaceSize(NcmStorageId_SdCard, free);
    if (R_FAILED(rc)) {
        return rc;
    }
    return nsGetTotalSpaceSize(NcmStorageId_SdCard, total);
}

auto App::GetEmmcSize(s64* free, s64* total) -> Result {
    Result rc = nsGetFreeSpaceSize(NcmStorageId_BuiltInUser, free);
    if (R_FAILED(rc)) {
        return rc;
    }
    return nsGetTotalSpaceSize(NcmStorageId_BuiltInUser, total);
}

void DrawElement(const Vec4& v, ThemeEntryID id) {
    const auto& e = App::Get().Theme().elements[id];
    if (e.type == ElementType::Colour) {
        ui::gfx::drawRect(App::GetVg(), v, e.colour);
    }
}

void DrawElement(float x, float y, float w, float h, ThemeEntryID id) {
    DrawElement({x, y, w, h}, id);
}

App::App(int argc, char** argv) {
    (void)argc;
    g_app = this;
    m_argv0 = argv != nullptr && argv[0] != nullptr ? argv[0] : "sdmc:/switch/nsmbw/launcher.nro";
    log_init(m_argv0.c_str());
    log_write("[app] argv0 %s\n", m_argv0.c_str());

    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&m_pad);
    hidInitializeTouchScreen();
    log_write("[input] pad styles 0x%llx\n", static_cast<unsigned long long>(padGetStyleSet(&m_pad)));
}

App::~App() {
    // join worker threads before the services and curl they use are shut down.
    m_widgets.clear();

    if (m_renderer) {
        m_renderer.reset();
    }
    if (m_vg != nullptr) {
        nvgDeleteDk(m_vg);
        m_vg = nullptr;
    }
    destroyFramebufferResources();
    http::Exit();
    if (m_socketReady) {
        socketExit();
    }
    if (m_nsReady) {
        nsExit();
    }
    if (m_plReady) {
        plExit();
    }
    log_close();
}

void App::LoadTheme() {
    // OLED Black theme, inheriting the base black values.
    SetColour(m_theme, ThemeEntryID_BACKGROUND, 0x000000ff);
    SetColour(m_theme, ThemeEntryID_GRID, 0x46464640);
    SetColour(m_theme, ThemeEntryID_POPUP, 0x323232ff);
    SetColour(m_theme, ThemeEntryID_ERROR, 0xfa5a3aff);
    SetColour(m_theme, ThemeEntryID_TEXT, 0xfbfbfbff);
    SetColour(m_theme, ThemeEntryID_TEXT_INFO, 0xd1d1d1ff);
    SetColour(m_theme, ThemeEntryID_TEXT_SELECTED, 0x00ffc8ff);
    SetColour(m_theme, ThemeEntryID_SELECTED_BACKGROUND, 0x323232ff);
    SetColour(m_theme, ThemeEntryID_FOCUS, 0x000000b4);
    SetColour(m_theme, ThemeEntryID_LINE, 0xfbfbfbff);
    SetColour(m_theme, ThemeEntryID_LINE_SEPARATOR, 0x707070ff);
    SetColour(m_theme, ThemeEntryID_SIDEBAR, 0x1f1f1fff);
    SetColour(m_theme, ThemeEntryID_SCROLLBAR, 0x00ffc8ff);
    SetColour(m_theme, ThemeEntryID_SCROLLBAR_BACKGROUND, 0x00000000);
    SetColour(m_theme, ThemeEntryID_PROGRESSBAR, 0x00ffc8ff);
    SetColour(m_theme, ThemeEntryID_PROGRESSBAR_BACKGROUND, 0x464646ff);
    SetColour(m_theme, ThemeEntryID_HIGHLIGHT_1, 0x1989c6ff);
    SetColour(m_theme, ThemeEntryID_HIGHLIGHT_2, 0x89f0f2ff);
}

void App::LoadFonts() {
    if (R_FAILED(plInitialize(PlServiceType_User))) {
        log_write("[font] plInitialize failed\n");
        return;
    }
    m_plReady = true;

    PlFontData standard{};
    PlFontData extended{};
    if (R_FAILED(plGetSharedFontByType(&standard, PlSharedFontType_Standard)) ||
        R_FAILED(plGetSharedFontByType(&extended, PlSharedFontType_NintendoExt))) {
        log_write("[font] shared font failed\n");
        return;
    }

    const int standardId = nvgCreateFontMem(m_vg, "Standard", (unsigned char*)standard.address, standard.size, 0);
    const int extendedId = nvgCreateFontMem(m_vg, "Extended", (unsigned char*)extended.address, extended.size, 0);
    log_write("[font] standard %d extended %d\n", standardId, extendedId);
    if (standardId >= 0 && extendedId >= 0) {
        nvgAddFallbackFontId(m_vg, standardId, extendedId);
    }
}

void App::Push(std::unique_ptr<ui::Widget>&& widget) {
    if (widget->ShouldPop()) {
        return;
    }
    if (!m_widgets.empty()) {
        m_widgets.back()->OnFocusLost();
    }
    m_widgets.emplace_back(std::move(widget))->OnFocusGained();
}

void App::Run() {
    const AppletOperationMode mode = appletGetOperationMode();
    m_width = mode == AppletOperationMode_Console ? 1920 : 1280;
    m_height = mode == AppletOperationMode_Console ? 1080 : 720;
    m_scale = {static_cast<float>(m_width) / SCREEN_WIDTH, static_cast<float>(m_height) / SCREEN_HEIGHT};

    if (R_SUCCEEDED(nsInitialize())) {
        m_nsReady = true;
    }
    if (R_SUCCEEDED(socketInitializeDefault())) {
        m_socketReady = true;
    }
    CleanupPendingInstalls();

    m_device = dk::DeviceMaker{}.setCbDebug(DekoError).create();
    m_queue = dk::QueueMaker{m_device}.setFlags(DkQueueFlags_Graphics).create();
    m_poolImages.emplace(m_device, DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image, 16 * 1024 * 1024);
    m_poolCode.emplace(m_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code,
                       128 * 1024);
    m_poolData.emplace(m_device, DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached, 1024 * 1024);
    m_cmdbuf = dk::CmdBufMaker{m_device}.create();
    const CMemPool::Handle commandMemory = m_poolData->allocate(StaticCmdSize);
    m_cmdbuf.addMemory(commandMemory.getMemBlock(), commandMemory.getOffset(), commandMemory.getSize());

    createFramebufferResources();
    m_renderer.emplace(m_width, m_height, m_device, m_queue, *m_poolImages, *m_poolCode, *m_poolData);
    m_vg = nvgCreateDk(&*m_renderer, NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    if (m_vg == nullptr) {
        log_write("[nvg] context failed\n");
        return;
    }

    LoadFonts();
    LoadTheme();

    log_write("[app] %ux%u scale %.2fx%.2f\n", m_width, m_height, m_scale.x, m_scale.y);
    Push(std::make_unique<ui::MainMenu>());

    u64 start = armTicksToNs(armGetSystemTick());
    while (!m_quit && appletMainLoop() && !m_widgets.empty()) {
        const AppletOperationMode current = appletGetOperationMode();
        const unsigned width = current == AppletOperationMode_Console ? 1920 : 1280;
        const unsigned height = current == AppletOperationMode_Console ? 1080 : 720;
        if (width != m_width || height != m_height) {
            m_width = width;
            m_height = height;
            m_scale = {static_cast<float>(m_width) / SCREEN_WIDTH, static_cast<float>(m_height) / SCREEN_HEIGHT};
            m_queue.waitIdle();
            destroyFramebufferResources();
            createFramebufferResources();
            m_renderer->UpdateViewSize(m_width, m_height);
        }

        Poll();
        ui::gfx::updateHighlightAnimation();

        ui::Widget* top = m_widgets.back().get();
        top->Update(&m_controller, &m_touch);
        if (m_widgets.back().get() == top && top->ShouldPop()) {
            m_widgets.pop_back();
            if (!m_widgets.empty()) {
                m_widgets.back()->OnFocusGained();
            }
        }

        Draw();

        const u64 now = armTicksToNs(armGetSystemTick());
        m_deltaTime = std::clamp(static_cast<double>(now - start) / 1e6, 1000.0 / 120.0, 1000.0 / 15.0) /
                      (1000.0 / 60.0);
        start = now;
    }
}

void App::Poll() {
    m_controller.Reset();

    HidTouchScreenState state{};
    hidGetTouchScreenStates(&state, 1);
    m_touch.is_clicked = false;

    if (state.count == 1 && !m_touch.is_touching) {
        m_touch.initial = m_touch.cur = state.touches[0];
        m_touch.is_touching = true;
        m_touch.is_tap = true;
    } else if (state.count >= 1 && m_touch.is_touching) {
        m_touch.cur = state.touches[0];
        if (m_touch.is_tap && (std::abs((s32)m_touch.initial.x - (s32)m_touch.cur.x) > 20 ||
                               std::abs((s32)m_touch.initial.y - (s32)m_touch.cur.y) > 20)) {
            m_touch.is_tap = false;
            m_touch.is_scroll = true;
        }
    } else if (m_touch.is_touching) {
        m_touch.is_touching = false;
        m_touch.is_scroll = false;
        if (m_touch.is_tap) {
            m_touch.is_clicked = true;
        } else {
            m_touch.is_end = true;
        }
    }

    u64 down = 0;
    u64 held = 0;
    u64 up = 0;
    if (!m_touch.is_touching && !m_touch.is_clicked) {
        padUpdate(&m_pad);
        down = padGetButtonsDown(&m_pad);
        held = padGetButtons(&m_pad);
        up = padGetButtonsUp(&m_pad);
    }

    m_controller.m_kdown = down;
    m_controller.m_kheld = held;
    m_controller.m_kup = up;
    m_controller.UpdateButtonHeld(static_cast<u64>(Button::UP) | static_cast<u64>(Button::DOWN), m_deltaTime);
}

void App::Draw() {
    const auto slot = m_queue.acquireImage(m_swapchain);
    m_queue.submitCommands(m_framebufferCmdlists[slot]);
    m_queue.submitCommands(m_renderCmdlist);

    nvgBeginFrame(m_vg, m_width, m_height, 1.f);
    nvgFontFace(m_vg, "Standard");
    nvgScale(m_vg, m_scale.x, m_scale.y);
    for (auto& widget : m_widgets) {
        if (!widget->IsHidden()) {
            widget->Draw(m_vg, &m_theme);
        }
    }
    nvgResetTransform(m_vg);
    nvgEndFrame(m_vg);
    m_queue.presentImage(m_swapchain, slot);
}

void App::Launch(const std::string& nroPath) {
    log_write("[launch] %s next-load %d\n", nroPath.c_str(), envHasNextLoad());
    if (!envHasNextLoad()) {
        SetStatus("The loader does not support chaining NROs.");
        return;
    }

    std::ofstream launch((std::filesystem::path(RootDirectory()) / "launch.toml").string());
    if (launch) {
        launch << "launcher = \"" << m_argv0 << "\"\n";
        launch << "nro = \"" << nroPath << "\"\n";
    }

    __nx_applet_exit_mode = 0;
    const Result rc = envSetNextLoad(nroPath.c_str(), nroPath.c_str());
    if (R_FAILED(rc)) {
        log_write("[launch] envSetNextLoad failed 0x%08X\n", static_cast<unsigned>(rc));
        SetStatus("Could not start that NRO.");
        return;
    }
    m_quit = true;
}

void App::createFramebufferResources() {
    m_swapchain = nullptr;

    dk::ImageLayout depthLayout;
    dk::ImageLayoutMaker{m_device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_S8)
        .setDimensions(m_width, m_height)
        .initialize(depthLayout);

    m_depthBufferMem = m_poolImages->allocate(depthLayout.getSize(), depthLayout.getAlignment());
    m_depthBuffer.initialize(depthLayout, m_depthBufferMem.getMemBlock(), m_depthBufferMem.getOffset());

    dk::ImageLayout colourLayout;
    dk::ImageLayoutMaker{m_device}
        .setFlags(DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression)
        .setFormat(DkImageFormat_RGBA8_Unorm)
        .setDimensions(m_width, m_height)
        .initialize(colourLayout);

    std::array<DkImage const*, NumFramebuffers> images;
    const u64 size = colourLayout.getSize();
    const u32 alignment = colourLayout.getAlignment();
    for (unsigned i = 0; i < images.size(); ++i) {
        m_framebuffersMem[i] = m_poolImages->allocate(size, alignment);
        m_framebuffers[i].initialize(colourLayout, m_framebuffersMem[i].getMemBlock(),
                                     m_framebuffersMem[i].getOffset());
        dk::ImageView colourTarget{m_framebuffers[i]};
        dk::ImageView depthTarget{m_depthBuffer};
        m_cmdbuf.bindRenderTargets(&colourTarget, &depthTarget);
        m_framebufferCmdlists[i] = m_cmdbuf.finishList();
        images[i] = &m_framebuffers[i];
    }

    m_swapchain = dk::SwapchainMaker{m_device, nwindowGetDefault(), images}.create();
    recordStaticCommands();
}

void App::destroyFramebufferResources() {
    if (!m_swapchain) {
        return;
    }
    m_queue.waitIdle();
    m_cmdbuf.clear();
    m_swapchain.destroy();
    for (unsigned i = 0; i < NumFramebuffers; ++i) {
        m_framebuffersMem[i].destroy();
    }
    m_depthBufferMem.destroy();
}

void App::recordStaticCommands() {
    dk::RasterizerState rasterizerState;
    dk::ColorState colorState;
    dk::ColorWriteState colorWriteState;
    dk::BlendState blendState;

    m_cmdbuf.setViewports(0, {{0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f}});
    m_cmdbuf.setScissors(0, {{0, 0, static_cast<u32>(m_width), static_cast<u32>(m_height)}});
    m_cmdbuf.clearColor(0, DkColorMask_RGBA, 0.f, 0.f, 0.f, 1.f);
    m_cmdbuf.clearDepthStencil(true, 1.0f, 0xFF, 0);
    m_cmdbuf.bindRasterizerState(rasterizerState);
    m_cmdbuf.bindColorState(colorState);
    m_cmdbuf.bindColorWriteState(colorWriteState);
    m_renderCmdlist = m_cmdbuf.finishList();
}

} // namespace launcher
