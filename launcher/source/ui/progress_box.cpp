#include "ui/progress_box.hpp"

#include "app.hpp"
#include "ui/nvg_util.hpp"
#include "ui/option_box.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>

namespace launcher::ui {
namespace {

void threadFunc(void* arg) {
    auto d = static_cast<ProgressBox::ThreadData*>(arg);
    d->result = d->callback(d->pbox);
    d->pbox->RequestExit();
}

auto FormatSize(double size, double base) -> std::string {
    static const char* const suffixes[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB"};
    size_t index = 0;
    while (size >= base && index < std::size(suffixes) - 1) {
        size /= base;
        index++;
    }

    char buffer[32];
    if (index == 0) {
        std::snprintf(buffer, sizeof(buffer), "%.0f %s", size, suffixes[index]);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.2f %s", size, suffixes[index]);
    }
    return buffer;
}

} // namespace

ProgressBox::ProgressBox(const std::string& action, const std::string& title,
                         const ProgressBoxCallback& callback, const ProgressBoxDoneCallback& done)
    : m_done{done}, m_action{action}, m_title{title} {
    SetAction(Button::B, Action{[this] {
        App::Get().Push(std::make_unique<OptionBox>("Are you sure you wish to cancel?", "No", "Yes", 1,
            [this](auto op_index) {
                if (op_index && *op_index) {
                    RequestExit();
                    SetPop();
                }
            }));
    }});

    SetupPosition();

    m_thread_data.pbox = this;
    m_thread_data.callback = callback;
    m_thread = std::thread(threadFunc, &m_thread_data);
}

ProgressBox::~ProgressBox() {
    m_stop_source.request_stop();

    if (m_thread.joinable()) {
        m_thread.join();
    }

    if (m_done) {
        m_done(m_thread_data.result);
    }
}

void ProgressBox::SetupPosition() {
    m_pos.w = 770.f;
    m_pos.h = 295.f;
    m_pos.x = (SCREEN_WIDTH / 2.f) - (m_pos.w / 2.f);
    m_pos.y = (SCREEN_HEIGHT / 2.f) - (m_pos.h / 2.f);
}

auto ProgressBox::Update(Controller* controller, TouchInfo* touch) -> void {
    Widget::Update(controller, touch);

    if (ShouldExit()) {
        SetPop();
    }
}

auto ProgressBox::Draw(NVGcontext* vg, Theme* theme) -> void {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_timestamp.GetSeconds()) {
        m_timestamp.Update();
        m_speed = m_offset - m_last_offset;
        m_last_offset = m_offset;
    }

    const auto action = m_action;
    const auto title = m_title;
    const auto transfer = m_transfer;
    const auto size = m_size;
    const auto offset = m_offset;
    const auto speed = m_speed;
    const auto last_offset = m_last_offset;

    gfx::dimBackground(vg);
    gfx::drawRect(vg, m_pos, theme->GetColour(ThemeEntryID_POPUP), 5);

    const auto center_x = m_pos.x + m_pos.w / 2;
    const auto end_y = m_pos.y + m_pos.h;
    const auto progress_bar_w = m_pos.w - 230;
    const Vec4 prog_bar = {center_x - progress_bar_w / 2, end_y - 95, progress_bar_w, 12};

    nvgSave(vg);
    nvgIntersectScissor(vg, GetX(), GetY(), GetW(), GetH());

