#include "frame_stats.h"

#include "runtime_config.h"

#include <algorithm>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#if defined(MKW_PLATFORM_SWITCH)
#include <switch.h>
#endif

namespace FrameStats {
namespace {

// One CSV row per two second window so a run can be diffed by scripts/perf_compare.py.
// The guest thread only formats and enqueues: on Switch, stderr and perf.csv are
// unbuffered SD writes, and doing them inline stalled the guest for 150-200 ms at
// every window boundary. This sink owns both writes on core 1.
class Sink {
public:
    static Sink& Instance() {
        static Sink* sink = new Sink();
        return *sink;
    }

    void Csv(std::string text) { Push(true, std::move(text)); }
    void Log(std::string text) { Push(false, std::move(text)); }

private:
    struct Item {
        bool csv = false;
        std::string text;
    };

    Sink() {
        thread_ = std::thread([this] { Run(); });
        thread_.detach();
    }

    void Push(bool csv, std::string text) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(Item{csv, std::move(text)});
        }
        cv_.notify_one();
    }

    void Run() {
#if defined(MKW_PLATFORM_SWITCH)
        // Host-side I/O belongs on core 1 with the frame worker and GX thread.
        svcSetThreadCoreMask(threadGetCurHandle(), 1, 1ull << 1);
#endif
        std::FILE* csv = nullptr;
        for (;;) {
            Item item;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return !queue_.empty(); });
                item = std::move(queue_.front());
                queue_.pop_front();
            }
            if (item.csv) {
                if (csv == nullptr) {
                    const auto path = RuntimeConfigFile::ApplicationDataDirectory() / "Logs" / "perf.csv";
                    std::error_code ec;
                    std::filesystem::create_directories(path.parent_path(), ec);
                    const std::string text = RuntimeConfigFile::PathToUtf8(path);
                    const bool exists = std::filesystem::exists(path, ec);
                    csv = std::fopen(text.c_str(), "ab");
                    if (csv == nullptr) {
                        continue;
                    }
                    if (!exists) {
                        std::fputs("build,scene,frames,fps,p50,p95,p99,worst,game_ms,gx_ms,guest_ms,audio_ms,"
                                   "gxfifo_ms,gxfifo_writes,bp_ms,dl_ms,draw_ms,submit_ms,guest_core,audio_core,over20,"
                                   "mode,passes,efb_passes,scale\n",
                                   csv);
                    }
                }
                std::fputs(item.text.c_str(), csv);
                std::fflush(csv);
            } else {
                std::fputs(item.text.c_str(), stderr);
            }
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Item> queue_;
    std::thread thread_;
};

} // namespace

double Percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    const size_t index = std::min(values.size() - 1,
                                  static_cast<size_t>(fraction * static_cast<double>(values.size())));
    std::nth_element(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), values.end());
    return values[index];
}

void Csv(uint32_t scene, int frames, double fps, double p50, double p95, double p99, double worst,
         double gameMs, double gxMs, double guestMs, double audioMs, double gxFifoMs, double gxFifoWrites,
         double bpMs, double dlMs, double drawMs, double submitMs, int guestCore, int audioCore, int over20,
         const char* mode, uint32_t renderPasses, uint32_t efbPasses, float renderScale) {
    char row[512];
    std::snprintf(row, sizeof(row),
                 "%s %s,0x%03X,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.0f,%.2f,%.2f,%.2f,%.2f,%d,%d,%d,%s,%u,%u,%.2f\n",
                 __DATE__, __TIME__, scene, frames, fps, p50, p95, p99, worst, gameMs, gxMs, guestMs, audioMs,
                 gxFifoMs, gxFifoWrites, bpMs, dlMs, drawMs, submitMs, guestCore, audioCore, over20, mode,
                 renderPasses, efbPasses, static_cast<double>(renderScale));
    Sink::Instance().Csv(row);
}

void Log(const char* format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    std::string line("[vi] ");
    line += buffer;
    Sink::Instance().Log(std::move(line));
}

} // namespace FrameStats
