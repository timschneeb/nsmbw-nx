#pragma once

#if defined(LAUNCHER_TEST_UI)
#include "ui/widget.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace launcher::ui {

struct ProgressBox;
using ProgressBoxCallback = std::function<Result(ProgressBox*)>;
using ProgressBoxDoneCallback = std::function<void(Result)>;

// Test hooks.
auto TestProgressCancelAfter() -> int&;
auto TestProgressCancelCalls() -> int&;
auto TestProgressCancelOnLabel() -> std::string&;
auto TestProgressCancelAfterBytes() -> s64&;
auto TestProgressEvents() -> std::vector<std::pair<s64, s64>>&;

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
    auto NewTransfer(const std::string& transfer) -> ProgressBox& {
        m_transfer = transfer;
        if (!m_combined) {
            m_offset = 0;
            m_size = 0;
        }
        return *this;
    }
    auto SetTransfer(const std::string& transfer) -> ProgressBox& {
        m_transfer = transfer;
        return *this;
    }
    auto UpdateTransfer(s64 offset, s64 size) -> ProgressBox& {
        if (m_combined) {
            m_size = m_total;
            m_offset = m_base + offset;
        } else {
            m_size = size;
            m_offset = offset;
        }
        TestProgressEvents().push_back({m_offset, m_size});
        return *this;
    }
    auto SetCombinedProgress(s64 total) -> ProgressBox& {
        m_combined = true;
        m_total = total;
        m_base = 0;
        m_offset = 0;
        m_size = total;
        return *this;
    }
    auto SetProgressBase(s64 base) -> ProgressBox& {
        if (m_combined) {
            m_base = base;
            m_offset = base;
        }
        return *this;
    }
    auto IsCombined() -> bool { return m_combined; }

    void RequestExit() {}
    auto ShouldExit() -> bool {
        const auto& label = TestProgressCancelOnLabel();
        if (!label.empty() && m_transfer.find(label) != std::string::npos) {
            const s64 component_offset = m_combined ? m_offset - m_base : m_offset;
            if (component_offset >= TestProgressCancelAfterBytes()) {
                return true;
            }
        }
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
    bool m_combined{};
    s64 m_base{};
    s64 m_total{};
    s64 m_offset{};
    s64 m_size{};
    std::string m_transfer{};
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
    auto SetCombinedProgress(s64) -> ProgressBox& { return *this; }
    auto SetProgressBase(s64) -> ProgressBox& { return *this; }
    auto IsCombined() -> bool { return false; }
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
    auto SetCombinedProgress(s64 total) -> ProgressBox&;
    auto SetProgressBase(s64 base) -> ProgressBox&;
    auto IsCombined() -> bool;

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
    s64 m_base{};
    s64 m_total{};
    bool m_combined{};
    TimeStamp m_timestamp{};

    ScrollingText m_scroll_title{};
    ScrollingText m_scroll_transfer{};
};

} // namespace launcher::ui

#endif // LAUNCHER_TEST_UI
