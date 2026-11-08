#pragma once

#include <cstdint>
#include <vector>

// Perf-window output. The guest thread only formats and enqueues; a background thread owns the
// stderr and perf.csv writes.
namespace FrameStats {

// Appends one row to Logs/perf.csv. The header is written on the first row.
void Csv(uint32_t scene, int frames, double fps, double p50, double p95, double p99, double worst,
         double gameMs, double gxMs, double guestMs, double audioMs, double gxFifoMs, double gxFifoWrites,
         double bpMs, double dlMs, double drawMs, double submitMs, int guestCore, int audioCore, int over20,
         const char* mode, uint32_t renderPasses, uint32_t efbPasses, float renderScale);

// Formats and enqueues one "[vi] ..." line for stderr.
void Log(const char* format, ...);

// Nearest-rank percentile. Takes the samples by value so the caller's window stays untouched.
double Percentile(std::vector<double> values, double fraction);

} // namespace FrameStats