    if (offset && size) {
        const auto font_size = 18.F;
        const auto pad = 15.F;
        const float rounding = 5;

        gfx::drawRect(vg, prog_bar, theme->GetColour(ThemeEntryID_PROGRESSBAR_BACKGROUND), rounding);
        const u32 percentage = ((double)offset / (double)size) * 100.0;
        gfx::drawRect(vg, prog_bar.x, prog_bar.y, ((float)offset / (float)size) * prog_bar.w, prog_bar.h,
                      theme->GetColour(ThemeEntryID_PROGRESSBAR), rounding);
        gfx::drawTextArgs(vg, prog_bar.x + prog_bar.w + pad, prog_bar.y + prog_bar.h / 2, font_size,
                          NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, theme->GetColour(ThemeEntryID_TEXT), "%u%%",
                          percentage);

        const auto rad = 15;
        gfx::drawSpinner(vg, theme, prog_bar.x - pad - rad, prog_bar.y + prog_bar.h / 2, rad,
                         armTicksToNs(armGetSystemTick()) / 1e+9);

        const auto left = size - last_offset;
        const auto left_seconds = speed > 0 ? left / speed : 0;
        const auto hours = left_seconds / (60 * 60);
        const auto minutes = left_seconds % (60 * 60) / 60;
        const auto seconds = left_seconds % 60;

        char time_str[64];
        if (hours) {
            std::snprintf(time_str, sizeof(time_str), "%zu hours %zu minutes remaining", hours, minutes);
        } else if (minutes) {
            std::snprintf(time_str, sizeof(time_str), "%zu minutes %zu seconds remaining", minutes, seconds);
        } else {
            std::snprintf(time_str, sizeof(time_str), "%zu seconds remaining", seconds);
        }

        gfx::drawTextArgs(vg, center_x, prog_bar.y + prog_bar.h + 30, 18,
                          NVG_ALIGN_CENTER | NVG_ALIGN_TOP, theme->GetColour(ThemeEntryID_TEXT), "%s (%s)",
                          time_str, FormatSize(speed, 1000.0).c_str());
    }

    gfx::drawTextArgs(vg, center_x, m_pos.y + 40, 24, NVG_ALIGN_CENTER | NVG_ALIGN_TOP,
                      theme->GetColour(ThemeEntryID_TEXT), action.c_str());

    const auto draw_text = [&](ScrollingText& scroll, const std::string& txt, float y, float text_size,
                               float pad, ThemeEntryID id) {
        float bounds[4];
        nvgFontSize(vg, text_size);
        gfx::textBounds(vg, 0, 0, bounds, txt.c_str());

        const auto min_x = GetX() + pad;
        const auto title_x = std::max(min_x, center_x - (bounds[2] - bounds[0]) / 2);

        scroll.Draw(vg, true, title_x, y, GetW() - pad * 2, text_size, NVG_ALIGN_LEFT | NVG_ALIGN_TOP,
                    theme->GetColour(id), txt.c_str());
    };

    draw_text(m_scroll_title, title, m_pos.y + 100, 22, 160, ThemeEntryID_TEXT);
    if (!transfer.empty()) {
        draw_text(m_scroll_transfer, transfer, m_pos.y + 160, 18, 30, ThemeEntryID_TEXT_INFO);
    }

    nvgRestore(vg);
}

auto ProgressBox::SetActionName(const std::string& action) -> ProgressBox& {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_action = action;
    return *this;
}

auto ProgressBox::SetTitle(const std::string& title) -> ProgressBox& {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_title = title;
    return *this;
}

auto ProgressBox::NewTransfer(const std::string& transfer) -> ProgressBox& {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_transfer = transfer;
    m_size = 0;
    m_offset = 0;
    m_last_offset = 0;
    m_timestamp.Update();
    return *this;
}

auto ProgressBox::SetTransfer(const std::string& transfer) -> ProgressBox& {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_transfer = transfer;
    return *this;
}

auto ProgressBox::UpdateTransfer(s64 offset, s64 size) -> ProgressBox& {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_size = size;
    m_offset = offset;
    return *this;
}

void ProgressBox::RequestExit() {
    m_stop_source.request_stop();
}

auto ProgressBox::ShouldExit() -> bool {
    return m_stop_source.stop_requested();
}

auto ProgressBox::ShouldExitResult() -> Result {
    if (ShouldExit()) {
        return MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    return 0;
}

} // namespace launcher::ui
