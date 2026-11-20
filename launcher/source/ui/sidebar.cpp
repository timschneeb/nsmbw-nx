#include "ui/sidebar.hpp"

#include "app.hpp"
#include "ui/nvg_util.hpp"
#include "ui/popup_list.hpp"

#include <algorithm>

namespace launcher::ui {
namespace {

auto DistanceBetweenY(const Vec4& va, const Vec4& vb) -> Vec4 {
    return Vec4{va.x, va.y, va.w, vb.y - va.y};
}

} // namespace

SidebarEntryBase::SidebarEntryBase(const std::string& title, const std::string& info)
    : m_title{title}, m_info{info} {
}

void SidebarEntryBase::Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) {
    if (!HasFocus()) {
        return;
    }

    gfx::drawRectOutline(vg, theme, 4.f, m_pos);

    if (m_info.empty()) {
        return;
    }

    nvgSave(vg);
    nvgScissor(vg, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    Vec4 info_box{};
    info_box.y = 86;
    info_box.w = 400;
    info_box.x = left ? root_pos.x + root_pos.w + 10 : root_pos.x - info_box.w - 10;

    const float info_pad = 30;
    const float title_font_size = 18;
    const float info_font_size = 18;
    const float pad_after_title = title_font_size + info_pad;
    const float x = info_box.x + info_pad;
    const auto end_w = info_box.w - info_pad * 2;

    float bounds[4];
    nvgFontSize(vg, info_font_size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgTextLineHeight(vg, 1.7);
    nvgTextBoxBounds(vg, 0, 0, end_w, m_info.c_str(), nullptr, bounds);
    info_box.h = pad_after_title + info_pad * 2 + bounds[3] - bounds[1];

    gfx::drawRect(vg, info_box, theme->GetColour(ThemeEntryID_SIDEBAR), 5);

    float y = info_box.y + info_pad;
    m_scolling_info_title.Draw(vg, true, x, y, end_w, title_font_size,
                               NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT),
                               m_title.c_str());

    y += pad_after_title;
    gfx::drawTextBox(vg, x, y, info_font_size, end_w, theme->GetColour(ThemeEntryID_TEXT)
                     , m_info.c_str());

    nvgRestore(vg);
}

auto SidebarEntryBase::OnFocusGained() noexcept -> void {
    Widget::OnFocusGained();
}

auto SidebarEntryBase::OnFocusLost() noexcept -> void {
    Widget::OnFocusLost();
    m_scolling_title.Reset();
    m_scolling_info_title.Reset();
    m_scolling_value.Reset();
}

void SidebarEntryBase::DrawEntry(NVGcontext* vg, Theme* theme, const std::string& left,
                                 const std::string& right, bool use_selected) {
    const auto colour_id = ThemeEntryID_TEXT;

    constexpr float font_size = 20.f;
    constexpr float padding = 15.f;
    constexpr float column_gap = 20.f;
    const float ypos = m_pos.y + (m_pos.h / 2.f);
    const float label_x = m_pos.x + padding;
    const float content_width = std::max(0.f, m_pos.w - padding * 2.f);

    if (right.empty()) {
        m_scolling_title.Draw(vg, HasFocus(), label_x, ypos, content_width, font_size,
                              NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(colour_id), left);
        return;
    }

    float bounds[4];
    nvgFontSize(vg, font_size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgTextBounds(vg, 0, 0, right.c_str(), nullptr, bounds);
    const float value_text_width = std::max(0.f, bounds[2] - bounds[0]);

    const float value_width = std::clamp(value_text_width, 48.f, content_width * 0.45f);
    const float value_x = m_pos.x + m_pos.w - padding - value_width;
    const float label_width = std::max(0.f, value_x - column_gap - label_x);

    m_scolling_title.Draw(vg, HasFocus(), label_x, ypos, label_width, font_size,
                          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(colour_id), left);

    const auto value_id = use_selected ? ThemeEntryID_TEXT_SELECTED : ThemeEntryID_TEXT;
    const float value_draw_x = value_text_width < value_width
        ? value_x + value_width - value_text_width
        : value_x;
    const float value_clip_width = m_pos.x + m_pos.w - padding - value_draw_x;
    m_scolling_value.Draw(vg, HasFocus(), value_draw_x, ypos, value_clip_width, font_size,
                          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(value_id), right);
}

SidebarEntryCallback::SidebarEntryCallback(const std::string& title, const Callback& cb, bool pop_on_click,
                                           const std::string& info)
    : SidebarEntryBase{title, info}, m_callback{cb}, m_pop_on_click{pop_on_click} {
    SetAction(Button::A, Action{"OK", [this] {
        m_callback();
        if (m_pop_on_click) {
            SetPop();
        }
    }});
}

SidebarEntryCallback::SidebarEntryCallback(const std::string& title, const Callback& cb,
                                           const std::string& info)
    : SidebarEntryCallback{title, cb, false, info} {
}

void SidebarEntryCallback::Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) {
    SidebarEntryBase::Draw(vg, theme, root_pos, left);
    SidebarEntryBase::DrawEntry(vg, theme, m_title, {}, false);
}

SidebarEntryBool::SidebarEntryBool(const std::string& title, bool option, const Callback& cb,
                                   const std::string& info, const std::string& true_str,
                                   const std::string& false_str)
    : SidebarEntryBase{title, info}
    , m_option{option}
    , m_callback{cb}
    , m_true_str{true_str}
    , m_false_str{false_str} {
    SetAction(Button::A, Action{"OK", [this] {
        m_option ^= 1;
        if (m_callback) {
            m_callback(m_option);
        }
    }});
}

void SidebarEntryBool::Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) {
    SidebarEntryBase::Draw(vg, theme, root_pos, left);
    SidebarEntryBase::DrawEntry(vg, theme, m_title, m_option ? m_true_str : m_false_str, m_option);
}

