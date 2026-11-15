#pragma once

#ifdef LAUNCHER_TEST
#include <switch.h>
#include <ui/types.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

struct NVGcontext;

namespace launcher {

namespace ui { struct Widget; }
struct Controller;
struct TouchInfo;

enum class SoundEffect {
    Focus,
    Scroll,
};

class App {
public:
    App();
    ~App();

    void Run();
    void Quit() { m_quit = true; }
    auto IsQuitting() const -> bool { return m_quit; }
    auto IsShuttingDown() const -> bool { return m_shutting_down; }

    static auto Get() -> App&;
    static auto GetApp() -> App*;
    static auto GetVg() -> NVGcontext*;
    static auto PlaySoundEffect(SoundEffect effect) -> void;
    static auto GetTextScrollSpeed() -> int;
    static auto IsApplication() -> bool;
    static auto Exit() -> void;
    static auto GetSdSize(s64* free, s64* total) -> Result;
    static auto GetEmmcSize(s64* free, s64* total) -> Result;

    void Launch(const std::string& path);
    void Push(std::unique_ptr<ui::Widget>&& widget);
    auto Theme() -> launcher::Theme& { return m_theme; }
    auto RenderScreenshot(const std::string& path) -> bool;
    void RenderFrame();
    auto SetPixelRatio(float ratio) -> void { m_pixel_ratio = ratio; }
    auto SetStatus(std::string status) -> void { m_status = std::move(status); }
    auto Status() const -> const std::string& { return m_status; }

    auto TestWidgetCount() const -> size_t { return m_widgets.size(); }
    auto TestTop() -> ui::Widget* { return m_widgets.empty() ? nullptr : m_widgets.back().get(); }
    auto TestWidgetAt(size_t index) -> ui::Widget* {
        return index < m_widgets.size() ? m_widgets[index].get() : nullptr;
    }
    void TestUpdateTop(Controller* controller, TouchInfo* touch);
    void TestSettlePop();
    auto LaunchedPath() const -> const std::string& { return m_launched; }

private:
    std::vector<std::unique_ptr<ui::Widget>> m_widgets;
    bool m_quit{};
    bool m_shutting_down{};
    std::string m_status;
    std::string m_launched;
    launcher::Theme m_theme{};
    NVGcontext* m_vg{};
    int m_width{1280};
    int m_height{720};
    float m_pixel_ratio{1.f};
    void* m_egl_display{};
    void* m_egl_context{};
    unsigned m_fbo{};
    unsigned m_color{};
    unsigned m_depth{};
};

void DrawElement(float x, float y, float w, float h, ThemeEntryID id);
void DrawElement(const Vec4& v, ThemeEntryID id);

} // namespace launcher
#else

#include "ui/types.hpp"

#include <deko3d.hpp>
#include <nanovg.h>
#include <nanovg/dk_renderer.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace launcher {

namespace ui { struct Widget; }

enum class SoundEffect {
    Focus,
    Scroll,
};

class App {
public:
    App(int argc, char** argv);
    ~App();

    void Run();
    void Quit() { m_quit = true; }
    auto IsQuitting() const -> bool { return m_quit; }
    auto IsShuttingDown() const -> bool { return m_shutting_down; }

    static auto Get() -> App&;
    static auto GetApp() -> App*;
    static auto GetVg() -> NVGcontext*;
    static auto PlaySoundEffect(SoundEffect effect) -> void;
    static auto GetTextScrollSpeed() -> int;
    static auto IsApplication() -> bool;
    static auto Exit() -> void;
    static auto GetSdSize(s64* free, s64* total) -> Result;
    static auto GetEmmcSize(s64* free, s64* total) -> Result;

    auto Vg() const -> NVGcontext* { return m_vg; }
    auto Theme() -> launcher::Theme& { return m_theme; }

    void Launch(const std::string& nroPath);
    void Push(std::unique_ptr<ui::Widget>&& widget);

    auto SetStatus(std::string status) { m_status = std::move(status); }
    const auto& Status() const { return m_status; }

private:
    void Poll();
    void Draw();
    void SettlePop();
    void createFramebufferResources();
    void destroyFramebufferResources();
    void recordStaticCommands();
    void LoadFonts();
    void LoadTheme();

    bool m_quit{};
    bool m_shutting_down{};
    std::string m_argv0;
    std::string m_status;
    double m_deltaTime{1.0};

    std::vector<std::unique_ptr<ui::Widget>> m_widgets;
    launcher::Theme m_theme{};
    launcher::Controller m_controller{};
    launcher::TouchInfo m_touch{};
    PadState m_pad{};

    unsigned m_width{1280};
    unsigned m_height{720};
    launcher::Vec2 m_scale{1.f, 1.f};

    NVGcontext* m_vg{};
    bool m_plReady{};
    bool m_nsReady{};
    bool m_socketReady{};

    dk::UniqueDevice m_device;
    dk::UniqueQueue m_queue;
    std::optional<CMemPool> m_poolImages;
    std::optional<CMemPool> m_poolCode;
    std::optional<CMemPool> m_poolData;
    dk::UniqueCmdBuf m_cmdbuf;
    CMemPool::Handle m_depthBufferMem;
    CMemPool::Handle m_framebuffersMem[2];
    dk::Image m_depthBuffer;
    dk::Image m_framebuffers[2];
    DkCmdList m_framebufferCmdlists[2];
    dk::UniqueSwapchain m_swapchain;
    DkCmdList m_renderCmdlist;
    std::optional<nvg::DkRenderer> m_renderer;

    static constexpr unsigned NumFramebuffers = 2;
    static constexpr unsigned StaticCmdSize = 0x1000;
};

void DrawElement(float x, float y, float w, float h, ThemeEntryID id);
void DrawElement(const Vec4& v, ThemeEntryID id);

} // namespace launcher

#endif // LAUNCHER_TEST
