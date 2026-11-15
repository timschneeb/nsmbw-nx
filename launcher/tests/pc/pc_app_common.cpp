#include "pc_app_common.hpp"

#include "ui/nvg_util.hpp"
#include "ui/progress_box.hpp"
#include "ui/widget.hpp"

#include <nanovg.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <utility>

extern "C" {
unsigned char nsmbw_logo_rgba[820 * 339 * 4] = {};
}

namespace launcher::pc {
namespace {
constexpr int kLogoWidth = 820;
constexpr int kLogoHeight = 339;

auto CurrentAppStorage() -> App*& {
    static App* app{};
    return app;
}
} // namespace

auto CurrentApp() -> App*& {
    return CurrentAppStorage();
}

void LoadLogo() {
    const std::filesystem::path path =
        std::filesystem::path(LAUNCHER_SOURCE_DIR) / "runtime/src/platform/switch/nsmbw_logo.rgba";
    std::ifstream file(path, std::ios::binary);
    if (file) {
        file.read(reinterpret_cast<char*>(nsmbw_logo_rgba), sizeof(nsmbw_logo_rgba));
        return;
    }
    for (int y = 0; y < kLogoHeight; ++y) {
        for (int x = 0; x < kLogoWidth; ++x) {
            const size_t at = (static_cast<size_t>(y) * kLogoWidth + x) * 4;
            nsmbw_logo_rgba[at + 0] = static_cast<unsigned char>(x * 255 / (kLogoWidth - 1));
            nsmbw_logo_rgba[at + 1] = static_cast<unsigned char>(y * 255 / (kLogoHeight - 1));
            nsmbw_logo_rgba[at + 2] = 32;
            nsmbw_logo_rgba[at + 3] = 255;
        }
    }
}

void LoadFonts(NVGcontext* vg) {
    const char* fonts[] = {
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf",
    };
    for (const char* font : fonts) {
        if (std::filesystem::is_regular_file(font)) {
            const int id = nvgCreateFont(vg, "Standard", font);
            if (id >= 0) {
                std::fprintf(stderr, "pc: font %s\n", font);
                break;
            }
        }
    }
}

void LoadTheme(Theme& t) {
    t.elements[ThemeEntryID_BACKGROUND] = {ElementType::Colour, 0, nvgRGBA(0, 0, 0, 255)};
    t.elements[ThemeEntryID_GRID] = {ElementType::Colour, 0, nvgRGBA(70, 70, 70, 64)};
    t.elements[ThemeEntryID_POPUP] = {ElementType::Colour, 0, nvgRGBA(50, 50, 50, 255)};
    t.elements[ThemeEntryID_ERROR] = {ElementType::Colour, 0, nvgRGBA(250, 90, 58, 255)};
    t.elements[ThemeEntryID_TEXT] = {ElementType::Colour, 0, nvgRGBA(251, 251, 251, 255)};
    t.elements[ThemeEntryID_TEXT_INFO] = {ElementType::Colour, 0, nvgRGBA(209, 209, 209, 255)};
    t.elements[ThemeEntryID_TEXT_SELECTED] = {ElementType::Colour, 0, nvgRGBA(0, 255, 200, 255)};
    t.elements[ThemeEntryID_SELECTED_BACKGROUND] = {ElementType::Colour, 0, nvgRGBA(50, 50, 50, 255)};
    t.elements[ThemeEntryID_FOCUS] = {ElementType::Colour, 0, nvgRGBA(0, 0, 0, 180)};
    t.elements[ThemeEntryID_LINE] = {ElementType::Colour, 0, nvgRGBA(251, 251, 251, 255)};
    t.elements[ThemeEntryID_LINE_SEPARATOR] = {ElementType::Colour, 0, nvgRGBA(112, 112, 112, 255)};
    t.elements[ThemeEntryID_SIDEBAR] = {ElementType::Colour, 0, nvgRGBA(31, 31, 31, 255)};
    t.elements[ThemeEntryID_SCROLLBAR] = {ElementType::Colour, 0, nvgRGBA(0, 255, 200, 255)};
    t.elements[ThemeEntryID_PROGRESSBAR] = {ElementType::Colour, 0, nvgRGBA(0, 255, 200, 255)};
    t.elements[ThemeEntryID_PROGRESSBAR_BACKGROUND] = {ElementType::Colour, 0, nvgRGBA(70, 70, 70, 255)};
    t.elements[ThemeEntryID_HIGHLIGHT_1] = {ElementType::Colour, 0, nvgRGBA(25, 137, 198, 255)};
    t.elements[ThemeEntryID_HIGHLIGHT_2] = {ElementType::Colour, 0, nvgRGBA(137, 240, 242, 255)};
}

} // namespace launcher::pc

