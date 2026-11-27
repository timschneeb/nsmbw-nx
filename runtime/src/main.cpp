#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <array>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <sys/stat.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <io.h>
#include <crtdbg.h>
#include <windows.h>
#include <mmsystem.h>
#include <dbghelp.h>
#else
#include <signal.h>
#if defined(__x86_64__)
// Only the x86 POSIX fault path inspects ucontext_t to recover the page-fault
// write bit. macOS exposes the signal-handler context through sys/ucontext.h;
// avoid ucontext.h itself because its deprecated user-context APIs require
// _XOPEN_SOURCE. The arm64 handler does not inspect a host context at all.
#if defined(__APPLE__)
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif
#endif
#include <unistd.h>
#endif
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif
#if defined(MKW_PLATFORM_SWITCH)
#include <switch.h>

#if defined(__SWITCH__)
// See MkwCurrentCpuSlotRef in isa/ppc_isa_context.h. libnx places per-thread slots after its own
// TLS block; 0x180 + 8 * slot is the layout threadTlsGet uses, read inline here because the
// helper call is exactly what this is avoiding.
uint32_t MkwAllocCurrentCpuTlsSlot() noexcept {
    const s32 slot = threadTlsAlloc(nullptr);
    if (slot < 0) {
        std::abort();
    }
    return 0x180u + 8u * static_cast<uint32_t>(slot);
}

#if defined(MKW_PGO_GENERATE)
extern "C" void __gcov_dump(void);
extern "C" void __gcov_reset(void);

// gcov resolves GCOV_PREFIX in its per-object initialisers, which run before main, so the
// redirect has to be installed from an early constructor. The console never returns from main,
// so the counters are dumped on a timer instead of at exit.
__attribute__((constructor(101))) void MkwPgoRedirectGcov() {
    // Strip every leading component: libgcov creates no directories, so a flat tree under one
    // directory we can create is the difference between a profile and 2,000 skipped dumps. The
    // object base names are unique, so nothing collides.
    ::setenv("GCOV_PREFIX", "sdmc:/gcov", 1);
    ::setenv("GCOV_PREFIX_STRIP", "100", 1);
}

void MkwStartPgoDumpTimer() {
    ::mkdir("sdmc:/gcov", 0777);
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::minutes(1));
            __gcov_dump();
            __gcov_reset();
        }
    }).detach();
}
#endif
#endif
#endif
#if defined(__linux__) && !defined(__ANDROID__)
#include <SDL3/SDL_messagebox.h>
#endif

#include "netplay_ui.h"
#include "netplay_start.h"
#include "netplay_session.h"
#include "bt_passthrough.h"
#include "frame_input.h"
#include "det_clock.h"
#include "game_folder.h"
#include "abi_bridge.h"
#include "guest_flat_memory.h"
#include "gx_guest_write.h"
#include "hle/audio/ax_out_native.h"
#include "hle/audio/axfx_delay_native.h"
#include "hle/audio/axfx_reverb_native.h"
#include "memory.h"
#include "system_bridge.h"
#include "ppc_runtime.h"
#include "aurora_events.h"
#include "display_settings.h"
#include "graphics_info.h"
#include "wii_remote_input.h"
#include "discord_presence.h"
#include "fiber_manager.h"
#include "hle_stubs.h"
#include "perf_stats.h"
#include "runtime_config.h"
#include "shadow_sampler.h"
#include "runtime_log.h"
#if defined(NSMBW_MEMTRACE)
#include "mem_tracker.h"
#endif
#include "updater.h"
#include "runtime_product.h"
#include "recomp_mod_loader.h"
#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>

// Defined in `runtime/src/hle/vi.cpp` (used by GX/VI HLE).
extern std::atomic_bool g_auroraFrameActive;
extern "C" int g_gxFrameCount;
extern "C" const char* DVDResolveHostPathForTest(const char* dvdPath);
bool OS_HLE_InterruptsEnabled() noexcept;