SidebarEntryArray::SidebarEntryArray(const std::string& title, const Items& items,
                                     const Callback& cb, s64 index, const std::string& info)
    : SidebarEntryBase{title, info}
    , m_items{items}
    , m_callback{cb}
    , m_index{index} {
    m_list_callback = [this] {
        auto list = std::make_unique<PopupList>(m_title, m_items, [this](std::optional<s64> op_idx) {
            if (op_idx) {
                m_index = *op_idx;
                if (m_callback) {
                    m_callback(m_index);
                }
            }
        }, m_index);
        App::Get().Push(std::move(list));
    };

    SetAction(Button::A, Action{"OK", [this] {
        m_list_callback();
    }});
}

void SidebarEntryArray::Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) {
    SidebarEntryBase::Draw(vg, theme, root_pos, left);
    SidebarEntryBase::DrawEntry(vg, theme, m_title, m_items[m_index], true);
}

Sidebar::Sidebar(const std::string& title, Side side, float width)
    : Sidebar{title, "", side, width} {
}

Sidebar::Sidebar(const std::string& title, const std::string& sub, Side side, float width)
    : m_title{title}, m_sub{sub}, m_side{side} {
    switch (m_side) {
        case Side::LEFT:
            SetPos(Vec4{0.f, 0.f, width, SCREEN_HEIGHT});
            break;
        case Side::RIGHT:
            SetPos(Vec4{SCREEN_WIDTH - width, 0.f, width, SCREEN_HEIGHT});
            break;
    }

    m_top_bar = Vec4{m_pos.x + 15.f, 86.f, m_pos.w - 30.f, 1.f};
    m_bottom_bar = Vec4{m_pos.x + 15.f, 646.f, m_pos.w - 30.f, 1.f};
    m_title_pos = Vec2{m_pos.x + 30.f, m_pos.y + 40.f};
    m_base_pos = Vec4{GetX() + 30.f, GetY() + 170.f, m_pos.w - (30.f * 2.f), 70.f};

    SetUiButtonPos({m_pos.x + m_pos.w - 60.f, 675});

    const Vec4 pos = DistanceBetweenY(m_top_bar, m_bottom_bar);
    m_list = std::make_unique<List>(1, 6, pos, m_base_pos);
    m_list->SetScrollBarPos(GetX() + GetW() - 20, m_base_pos.y - 10, pos.h - m_base_pos.y + 48);
}

Sidebar::~Sidebar() = default;

