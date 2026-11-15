#pragma once

#include "ui/list.hpp"
#include "ui/scrolling_text.hpp"
#include "ui/widget.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace launcher::ui {

// Vendored from sphaira, reduced to callback entries.
class SidebarEntryBase : public Widget {
public:
    explicit SidebarEntryBase(const std::string& title, const std::string& info);

    using Widget::Draw;
    virtual void Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left);
    auto OnFocusGained() noexcept -> void override;
    auto OnFocusLost() noexcept -> void override;

    void DrawEntry(NVGcontext* vg, Theme* theme, const std::string& left, const std::string& right, bool use_selected);

protected:
    std::string m_title;

private:
    const std::string m_info;
    ScrollingText m_scolling_title{};
    ScrollingText m_scolling_info_title{};
    ScrollingText m_scolling_value{};
};

class SidebarEntryCallback final : public SidebarEntryBase {
public:
    using Callback = std::function<void()>;

    explicit SidebarEntryCallback(const std::string& title, const Callback& cb, const std::string& info);
    explicit SidebarEntryCallback(const std::string& title, const Callback& cb, bool pop_on_click = false, const std::string& info = "");
    void Draw(NVGcontext* vg, Theme* theme, const Vec4& root_pos, bool left) override;

private:
    const Callback m_callback;
    const bool m_pop_on_click;
};

class Sidebar : public Widget {
public:
    enum class Side { LEFT, RIGHT };
    using Items = std::vector<std::unique_ptr<SidebarEntryBase>>;

public:
    explicit Sidebar(const std::string& title, Side side, float width = 450.f);
    explicit Sidebar(const std::string& title, const std::string& sub, Side side, float width = 450.f);
    ~Sidebar();

    auto Update(Controller* controller, TouchInfo* touch) -> void override;
    auto Draw(NVGcontext* vg, Theme* theme) -> void override;
    auto OnFocusGained() noexcept -> void override;
    auto OnFocusLost() noexcept -> void override;

    auto Add(std::unique_ptr<SidebarEntryBase>&& entry) -> SidebarEntryBase*;

    template<typename T, typename... Args>
    auto Add(Args&&... args) -> T* {
        return static_cast<T*>(Add(std::make_unique<T>(std::forward<Args>(args)...)));
    }

private:
    void SetIndex(s64 index);
    void SetupButtons();

private:
    const std::string m_title;
    const std::string m_sub;
    const Side m_side;
    Items m_items{};
    s64 m_index{};

    std::unique_ptr<List> m_list{};

    Vec4 m_top_bar{};
    Vec4 m_bottom_bar{};
    Vec2 m_title_pos{};
    Vec4 m_base_pos{};
    ScrollingText m_scroll_title{};
    ScrollingText m_scroll_sub{};

    static constexpr float m_title_size{28.f};
    static constexpr Vec2 m_box_size{400.f, 70.f};
};

} // namespace launcher::ui