namespace {

// Defined below, beside the fatal-log machinery.
std::string FormatHostStackTrace(unsigned framesToSkip = 0);

void ServiceGuestTimingDuringAuroraFrameWait() {
    // Aurora can block inside FIFO drains before control returns to GX HLE, for as long as a
    // whole display period. Keep VI retraces, alarms and audio moving at wall-clock cadence here
    // while still suppressing guest rescheduling and recursive Aurora work.
    VI_HLE_ProcessRetracesDeferred(8);
    OS_HLE_ProcessAlarmsDeferred(8);
    Audio_HLE_PollDeferred();
}

#if defined(_WIN32)
int __cdecl WindowsCrtReportHook(int reportType, char* message, int* returnValue) {
    if (returnValue) {
        *returnValue = 0;
    }

    RT_LOG(RT_TAG_RUNTIME) << "CRT report type=" << reportType;
    if (message) {
        std::cerr << ": " << message;
    } else {
        std::cerr << '\n';
    }

    RT_LOG(RT_TAG_RUNTIME) << "CRT report stack:\n" << FormatHostStackTrace(1);
    std::cerr.flush();
    return TRUE;
}

void ConfigureWindowsFatalDialogBehavior() {
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportHook(WindowsCrtReportHook);
}

// A busy PC (browsers, launchers, sync and antivirus tools) delays the game's threads when they wake
// for a frame, which shows as dropped frames at 60. Run above normal priority, and opt out of
// Windows' power throttling (EcoQoS), which can slow the process down and coarsen its timers.
void RaiseWindowsProcessPriority() {
    if (!::SetPriorityClass(::GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) {
        RT_LOG(RT_TAG_RUNTIME) << "SetPriorityClass failed: " << ::GetLastError() << std::endl;
    }
    struct PowerThrottlingState {
        ULONG Version;
        ULONG ControlMask;
        ULONG StateMask;
    };
    constexpr int kProcessPowerThrottling = 4;  // PROCESS_INFORMATION_CLASS::ProcessPowerThrottling
    constexpr ULONG kExecutionSpeed = 0x1, kIgnoreTimerResolution = 0x4;
    using SetProcessInformationFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
    const auto setInfo = reinterpret_cast<SetProcessInformationFn>(
        reinterpret_cast<void*>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation")));
    if (setInfo != nullptr) {
        PowerThrottlingState state{1, kExecutionSpeed | kIgnoreTimerResolution, 0};
        if (!setInfo(::GetCurrentProcess(), kProcessPowerThrottling, &state, sizeof(state))) {
            state.ControlMask = kExecutionSpeed;  // before Windows 11 the timer flag is unknown
            setInfo(::GetCurrentProcess(), kProcessPowerThrottling, &state, sizeof(state));
        }
    }
}

class WindowsTimerResolutionGuard {
public:
    WindowsTimerResolutionGuard() {
        RaiseWindowsProcessPriority();
        const MMRESULT result = ::timeBeginPeriod(1);
        if (result == TIMERR_NOERROR) {
            armed_ = true;
        } else {
            RT_LOG(RT_TAG_RUNTIME) << "timeBeginPeriod(1) failed: " << result << std::endl;
        }
    }

    ~WindowsTimerResolutionGuard() {
        if (armed_) {
            ::timeEndPeriod(1);
        }
    }

    WindowsTimerResolutionGuard(const WindowsTimerResolutionGuard&) = delete;
    WindowsTimerResolutionGuard& operator=(const WindowsTimerResolutionGuard&) = delete;

private:
    bool armed_ = false;
};
#endif
} // namespace

namespace {

std::string g_lastEntryLabel;
std::atomic_bool g_auroraInitialized{false};
std::atomic_flag g_abortSignalHandled = ATOMIC_FLAG_INIT;
std::atomic_bool g_fatalErrorReported{false};
std::atomic_bool g_fatalPopupShown{false};
std::atomic<int> g_lastExitCode{0};
std::atomic_bool g_exitCodeSet{false};

struct ProcessTranscriptState {
    bool enabled = false;
    std::filesystem::path path;
    std::ofstream file;
    std::mutex fileMutex;
    std::atomic_bool initialized{false};
    int savedStdoutFd = -1;
    int savedStderrFd = -1;
    int stdoutPipeReadFd = -1;
    int stdoutPipeWriteFd = -1;
    int stderrPipeReadFd = -1;
    int stderrPipeWriteFd = -1;
    std::thread stdoutThread;
    std::thread stderrThread;
};

std::filesystem::path GetDefaultRuntimeLogDirectory() {
    return RuntimeConfigFile::ApplicationDataDirectory() / "Logs";
}

// Every entry in the Logs root - both the per-run folders written by this
// scheme and any flat .log files left over from the previous one - is removed
// once it is older than the retention window.
void PruneOldRunLogs(const std::filesystem::path& logRoot) {
    constexpr auto kRetention = std::chrono::hours(24 * 4);

    std::error_code ec;
    const auto now = std::filesystem::file_time_type::clock::now();
    for (const auto& entry : std::filesystem::directory_iterator(logRoot, ec)) {
        std::error_code entryEc;
        const auto writeTime = std::filesystem::last_write_time(entry.path(), entryEc);
        if (entryEc) {
            continue;
        }
        if (now - writeTime > kRetention) {
            std::filesystem::remove_all(entry.path(), entryEc);
        }
    }
}

// Logs/<product>_<epochSeconds>_pid<pid>/ - one folder per run. The console
// transcript and every crash artifact for the run land in here, so "zip this
// folder" is a complete diagnostic. Created lazily so even a crash before
// transcript setup still has somewhere to write.
const std::filesystem::path& GetRunLogDirectory() {
    static const std::filesystem::path runDirectory = [] {
        const std::filesystem::path logRoot = GetDefaultRuntimeLogDirectory();

        std::error_code ec;
        std::filesystem::create_directories(logRoot, ec);
        PruneOldRunLogs(logRoot);

#if defined(_WIN32)
        const unsigned long pid = ::GetCurrentProcessId();
#else
        const auto pid = static_cast<unsigned long>(::getpid());
#endif
        const auto now = std::chrono::system_clock::now();
        const auto secs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

        std::ostringstream name;
        name << (RuntimeProduct::IsRetroRewind() ? "retro_rewind" : "base")
             << "_" << secs << "_pid" << pid;
        const std::filesystem::path directory = logRoot / name.str();
        std::filesystem::create_directories(directory, ec);
        return directory;
    }();
    return runDirectory;
}

ProcessTranscriptState& GetProcessTranscriptState() {
    static ProcessTranscriptState state;
    return state;
}

void WriteProcessTranscriptChunk(ProcessTranscriptState& state, const char* data, size_t size) {
    if (!state.enabled || !state.file || size == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(state.fileMutex);
    state.file.write(data, static_cast<std::streamsize>(size));
    state.file.flush();
}

void PumpTranscriptPipe(ProcessTranscriptState& state, int readFd, int mirrorFd) {
    std::array<char, 4096> buffer{};
    for (;;) {
#if defined(_WIN32)
        const int bytesRead = _read(readFd, buffer.data(), static_cast<unsigned int>(buffer.size()));
#else
        const ssize_t bytesRead = ::read(readFd, buffer.data(), buffer.size());
#endif
        if (bytesRead <= 0) {
            break;
        }

        if (mirrorFd >= 0) {
            size_t offset = 0;
            while (offset < static_cast<size_t>(bytesRead)) {
#if defined(_WIN32)
                const int written = _write(mirrorFd,
                                           buffer.data() + offset,
                                           static_cast<unsigned int>(static_cast<size_t>(bytesRead) - offset));
#else
                const ssize_t written = ::write(mirrorFd,
                                                buffer.data() + offset,
                                                static_cast<size_t>(bytesRead) - offset);
#endif
                if (written <= 0) {
                    break;
                }
                offset += static_cast<size_t>(written);
            }
        }

        WriteProcessTranscriptChunk(state, buffer.data(), static_cast<size_t>(bytesRead));
    }
}

#if defined(_WIN32)
int GetFileDescriptor(FILE* file) {
    return _fileno(file);
}

int DuplicateFileDescriptor(int fd) {
    return _dup(fd);
}

int DuplicateFileDescriptorTo(int sourceFd, int targetFd) {
    return _dup2(sourceFd, targetFd);
}

void CloseFileDescriptor(int fd) {
    _close(fd);
}
#else
int GetFileDescriptor(FILE* file) {
    return fileno(file);
}

int DuplicateFileDescriptor(int fd) {
    return ::dup(fd);
}

int DuplicateFileDescriptorTo(int sourceFd, int targetFd) {
    return ::dup2(sourceFd, targetFd);
}

void CloseFileDescriptor(int fd) {
    ::close(fd);
}
#endif

bool InstallTranscriptPipe(int& outReadFd, int& outWriteFd, int targetFd) {
#if defined(_WIN32)
    int pipeFds[2]{-1, -1};
    if (_pipe(pipeFds, 8192, _O_BINARY) != 0) {
        return false;
    }
#else
    int pipeFds[2]{-1, -1};
    if (::pipe(pipeFds) != 0) {
        return false;
    }
#endif

    outReadFd = pipeFds[0];
    outWriteFd = pipeFds[1];
    if (targetFd == GetFileDescriptor(stdout)) {
        std::fflush(stdout);
    } else if (targetFd == GetFileDescriptor(stderr)) {
        std::fflush(stderr);
    }

    if (DuplicateFileDescriptorTo(outWriteFd, targetFd) < 0) {
        CloseFileDescriptor(outReadFd);
        CloseFileDescriptor(outWriteFd);
        outReadFd = -1;
        outWriteFd = -1;
        return false;
    }
    return true;
}

#if defined(_WIN32)
void AttachParentConsoleForDiagnostics() {
    // GUI-subsystem products get no console and no bound stdout/stderr unless a parent already
    // redirected them (pipe/file: leave alone) or has a console to attach to (bind only the
    // streams still unbound). With no parent console, fall back to NUL rather than leaving
    // _fileno(stdout) == -2, which would stop InitializeProcessTranscript from redirecting into
    // the log file at all.
    const HANDLE outHandle = ::GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE errHandle = ::GetStdHandle(STD_ERROR_HANDLE);
    const bool haveOut = outHandle != nullptr && outHandle != INVALID_HANDLE_VALUE;
    const bool haveErr = errHandle != nullptr && errHandle != INVALID_HANDLE_VALUE;
    if (haveOut && haveErr) {
        return;
    }

    const bool attached =
        ::GetConsoleWindow() != nullptr || ::AttachConsole(ATTACH_PARENT_PROCESS) != 0;
    const char* const sink = attached ? "CONOUT$" : "NUL";
    if (!haveOut) {
        (void)std::freopen(sink, "w", stdout);
    }
    if (!haveErr) {
        (void)std::freopen(sink, "w", stderr);
    }
    std::cout.clear();
    std::cerr.clear();
}
#else
void AttachParentConsoleForDiagnostics() {}
#endif

// The setup writes build-fingerprint.json beside every product executable; its SetupVersion is
// the only version identity the runtime has (products are compiled locally, so nothing is baked
// into the binary). Surface it at the top of the transcript so every attached log self-identifies.
std::string ReadInstalledSetupVersion() {
    const auto directory = RuntimeConfigFile::ExecutableDirectory();
    if (!directory) {
        return {};
    }
    std::ifstream file(*directory / "build-fingerprint.json", std::ios::binary);
    if (!file) {
        return {};
    }
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    constexpr std::string_view kKey = "\"SetupVersion\"";
    const auto keyPos = text.find(kKey);
    if (keyPos == std::string::npos) {
        return {};
    }
    const auto colon = text.find(':', keyPos + kKey.size());
    const auto open = colon == std::string::npos ? std::string::npos : text.find('"', colon + 1);
    const auto close = open == std::string::npos ? std::string::npos : text.find('"', open + 1);
    if (close == std::string::npos) {
        return {};
    }
    return text.substr(open + 1, close - open - 1);
}

void InitializeProcessTranscript(int argc, char** argv) {
    auto& state = GetProcessTranscriptState();
    if (state.initialized.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    const std::filesystem::path path = GetRunLogDirectory() / "console.log";
    state.file.open(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!state.file) {
        return;
    }

    state.enabled = true;
    state.path = path;

#if defined(_WIN32)
    const unsigned long pid = ::GetCurrentProcessId();
#else
    const auto pid = static_cast<unsigned long>(::getpid());
#endif

    const std::string setupVersion = ReadInstalledSetupVersion();

    {
        std::lock_guard<std::mutex> lock(state.fileMutex);
        state.file << "[runtime] NSMBW "
                   << (setupVersion.empty() ? "version unknown" : setupVersion) << "\n";
        state.file << "[runtime] process transcript started\n";
        state.file << "[runtime] pid=" << pid << "\n";
        state.file << "[runtime] argv=";
        for (int i = 0; i < argc; ++i) {
            if (i != 0) {
                state.file << ' ';
            }
            state.file << argv[i];
        }
        state.file << "\n";
        state.file.flush();
    }

    state.savedStdoutFd = DuplicateFileDescriptor(GetFileDescriptor(stdout));
    state.savedStderrFd = DuplicateFileDescriptor(GetFileDescriptor(stderr));

    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    auto restoreFailedSetup = [&state]() {
        if (state.savedStdoutFd >= 0) {
            DuplicateFileDescriptorTo(state.savedStdoutFd, GetFileDescriptor(stdout));
        }
        if (state.savedStderrFd >= 0) {
            DuplicateFileDescriptorTo(state.savedStderrFd, GetFileDescriptor(stderr));
        }
        if (state.stdoutPipeReadFd >= 0) {
            CloseFileDescriptor(state.stdoutPipeReadFd);
            state.stdoutPipeReadFd = -1;
        }
        if (state.stdoutPipeWriteFd >= 0) {
            CloseFileDescriptor(state.stdoutPipeWriteFd);
            state.stdoutPipeWriteFd = -1;
        }
        if (state.stderrPipeReadFd >= 0) {
            CloseFileDescriptor(state.stderrPipeReadFd);
            state.stderrPipeReadFd = -1;
        }
        if (state.stderrPipeWriteFd >= 0) {
            CloseFileDescriptor(state.stderrPipeWriteFd);
            state.stderrPipeWriteFd = -1;
        }
        if (state.savedStdoutFd >= 0) {
            CloseFileDescriptor(state.savedStdoutFd);
            state.savedStdoutFd = -1;
        }
        if (state.savedStderrFd >= 0) {
            CloseFileDescriptor(state.savedStderrFd);
            state.savedStderrFd = -1;
        }
        state.enabled = false;
        state.file.close();
    };

#if defined(MKW_PLATFORM_SWITCH)
    // Horizon has no pipe(): capture stdout/stderr straight to sibling files instead.
    const std::string stdoutPath = (path.parent_path() / "stdout.log").string();
    const std::string stderrPath = (path.parent_path() / "stderr.log").string();
    if (std::freopen(stdoutPath.c_str(), "w", stdout) == nullptr ||
        std::freopen(stderrPath.c_str(), "w", stderr) == nullptr) {
        restoreFailedSetup();
        return;
    }
    // Unbuffered SD writes block the calling thread for tens of milliseconds, and both the OSReport
    // lines and the stats lines land on gameplay threads. Buffer both streams and flush them from
    // a background thread; a hard abort can lose at most the last quarter second of the log.
    std::setvbuf(stdout, nullptr, _IOFBF, 256 * 1024);
    std::setvbuf(stderr, nullptr, _IOFBF, 256 * 1024);
    static std::thread stderrFlusher([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            std::fflush(stdout);
            std::fflush(stderr);
        }
    });
    stderrFlusher.detach();
#else
    if (!InstallTranscriptPipe(state.stdoutPipeReadFd, state.stdoutPipeWriteFd, GetFileDescriptor(stdout)) ||
        !InstallTranscriptPipe(state.stderrPipeReadFd, state.stderrPipeWriteFd, GetFileDescriptor(stderr))) {
        restoreFailedSetup();
        return;
    }
#endif

    state.stdoutThread = std::thread([&state]() {
        PumpTranscriptPipe(state, state.stdoutPipeReadFd, state.savedStdoutFd);
    });
    state.stderrThread = std::thread([&state]() {
        PumpTranscriptPipe(state, state.stderrPipeReadFd, state.savedStderrFd);
    });
}

void ShutdownProcessTranscript() {
    auto& state = GetProcessTranscriptState();
    if (!state.enabled) {
        return;
    }

    std::fflush(stdout);
    std::fflush(stderr);
    std::cout.flush();
    std::cerr.flush();

    if (state.savedStdoutFd >= 0) {
        DuplicateFileDescriptorTo(state.savedStdoutFd, GetFileDescriptor(stdout));
    }
    if (state.savedStderrFd >= 0) {
        DuplicateFileDescriptorTo(state.savedStderrFd, GetFileDescriptor(stderr));
    }

    if (state.stdoutPipeWriteFd >= 0) {
        CloseFileDescriptor(state.stdoutPipeWriteFd);
        state.stdoutPipeWriteFd = -1;
    }
    if (state.stderrPipeWriteFd >= 0) {
        CloseFileDescriptor(state.stderrPipeWriteFd);
        state.stderrPipeWriteFd = -1;
    }

    if (state.stdoutThread.joinable()) {
        state.stdoutThread.join();
    }
    if (state.stderrThread.joinable()) {
        state.stderrThread.join();
    }

    if (state.stdoutPipeReadFd >= 0) {
        CloseFileDescriptor(state.stdoutPipeReadFd);
        state.stdoutPipeReadFd = -1;
    }
    if (state.stderrPipeReadFd >= 0) {
        CloseFileDescriptor(state.stderrPipeReadFd);
        state.stderrPipeReadFd = -1;
    }
    if (state.savedStdoutFd >= 0) {
        CloseFileDescriptor(state.savedStdoutFd);
        state.savedStdoutFd = -1;
    }
    if (state.savedStderrFd >= 0) {
        CloseFileDescriptor(state.savedStderrFd);
        state.savedStderrFd = -1;
    }

    {
        std::lock_guard<std::mutex> lock(state.fileMutex);
        state.file << "\n[runtime] process transcript ended\n";
        state.file.flush();
    }
    state.file.close();
    state.enabled = false;
}

// The one host stack walker. Every caller - the CRT report hook, the fatal log
// and the stderr crash dump - goes through this, so the log and the console see
// exactly the same frames, including the TranslatedFunctionRegistry fallback for
// addresses DbgHelp cannot name.
std::string FormatHostStackTrace(unsigned framesToSkip) {
#if defined(_WIN32)
    static std::atomic_bool s_symbolsReady{false};
    HANDLE process = GetCurrentProcess();
    if (!s_symbolsReady.load(std::memory_order_acquire)) {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
        if (SymInitialize(process, nullptr, TRUE)) {
            s_symbolsReady.store(true, std::memory_order_release);
        }
    }
    const bool symbolsReady = s_symbolsReady.load(std::memory_order_acquire);

    void* frames[64]{};
    const USHORT captured = CaptureStackBackTrace(static_cast<DWORD>(framesToSkip),
                                                  static_cast<DWORD>(std::size(frames)), frames, nullptr);
    std::ostringstream out;
    if (captured == 0) {
        out << "[runtime] host stack trace unavailable (CaptureStackBackTrace returned 0)\n";
        return out.str();
    }
    out << "[runtime] host stack trace (most recent call first):\n";
    for (USHORT i = 0; i < captured; ++i) {
        const DWORD64 addr = reinterpret_cast<DWORD64>(frames[i]);
        HMODULE module = nullptr;
        std::string modulePath = "?";
        DWORD64 moduleBase = 0;
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               reinterpret_cast<LPCWSTR>(frames[i]),
                               &module) != 0 &&
            module != nullptr) {
            moduleBase = reinterpret_cast<DWORD64>(module);
            std::wstring modulePathBuffer(MAX_PATH, L'\0');
            for (;;) {
                const DWORD length =
                    GetModuleFileNameW(module, modulePathBuffer.data(), static_cast<DWORD>(modulePathBuffer.size()));
                if (length == 0) {
                    break;
                }
                if (length < modulePathBuffer.size()) {
                    modulePathBuffer.resize(length);
                    modulePath = RuntimeConfigFile::PathToUtf8(std::filesystem::path(modulePathBuffer));
                    break;
                }
                // Truncated; retry with a larger buffer up to the extended path limit.
                if (modulePathBuffer.size() >= 32768) {
                    break;
                }
                modulePathBuffer.resize(modulePathBuffer.size() * 2);
            }
        }

        const char* symbolName = "?";
        std::string translatedFuncName;
        uint32_t ppcAddress = 0;
        DWORD64 symbolDisp = 0;
        std::array<char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> symbolBuffer{};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer.data());
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        if (symbolsReady && SymFromAddr(process, addr, &symbolDisp, symbol)) {
            symbolName = symbol->Name;
        } else if (auto info = TranslatedFunctionRegistry::FindByHostAddress(static_cast<uintptr_t>(addr))) {
            // DbgHelp could not name it; the translated-function registry can.
            translatedFuncName = info->name;
            ppcAddress = info->address;
            if (!translatedFuncName.empty()) {
                symbolName = translatedFuncName.c_str();
                symbolDisp = addr - reinterpret_cast<DWORD64>(info->entryPoint);
            }
        }

        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisp = 0;
        const char* fileName = nullptr;
        DWORD lineNumber = 0;
        if (symbolsReady && SymGetLineFromAddr64(process, addr, &lineDisp, &line)) {
            fileName = line.FileName;
            lineNumber = line.LineNumber;
        }

        out << "  ["
            << std::setw(2) << std::setfill('0') << static_cast<unsigned>(i)
            << std::setfill(' ') << "] "
            << modulePath << "!" << symbolName
            << " + 0x" << std::hex << std::uppercase << symbolDisp
            << " (0x" << addr;
        if (moduleBase != 0) {
            out << ", module+0x" << (addr - moduleBase);
        }
        if (ppcAddress != 0) {
            out << ", PPC:0x" << std::setw(8) << std::setfill('0') << ppcAddress << std::setfill(' ');
        }
        out << std::dec << std::nouppercase;
        if (fileName) {
            out << ", " << fileName << ":" << lineNumber;
        }
        out << ")\n";
    }
    return out.str();
#elif defined(__aarch64__)
    // aarch64 frame chain: [x29] = caller frame, [x29 + 8] = return address.
    (void)framesToSkip;
    std::ostringstream out;
    out << "[runtime] host stack trace (most recent call first):\n";
    const auto anchor = reinterpret_cast<std::uintptr_t>(&FormatHostStackTrace);
    out << "[runtime] host module anchor=0x" << std::hex << anchor << std::dec << "\n";
    const auto sp = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
    std::uintptr_t fp = sp;
    for (unsigned i = 0; i < 32; ++i) {
        if (fp == 0 || (fp & 0xF) != 0 || fp + 0x10 < sp || fp > sp + 0x200000) {
            break;
        }
        const auto* words = reinterpret_cast<const std::uintptr_t*>(fp);
        const std::uintptr_t next = words[0];
        const std::uintptr_t lr = words[1];
        out << "  [" << std::setw(2) << std::setfill('0') << i << std::setfill(' ') << "] 0x"
            << std::hex << lr << std::dec << "\n";
        if (next <= fp) {
            break;
        }
        fp = next;
    }
    return out.str();
#else
    (void)framesToSkip;
    return {};
#endif
}

void WriteFatalLogImpl(std::string_view reason, std::string_view extraDetails = {},
                       const uint32_t* missingGuestTarget = nullptr) {
    const std::filesystem::path runDirectory = GetRunLogDirectory();
    std::string fileName = "crash_";
    fileName.append(reason);
    fileName.append(".txt");
    std::ofstream out(runDirectory / fileName, std::ios::out | std::ios::trunc);
    if (!out) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const auto nowSecs = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

    out << "[runtime] fatal log" << std::endl;
    out << "[runtime] reason: " << reason << std::endl;
    out << "[runtime] timestamp(seconds): " << nowSecs << std::endl;
    out << "[runtime] entry: " << (g_lastEntryLabel.empty() ? "<unknown>" : g_lastEntryLabel) << std::endl;
    if (!extraDetails.empty()) {
        out << "[runtime] details: " << extraDetails << std::endl;
    }
    if (missingGuestTarget) {
        out << "[runtime] guest jump target: 0x" << std::hex << std::uppercase
            << *missingGuestTarget << std::dec << std::endl;
    }

    const CpuContext* cpu = TryGetCpuContext();
    if (!cpu) {
        // Exceptions can unwind CpuContextScope before we get here.
        // Fall back to the persistent CPU context snapshot so fatal logs still include registers.
        cpu = &GetPersistentCpuContext();
    }
    if (cpu) {
        SystemBridge::DumpCrashHeuristics(out, cpu, missingGuestTarget);
        SystemBridge::DumpCpuState(out, cpu);
    } else {
        out << "[runtime] CPU context unavailable." << std::endl;
    }

    // Guest memory snapshots so the heap/object state can be walked offline.
    // Written once per process: several fatal paths can fire in sequence
    // (e.g. an exception followed by the exit-code report) and the snapshots
    // are large.
    static std::atomic_bool s_memorySnapshotWritten{false};
    if (!s_memorySnapshotWritten.exchange(true, std::memory_order_acq_rel)) {
        SystemBridge::WriteGuestMemorySnapshot(out, runDirectory / "mem1.bin");
    }

    out.flush();
    RT_LOG(RT_TAG_RUNTIME) << "crash artifacts written to "
                           << RuntimeConfigFile::PathToUtf8(runDirectory) << std::endl;
}

void SetRuntimeExitCodeImpl(int code) {
    g_lastExitCode.store(code, std::memory_order_relaxed);
    g_exitCodeSet.store(true, std::memory_order_relaxed);
}

} // namespace