auto Sidebar::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);

    if (m_items.empty()) {
        SetPop();
        return;
    }

    if (touch->is_clicked && !touch->in_range(GetPos())) {
        SetPop();
    } else {
        m_list->OnUpdate(controller, touch, m_index, m_items.size(), [this](bool touch, auto i) {
            SetIndex(i);
            if (touch) {
                FireAction(Button::A);
            }
        });
    }

    if (m_items[m_index]->ShouldPop()) {
        SetPop();
    }
}

auto Sidebar::Draw(NVGcontext* vg, Theme* theme) -> void {
    gfx::drawRect(vg, 0.f, 0.f, SCREEN_WIDTH, SCREEN_HEIGHT, nvgRGBA(0, 0, 0, 120));
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_SIDEBAR));
    constexpr float header_padding = 30.f;
    constexpr float header_gap = 16.f;
    const auto header_right = m_pos.x + m_pos.w - header_padding;
    auto title_width = std::max(0.f, header_right - m_title_pos.x);
    if (!m_sub.empty()) {
        float bounds[4]{};
        nvgFontSize(vg, 16.f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
        nvgTextBounds(vg, 0.f, 0.f, m_sub.c_str(), nullptr, bounds);
        const auto available_width = std::max(0.f, header_right - m_title_pos.x);
        const auto sub_width = std::min(std::max(0.f, bounds[2] - bounds[0]), available_width * 0.4f);
        const auto sub_x = header_right - sub_width;
        title_width = std::max(0.f, sub_x - header_gap - m_title_pos.x);
        m_scroll_sub.Draw(vg, true, sub_x, m_title_pos.y + 10.f, sub_width, 16.f,
                          NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                          theme->GetColour(ThemeEntryID_TEXT_INFO), m_sub);
    }
    m_scroll_title.Draw(vg, true, m_title_pos.x, m_title_pos.y, title_width, m_title_size,
                        NVG_ALIGN_LEFT | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), m_title);
    gfx::drawRect(vg, m_top_bar, theme->GetColour(ThemeEntryID_LINE));
    gfx::drawRect(vg, m_bottom_bar, theme->GetColour(ThemeEntryID_LINE));
    gfx::drawTextArgs(vg, m_pos.x + 30, 675, 18.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                      theme->GetColour(ThemeEntryID_TEXT), "%zu / %zu", m_index + 1, m_items.size());

    Widget::Draw(vg, theme);

    m_list->Draw(vg, theme, m_items.size(), [this](auto* vg, auto* theme, auto& v, auto i) {
        const auto& [x, y, w, h] = v;

        if (i != static_cast<s64>(m_items.size()) - 1) {
            gfx::drawRect(vg, x, y + h, w, 1.f, theme->GetColour(ThemeEntryID_LINE_SEPARATOR));
        }

        m_items[i]->SetY(y);
        m_items[i]->Draw(vg, theme, m_pos, m_side == Side::LEFT);
    });
}

auto Sidebar::OnFocusGained() noexcept -> void {
    Widget::OnFocusGained();
    SetHidden(false);
}

auto Sidebar::OnFocusLost() noexcept -> void {
    Widget::OnFocusLost();
    SetHidden(true);
}

auto Sidebar::Add(std::unique_ptr<SidebarEntryBase>&& entry) -> SidebarEntryBase* {
    auto& stored = m_items.emplace_back(std::forward<decltype(entry)>(entry));
    stored->SetPos(m_base_pos);

    if (m_items.size() == 1) {
        stored->OnFocusGained();
        SetupButtons();
    }

    return stored.get();
}

void Sidebar::SetIndex(s64 index) {
    if (m_index != index) {
        m_items[m_index]->OnFocusLost();
        m_index = index;
        m_items[m_index]->OnFocusGained();
        SetupButtons();
    }
}

void Sidebar::SetupButtons() {
    RemoveActions();

    for (const auto& [button, action] : m_items[m_index]->GetActions()) {
        SetAction(button, action);
    }

    SetActions(
        std::make_pair(Button::B, Action{"Back", [this] {
            SetPop();
        }})
    );

    if (HasAction(Button::LEFT) || HasAction(Button::RIGHT)) {
        m_list->SetPageJump(false);
    } else {
        m_list->SetPageJump(true);
    }
}

} // namespace launcher::ui
