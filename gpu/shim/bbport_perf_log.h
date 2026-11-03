// bbport (macOS, M5 A1): BB_PERF_LOG=1 prints one line every 5 s:
//   PERF: t=<s> fps=<f> ft_p50=<ms> ft_p99=<ms> gpu_thread_cpu=<%> footprint_mb=<MB> frames=<n> window_s=<s>
// t = seconds since the GPU library was loaded (~launch). F8 in the game window prints
//   MARK <n> t=<s>
// so a player can tie PERF lines and bugs to a place in the game (window.cpp).
// fps / frame times come from guest flips (VideoOutDriver::Flip), gpu_thread_cpu is the CPU time of
// the GPU command thread (Liverpool::Process) over the window via thread_info(THREAD_BASIC_INFO),
// footprint is task_info(TASK_VM_INFO).phys_footprint. Darwin only (the Linux statistics use
// pthread_getcpuclockid / RUSAGE_THREAD, which Darwin lacks).
#pragma once
#ifdef __APPLE__
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <mach/mach.h>
#include <pthread.h>

namespace BbPerfLog {

/// Seconds since the GPU library was loaded (about the process start).
inline const std::chrono::steady_clock::time_point load_time = std::chrono::steady_clock::now();
inline double Seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - load_time).count();
}

/// F8: a numbered marker line in the log (always on).
inline void Mark() {
    static std::atomic<int> count{0};
    std::printf("MARK %d t=%.1f (F8)\n", ++count, Seconds());
    std::fflush(stdout);
}

/// Mach port of the GPU command thread (0 until Liverpool::Process starts).
inline std::atomic<std::uint32_t> gpu_thread_port{0};

inline const bool enabled = [] {
    const char* env = std::getenv("BB_PERF_LOG");
    return env && env[0] == '1';
}();

/// Called once by the GPU command thread.
inline void RegisterGpuThread() {
    gpu_thread_port.store(pthread_mach_thread_np(pthread_self()), std::memory_order_relaxed);
}

/// CPU time (user + system) of a thread in microseconds, -1 if unavailable.
inline std::int64_t ThreadCpuUs(std::uint32_t port) {
    if (port == 0) {
        return -1;
    }
    thread_basic_info_data_t info{};
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    if (thread_info(port, THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&info), &count) !=
        KERN_SUCCESS) {
        return -1;
    }
    return std::int64_t(info.user_time.seconds + info.system_time.seconds) * 1000000 +
           info.user_time.microseconds + info.system_time.microseconds;
}

/// Process phys_footprint in bytes, 0 if unavailable.
inline std::uint64_t PhysFootprint() {
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) !=
        KERN_SUCCESS) {
        return 0;
    }
    return info.phys_footprint;
}

/// Called on every guest flip (single caller thread).
inline void OnFlip() {
    if (!enabled) {
        return;
    }
    using Clock = std::chrono::steady_clock;
    static Clock::time_point window_start = Clock::now(), last = window_start;
    static std::int64_t window_cpu_us = ThreadCpuUs(gpu_thread_port.load(std::memory_order_relaxed));
    static std::vector<double> frame_ms;
    const auto now = Clock::now();
    frame_ms.push_back(std::chrono::duration<double, std::milli>(now - last).count());
    last = now;
    const double window = std::chrono::duration<double>(now - window_start).count();
    if (window < 5.0) {
        return;
    }
    const std::int64_t cpu_us = ThreadCpuUs(gpu_thread_port.load(std::memory_order_relaxed));
    const double gpu_share =
        (cpu_us >= 0 && window_cpu_us >= 0) ? (cpu_us - window_cpu_us) / (window * 1e4) : -1.0;
    std::vector<double> sorted = frame_ms;
    std::sort(sorted.begin(), sorted.end());
    const auto pct = [&](double p) {
        const std::size_t i = std::min(sorted.size() - 1, std::size_t(p * double(sorted.size() - 1) + 0.5));
        return sorted[i];
    };
    std::printf("PERF: t=%.0f fps=%.1f ft_p50=%.2f ft_p99=%.2f gpu_thread_cpu=%.1f%% "
                "footprint_mb=%.0f frames=%zu window_s=%.2f\n",
                Seconds(), frame_ms.size() / window, pct(0.50), pct(0.99), gpu_share,
                PhysFootprint() / (1024.0 * 1024.0), frame_ms.size(), window);
    std::fflush(stdout);
    frame_ms.clear();
    window_start = now;
    window_cpu_us = cpu_us;
}

} // namespace BbPerfLog
#endif