// This run's log folder, for other runtime files that write next to console.log (guest_profiler.cpp).
std::filesystem::path RuntimeRunLogDirectory() {
    return GetRunLogDirectory();
}

namespace {

void DumpHostStackTrace() {
#if defined(_WIN32) || defined(__aarch64__)
    static std::atomic_flag s_inProgress = ATOMIC_FLAG_INIT;
    if (s_inProgress.test_and_set()) {
        return;
    }
    const std::string trace = FormatHostStackTrace(1);
    std::fputs(trace.c_str(), stderr);
    std::fflush(stderr);
    s_inProgress.clear();
#else
    RT_LOGF(RT_TAG_RUNTIME, "Host stack trace unavailable on this platform\n");
    std::fflush(stderr);
#endif
}

} // namespace

extern "C" void DumpHostStackTraceForRuntimeHelper() {
    DumpHostStackTrace();
}

void MarkFatalErrorReported() {
    g_fatalErrorReported.store(true, std::memory_order_release);
}

#if defined(MKW_PLATFORM_SWITCH)
// Launching the error applet needs a few tens of KiB of this process's heap for the applet holder
// and its transfer storage, which is exactly what an out-of-memory crash cannot spare. One block is
// held from startup and handed back the first time a fatal path has to report something; after that
// it is gone, and the release is idempotent because terminate can run on any thread.
constexpr size_t kFatalReserveBytes = 1u << 20;
void* g_fatalReserve = nullptr;
std::atomic<bool> g_fatalReserveReleased{false};

void ReserveFatalMemory() noexcept {
    if (g_fatalReserve == nullptr) {
        g_fatalReserve = std::malloc(kFatalReserveBytes);
    }
}

void ReleaseFatalReserve() noexcept {
    if (g_fatalReserveReleased.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    std::free(g_fatalReserve);
    g_fatalReserve = nullptr;
}
#endif

// Puts `text` in front of the player: a message box on Windows, a dialog on a Linux desktop, and the
// system error applet on the Switch. Callers compose the whole text; the applet's details page is
// never used, so nothing is hidden behind a button. On Switch this returns once the player closes
// the applet, so a caller can act on that (the folder repair does).
static void ShowAppletText(std::string_view text) noexcept {
    try {
        constexpr size_t kMaxAppletText = 0x7FF;
#if defined(_WIN32)
        const std::string message(text);
        ::MessageBoxA(nullptr, message.c_str(), "NSMBW - Fatal Error",
                      MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TASKMODAL);
#else
#if defined(__linux__) && !defined(__ANDROID__)
        const std::string message(text);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "NSMBW - Fatal Error", message.c_str(), nullptr);
#elif defined(MKW_PLATFORM_SWITCH)
        // The Switch has no SDL message-box backend, so fatal errors would otherwise be invisible:
        // show the system error applet with the whole text in its dialog and no details page. The
        // buffer keeps this path free of heap use.
        char dialog[kMaxAppletText + 1];
        const size_t length = std::min(text.size(), kMaxAppletText);
        std::memcpy(dialog, text.data(), length);
        dialog[length] = '\0';
        ErrorSystemConfig config{};
        if (R_SUCCEEDED(errorSystemCreate(&config, dialog, nullptr))) {
            errorSystemShow(&config);
        }
#endif
#endif
    } catch (...) {
        // Reporting a crash must never throw or mask the original failure.
    }
}

static void ShowFatalText(std::string_view text) noexcept {
    RT_LOGF(RT_TAG_RUNTIME, "fatal dialog: %.*s\n",
            static_cast<int>(std::min<size_t>(text.size(), 0x7FF)), text.data());
    ShowAppletText(text);
}

