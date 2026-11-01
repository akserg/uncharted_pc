// SPDX-License-Identifier: GPL-2.0-or-later
// bbport (macOS): small Linux helpers the renderer uses for diagnostics and thread identity.
// Force-included into the GPU library on Apple platforms only (CMakeLists.txt).
#pragma once
#ifdef __APPLE__
#include <pthread.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
static inline int gettid(void) {
    uint64_t tid = 0;
    pthread_threadid_np(NULL, &tid);
    return (int)tid;
}
#ifdef __cplusplus
}
#endif
#endif
