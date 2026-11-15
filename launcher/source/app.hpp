#pragma once

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
    void createFramebufferResources();
    void destroyFramebufferResources();
    void recordStaticCommands();
    void LoadFonts();
    void LoadTheme();

    bool m_quit{};
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
