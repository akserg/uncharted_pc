// bbport (macOS, M5b round 2): BB_FRAME_TRACE=1 writes one line per presented (flipped) frame:
//   <frame> <t_s> <replay_s> <frame_ms> | drain_ms drain_max | rec_ms rec_max | submit_ms submit_max |
//   present_ms present_max | pipe_ms pipe_max
// t_s: seconds since the GPU library was loaded; replay_s: seconds since the pad replay started
// (-1 before; runtime_pad.c exports BB_REPLAY_T0_US, CLOCK_MONOTONIC microseconds). The slots are the
// times spent by any thread since the previous flip:
//   drain   GPU command thread waiting in DrawPipe::Drain (draw recording thread behind)
//   rec     draw recording thread (bb:DrawRec) running packets
//   submit  Scheduler: vkQueueSubmit
//   present swapchain acquire, vkQueuePresent and the waits for the previous presentation
//   pipe    pipeline creation (compile) on whichever thread
// File: BB_FRAME_TRACE_FILE (default frames-trace.txt). Off: one predictable branch per scope, no logging.
// Other platforms: Scope and OnFlip are empty.
#pragma once
#ifdef __APPLE__
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#endif

namespace BbFrameTrace {

enum Slot : unsigned { Drain, Rec, Submit, Present, Pipe, NumSlots };

#ifdef __APPLE__
inline const bool enabled = [] {
    const char* env = std::getenv("BB_FRAME_TRACE");
    return env && env[0] && env[0] != '0';
}();

inline std::int64_t NowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct Counters {
    std::atomic<std::int64_t> total_ns{0};
    std::atomic<std::int64_t> max_ns{0};
};
inline Counters counters[NumSlots];

inline void Add(Slot slot, std::int64_t ns) {
    auto& c = counters[slot];
    c.total_ns.fetch_add(ns, std::memory_order_relaxed);
    std::int64_t seen = c.max_ns.load(std::memory_order_relaxed);
    while (ns > seen && !c.max_ns.compare_exchange_weak(seen, ns, std::memory_order_relaxed)) {
    }
}

/// Times its lifetime into a slot.
class Scope {
public:
    explicit Scope(Slot slot_) : slot{slot_}, start{enabled ? NowNs() : 0} {}
    ~Scope() {
        if (enabled) {
            Add(slot, NowNs() - start);
        }
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Slot slot;
    std::int64_t start;
};

/// Called on every guest flip (single caller thread).
inline void OnFlip() {
    if (!enabled) {
        return;
    }
    static std::FILE* file = [] {
        const char* path = std::getenv("BB_FRAME_TRACE_FILE");
        return std::fopen(path && path[0] ? path : "frames-trace.txt", "w");
    }();
    if (!file) {
        return;
    }
    static const auto load = std::chrono::steady_clock::now();
    static std::int64_t last = NowNs();
    static unsigned frame = 0;
    const std::int64_t now = NowNs();
    double replay = -1.0;
    if (const char* env = std::getenv("BB_REPLAY_T0_US")) {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        const double now_us = double(ts.tv_sec) * 1e6 + double(ts.tv_nsec) / 1e3;
        replay = (now_us - double(std::strtoull(env, nullptr, 10))) / 1e6;
    }
    double v[NumSlots][2];
    for (unsigned i = 0; i < NumSlots; ++i) {
        v[i][0] = double(counters[i].total_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
        v[i][1] = double(counters[i].max_ns.exchange(0, std::memory_order_relaxed)) / 1e6;
    }
    std::fprintf(file,
                 "%u %.4f %.4f %.3f | %.3f %.3f | %.3f %.3f | %.3f %.3f | %.3f %.3f | %.3f %.3f\n",
                 frame++, std::chrono::duration<double>(std::chrono::steady_clock::now() - load).count(),
                 replay, double(now - last) / 1e6, v[Drain][0], v[Drain][1], v[Rec][0], v[Rec][1],
                 v[Submit][0], v[Submit][1], v[Present][0], v[Present][1], v[Pipe][0], v[Pipe][1]);
    std::fflush(file);
    last = now;
}
#else
class Scope {
public:
    explicit Scope(Slot) {}
};
inline void OnFlip() {}
#endif

} // namespace BbFrameTrace
