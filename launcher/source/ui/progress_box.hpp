#pragma once

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
