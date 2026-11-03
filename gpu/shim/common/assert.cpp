// bbport: GPU-side assertion failures stop the port with exit code 23.
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include "bbport_toggles.h"
#include "common/assert.h"
#include "common/logging/log.h"

// A draw-preparation worker translates shaders and builds pipelines ahead of the GPU thread, from
// guest memory the game may already have reused. Where it armed a recovery point (the same one a
// faulting read uses), an assertion on such data only abandons the speculation; the GPU thread
// prepares the draw itself, from memory as it is when the draw executes.
static void abandon_speculation() {
    if (!runtime_fault_recover) {
        return;
    }
    sigjmp_buf* recover = runtime_fault_recover;
    runtime_fault_recover = nullptr;
    std::fputs("GPU assertion while preparing a draw ahead of the GPU thread: leaving the draw to the GPU thread\n",
               stderr);
#ifdef _WIN32
    bb_longjmp(*recover, 1);
#else
    siglongjmp(*recover, 1);
#endif
}

#ifdef __APPLE__
extern "C" void (*runtime_crash_hook)(const char* why); // probe.c crash record (M5 A2)
#endif

void assert_fail_impl() {
    abandon_speculation();
    std::fflush(stdout);
#ifdef __APPLE__
    if (runtime_crash_hook) {
        runtime_crash_hook("GPU library assertion");
    }
#endif
    std::fputs("STOP: GPU library assertion failed (see GPU log above)\n", stderr);
    std::_Exit(23);
}

[[noreturn]] void unreachable_impl() {
    assert_fail_impl();
    throw std::runtime_error("Unreachable code");
}

void assert_fail_debug_msg(const char* msg) {
    LOG_CRITICAL(Debug, "Assertion Failed!\n{}", msg);
    assert_fail_impl();
}