void ShowRuntimeFatalPopup(std::string_view category, std::string_view details) noexcept {
    if (g_fatalPopupShown.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
#if defined(MKW_PLATFORM_SWITCH)
    // The message string below and the applet both need heap; a fatal error is often the moment
    // there is none left.
    ReleaseFatalReserve();
#endif

    try {
        std::string message;
        message.reserve(category.size() + details.size() + 220);
        message.append("The game stopped because ");
        message.append(category.empty() ? "a fatal error occurred." : category);
        message.append(".\n\n");
        if (details.empty()) {
            message.append("No additional details were available.");
        } else {
            constexpr size_t kMaxPopupDetails = 4096;
            message.append(details.data(), std::min(details.size(), kMaxPopupDetails));
            if (details.size() > kMaxPopupDetails) {
                message.append("\n\n[Additional details were written to the crash log.]");
            }
        }
        message.append("\n\nSee the Logs folder for the full diagnostic.");
        ShowFatalText(message);
    } catch (...) {
        // Reporting a crash must never throw or mask the original failure.
    }
}

void EndProcessTranscript() {
    ShutdownProcessTranscript();
}

namespace RuntimeCrash {

void WriteCrashArtifacts(std::string_view reason, std::string_view extraDetails,
                         const uint32_t* missingGuestTarget) noexcept {
    try {
        WriteFatalLogImpl(reason, extraDetails, missingGuestTarget);
    } catch (...) {
        // Crash reporting must never mask the original failure.
    }
}

[[noreturn]] void FatalMissingGuestTarget(uint32_t target, CpuContext* cpu) noexcept {
    RT_LOG(RT_TAG_RUNTIME) << "InvokeIndirectCpu: target 0x" << std::hex << target
              << " not translated (missing function)" << std::dec << std::endl;
    RT_LOG(RT_TAG_RUNTIME) << "Caller LR = 0x" << std::hex << (cpu ? cpu->lr : 0u)
              << std::dec << std::endl;
    try {
        SystemBridge::DumpCrashHeuristics(std::cerr, cpu, &target);
        SystemBridge::DumpCpuState(cpu);
    } catch (...) {
    }
    std::fflush(stderr);

    std::ostringstream message;
    message << "The game stopped because it tried to execute guest address 0x"
            << std::hex << target
            << ", but that function was not translated or registered.\n\n"
            << "Caller LR: 0x" << (cpu ? cpu->lr : 0u);
    if (target == 0) {
        message << "\n\nA jump to address 0 usually means a virtual call through a bad "
                   "object pointer; the crash log heuristics have details.";
    }
    WriteCrashArtifacts("missing_target", message.str(), &target);
    ShowRuntimeFatalPopup("Missing translated function", message.str());
    MarkFatalErrorReported();
    std::exit(EXIT_FAILURE);
}

} // namespace RuntimeCrash


namespace {

#if defined(__linux__) && !defined(__ANDROID__)
// What a Linux PC is missing when the renderer can't start, in words a player can act on.
constexpr const char* kNoDisplayHint =
    "No desktop display was found (X11 or Wayland). Start the game from your desktop, not from a "
    "text console or over SSH.\n\n"
    "On a minimal install, add the display libraries:\n"
    "  Fedora: sudo dnf install libX11 libXcursor libXext libXi libXrandr libwayland-client "
    "libwayland-cursor libxkbcommon libdecor\n"
    "  Ubuntu, Debian: sudo apt install libx11-6 libxcursor1 libxext6 libxi6 libxrandr2 "
    "libwayland-client0 libwayland-cursor0 libxkbcommon0 libdecor-0-0";
constexpr const char* kNoVulkanHint =
    "No Vulkan driver was found. The game draws with Vulkan, so install your graphics card's "
    "Vulkan driver:\n"
    "  Fedora: sudo dnf install mesa-vulkan-drivers vulkan-loader\n"
    "  Ubuntu, Debian: sudo apt install mesa-vulkan-drivers libvulkan1\n"
    "  NVIDIA: the NVIDIA driver includes it (on Fedora, RPM Fusion's akmod-nvidia).";
std::atomic<const char*> g_auroraLinuxHint{nullptr};
#endif

void RuntimeAuroraLogCallback(AuroraLogLevel level, const char* module,
                              const char* message, unsigned int len) {
    const std::string_view moduleView = module != nullptr ? std::string_view(module) : std::string_view{};
    const std::string_view messageView = message != nullptr ? std::string_view(message, len) : std::string_view{};
#if defined(MKW_PLATFORM_SWITCH)
    // stderr is fully buffered on Switch and flushed by a background thread. std::cerr is unit
    // buffered, so a line from the presenter or frame worker would write straight to the SD card
    // and stall the guest's display-copy wait if the filesystem hesitates. Keep it buffered.
    std::fprintf(stderr, "[aurora] [%d] [%.*s] %.*s\n", static_cast<int>(level),
                 static_cast<int>(moduleView.size()), moduleView.empty() ? "" : moduleView.data(),
                 static_cast<int>(messageView.size()), messageView.data());
    if (level == LOG_FATAL) {
        std::fflush(stderr);
    }
#else
    std::cerr << "[aurora] [" << static_cast<int>(level) << "] [" << moduleView << "] "
              << messageView << std::endl;
#endif
#if defined(__linux__) && !defined(__ANDROID__)
    if (messageView.find("No available video device") != std::string_view::npos) {
        g_auroraLinuxHint.store(kNoDisplayHint);
    } else if (messageView.find("VK_KHR_surface") != std::string_view::npos ||
               messageView.find("Null backend") != std::string_view::npos) {
        g_auroraLinuxHint.store(kNoVulkanHint);
    }
    if (level == LOG_FATAL) {
        if (const char* hint = g_auroraLinuxHint.load()) {
            ShowRuntimeFatalPopup("the graphics could not start", hint);
            return;
        }
    }
#endif
    if (level == LOG_FATAL) {
        ShowRuntimeFatalPopup("Aurora reported a fatal renderer error", messageView);
    }
}

const TranslatedFunctionInfo* ResolveEntry() {
    const auto* entry = TranslatedFunctionRegistry::FindByAddressPtr(kDefaultEntryAddress);
    if (!entry) {
        std::ostringstream oss;
        oss << "No translated function registered at address 0x" << std::hex << kDefaultEntryAddress;
        throw std::runtime_error(oss.str());
    }
    return entry;
}

void SeedCpuContext(CpuContext& cpu) {
    cpu.gpr[1] = 0x81700000u;
}

void DumpAccessViolationReport(const Memory::AccessViolation& ex,
                               std::string_view entryLabel) {
    const uint32_t address = ex.address();
    const size_t length = ex.length();
    const CpuContext* cpu = TryGetCpuContext();

    RT_LOG(RT_TAG_RUNTIME) << "===== Memory Access Violation =====" << std::endl;
    RT_LOG(RT_TAG_RUNTIME) << "Reason : " << ex.reason() << std::endl;
    std::cerr << std::hex << std::uppercase;
    RT_LOG(RT_TAG_RUNTIME) << "Address: 0x" << std::setw(8) << std::setfill('0') << address
              << " (+0x" << length << ")" << std::dec << std::setfill(' ') << std::endl;
    RT_LOG(RT_TAG_RUNTIME) << "Entry  : " << (entryLabel.empty() ? "(unknown)" : std::string(entryLabel)) << std::endl;
    RT_LOG(RT_TAG_RUNTIME) << "Mode   : strict (trap on unmapped)" << std::endl;
    RT_LOG(RT_TAG_RUNTIME) << "r1 seed: 0x81700000" << std::endl;
    if (cpu) {
        RT_LOG(RT_TAG_RUNTIME) << "CurrentCpuContext: " << cpu << "  r1=0x"
                  << std::hex << std::uppercase << cpu->gpr[1] << std::dec << std::nouppercase << std::endl;
        RT_LOG(RT_TAG_RUNTIME) << "Last recorded PC : 0x" << std::hex << std::uppercase << cpu->pc
                  << std::dec << std::nouppercase << std::endl;
    } else {
        RT_LOG(RT_TAG_RUNTIME) << "CurrentCpuContext: (null)" << std::endl;
    }

    SystemBridge::DumpCpuState(cpu);

    const auto regions = Memory::DescribeRegions();
    if (regions.empty()) {
        RT_LOG(RT_TAG_RUNTIME) << "Memory not initialized; no regions mapped." << std::endl;
        return;
    }

    RT_LOG(RT_TAG_RUNTIME) << "Mapped regions:" << std::endl;
    uint64_t bestDistance = std::numeric_limits<uint64_t>::max();
    std::string bestRegion;
    bool insideRegion = false;

    for (const auto& region : regions) {
        const uint64_t base = region.baseAddress;
        const uint64_t end = base + region.sizeBytes;
        const bool contains = address >= base && address < end;
        if (contains) {
            insideRegion = true;
            bestDistance = 0;
            bestRegion = region.name;
        } else {
            const uint64_t distance = address < base ? base - address : address - end + 1;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestRegion = region.name;
            }
        }

        std::cerr << "  - " << region.name
                  << " 0x" << std::hex << std::setw(8) << std::setfill('0') << region.baseAddress
                  << " .. 0x" << std::setw(8) << (end - 1)
                  << std::dec << std::setfill(' ')
                  << " (" << region.sizeBytes / 1024 << " KiB";
        if (contains) {
            std::cerr << ", <-- access landed here";
        }
        std::cerr << ")" << std::endl;
    }

    if (!bestRegion.empty() && !insideRegion) {
        RT_LOG(RT_TAG_RUNTIME) << "Nearest region: " << bestRegion << " (" << bestDistance << " bytes away)" << std::endl;
    }

    RT_LOG(RT_TAG_RUNTIME) << "Verify that the installed game data and runtime build match." << std::endl;
}

#if defined(_WIN32)
PVOID g_vectoredSehHandle = nullptr;
constexpr DWORD kCppExceptionCodeGcc = 0x20474343; // "GCC" exception code
constexpr DWORD kCppExceptionCodeMsvc = 0xE06D7363;
// AddressSanitizer uses STATUS_FATAL_APP_EXIT when it detects an error and wants to report it.
// We must let ASan's handler run so it can print file/line information.
constexpr DWORD kAsanFatalAppExit = 0x40000015; // STATUS_FATAL_APP_EXIT
LONG ReportFatalSehAndExit(EXCEPTION_POINTERS* info);

void ReportStructuredException(EXCEPTION_POINTERS* info) {
    if (!info || !info->ExceptionRecord) {
        RT_LOG(RT_TAG_RUNTIME) << "Structured exception occurred, but no diagnostic info was captured." << std::endl;
        return;
    }

    const auto* record = info->ExceptionRecord;
    const auto code = record->ExceptionCode;
    std::cerr << std::hex << std::uppercase;
    RT_LOG(RT_TAG_RUNTIME) << "Structured exception 0x" << code;
    if (!g_lastEntryLabel.empty()) {
        std::cerr << " while executing " << g_lastEntryLabel;
    }
    std::cerr << std::dec << std::nouppercase << std::endl;

    const auto faultAddress = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
    std::cerr << std::hex << std::uppercase;
    RT_LOG(RT_TAG_RUNTIME) << "Fault address: 0x" << faultAddress << std::dec << std::nouppercase << std::endl;

    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        const auto accessType = record->ExceptionInformation[0];
        const auto accessed = record->ExceptionInformation[1];
        RT_LOG(RT_TAG_RUNTIME) << "Access type: " << (accessType ? "write" : "read")
                  << " at 0x" << std::hex << std::uppercase << accessed << std::dec << std::nouppercase << std::endl;

        // Guardrail: flag raw GX gather pipe touches (0xCC00_8xxx) which must be routed through HLE.
        // Direct stores to this MMIO region (e.g., translated stfs/stb -0x8000(r4) with r4=0xCC010000)
        // will fault on the host. Emit an explicit hint so we know to fix the translation/HLE path instead
        // of chasing generic access violations.
        constexpr uintptr_t kGxGatherLo = 0xCC008000;
        constexpr uintptr_t kGxGatherHi = 0xCC009000; // one page past the 0x100-byte gather range for clarity
        if (accessed >= kGxGatherLo && accessed < kGxGatherHi) {
            RT_LOG(RT_TAG_RUNTIME) << "HINT: guest attempted a direct GX gather pipe write (0xCC00_8xxx)." << std::endl;
            RT_LOG(RT_TAG_RUNTIME) << "      These must go through GX_HLE_FIFO_Write*; check the translated function for" << std::endl;
            RT_LOG(RT_TAG_RUNTIME) << "      literal stores to -0x8000(r4) after lis r4,0xCC01 and route them via HLE." << std::endl;
        }
    }

#if defined(_M_X64) || defined(__x86_64__)
    const auto rip = info->ContextRecord ? info->ContextRecord->Rip : 0;
    const auto rsp = info->ContextRecord ? info->ContextRecord->Rsp : 0;
    std::cerr << std::hex << std::uppercase;
    RT_LOG(RT_TAG_RUNTIME) << "RIP=0x" << rip << " RSP=0x" << rsp << std::dec << std::nouppercase << std::endl;
#elif defined(_M_IX86)
    const auto eip = info->ContextRecord ? info->ContextRecord->Eip : 0;
    const auto esp = info->ContextRecord ? info->ContextRecord->Esp : 0;
    std::cerr << std::hex << std::uppercase;
    RT_LOG(RT_TAG_RUNTIME) << "EIP=0x" << eip << " ESP=0x" << esp << std::dec << std::nouppercase << std::endl;
#endif

    // Always dump CPU state on crash.
    RT_LOG(RT_TAG_RUNTIME) << "===== DUMPING CPU STATE =====" << std::endl;
    SystemBridge::DumpCpuState(TryGetCpuContext());
    std::cerr.flush();

    RT_LOG(RT_TAG_RUNTIME) << "Enable /DEBUG builds or capture a dump for full stack details." << std::endl;
    std::cerr.flush();
}



LONG CALLBACK SehLogger(EXCEPTION_POINTERS* info) {
    // Guest-space faults are the flat memory interception mechanism (MMIO,
    // deferred EFB reads, the executable-write guard, unmapped pages). The
    // flat module registers its own handler first, but registration order is
    // not guaranteed once another VEH is installed later, so consult it here
    // too - resolving a fault twice is a no-op.
    if (info->ExceptionRecord != nullptr &&
        info->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        info->ExceptionRecord->NumberParameters >= 2 &&
        GuestFlat::HandleAccessViolation(
            reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]),
            info->ExceptionRecord->ExceptionInformation[0] != 0)) {
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    if (g_suppressSehReporting && g_sehJumpTarget) {
        g_sehLastExceptionCode = info->ExceptionRecord->ExceptionCode;
        g_sehLastExceptionAddress = reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress);
        g_sehLastAccessType = 0;
        g_sehLastAccessedAddress = 0;
        if (g_sehLastExceptionCode == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2) {
            g_sehLastAccessType = static_cast<uint32_t>(info->ExceptionRecord->ExceptionInformation[0]);
            g_sehLastAccessedAddress = static_cast<uintptr_t>(info->ExceptionRecord->ExceptionInformation[1]);
        }
        longjmp(*g_sehJumpTarget, 1);
    }
    if (g_suppressSehReporting) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // Let C++ exceptions propagate to std::terminate so we can log their what().
    if (info->ExceptionRecord->ExceptionCode == kCppExceptionCodeGcc ||
        info->ExceptionRecord->ExceptionCode == kCppExceptionCodeMsvc) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (info->ExceptionRecord->ExceptionCode == 0x40010006 || // DBG_PRINTEXCEPTION_C
        info->ExceptionRecord->ExceptionCode == 0x4001000A || // DBG_PRINTEXCEPTION_WIDE_C (OutputDebugStringW)
        info->ExceptionRecord->ExceptionCode == 0x406D1388 || // SetThreadName
        info->ExceptionRecord->ExceptionCode == kAsanFatalAppExit) { // ASan reporting - let it print first
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // Software-raised exceptions (customer bit set) are used for internal control flow by
    // system DLLs (e.g. msxml6 while mscms parses a display colour profile) and are caught
    // by their own frame handlers. Only hardware faults are fatal at first chance; anything
    // else that truly goes unhandled reaches UnhandledSehFilter.
    if ((info->ExceptionRecord->ExceptionCode & 0x20000000u) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // Likewise anything that isn't a processor fault: RPC raises its Win32 errors as exceptions and
    // handles them itself (the folder picker's shell raises RPC_S_SERVER_UNAVAILABLE, 0x6BA, on
    // some PCs). Faults are NTSTATUS errors of facility 0 (0xC000xxxx) or a misalignment.
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if ((code & 0xFFFF0000u) != 0xC0000000u && code != EXCEPTION_DATATYPE_MISALIGNMENT) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return ReportFatalSehAndExit(info);
}

LONG WINAPI UnhandledSehFilter(EXCEPTION_POINTERS* info) {
    if (info == nullptr || info->ExceptionRecord == nullptr) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code == kCppExceptionCodeGcc || code == kCppExceptionCodeMsvc || code == kAsanFatalAppExit) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return ReportFatalSehAndExit(info);
}

