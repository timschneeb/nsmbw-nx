#pragma once

#include "nanovg.h"
#include <switch.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <type_traits>
#include <variant>

#define SCREEN_WIDTH 1280.f
#define SCREEN_HEIGHT 720.f

namespace launcher {

struct Vec2 {
    constexpr Vec2() = default;
    constexpr Vec2(float _x, float _y) : x{_x}, y{_y} {}
    float x{}, y{};
};

struct Vec4 {
    constexpr Vec4() = default;
    constexpr Vec4(float _x, float _y, float _w, float _h) : x{_x}, y{_y}, w{_w}, h{_h} {}
    constexpr Vec4(const Vec2& p, const Vec2& s) : x{p.x}, y{p.y}, w{s.x}, h{s.y} {}
    float x{}, y{}, w{}, h{};
};

struct TimeStamp {
    TimeStamp() { Update(); }
    void Update() { start = std::chrono::steady_clock::now(); }
    auto GetSeconds() const -> u64 {
        return static_cast<u64>(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count());
    }
    std::chrono::steady_clock::time_point start{};
};

enum class ElementType { None, Texture, Colour };

struct ElementEntry {
    ElementType type{};
    int texture{};
    NVGcolor colour{};
};

enum ThemeEntryID {
    ThemeEntryID_BACKGROUND,
    ThemeEntryID_GRID,
    ThemeEntryID_POPUP,
    ThemeEntryID_ERROR,
    ThemeEntryID_TEXT,
    ThemeEntryID_TEXT_INFO,
    ThemeEntryID_TEXT_SELECTED,
    ThemeEntryID_SELECTED_BACKGROUND,
    ThemeEntryID_FOCUS,
    ThemeEntryID_LINE,
    ThemeEntryID_LINE_SEPARATOR,
    ThemeEntryID_SIDEBAR,
    ThemeEntryID_SCROLLBAR,
    ThemeEntryID_SCROLLBAR_BACKGROUND,
    ThemeEntryID_PROGRESSBAR,
    ThemeEntryID_PROGRESSBAR_BACKGROUND,
    ThemeEntryID_HIGHLIGHT_1,
    ThemeEntryID_HIGHLIGHT_2,
    ThemeEntryID_ICON_COLOUR,
    ThemeEntryID_ICON_AUDIO,
    ThemeEntryID_ICON_VIDEO,
    ThemeEntryID_ICON_IMAGE,
    ThemeEntryID_ICON_FILE,
    ThemeEntryID_ICON_FOLDER,
    ThemeEntryID_ICON_ZIP,
    ThemeEntryID_ICON_NRO,
    ThemeEntryID_MAX,
};

struct Theme {
    ElementEntry elements[ThemeEntryID_MAX];
    auto GetColour(ThemeEntryID id) const { return elements[id].colour; }
};

struct TouchPoint {
    s32 x{};
    s32 y{};
};

struct TouchInfo {
    TouchInfo() = default;
    TouchPoint initial{};
    TouchPoint cur{};
    bool is_touching{};
    bool is_tap{};
    bool is_scroll{};
    bool is_clicked{};
    bool is_end{};

    auto in_range(const Vec4& v) const -> bool {
        return cur.x >= v.x && cur.x <= v.x + v.w && cur.y >= v.y && cur.y <= v.y + v.h;
    }
};

enum class Button : u64 {
    A = 1ull << 0,
    B = 1ull << 1,
    X = 1ull << 2,
    Y = 1ull << 3,
    L = 1ull << 4,
    R = 1ull << 5,
    L2 = 1ull << 6,
    R2 = 1ull << 7,
    L3 = 1ull << 8,
    R3 = 1ull << 9,
    START = 1ull << 10,
    SELECT = 1ull << 11,
    DPAD_LEFT = 1ull << 12,
    DPAD_RIGHT = 1ull << 13,
    DPAD_UP = 1ull << 14,
    DPAD_DOWN = 1ull << 15,
    LEFT = DPAD_LEFT,
    RIGHT = DPAD_RIGHT,
    UP = DPAD_UP,
    DOWN = DPAD_DOWN,
    NONE = 0,
    ANY_BUTTON = static_cast<u64>(A) | static_cast<u64>(B) | static_cast<u64>(X) | static_cast<u64>(Y) |
                 static_cast<u64>(L) | static_cast<u64>(R) | static_cast<u64>(L2) | static_cast<u64>(R2) |
                 static_cast<u64>(L3) | static_cast<u64>(R3) | static_cast<u64>(START) | static_cast<u64>(SELECT),
    ANY_HORIZONTAL = static_cast<u64>(LEFT) | static_cast<u64>(RIGHT),
    ANY_VERTICAL = static_cast<u64>(UP) | static_cast<u64>(DOWN),
    ANY_DIRECTION = static_cast<u64>(LEFT) | static_cast<u64>(RIGHT) | static_cast<u64>(UP) | static_cast<u64>(DOWN),
    ANY = static_cast<u64>(ANY_BUTTON) | static_cast<u64>(ANY_DIRECTION),
};

inline Button operator|(Button a, Button b) {
    return static_cast<Button>(static_cast<u64>(a) | static_cast<u64>(b));
}

enum ActionType : u8 { DOWN = 1 << 0, UP = 1 << 1, HELD = 1 << 2 };

inline ActionType operator|(ActionType a, ActionType b) {
    return static_cast<ActionType>(static_cast<u64>(a) | static_cast<u64>(b));
}

struct Action final {
    using CallbackEmpty = std::function<void()>;
    using CallbackWithBool = std::function<void(bool)>;
    using Callback = std::variant<CallbackEmpty, CallbackWithBool>;

    explicit Action(const Callback& cb) : Action{ActionType::DOWN, "", cb} {}
    explicit Action(const std::string& hint, const Callback& cb) : Action{ActionType::DOWN, hint, cb} {}
    explicit Action(u8 type, const Callback& cb) : Action{type, "", cb} {}
    explicit Action(u8 type, const std::string& hint, const Callback& cb)
        : m_type{type}, m_callback{cb}, m_hint{hint} {}

    auto IsHidden() const noexcept { return m_hint.empty(); }

    auto Invoke(bool down) const {
        std::visit([down](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, CallbackEmpty>) {
                arg();
            } else {
                arg(down);
            }
        }, m_callback);
    }

    u8 m_type{};
    Callback m_callback{};
    std::string m_hint{};
};

struct Controller {
    u64 m_kdown{};
    u64 m_kheld{};
    u64 m_kup{};

    constexpr auto Set(Button button, bool down) noexcept -> void {
        m_kdown = down ? m_kdown | static_cast<u64>(button) : m_kdown & ~static_cast<u64>(button);
    }
    constexpr auto Got(u64 k, Button button) const noexcept -> bool {
        return (k & static_cast<u64>(button)) > 0;
    }
    constexpr auto GotDown(Button button) const noexcept -> bool { return Got(m_kdown, button); }
    constexpr auto GotHeld(Button button) const noexcept -> bool { return Got(m_kheld, button); }
    constexpr auto GotUp(Button button) const noexcept -> bool { return (m_kup & static_cast<u64>(button)) > 0; }
    constexpr auto Reset() noexcept -> void { m_kdown = 0; m_kup = 0; }

    void UpdateButtonHeld(u64 buttons, double delta) {
        (void)buttons;
        (void)delta;
    }
};

} // namespace launcher
