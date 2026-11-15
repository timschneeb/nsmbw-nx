#include "app.hpp"
#include "ui/menus/menu_base.hpp"
#include "ui/nvg_util.hpp"

namespace launcher::ui::menu {

MenuBase::MenuBase(const std::string& title, u32 flags) : m_title{title}, m_flags{flags} {
    // this->SetParent(this);
    this->SetPos(30, 87, 1220 - 30, 646 - 87);
    SetAction(Button::START, Action{App::Exit});
}

MenuBase::~MenuBase() {
}

void MenuBase::Update(Controller* controller, TouchInfo* touch) {
    Widget::Update(controller, touch);
}

void MenuBase::Draw(NVGcontext* vg, Theme* theme) {
    DrawElement(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, ThemeEntryID_BACKGROUND);
    Widget::Draw(vg, theme);

    const float start_y = 70;
    float bounds[4];

    const auto status_content_left = 1220.f;

    gfx::drawRect(vg, 30.f, 86.f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));
    gfx::drawRect(vg, 30.f, 646.0f, 1220.f, 1.f, theme->GetColour(ThemeEntryID_LINE));

    constexpr float header_left = 74.f;
    const auto text_w = SCREEN_WIDTH / 2 - 30;
    const auto header_right = std::min<float>(text_w, status_content_left - 15.f);
    const auto header_width = std::max(0.f, header_right - header_left);

    float title_width = header_width;
    float title_sub_x = header_right;
    float title_sub_width = 0.f;
    if (!m_title_sub_heading.empty() && header_width > 0.f) {
        nvgFontSize(vg, 28.f);
        gfx::textBounds(vg, 0, 0, bounds, m_title.c_str());
        const auto measured_title_width = std::max(0.f, bounds[2] - bounds[0]);
        const auto max_title_width = std::max(0.f, header_width * 0.55f);
        title_width = std::min(measured_title_width + 6.f, max_title_width);
        title_sub_x = header_left + title_width + 10.f;
        title_sub_width = std::max(0.f, header_right - title_sub_x);
    }

    m_scroll_title.Draw(vg, true, header_left, start_y, title_width, 28.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT), m_title);
    m_scroll_title_sub_heading.Draw(vg, true, title_sub_x, start_y, title_sub_width, 16.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_TEXT_INFO), m_title_sub_heading);
    m_scroll_title_warning.Draw(vg, true, title_sub_x, start_y - 24.f, title_sub_width, 16.f,
        NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, theme->GetColour(ThemeEntryID_ERROR), m_title_warning);
    m_scroll_sub_heading.Draw(vg, true, 80, 675, text_w - 160, 18, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), m_sub_heading.c_str());
}

} // namespace launcher::ui::menu