LONG ReportFatalSehAndExit(EXCEPTION_POINTERS* info) {
    // Guard against re-entrancy: if we crash while reporting, don't recurse
    static std::atomic_flag s_inCrashHandler = ATOMIC_FLAG_INIT;
    if (s_inCrashHandler.test_and_set()) {
        std::_Exit(EXIT_FAILURE);
    }

    // Report the structured exception with detailed information
    ReportStructuredException(info);
    const auto* record = info->ExceptionRecord;
    const DWORD code = record != nullptr ? record->ExceptionCode : 0;
    std::ostringstream popupDetails;
    popupDetails << "A native Windows exception (0x" << std::hex << std::uppercase << code << ") occurred";
    if (!g_lastEntryLabel.empty()) {
        popupDetails << " while executing " << g_lastEntryLabel;
    }
    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
        popupDetails << ".\n\nThe game attempted a "
                     << (record->ExceptionInformation[0] ? "write" : "read")
                     << " at host address 0x" << record->ExceptionInformation[1];
    }
    popupDetails << ".\n\nThe process transcript and crash log contain the full CPU and stack diagnostics.";
    ShowRuntimeFatalPopup("a native crash occurred", popupDetails.str());
    DumpHostStackTrace();

    WriteFatalLogImpl("seh");
    
    // CRITICAL: Explicitly flush all output to ensure visibility with PowerShell redirection
    std::cerr << '\n';
    RT_LOG(RT_TAG_RUNTIME) << "===== FLUSHING OUTPUT BEFORE EXIT =====" << std::endl;
    std::cerr.flush();
    std::cout.flush();
    std::fflush(stdout);
    std::fflush(stderr);
    
    std::_Exit(EXIT_FAILURE);
}

void InstallSehLogger() {
    if (!g_vectoredSehHandle) {
        g_vectoredSehHandle = AddVectoredExceptionHandler(1, SehLogger);
        SetUnhandledExceptionFilter(UnhandledSehFilter);
    }
}
#else
#if !defined(MKW_PLATFORM_SWITCH)
// Horizon has no sigaction/siginfo/ucontext, and checked-only access needs no
// fault interception.
// POSIX counterpart to SehLogger above. Unlike Windows' AddVectoredExceptionHandler, which lets
// GuestFlat and this module each install their own handler and defensively re-check each other,
// sigaction only allows one handler per signal - the second registration replaces the first
// instead of chaining. So this is the single SIGSEGV/SIGBUS handler for the whole process, and it
// owns checking GuestFlat's fault-interception logic first, exactly mirroring the order SehLogger
// already uses on Windows.
void ReportUnhandledSignalFault(int sig, void* faultAddress) {
    RT_LOG(RT_TAG_RUNTIME) << "Signal " << sig << " (fault address 0x" << std::hex
              << reinterpret_cast<uintptr_t>(faultAddress) << std::dec << ")";
    if (!g_lastEntryLabel.empty()) {
        std::cerr << " while executing " << g_lastEntryLabel;
    }
    std::cerr << std::endl;
    if (const CpuContext* cpu = TryGetCpuContext()) {
        RT_LOG(RT_TAG_RUNTIME) << "===== DUMPING CPU STATE =====" << std::endl;
        SystemBridge::DumpCpuState(cpu);
    }
    std::cerr.flush();
}

void PosixMemoryFaultHandler(int sig, siginfo_t* info, void* ucontextVoid) {
    void* faultAddress = info != nullptr ? info->si_addr : nullptr;
    bool isWrite = false;
#if defined(__x86_64__)
    // Standard glibc technique for a POSIX fastmem-style handler: bit 1 (0x2) of the hardware
    // error code x86 pushes on a page fault records whether it was a write.
    if (ucontextVoid != nullptr) {
        auto* uc = static_cast<ucontext_t*>(ucontextVoid);
#if defined(__APPLE__)
        isWrite = uc->uc_mcontext != nullptr && (uc->uc_mcontext->__es.__err & 0x2) != 0;
#else
        isWrite = (uc->uc_mcontext.gregs[REG_ERR] & 0x2) != 0;
#endif
    }
#endif

    // Guest-space faults are the flat memory interception mechanism (MMIO, deferred EFB reads,
    // the executable-write guard, unmapped pages). Resolving one here means resuming the
    // faulting instruction, which just returning from the handler does.
    if (faultAddress != nullptr && GuestFlat::HandleAccessViolation(faultAddress, isWrite)) {
        return;
    }

    if (g_suppressSehReporting && g_sehJumpTarget) {
        g_sehLastExceptionCode = static_cast<uint32_t>(sig);
        g_sehLastExceptionAddress = reinterpret_cast<uintptr_t>(faultAddress);
        g_sehLastAccessType = isWrite ? 1u : 0u;
        g_sehLastAccessedAddress = reinterpret_cast<uintptr_t>(faultAddress);
        siglongjmp(*g_sehJumpTarget, 1);
    }
    if (g_suppressSehReporting) {
        // Reporting suppressed but nobody armed a recovery jump: restore the default disposition
        // and re-raise so the process still terminates, instead of returning into the same fault.
        signal(sig, SIG_DFL);
        raise(sig);
        return;
    }

    // Guard against re-entrancy: if we crash while reporting, don't recurse.
    static std::atomic_flag s_inCrashHandler = ATOMIC_FLAG_INIT;
    if (s_inCrashHandler.test_and_set()) {
        std::_Exit(EXIT_FAILURE);
    }

    ReportUnhandledSignalFault(sig, faultAddress);
    std::ostringstream popupDetails;
    popupDetails << "A native signal (" << sig << ") occurred";
    if (!g_lastEntryLabel.empty()) {
        popupDetails << " while executing " << g_lastEntryLabel;
    }
    if (faultAddress != nullptr) {
        popupDetails << ".\n\nThe game attempted a " << (isWrite ? "write" : "read")
                     << " at host address 0x" << std::hex
                     << reinterpret_cast<uintptr_t>(faultAddress) << std::dec;
    }
    popupDetails << ".\n\nThe process transcript and crash log contain the full CPU and stack "
                    "diagnostics.";
    ShowRuntimeFatalPopup("a native crash occurred", popupDetails.str());
    DumpHostStackTrace();
    WriteFatalLogImpl(sig == SIGBUS ? "sigbus" : "sigsegv");

    std::cerr.flush();
    std::cout.flush();
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(EXIT_FAILURE);
}

void InstallPosixMemoryFaultHandler() {
    struct sigaction action {};
    action.sa_sigaction = PosixMemoryFaultHandler;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    sigaction(SIGSEGV, &action, nullptr);
    // A touch beyond a memfd-backed mapping's ftruncate()'d size raises SIGBUS rather than
    // SIGSEGV on Linux; region sizing should make this unreachable, but routing it to the same
    // handler costs nothing and avoids a silent gap if it ever isn't.
    sigaction(SIGBUS, &action, nullptr);
}
#endif
#endif

void AbortSignalHandler(int signum) {
    // Guard against re-entrancy if multiple aborts are raised in quick succession
    if (g_abortSignalHandled.test_and_set()) {
        std::_Exit(EXIT_FAILURE);
    }

    // abort() bypasses atexit, so the guest-memory fault summary has to be
    // emitted here too. It is idempotent, so a later AtExitHandler is a no-op.
    GuestFlat::LogFaultSummary();

    ShowRuntimeFatalPopup("a fatal internal error occurred",
                          "The process called abort while running the game or Aurora renderer.\n\n"
                          "This usually means an unimplemented function, failed renderer assertion, "
                          "or another unrecoverable runtime condition was reached.");

    // Skip detailed dump if already reported by another handler
    if (g_fatalErrorReported.load(std::memory_order_acquire)) {
        std::fflush(stderr);
        std::fflush(stdout);
        std::_Exit(EXIT_FAILURE);
    }

    WriteFatalLogImpl("sigabrt");

    std::fflush(stderr);
    std::fflush(stdout);
    std::_Exit(EXIT_FAILURE);
}

} // namespace

// Retained as the thin public wrapper over WriteFatalLogImpl (declared in
// system_bridge.h); it has no in-tree callers because every fatal path now goes
// through RuntimeCrash::WriteCrashArtifacts.
void WriteFatalLog(std::string_view reason) {
    WriteFatalLogImpl(reason);
}

void SetRuntimeExitCode(int code) {
    SetRuntimeExitCodeImpl(code);
}

// Global handler called via atexit() to flush buffers before any exit
static void AtExitHandler() {
    // End-of-run guest memory report. This runs before the fatal-report check
    // below because the counters describe the whole session and are just as
    // interesting after a crash as after a clean exit.
    GuestFlat::LogFaultSummary();

    // Skip if already reported by another handler
    if (g_fatalErrorReported.load(std::memory_order_acquire)) {
        return;
    }
    if (g_exitCodeSet.load(std::memory_order_relaxed) &&
        g_lastExitCode.load(std::memory_order_relaxed) != 0) {
        ShowRuntimeFatalPopup("the runtime exited with an error",
                              "The game stopped after reporting a fatal error. Check the process transcript and crash log for details.");
        WriteFatalLogImpl("exitcode");
    }

    std::cerr.flush();
    std::cout.flush();
    std::fflush(stdout);
    std::fflush(stderr);
}

// Global terminate handler for uncaught exceptions
static void TerminateHandler() {
    // Skip detailed dump if already reported
    if (g_fatalErrorReported.load(std::memory_order_acquire)) {
        std::fflush(stderr);
        std::_Exit(EXIT_FAILURE);
    }
#if defined(MKW_PLATFORM_SWITCH)
    // The detailed report below allocates strings and can itself fail on memory exhaustion.
    // Log the exception without allocating, then show the system error applet directly.
    char message[256] = "NSMBW stopped: unhandled C++ exception";
    if (auto ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            std::snprintf(message, sizeof(message), "NSMBW stopped: %s", e.what());
            std::fprintf(stderr, "[runtime] unhandled C++ exception: %s\n", e.what());
        } catch (...) {
            std::snprintf(message, sizeof(message), "NSMBW stopped: unknown C++ exception");
            std::fputs("[runtime] unhandled non-std C++ exception\n", stderr);
        }
    } else {
        std::snprintf(message, sizeof(message), "NSMBW stopped: std::terminate()");
        std::fputs("[runtime] std::terminate() without current exception\n", stderr);
    }
    std::fflush(stderr);
#if defined(NSMBW_MEMTRACE)
    MemTracker::Close();
#endif
    {
        ReleaseFatalReserve();
        ErrorSystemConfig config{};
        const Result created = errorSystemCreate(&config, message, "");
        if (R_SUCCEEDED(created)) {
            const Result shown = errorSystemShow(&config);
            std::fprintf(stderr, "[runtime] error applet: create 0x%08x, show 0x%08x\n",
                     static_cast<unsigned>(created), static_cast<unsigned>(shown));
        } else {
            std::fprintf(stderr, "[runtime] error applet: create failed 0x%08x\n",
                     static_cast<unsigned>(created));
        }
        std::fflush(stderr);
    }
    std::_Exit(EXIT_FAILURE);
