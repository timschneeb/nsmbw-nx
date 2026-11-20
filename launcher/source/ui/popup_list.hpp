#pragma once

#include "ui/list.hpp"
#include "ui/scrolling_text.hpp"
#include "ui/widget.hpp"

#include <optional>

namespace launcher::ui {

// Vendored from sphaira.
class PopupList final : public Widget {
public:
    using Items = std::vector<std::string>;
    using Callback = std::function<void(std::optional<s64>)>;

public:
    explicit PopupList(const std::string& title, const Items& items, const Callback& cb,
                       s64 index = 0);

    auto Update(Controller* controller, TouchInfo* touch) -> void override;
    auto Draw(NVGcontext* vg, Theme* theme) -> void override;
    auto OnFocusGained() noexcept -> void override;
    auto OnFocusLost() noexcept -> void override;

#ifdef LAUNCHER_TEST
    auto TestIndex() const -> s64 { return m_index; }
    auto ItemCount() const -> size_t { return m_items.size(); }
    auto ItemTitle(size_t index) const -> const std::string& { return m_items[index]; }
#endif

private:
    void SetIndex(s64 index);

private:
    static constexpr Vec2 m_title_pos{70.f, 28.f};
    static constexpr Vec4 m_block{280.f, 110.f, SCREEN_HEIGHT, 60.f};
    static constexpr float m_text_xoffset{15.f};
    static constexpr float m_line_width{1220.f};

    const std::string m_title;
    const Items m_items;
    Callback m_callback{};
    s64 m_index{};
    s64 m_starting_index{};

    std::unique_ptr<List> m_list{};
    ScrollingText m_scroll_title{};
    ScrollingText m_scroll_text{};

    float m_line_top{};
    float m_line_bottom{};
};

} // namespace launcher::ui