namespace launcher {

auto App::Get() -> App& { return *pc::CurrentApp(); }
auto App::GetApp() -> App* { return pc::CurrentApp(); }
auto App::GetVg() -> NVGcontext* {
    return pc::CurrentApp() != nullptr ? pc::CurrentApp()->m_vg : nullptr;
}
auto App::PlaySoundEffect(SoundEffect) -> void {}
auto App::GetTextScrollSpeed() -> int { return 1; }
auto App::IsApplication() -> bool { return true; }

auto App::Exit() -> void {
    if (pc::CurrentApp() != nullptr) {
        pc::CurrentApp()->Quit();
    }
}

auto App::GetEmmcSize(s64* free, s64* total) -> Result {
    return GetSdSize(free, total);
}

void App::Run() {}

void App::Launch(const std::string& path) {
    m_launched = path;
    m_quit = true;
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

void App::TestUpdateTop(Controller* controller, TouchInfo* touch) {
    if (m_widgets.empty()) {
        return;
    }
    ui::Widget* top = m_widgets.back().get();
    top->Update(controller, touch);
    if (m_widgets.back().get() == top && top->ShouldPop()) {
        m_widgets.pop_back();
        if (!m_widgets.empty()) {
            m_widgets.back()->OnFocusGained();
        }
    }
    TestSettlePop();
}

void App::TestSettlePop() {
    while (!m_widgets.empty() && m_widgets.back()->ShouldPop()) {
        m_widgets.pop_back();
        if (!m_widgets.empty()) {
            m_widgets.back()->OnFocusGained();
        }
    }
}

void App::RenderFrame() {
    if (m_vg == nullptr) {
        return;
    }
    nvgBeginFrame(m_vg, static_cast<float>(m_width), static_cast<float>(m_height), m_pixel_ratio);
    nvgFontFace(m_vg, "Standard");
    for (auto& widget : m_widgets) {
        if (!widget->IsHidden()) {
            widget->Draw(m_vg, &m_theme);
        }
    }
    nvgEndFrame(m_vg);
}

void DrawElement(float x, float y, float w, float h, ThemeEntryID id) {
    const auto& e = App::Get().Theme().elements[id];
    if (e.type == ElementType::Colour) {
        ui::gfx::drawRect(App::GetVg(), Vec4{x, y, w, h}, e.colour);
    }
}

void DrawElement(const Vec4& v, ThemeEntryID id) {
    DrawElement(v.x, v.y, v.w, v.h, id);
}

} // namespace launcher

namespace launcher::ui {

auto TestProgressCancelAfter() -> int& {
    static int value = -1;
    return value;
}

auto TestProgressCancelCalls() -> int& {
    static int value = 0;
    return value;
}

ProgressBox::ProgressBox(const std::string&, const std::string&,
                         const ProgressBoxCallback& callback, const ProgressBoxDoneCallback& done)
    : m_callback{callback}, m_done{done} {}

ProgressBox::~ProgressBox() = default;

auto ProgressBox::OnFocusGained() noexcept -> void {
    Widget::OnFocusGained();
    if (m_ran) {
        return;
    }
    m_ran = true;
    const Result rc = m_callback ? m_callback(this) : 0;
    if (m_done) {
        m_done(rc);
    }
    SetPop();
}

auto ProgressBox::Update(Controller*, TouchInfo*) -> void {}
auto ProgressBox::Draw(NVGcontext*, Theme*) -> void {}

} // namespace launcher::ui