#endif
    std::string terminateDetails;
    if (auto ex = std::current_exception()) {
        try {
            std::rethrow_exception(ex);
        } catch (const std::exception& e) {
            terminateDetails = std::string("Unhandled C++ exception: ") + e.what();
            RT_LOG(RT_TAG_RUNTIME) << "Unhandled C++ exception: " << e.what() << std::endl;
        } catch (...) {
            terminateDetails = "Unhandled non-std C++ exception.";
            RT_LOG(RT_TAG_RUNTIME) << "Unhandled non-std C++ exception." << std::endl;
        }
    } else {
        terminateDetails = "std::terminate() without current exception.";
    }
    const std::string hostStackSummary = FormatHostStackTrace(1);
    if (!hostStackSummary.empty()) {
        terminateDetails.append("\n");
        terminateDetails.append(hostStackSummary);
    }
    WriteFatalLogImpl("terminate", terminateDetails);
    RT_LOG(RT_TAG_RUNTIME) << "std::terminate() called - program exiting" << std::endl;
    std::cerr.flush();
    DumpHostStackTrace();
    if (const CpuContext* cpu = TryGetCpuContext()) {
        RT_LOG(RT_TAG_RUNTIME) << "CPU state at terminate:" << std::endl;
        SystemBridge::DumpCpuState(cpu);
    }
    std::fflush(stderr);
    // Everything is on disk before the applet blocks, so a stuck popup still
    // leaves usable evidence.
    ShowRuntimeFatalPopup("an unhandled C++ exception occurred", terminateDetails);
    std::_Exit(EXIT_FAILURE);
}

#if !defined(MKW_PLATFORM_SWITCH)
extern "C" void NsmbwPerfClocks(uint32_t* cpuMhz, uint32_t* gpuMhz, uint32_t* emcMhz) {
    *cpuMhz = 0;
    *gpuMhz = 0;
    *emcMhz = 0;
}
#else
namespace {
// Read-only: says which clock profile the console actually gave us, so a run's
// frame times can be attributed without guessing what the overclock tool did.
const char* PerformanceConfigurationName(u32 configuration) {
    switch (configuration) {
    case 0x00010000: return "CPU 1020, GPU 384, EMC 1600, docked";
    case 0x00010001: return "CPU 1020, GPU 768, EMC 1600, docked";
    case 0x00020001: return "CPU 1020, GPU 307, EMC 1600, handheld";
    case 0x00020003: return "CPU 1020, GPU 307, EMC 1331, handheld";
    case 0x92220007: return "CPU 1020, GPU 460, EMC 1600, boost";
    case 0x92220008: return "CPU 1020, GPU 460, EMC 1331, boost";
    default: return "unknown";
    }
}

u32 ReadClockRate(PcvModuleId module) {
    ClkrstSession session;
    if (R_FAILED(clkrstOpenSession(&session, module, 3))) {
        return 0;
    }
    u32 hz = 0;
    const Result rc = clkrstGetClockRate(&session, &hz);
    clkrstCloseSession(&session);
    return R_SUCCEEDED(rc) ? hz : 0;
}

} // namespace
// Reported once per perf window so a run's frame times can be attributed to the clocks it actually
// ran at (a launch with the overclock tool off is otherwise indistinguishable in the numbers).
extern "C" void NsmbwPerfClocks(uint32_t* cpuMhz, uint32_t* gpuMhz, uint32_t* emcMhz) {
    *cpuMhz = 0;
    *gpuMhz = 0;
    *emcMhz = 0;
    if (R_FAILED(clkrstInitialize())) {
        return;
    }
    *cpuMhz = ::ReadClockRate(PcvModuleId_CpuBus) / 1000000u;
    *gpuMhz = ::ReadClockRate(PcvModuleId_GPU) / 1000000u;
    *emcMhz = ::ReadClockRate(PcvModuleId_EMC) / 1000000u;
    clkrstExit();
}
namespace {
void LogHorizonPerformanceState() {
    u32 configuration = 0;
    const Result configurationRc = appletGetCurrentPerformanceConfiguration(&configuration);
    ApmPerformanceMode mode = ApmPerformanceMode_Invalid;
    const Result modeRc = apmGetPerformanceMode(&mode);
    const bool clocksOpen = R_SUCCEEDED(clkrstInitialize());
    const u32 cpu = clocksOpen ? ReadClockRate(PcvModuleId_CpuBus) : 0;
    const u32 gpu = clocksOpen ? ReadClockRate(PcvModuleId_GPU) : 0;
    const u32 emc = clocksOpen ? ReadClockRate(PcvModuleId_EMC) : 0;
    if (clocksOpen) {
        clkrstExit();
    }
    RT_LOGF(RT_TAG_RUNTIME,
            "perf: applet %d, %s, config 0x%08x [%s]%s, cpu %.0f MHz, gpu %.0f MHz, emc %.0f MHz\n",
            static_cast<int>(appletGetAppletType()),
            appletGetOperationMode() == AppletOperationMode_Console ? "docked" : "handheld", configuration,
            PerformanceConfigurationName(configuration), R_SUCCEEDED(configurationRc) ? "" : " (read failed)",
            cpu / 1e6, gpu / 1e6, emc / 1e6);
    if (R_SUCCEEDED(modeRc) && mode == ApmPerformanceMode_Boost) {
        RT_LOGF(RT_TAG_RUNTIME, "perf: performance mode is boost\n");
    }
}

// Which language the game will run in, and where that choice came from, so a bug report can tell a
// console language from one picked in Settings.
void LogLanguageChoice() {
    const bool chosen = RuntimeConfigFile::Get().language.has_value();
    const std::string language = RuntimeConfigFile::Language();
    const std::string console = RuntimePlatform::SystemLanguageCode();
    RT_LOGF(RT_TAG_CONFIG, "language: %s (%s), console %s\n", language.c_str(),
            chosen ? "Config.toml" : "not chosen yet", console.empty() ? "unknown" : console.c_str());
}

constexpr u32 kTargetCpuClockHz = 1581000000u;
ClkrstSession g_cpuClockSession{};
std::mutex g_cpuClockMutex;
bool g_cpuClockReady = false;

bool OpenCpuClockSession() {
    if (R_FAILED(clkrstInitialize())) {
        return false;
    }
    if (R_FAILED(clkrstOpenSession(&g_cpuClockSession, PcvModuleId_CpuBus, 3))) {
        clkrstExit();
        return false;
    }
    return true;
}

bool ApplyCpuClock() {
    std::lock_guard lock(g_cpuClockMutex);
    Result rc = clkrstSetClockRate(&g_cpuClockSession, kTargetCpuClockHz);
    if (R_FAILED(rc)) {
        // Sleep can invalidate the service session; reopen it once and retry.
        if (serviceIsActive(&g_cpuClockSession.s)) {
            clkrstCloseSession(&g_cpuClockSession);
        }
        if (!OpenCpuClockSession()) {
            RT_LOGF(RT_TAG_RUNTIME, "perf: could not reopen the CPU clock session\n");
            return false;
        }
        rc = clkrstSetClockRate(&g_cpuClockSession, kTargetCpuClockHz);
    }
    if (R_FAILED(rc)) {
        RT_LOGF(RT_TAG_RUNTIME, "perf: CPU clock set failed (0x%08x)\n", static_cast<unsigned>(rc));
    }
    return R_SUCCEEDED(rc);
}

bool StartCpuClock() {
    if (!OpenCpuClockSession()) {
        return false;
    }
    g_cpuClockReady = true;
    return ApplyCpuClock();
}
}  // namespace

extern "C" void NsmbwRestoreCpuClock() {
    ApplyCpuClock();
}
#endif

// Runtime entry point: loads the configuration, brings up aurora and runs the game.
#if defined(MKW_PLATFORM_SWITCH)
extern "C" void NsmbwBootSplashShow();
extern "C" void NsmbwBootSplashSetStatus(const char* status);
extern "C" void NsmbwBootSplashSetProgress(float fraction, const char* status);

namespace {
const char* BootSplashStatusForPhase(const char* phase) {
    if (std::strcmp(phase, "game folder ready") == 0) return "Preparing game data...";
    if (std::strcmp(phase, "aurora init begin") == 0) return "Starting graphics...";
    if (std::strcmp(phase, "dawn device ready") == 0) return "Creating render pipelines...";
    if (std::strcmp(phase, "gfx init begin") == 0) return "Preparing renderer...";
    if (std::strcmp(phase, "gfx resources ready") == 0) return "Loading shader cache...";
    if (std::strcmp(phase, "gfx init done") == 0) return "Starting game...";
    if (std::strcmp(phase, "aurora init done") == 0) return "Booting game...";
    return nullptr;
}
} // namespace
#endif

extern "C" void LogStartupPhase(const char* phase) {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    RT_LOGF(RT_TAG_RUNTIME, "[startup] %s at %lld ms\n", phase, static_cast<long long>(ms));
#if defined(MKW_PLATFORM_SWITCH)
    if (const char* status = BootSplashStatusForPhase(phase)) {
        NsmbwBootSplashSetStatus(status);
    }
#endif
}

