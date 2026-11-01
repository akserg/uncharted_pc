# M3 — Guest boots to the first GNM call

## Goal
`bb-probe` loads `eboot.bin`, patches it, enters it on the guest stack under Rosetta, and the game runs
its init until it first talks to the GPU (`sceGnm*` / `sceVideoOut*`). The renderer may still be
absent/stubbed; the point is proving guest execution + runtime HLE under Rosetta.

## Preconditions
- M2 done. Game dump available at `BB_GAME_DIR`.

## Tasks
1. **Signals**: replace the M1 stub in `probe.c` `fault()` with the Darwin layout:
   `((ucontext_t*)ctx)->uc_mcontext->__ss.__rip`, fault address from `info->si_addr`
   (or `__es.__faultvaddr`). Same for `bbgpu_handle_fault` in `gpu/shim` if it reads the context.
   `sigaltstack` and `SA_SIGINFO|SA_ONSTACK` as on Linux.
2. **Guest TLS** (most important item in M3). Linux points GS at the guest TCB via `arch_prctl(ARCH_SET_GS)`; macOS has no userspace equivalent. Reuse the **Windows design**: `probe.c` `patch_tcb_loads` rewrites every `mov rax, gs:[0]` (`65 48 8b 04 25 00 00 00 00`) to a fixed displacement, and `runtime_thread.c` `set_gs` stores the TCB in that slot. On macOS x86_64, GS is the pthread TSD base: `pthread_key_create` once → displacement = `key*8` (`runtime_thread_tcb_offset`), `set_gs` = `pthread_setspecific(key, tcb)`. Enable `patch_tcb_loads` for `_WIN32 || __APPLE__`. Check shadPS4 `src/core/tls.cpp` (APPLE branch) to confirm. Verify with a test that reads `gs:[key*8]` from inline asm under Rosetta and gets the value set.
   Also: `SYS_tgkill` in the watchdog sampler → `pthread_kill(pthread_t, SIGUSR2)`.
3. **CPU detection**: run `out/bb-probe` and log what `cpuid` reports under Rosetta (AVX, AVX2, BMI1,
   LZCNT, MOVBE, POPCNT, OSXSAVE/XGETBV). `cpu_patch.c` must see BMI1/LZCNT/MOVBE present or patch them;
   either outcome is fine, but AVX **must** be present (else macOS < 15.4 or Rosetta not installed).
4. **Smoke**: `out/bb-probe` with `src/vulkan_smoke.c` path (whatever upstream uses to test the loader
   without a game) if it exists as a mode; else go straight to the game.
5. **Run**: `BB_GAME_DIR=... BB_EXE=u1 ./run.sh` with the window backend still aborting at first
   `sceVideoOut`/GNM call. Watch `user/last_run.log`. Fix in order: ELF mapping, relocations, TLS,
   thread creation (`runtime_thread.c` — `pthread` attrs, affinity no-op, names), fibers, file I/O
   (`runtime_file.c` — path handling, `O_DIRECT`, `pread` sizes), time (`runtime_time.c`,
   `runtime_rtc.c` — `clock_gettime(CLOCK_MONOTONIC_RAW)` exists on Darwin), save data dir.
6. Confirm that Xbyak-emitted code and the `ms_abi` hook callbacks execute under Rosetta (they will
   only be exercised once GNM runs — note as "not yet verified" if M3 ends before that).

## Acceptance
- Log shows the guest reached the point where upstream Linux logs its first GNM/VideoOut call,
  and the process exits via the intentional window abort, not via a signal.
- No `SIGILL` from the guest.

## Hazards
- Rosetta and `sigaltstack`: works, but the alt stack must be allocated by the host, not in guest memory.
- Darwin `pthread_setname_np` takes only a name (current thread). Thread names set from another thread
  are not possible; make it a no-op in that case.
- `MXCSR`/`x87 CW` set in `runtime_thread.c` are honoured by Rosetta (it emulates them); keep the code.

## Result
_(fill in: date, commit, cpuid dump under Rosetta, cpu_patch decisions, list of runtime fixes with
file:function, last 30 lines of last_run.log at the stopping point, open issues)_
