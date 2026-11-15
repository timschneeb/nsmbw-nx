#pragma once

#if defined(LAUNCHER_TEST_UI)
#include "ui/widget.hpp"

#include <functional>
#include <string>

namespace launcher::ui {

struct ProgressBox;
using ProgressBoxCallback = std::function<Result(ProgressBox*)>;
using ProgressBoxDoneCallback = std::function<void(Result)>;

// Test hook: cancel on ShouldExit call number N (0 based), negative disables it.
auto TestProgressCancelAfter() -> int&;
auto TestProgressCancelCalls() -> int&;

struct ProgressBox : Widget {
    ProgressBox() = default;
    ProgressBox(const std::string& action, const std::string& title,
                const ProgressBoxCallback& callback = {},
                const ProgressBoxDoneCallback& done = {});
    ~ProgressBox() override;

    auto Update(Controller* controller, TouchInfo* touch) -> void override;
    auto Draw(NVGcontext* vg, Theme* theme) -> void override;
    auto OnFocusGained() noexcept -> void override;

    auto SetActionName(const std::string&) -> ProgressBox& { return *this; }
    auto SetTitle(const std::string&) -> ProgressBox& { return *this; }
    auto NewTransfer(const std::string&) -> ProgressBox& { return *this; }
    auto SetTransfer(const std::string&) -> ProgressBox& { return *this; }
    auto UpdateTransfer(s64, s64) -> ProgressBox& { return *this; }

    void RequestExit() {}
    auto ShouldExit() -> bool {
        const int after = TestProgressCancelAfter();
        if (after < 0) {
            return false;
        }
        return ++TestProgressCancelCalls() > after;
    }
    auto ShouldExitResult() -> Result { return 0; }

private:
    ProgressBoxCallback m_callback;
    ProgressBoxDoneCallback m_done;
    bool m_ran{};
};

} // namespace launcher::ui
#elif defined(LAUNCHER_TEST)
#include <switch.h>
#include <string>

namespace launcher::ui {
struct ProgressBox {
    bool exited{};
    auto ShouldExit() -> bool { return exited; }
    void RequestExit() { exited = true; }
    auto NewTransfer(const std::string&) -> ProgressBox& { return *this; }
    auto SetTransfer(const std::string&) -> ProgressBox& { return *this; }
    auto UpdateTransfer(s64, s64) -> ProgressBox& { return *this; }
    auto SetTitle(const std::string&) -> ProgressBox& { return *this; }
    auto SetActionName(const std::string&) -> ProgressBox& { return *this; }
    auto ShouldExitResult() -> Result { return exited ? 1 : 0; }
};
} // namespace launcher::ui
#else

#include "ui/scrolling_text.hpp"
#include "ui/widget.hpp"

#include <functional>
#include <mutex>
#include <stop_token>
#include <thread>

namespace launcher::ui {

struct ProgressBox;
using ProgressBoxCallback = std::function<Result(ProgressBox*)>;
using ProgressBoxDoneCallback = std::function<void(Result)>;

// Vendored from sphaira, image and footer options removed.
struct ProgressBox final : Widget {
    ProgressBox(
        const std::string& action,
        const std::string& title,
        const ProgressBoxCallback& callback,
        const ProgressBoxDoneCallback& done = nullptr
    );
    ~ProgressBox();

    auto Update(Controller* controller, TouchInfo* touch) -> void override;
    auto Draw(NVGcontext* vg, Theme* theme) -> void override;

    auto SetActionName(const std::string& action) -> ProgressBox&;
    auto SetTitle(const std::string& title) -> ProgressBox&;
    auto NewTransfer(const std::string& transfer) -> ProgressBox&;
    auto SetTransfer(const std::string& transfer) -> ProgressBox&;
    auto UpdateTransfer(s64 offset, s64 size) -> ProgressBox&;

    void RequestExit();
    auto ShouldExit() -> bool;
    auto ShouldExitResult() -> Result;

private:
    void SetupPosition();

public:
    struct ThreadData {
        ProgressBox* pbox{};
        ProgressBoxCallback callback{};
        Result result{};
    };

private:
    std::mutex m_mutex{};
    std::thread m_thread{};
    ThreadData m_thread_data{};
    ProgressBoxDoneCallback m_done{};
    std::stop_source m_stop_source{};

    std::string m_action{};
    std::string m_title{};
    std::string m_transfer{};
    s64 m_size{};
    s64 m_offset{};
    s64 m_last_offset{};
    s64 m_speed{};
    TimeStamp m_timestamp{};

    ScrollingText m_scroll_title{};
    ScrollingText m_scroll_transfer{};
};

} // namespace launcher::ui

#endif // LAUNCHER_TEST_UI