int RuntimeMain(int argc, char** argv) {
#if defined(NSMBW_MEMTRACE)
    MemTracker::Init();
#endif
    LogStartupPhase("runtime main");
#if defined(MKW_PLATFORM_SWITCH)
    ReserveFatalMemory();
    // Read-only assets (TLS bundle, WC24 seed, pipeline cache) live in the NRO's RomFS. If the mount
    // fails the devoptab is absent and the files beside the NRO still resolve, so startup degrades
    // instead of breaking. The result is logged below, once the transcript is capturing stderr.
    static const char* romfsStatus = "";
    romfsStatus = R_SUCCEEDED(romfsInit()) ? "romfs mounted"
                                           : "romfs mount failed; using the files beside the NRO";
    // LAN play and the texture pack downloader both use libnx's bsd service; bring it up once for the
    // whole run. A wider receive window than libnx's default keeps downloads near the console's Wi-Fi
    // speed and costs a few MB of transfer memory.
    static const char* socketStatus = "";
    {
        SocketInitConfig config = *socketGetDefaultInitConfig();
        config.tcp_tx_buf_size = 0x10000;
        config.tcp_tx_buf_max_size = 0x40000;
        config.tcp_rx_buf_size = 0x40000;
        config.tcp_rx_buf_max_size = 0x100000;
        config.sb_efficiency = 8;
        config.num_bsd_sessions = 4;
        socketStatus = R_SUCCEEDED(socketInitialize(&config))
                           ? "socket service up"
                           : "socket service failed to start; network play is unavailable";
    }
    const AppletType type = appletGetAppletType();
    if (type != AppletType_Application && type != AppletType_SystemApplication) {
        ShowFatalText(
            "Launch this game from hbmenu with full memory access. Start any game title on the "
            "home menu while holding R.\n\nFor easier access, you can also use Sphaira to create a home "
            "menu forwarder for this game.");
        return 0;
    }

    LogHorizonPerformanceState();
    // Before the boot splash, config load and Aurora bring-up, so the whole start runs at this clock.
    if (StartCpuClock()) {
        RT_LOGF(RT_TAG_RUNTIME, "perf: CPU clock set to %u MHz\n", kTargetCpuClockHz / 1000000u);
    } else {
        RT_LOGF(RT_TAG_RUNTIME, "perf: could not set the CPU clock to %u MHz\n",
                kTargetCpuClockHz / 1000000u);
    }
#endif
    // Must run before the transcript duplicates stdout/stderr: it decides what
    // those descriptors are mirrored to now that the products are GUI-subsystem.
    AttachParentConsoleForDiagnostics();
#if defined(_WIN32)
    ConfigureWindowsFatalDialogBehavior();
    InstallSehLogger();
    WindowsTimerResolutionGuard timerResolutionGuard;
#elif !defined(MKW_PLATFORM_SWITCH)
    InstallPosixMemoryFaultHandler();
#endif
#if defined(MKW_PLATFORM_SWITCH)
    // Draw before the transcript claims stdout, then leave the frame up until the first present.
    NsmbwBootSplashShow();
#endif
    InitializeProcessTranscript(argc, argv);
#if defined(MKW_PLATFORM_SWITCH)
    RT_LOG(RT_TAG_RUNTIME) << romfsStatus << std::endl;
    RT_LOG(RT_TAG_RUNTIME) << socketStatus << std::endl;
    LogLanguageChoice();
    if (g_cpuClockReady) {
        std::lock_guard lock(g_cpuClockMutex);
        u32 hz = 0;
        if (R_SUCCEEDED(clkrstGetClockRate(&g_cpuClockSession, &hz))) {
            RT_LOGF(RT_TAG_RUNTIME, "perf: CPU clock now %u MHz\n", hz / 1000000u);
        }
    }
#endif
    std::signal(SIGABRT, AbortSignalHandler);
    // Install exit/terminate handlers to ensure we get crash info
    std::atexit(AtExitHandler);
    std::set_terminate(TerminateHandler);
    
    std::string currentEntryLabel;

    try {
        if (argc != 1) {
            throw std::invalid_argument("The game runtime does not accept command-line options; use Config.toml through the installed host.");
        }
        RuntimeConfigFile::LogLoadedConfig();
        // [debug] env and stats must be in place before the runtime reads any NSMBW_* switch.
        PerfStats::ApplyConfig();
        PerfStats::Pin(PerfStats::RoleGuest);
#if defined(MKW_PGO_GENERATE) && defined(__SWITCH__)
        MkwStartPgoDumpTimer();
#endif
        NsmbwShadowSamplerInit(GetRunLogDirectory());
        // The player's own extracted game (game_folder.h); on Windows it is asked for at first start.
        if (!GameFolder::EnsureConfigured()) {
            // Switch has no chooser, so nothing found is said now, before the boot and its prewarm,
            // instead of a minute later when the guest first asks the DVD layer for a file.
            const std::string& problem = GameFolder::StartupProblem();
            if (!problem.empty()) {
                RT_LOGF(RT_TAG_RUNTIME, "ERROR: %s\n", problem.c_str());
                std::string message = GameFolder::StartupProblemTitle();
                message += "\n\n";
                message += problem;
                message += "\n\nSee the Logs folder for the full diagnostic.";
                ShowFatalText(message);
                SetRuntimeExitCodeImpl(EXIT_FAILURE);
            } else {
                SetRuntimeExitCodeImpl(0);
            }
            ShutdownProcessTranscript();
            return 0;
        }
        LogStartupPhase("game folder ready");
        GameFolder::RetailDisc();  // main.dol hashed once here, not when the Settings window first asks
#if defined(__ANDROID__)
        // Determinism testing on a phone: debug.nsmbw.det=1 turns the lockstep timing on,
        // debug.nsmbw.dethash / detdump set NSMBW_DET_HASH / NSMBW_DET_DUMP (det_hash.h) and
        // debug.nsmbw.script[2|3] the scripted remote (NSMBW_INPUT_SCRIPT), 91 characters each.
        {
            const auto property = [](const char* name) {
                char value[PROP_VALUE_MAX] = {};
                return __system_property_get(name, value) > 0 ? std::string(value) : std::string();
            };
            if (property("debug.nsmbw.det") == "1") {
                setenv("NSMBW_DETERMINISTIC", "1", 1);
            }
            for (const auto& [prop, env] : {std::pair{"debug.nsmbw.dethash", "NSMBW_DET_HASH"},
                                            std::pair{"debug.nsmbw.detdump", "NSMBW_DET_DUMP"},
                                            std::pair{"debug.nsmbw.lobbytest", "NSMBW_LOBBY_TEST"},
                                            std::pair{"debug.nsmbw.tsonly", "NSMBW_NET_TAILSCALE_ONLY"},
                                            std::pair{"debug.nsmbw.tapscript", "NSMBW_TAP_SCRIPT"},
                                            std::pair{"debug.nsmbw.warp", "NSMBW_WARP"},
                                            std::pair{"debug.nsmbw.logfps", "NSMBW_LOG_FPS"},
                                            std::pair{"debug.nsmbw.patchreadback", "NSMBW_PATCH_READBACK"}}) {
                if (const std::string value = property(prop); !value.empty()) {
                    setenv(env, value.c_str(), 1);
                }
            }
            std::string script = property("debug.nsmbw.script") + property("debug.nsmbw.script2") +
                                 property("debug.nsmbw.script3");
            if (!script.empty()) {
                setenv("NSMBW_INPUT_SCRIPT", script.c_str(), 1);
            }
            if (const std::string session = property("debug.nsmbw.sscript"); !session.empty()) {
                setenv("NSMBW_SESSION_SCRIPT", session.c_str(), 1);
            }
            // Any other variable for an experiment: debug.nsmbw.env = "NAME=value;NAME=value".
            const std::string extra = property("debug.nsmbw.env");
            for (size_t start = 0; start < extra.size();) {
                size_t end = extra.find(';', start);
                if (end == std::string::npos) end = extra.size();
                const std::string item = extra.substr(start, end - start);
                if (const size_t eq = item.find('='); eq != std::string::npos && eq > 0) {
                    setenv(item.substr(0, eq).c_str(), item.substr(eq + 1).c_str(), 1);
                }
                start = end + 1;
            }
        }
#endif
        if (const char* deterministic = std::getenv("NSMBW_DETERMINISTIC");
            deterministic != nullptr && std::string(deterministic) == "1") {
            DetClock::Enable();
        }
        // Network play: the session settings turn deterministic mode on and feed every frame's
        // input from all players (netplay_session.h).
        // A restart out of a LAN room lands here: the session file names the players, and the NAND
        // switches to the session's copy of the host's save before anything reads it.
        {
            NetplaySession::Config netplay;
            int saveFile = 0;
            if (NetplayStart::TakePendingSession(netplay, saveFile)) {
                netplay.autopilot = true;
                netplay.saveFile = saveFile;
                NetplaySession::Start(netplay);
            } else if (NetplaySession::LoadConfigFromEnvironment(netplay)) {
                NetplaySession::Start(netplay);
            }
            NetplaySession::SetStallHandler(&NetplayUi::OnSessionStall);
        }
        // Bluetooth passthrough: look for a USB Bluetooth adapter while the graphics start up. Knows by
        // now whether this run is a LAN session or deterministic, where it stays off.
        BtPassthrough::StartProbe();
        if (RuntimeConfigFile::DiscordPresenceEnabled()) {
            DiscordPresence::Initialize(RuntimeConfigFile::DiscordClientId(),
                                        RuntimeProduct::Active().displayName.data());
        }
        SystemBridge::Initialize();
        TranslatedFunctionRegistry::Finalize();

        // Initialize Aurora (graphics backend)
        // We use auto backend (or specific if needed) and set a default window size.
        // This is required for GX commands (like texture loading) to work.
        AuroraConfig auroraConfig = {};
        auroraConfig.appName = RuntimeProduct::Active().displayName.data();
        const auto applicationDataDirectory = RuntimeConfigFile::ApplicationDataDirectory();
        const auto rendererCacheDirectory = applicationDataDirectory / "Cache";
        std::error_code rendererPathError;
        std::filesystem::create_directories(rendererCacheDirectory, rendererPathError);
        if (rendererPathError) {
            RT_LOG(RT_TAG_RUNTIME) << "Unable to create renderer cache directory "
                      << RuntimeConfigFile::PathToUtf8(rendererCacheDirectory) << ": "
                      << rendererPathError.message() << std::endl;
        }
#if defined(MKW_PLATFORM_SWITCH)
        // Pack mods reuse the base game's shaders. Copy the vanilla Dawn cache once,
        // before the renderer opens it and the shader compile screen appears.
        if (RuntimeProduct::UsesPack()) {
            const auto dawnCache = rendererCacheDirectory / "dawn_cache.db";
            std::error_code dawnCacheError;
            if (!std::filesystem::exists(dawnCache, dawnCacheError)) {
                const auto program = RuntimeConfigFile::ExecutableDirectory();
                if (program) {
                    const auto source = program->parent_path() / "vanilla" / "data" / "Cache" / "dawn_cache.db";
                    if (std::filesystem::is_regular_file(source, dawnCacheError)) {
                        std::FILE* in = std::fopen(RuntimeConfigFile::PathToUtf8(source).c_str(), "rb");
                        std::FILE* out = in ? std::fopen(RuntimeConfigFile::PathToUtf8(dawnCache).c_str(), "wb") : nullptr;
                        bool ok = in != nullptr && out != nullptr;
                        if (ok) {
                            std::vector<unsigned char> buffer(1024 * 1024);
                            size_t read = 0;
                            while ((read = std::fread(buffer.data(), 1, buffer.size(), in)) > 0) {
                                if (std::fwrite(buffer.data(), 1, read, out) != read) {
                                    ok = false;
                                    break;
                                }
                            }
                            ok = ok && std::ferror(in) == 0 && std::ferror(out) == 0;
                        }
                        if (in != nullptr) std::fclose(in);
                        if (out != nullptr) std::fclose(out);
                        if (ok) {
                            RT_LOG(RT_TAG_RUNTIME) << "copied the vanilla Dawn cache to "
                                      << RuntimeConfigFile::PathToUtf8(dawnCache) << std::endl;
                        } else {
                            std::filesystem::remove(dawnCache, dawnCacheError);
                        }
                    }
                }
            }
        }
#endif
        const std::string auroraUserPath = RuntimeConfigFile::PathToUtf8(applicationDataDirectory);
        const std::string auroraCachePath = RuntimeConfigFile::PathToUtf8(rendererCacheDirectory);
        auroraConfig.userPath = auroraUserPath.c_str();
        auroraConfig.cachePath = auroraCachePath.c_str();
        // initial_pipeline_cache.db (NSMBW's pipelines, merged into the user's cache at startup)
        // travels in the Switch NRO's RomFS, sits beside the executable elsewhere, or in the app's
        // unpacked runtime folder on Android.
        std::string auroraResourcesPath;
#if defined(MKW_PLATFORM_SWITCH)
        if (RuntimeConfigFile::RomfsAsset("initial_pipeline_cache.db")) {
            auroraResourcesPath = "romfs:/";
        }
#endif
        if (auroraResourcesPath.empty()) {
            const auto runtimeDirectory = RuntimeConfigFile::ExecutableDirectory();
            if (runtimeDirectory) {
                auroraResourcesPath = RuntimeConfigFile::PathToUtf8(*runtimeDirectory);
            }
        }
        if (!auroraResourcesPath.empty()) {
            auroraConfig.resourcesPath = auroraResourcesPath.c_str();
        }
        auroraConfig.logCallback = &RuntimeAuroraLogCallback;
        auroraConfig.logLevel = LOG_DEBUG;
        // NSMBW display settings: aspect_mode, else the older widescreen/force_16_9 keys.
        DisplaySettings::Settings displaySettings;
        displaySettings.aspect = RuntimeConfigFile::AspectMode(
            RuntimeConfigFile::ForceAspect169Enabled()      ? DisplaySettings::kAspect16x9
            : !RuntimeConfigFile::WidescreenEnabled(true) ? DisplaySettings::kAspect4x3
                                                          : DisplaySettings::kAspectFill);
        displaySettings.renderScale = RuntimeConfigFile::ResolutionMultiplier(1.0f);
        DisplaySettings::Initialize(displaySettings);
        const bool configWidescreen = DisplaySettings::GameWidescreen();
        auroraConfig.windowWidth = configWidescreen ? 854 : 640;
        auroraConfig.windowHeight = 480;
        auroraConfig.windowWidth = RuntimeConfigFile::WindowWidth(auroraConfig.windowWidth);
        auroraConfig.windowHeight = RuntimeConfigFile::WindowHeight(auroraConfig.windowHeight);
        auroraConfig.hasWindowPosition = RuntimeConfigFile::WindowPosition(
            auroraConfig.windowPosX, auroraConfig.windowPosY);
        auroraConfig.allowJoystickBackgroundEvents = true;
        auroraConfig.disableCopyFilter = RuntimeConfigFile::DisableCopyFilter(true);
        // Dolphin-style custom textures. Aurora indexes <userPath>/texture_replacements
        // once during aurora_initialize, so both knobs only take effect on the next launch.
        // Dumps name each unmatched texture the way its replacement would have to be named,
        // which is only useful while the index is live - hence the conjunction.
        auroraConfig.allowTextureReplacements = RuntimeConfigFile::TextureReplacements(false);
        auroraConfig.allowTextureDumps = auroraConfig.allowTextureReplacements &&
                                         RuntimeConfigFile::TextureDumps(false);
        // No vsync knob: aurora always configures a non-blocking present mode.
        auroraConfig.desiredBackend = BACKEND_AUTO;
        // NSMBW: GX has no anisotropic filtering; the Wii only did bilinear and mip filtering. The
        // 16x default is also the most expensive sampler mode on the Switch. NSMBW_ANISO=<1..16>
        // pins it for an A/B.
        if (const char* aniso = std::getenv("NSMBW_ANISO"); aniso != nullptr) {
            auroraConfig.maxTextureAnisotropy = static_cast<uint16_t>(std::clamp(std::atoi(aniso), 1, 16));
        }
        // GPU driver workarounds: picked from the graphics adapter unless the player forced them.
        auroraConfig.gpuCompat = static_cast<AuroraGpuCompat>(RuntimeConfigFile::GpuCompat());
        // DisplaySettings applies the viewport policy and render height at the first frame boundary.
        // One table for both directions. RuntimeConfigFile::IsSupportedGraphicsApi
        // whitelists exactly these config names, so an unrecognised value has
        // already been rejected (and reported) at parse time.
        struct GraphicsBackendEntry {
            const char* configName;
            AuroraBackend backend;
        };
#if defined(__APPLE__)
        static constexpr std::array<GraphicsBackendEntry, 2> kGraphicsBackends{{
            {"auto", BACKEND_AUTO}, {"metal", BACKEND_METAL},
        }};
#elif defined(__ANDROID__)
        // NSMBW: OpenGL ES can't be picked for now (walled off for a later version); a saved
        // "opengles" runs Vulkan.
        static constexpr std::array<GraphicsBackendEntry, 3> kGraphicsBackends{{
            {"auto", BACKEND_AUTO}, {"vulkan", BACKEND_VULKAN}, {"opengles", BACKEND_VULKAN},
        }};
// only vulkan for linux
#elif defined(__linux__)
            static constexpr std::array<GraphicsBackendEntry, 2> kGraphicsBackends{{
            {"auto", BACKEND_AUTO}, {"vulkan", BACKEND_VULKAN},
        }};
#elif defined(MKW_PLATFORM_SWITCH)
        static constexpr std::array<GraphicsBackendEntry, 2> kGraphicsBackends{{
            {"auto", BACKEND_AUTO}, {"vulkan", BACKEND_VULKAN},
        }};
#elif defined(_WIN32)
        static constexpr std::array<GraphicsBackendEntry, 4> kGraphicsBackends{{
            {"auto", BACKEND_AUTO}, {"d3d12", BACKEND_D3D12}, {"vulkan", BACKEND_VULKAN}, {"d3d11", BACKEND_D3D11},
        }};

#endif
        const auto backendDisplayName = [](AuroraBackend value) -> const char* {
            for (const auto& entry : kGraphicsBackends) {
                if (entry.backend == value) {
                    return entry.configName;
                }
            }
            return "unknown";
        };

        const std::string backend = RuntimeConfigFile::GraphicsApi("auto");
        for (const auto& entry : kGraphicsBackends) {
            if (backend == entry.configName) {
                auroraConfig.desiredBackend = entry.backend;
                break;
            }
        }
        const AuroraBackend requestedBackend = auroraConfig.desiredBackend;

        // SDL only reads its Wii driver hint when the joystick subsystem starts, which
        // aurora_initialize does; a Bluetooth Wii Remote paired before launch must be
        // visible on that first scan. Switch homebrew has no Bluetooth Wii Remote
        // path (Joy-Cons arrive through the Switch HID driver instead), so the
        // driver defaults off there.
#if defined(MKW_PLATFORM_SWITCH)
        WiiRemoteInput::ConfigureSdlHints(RuntimeConfigFile::WiiRemotesEnabled(false));
#else
        WiiRemoteInput::ConfigureSdlHints(RuntimeConfigFile::WiiRemotesEnabled(true));
#endif
#if defined(__ANDROID__)
        // Graphics debugging on a phone without a rebuild: `adb shell setprop debug.nsmbw.gfx
        // validate,robust` reaches aurora's device setup as AURORA_GFX_FLAGS (webgpu/gpu.cpp).
        // A property holds 91 characters, so debug.nsmbw.gfx2 and gfx3 continue the list.
        {
            std::string flags;
            for (const char* name : {"debug.nsmbw.gfx", "debug.nsmbw.gfx2", "debug.nsmbw.gfx3"}) {
                char value[PROP_VALUE_MAX] = {};
                if (__system_property_get(name, value) > 0) {
                    flags += flags.empty() ? "" : ",";
                    flags += value;
                }
            }
            if (!flags.empty()) {
                setenv("AURORA_GFX_FLAGS", flags.c_str(), 1);
                RT_LOG(RT_TAG_RUNTIME) << "debug.nsmbw.gfx flags: " << flags << std::endl;
            }
        }
#endif


#if defined(MKW_PLATFORM_SWITCH)
        // NVK needs the nv services before first use (same as the reference ports).
        setenv("NVK_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1);
        nvInitialize();
#endif

        LogStartupPhase("aurora init begin");
        const AuroraInfo auroraInfo = aurora_initialize(0, nullptr, &auroraConfig);
        LogStartupPhase("aurora init done");
        if (auroraInfo.initializationStatus != AURORA_INITIALIZATION_SUCCESS) {
            throw std::runtime_error(auroraInfo.initializationError != nullptr
                ? auroraInfo.initializationError : "No supported graphics backend is available");
        }
        if (requestedBackend != BACKEND_AUTO && auroraInfo.backend != requestedBackend) {
            RT_LOG(RT_TAG_RUNTIME) << "graphics_api=\"" << backend
                      << "\" is not available on this system; aurora fell back to \""
                      << backendDisplayName(auroraInfo.backend)
                      << "\". See the [aurora::gpu] lines above for the reason." << std::endl;
        } else {
            RT_LOG(RT_TAG_RUNTIME) << "graphics backend: " << backendDisplayName(auroraInfo.backend)
                      << std::endl;
        }
        // Visual validation: NSMBW_CAPTURE_FRAME=<n> writes that frame to NSMBW_CAPTURE_PATH (or
        // frame_capture.bmp). Deterministic mode plus the input script makes two builds capture the
        // same frame, so a renderer change can be diffed instead of eyeballed.
        if (const char* captureFrame = std::getenv("NSMBW_CAPTURE_FRAME"); captureFrame != nullptr) {
            const uint32_t frame = static_cast<uint32_t>(std::strtoul(captureFrame, nullptr, 10));
            const char* path = std::getenv("NSMBW_CAPTURE_PATH");
            aurora_request_frame_capture(frame, path != nullptr ? path : "frame_capture.bmp");
            RT_LOG(RT_TAG_RUNTIME) << "frame capture requested: frame " << frame << " -> "
                      << (path != nullptr ? path : "frame_capture.bmp") << std::endl;
        }
#if defined(__linux__) && !defined(__ANDROID__)
        // Dawn falls back to its null adapter when there is no Vulkan driver; nothing would draw.
        if (auroraInfo.backend == BACKEND_NULL) {
            RT_LOG(RT_TAG_RUNTIME) << "no Vulkan driver: aurora fell back to the null backend" << std::endl;
            ShowRuntimeFatalPopup("the graphics could not start", kNoVulkanHint);
            MarkFatalErrorReported();
            std::exit(EXIT_FAILURE);
        }
#endif
        GraphicsInfo::Set(auroraInfo.gpuName != nullptr ? auroraInfo.gpuName : "", auroraInfo.gpuCompatActive);
        RT_LOG(RT_TAG_RUNTIME) << "graphics adapter: " << GraphicsInfo::Name() << ", compatibility fixes "
                  << (auroraInfo.gpuCompatActive ? "on" : "off") << std::endl;
        aurora_set_frame_worker_wait_callback(ServiceGuestTimingDuringAuroraFrameWait);
        GxGuestWrite::InstallAuroraHooks();
        AxfxReverbNative::Install();
        AxfxDelayNative::Install();
        AxOutNative::Install();
        DisplaySettings::OnFrameBoundary(auroraInfo.windowSize.native_fb_width,
                                         auroraInfo.windowSize.native_fb_height);
        settings_overlay::InitializeRuntimeSettings();
        Updater::Start();  // the file select's Update button: checks GitHub in the background
        static constexpr const char* kAspectNames[] = {"original 4:3", "16:9", "fill", "16:10"};
        RT_LOG(RT_TAG_CONFIG) << "display aspect=" << kAspectNames[displaySettings.aspect]
                  << " SCGetAspectRatio=" << (configWidescreen ? 1 : 0)
                  << " render=" << DisplaySettings::LinesForScale(displaySettings.renderScale) << " lines ("
                  << displaySettings.renderScale << "x)"
                  << " window=" << auroraInfo.windowSize.width << "x" << auroraInfo.windowSize.height
                  << " native=" << auroraInfo.windowSize.native_fb_width << "x"
                  << auroraInfo.windowSize.native_fb_height << std::endl;
        g_auroraInitialized.store(true, std::memory_order_release);
        settings_overlay::RunShaderLoadingScreen();

        auto entry = ResolveEntry();
        InitializePersistentCpuContext();
        auto& cpu = GetPersistentCpuContext();
        SeedCpuContext(cpu);
        
        // Initialize the fiber-based threading system
        Fiber::GuestFiberManager::Initialize();

        // Deterministic mode: the controllers as boot sees them, until the first retrace latches.
        if (DetClock::Enabled()) {
            FrameInput::Latch(0);
        }
        
        CpuContextScope cpuScope(&cpu);

        std::string label = entry->name;
        if (label.empty()) {
            std::ostringstream oss;
            oss << "0x" << std::hex << entry->address;
            label = oss.str();
        }
        currentEntryLabel = label;
        g_lastEntryLabel = currentEntryLabel;

        InvokeIndirectCpu(entry->address, &cpu);
        const uint32_t result = cpu.gpr[3];
        RT_LOG(RT_TAG_RUNTIME) << label << " => 0x" << std::hex << result << std::dec << " (" << result << ")" << std::endl;
        
        // Shutdown fiber system
        Fiber::GuestFiberManager::Shutdown();
        WindowPlacementPersistence::Flush(true);
        aurora_shutdown();
#if defined(MKW_PLATFORM_SWITCH)
        nvExit();
#endif
        DiscordPresence::Shutdown();
        SetRuntimeExitCodeImpl(0);
        ShutdownProcessTranscript();
#if defined(NSMBW_MEMTRACE)
        MemTracker::Close();
#endif
        return 0;
    } catch (const Memory::AccessViolation& ex) {
        std::cerr << "Runtime error: " << ex.what() << std::endl;
        DumpAccessViolationReport(ex, currentEntryLabel);
        std::ostringstream details;
        details << "addr=0x" << std::hex << std::uppercase << ex.address()
                << " len=0x" << ex.length()
                << std::dec << std::nouppercase
                << " reason=" << ex.reason();
        ShowRuntimeFatalPopup("a guest memory access was out of bounds", details.str());
        WriteFatalLogImpl("access_violation", details.str());
        SetRuntimeExitCodeImpl(1);
        Fiber::GuestFiberManager::Shutdown();
        WindowPlacementPersistence::Flush(true);
        aurora_shutdown();
#if defined(MKW_PLATFORM_SWITCH)
        nvExit();
#endif
        DiscordPresence::Shutdown();
        ShutdownProcessTranscript();
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "Runtime error: " << ex.what() << std::endl;
        SystemBridge::DumpCpuState(TryGetCpuContext());
        ShowRuntimeFatalPopup("a runtime exception occurred", ex.what());
        WriteFatalLogImpl("exception", ex.what());
        SetRuntimeExitCodeImpl(1);
        Fiber::GuestFiberManager::Shutdown();
        WindowPlacementPersistence::Flush(true);
        aurora_shutdown();
#if defined(MKW_PLATFORM_SWITCH)
        nvExit();
#endif
        DiscordPresence::Shutdown();
        ShutdownProcessTranscript();
        return 1;
    }
}

#if defined(__ANDROID__)
// SDL's Android activity loads libmain.so and runs the game through its exported SDL_main.
extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) {
    return RuntimeMain(argc, argv);
}
#else
int main(int argc, char** argv) {
    return RuntimeMain(argc, argv);
}
#endif
extern "C" bool g_dynamicAspectRatioEnabled = false;
