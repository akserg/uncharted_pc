/* The PS4 user address range the runtime reserves for the guest (src/runtime_memory.c) and the
 * GPU code that treats guest addresses specially (buffer cache, usable regions). One definition
 * for both sides: plain macros, valid in C and C++. */
#ifndef BB_USER_RANGE_H
#define BB_USER_RANGE_H
#ifdef __APPLE__
/* Rosetta x86_64 process: [0x1000000000, 0x7000000000) is reserved by the system (GPU carveout; every
 * mmap there fails with EACCES, measured in M2) and [0xFC0000000, 0x1000000000) is the commpage.
 * shadPS4 makes the same cut for macOS x86_64 (USER_MIN 0x7000000000). */
#define BB_USER_MIN 0x7000000000ull
#define BB_LOW_MAX 0xfc0000000ull
#else
#define BB_USER_MIN 0x1000000000ull
#define BB_LOW_MAX BB_USER_MIN
#endif
#define BB_USER_MAX 0xfc00000000ull
#endif
