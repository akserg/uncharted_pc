#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "$0")"
mkdir -p out
# Windows: MSYS2 CLANG64 (packages in packaging/windows/README.md). The GPU library is static
# there and CMake links bb-probe.exe as well; no PGO (GCC-only flags), LTO off by default.
case $(uname -s) in
MINGW*|MSYS*)
    # shellcheck source=msys2-env.sh
    source ./msys2-env.sh
    for tool in clang cmake ninja pkg-config; do
        command -v "$tool" >/dev/null || { echo "Need $tool from MSYS2 CLANG64 (packaging/windows/README.md)." >&2; exit 1; }
    done
    if [[ ! -f gpu/third_party/fsr-vulkan/CMakeLists.txt || ! -f gpu/third_party/imgui/imgui.h ||
          ! -f gpu/third_party/xbyak/xbyak/xbyak.h ]]; then
        git submodule update --init --recursive
    fi
    for patch in gpu/patches/fsr-vulkan/*.patch; do
        if ! git -C gpu/third_party/fsr-vulkan apply --ignore-whitespace --reverse --check "$PWD/$patch" 2>/dev/null; then
            git -C gpu/third_party/fsr-vulkan apply --ignore-whitespace "$PWD/$patch"
        fi
    done
    cmake -S gpu -B out/gpu -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBB_PGO=off \
        -DBB_LTO="${BB_LTO:-OFF}" -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ >/dev/null
    echo "GPU library and loader: LTO ${BB_LTO:-OFF}"
    if ! ninja -C out/gpu bb-probe bb-gpu-capabilities bb-play > out/gpu-build.log 2>&1; then
        grep -v '^\[' out/gpu-build.log | tail -40 >&2
        echo 'Build failed (full log: out/gpu-build.log)' >&2; exit 1
    fi
    cp out/gpu/bb-probe.exe out/gpu/bb-gpu-capabilities.exe out/gpu/bb-play.exe out/
    echo "Built $PWD/out/bb-probe.exe"
    exit 0
    ;;
esac
# macOS (Apple Silicon): the host is an x86_64 binary that runs under Rosetta 2 (docs/macos/README.md).
# Libraries come only from $BB_DEPS (scripts/macos/build-deps.sh) and the LunarG Vulkan SDK; the
# environment is set here because shell calls do not keep it. Build tools (cmake, ninja, pkg-config,
# nasm) may be arm64 Homebrew ones; nothing from /opt/homebrew is ever linked.
darwin=()      # extra cmake arguments
arch_flags=()  # added to every compile and link line
if [[ $(uname -s) == Darwin ]]; then
    export VULKAN_SDK=${VULKAN_SDK:-$HOME/VulkanSDK/1.4.363.0/macOS}
    export BB_DEPS=${BB_DEPS:-$HOME/.local/uncharted-x86_64}
    # LIBDIR, not PATH: no fallback to /opt/homebrew/lib/pkgconfig. The SDK provides vulkan.pc.
    export PKG_CONFIG_LIBDIR=$BB_DEPS/lib/pkgconfig:$VULKAN_SDK/lib/pkgconfig
    export CMAKE_PREFIX_PATH=$BB_DEPS
    export MACOSX_DEPLOYMENT_TARGET=15.4
    export PATH=$VULKAN_SDK/bin:$PATH   # glslang
    [[ -f $BB_DEPS/lib/pkgconfig/sdl3.pc ]] || { echo "Dependencies missing: run scripts/macos/build-deps.sh" >&2; exit 1; }
    CC=${CC:-clang}
    arch_flags=(-arch x86_64 -mmacosx-version-min=15.4)
    # PGO profiles are GCC-format; LTO starts off. Old CMake projects (sirit, FSR) need the policy floor.
    BB_PGO=${BB_PGO:-off}; BB_LTO=${BB_LTO:-OFF}
    darwin=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_OSX_ARCHITECTURES=x86_64
        -DCMAKE_OSX_DEPLOYMENT_TARGET=15.4 -DCMAKE_PREFIX_PATH="$BB_DEPS" -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -DCMAKE_IGNORE_PREFIX_PATH='/opt/homebrew;/usr/local' -DCMAKE_FIND_FRAMEWORK=LAST)
fi
if [[ -z ${CC:-} ]]; then
    CC=$(command -v cc || command -v gcc || true)
    if [[ -z $CC ]]; then
        for candidate in /nix/store/*-gcc-wrapper-*/bin/gcc; do
            if [[ -x $candidate ]]; then CC=$candidate; break; fi
        done
    fi
fi
if [[ -z ${CC:-} ]]; then echo 'Install GCC/Clang or set CC.' >&2; exit 1; fi
# Dependencies come from pkg-config (Vulkan loader/headers, SDL3). On NixOS the
# environment is provided by shell.nix; re-enter it automatically if needed.
if ! { command -v pkg-config >/dev/null && pkg-config --exists vulkan sdl3 && command -v cmake >/dev/null && command -v ninja >/dev/null; }; then
    if [[ -z ${BB_IN_NIX_SHELL:-} ]] && command -v nix-shell >/dev/null; then
        exec env BB_IN_NIX_SHELL=1 nix-shell shell.nix --run "bash build.sh $*"
    fi
    echo 'Need pkg-config with vulkan and sdl3, cmake and ninja (see shell.nix).' >&2; exit 1
fi
read -r -a includes <<< "$(pkg-config --cflags vulkan sdl3 libavcodec libavutil)"
# Newer Vulkan headers than distributions ship (third_party/Vulkan-Headers; the video core uses
# VK_VALVE_shader_mixed_float_dot_product and other recent names).
[[ -f third_party/Vulkan-Headers/include/vulkan/vulkan.h ]] || git submodule update --init third_party/Vulkan-Headers
vulkan_headers=$PWD/third_party/Vulkan-Headers/include
includes=(-I"$vulkan_headers" "${includes[@]}")
read -r -a libraries <<< "$(pkg-config --libs vulkan sdl3 libavcodec libavutil)"
# GPU library (shadPS4 video core + drivers), built by CMake into out/gpu/libbbgpu.so.
# BB_PGO: generate (instrumented build that writes pgo/ while the game runs), use, off.
# Default: use the profile in pgo/ when there is one. BB_LTO=OFF disables link-time optimization.
pgo=${BB_PGO:-}
if [[ -z $pgo ]]; then
    if [[ -n $(find pgo -name '*.gcda' -print -quit 2>/dev/null) ]]; then pgo=use; else pgo=off; fi
fi
mkdir -p pgo
# Submodules (git clone --recursive, or: git submodule update --init) and this port's changes
# to FSR-Vulkan (gpu/patches/fsr-vulkan), applied to its working tree once.
if [[ ! -f gpu/third_party/fsr-vulkan/CMakeLists.txt || ! -f gpu/third_party/imgui/imgui.h ]]; then
    git submodule update --init --recursive
fi
for patch in gpu/patches/fsr-vulkan/*.patch; do
    if ! git -C gpu/third_party/fsr-vulkan apply --ignore-whitespace --reverse --check "$PWD/$patch" 2>/dev/null; then
        git -C gpu/third_party/fsr-vulkan apply --ignore-whitespace "$PWD/$patch"
    fi
done
cmake -S gpu -B out/gpu -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBB_PGO="$pgo" \
    -DBB_LTO="${BB_LTO:-ON}" -DBB_PGO_DIR="$PWD/pgo" -DVulkan_INCLUDE_DIR="$vulkan_headers" \
    ${darwin[@]+"${darwin[@]}"} >/dev/null
echo "GPU library: PGO $pgo, LTO ${BB_LTO:-ON}"
# A failed GPU build must stop here: an older libbbgpu.so would otherwise be used silently.
if ! ninja ${BB_JOBS:+-j"$BB_JOBS"} -C out/gpu bbgpu > out/gpu-build.log 2>&1; then
    grep -v '^\[' out/gpu-build.log | tail -40 >&2
    echo 'GPU library build failed (full log: out/gpu-build.log)' >&2; exit 1
fi
# $ORIGIN/gpu: packaged copies keep the library next to the binary without patching it.
gpu=(-Lout/gpu -lbbgpu -Wl,-rpath,'$ORIGIN/gpu' -Wl,-rpath,"$PWD/out/gpu" -rdynamic)
# Mach-O: @loader_path/gpu; the executable exports its symbols to libbbgpu (runtime_*); non-PIE with a
# 4 KiB __PAGEZERO so the guest image can sit at 0x400000 and the low 2 GiB are usable (M2).
# Zydis is not described by pkg-config.
zydis=(-lZydis)
pie=(-no-pie)
cc=("$CC")
testpie=()
if [[ $(uname -s) == Darwin ]]; then
    gpu=(-Lout/gpu -lbbgpu -Wl,-rpath,@loader_path/gpu -Wl,-rpath,"$PWD/out/gpu" -Wl,-export_dynamic)
    zydis=(-L"$BB_DEPS/lib" -lZydis)
    pie=(-Wl,-no_pie -Wl,-pagezero_size,0x1000)
    testpie=("${pie[@]}") # tests that map the low 4 GiB need the small __PAGEZERO as well
    cc=("$CC" "${arch_flags[@]}")
    includes=(-I"$BB_DEPS/include" "${includes[@]}")
    libraries+=(-Wl,-rpath,"$VULKAN_SDK/lib") # @rpath/libvulkan.1.dylib for the test binaries too
    libraries+=(-framework CoreFoundation) # probe.c main(): CFRunLoop on the main thread (Cocoa)
fi
runtime=(src/runtime*.c)
# Third-party decoders: compiled once, without this project's -Werror policy.
atrac9=(third_party/LibAtrac9/C/src/*.c)
if [[ ! -f out/libatrac9.a || -n $(find third_party/LibAtrac9/C/src -newer out/libatrac9.a -name '*.c') ]]; then
    rm -rf out/atrac9 && mkdir -p out/atrac9
    for source in "${atrac9[@]}"; do "${cc[@]}" -std=c99 -O2 -g -w -c "$source" -o "out/atrac9/$(basename "${source%.c}").o"; done
    ar rcs out/libatrac9.a out/atrac9/*.o
fi
"${cc[@]}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${pie[@]}" "${includes[@]}" -I. -Isrc src/probe.c src/cpu_patch.c "${runtime[@]}" src/vulkan_smoke.c out/libatrac9.a -lm "${zydis[@]}" "${gpu[@]}" "${libraries[@]}" -o out/bb-probe
echo "Built $PWD/out/bb-probe"
# GPU check for run.sh (live_resolution=auto): links only the Vulkan loader.
"${cc[@]}" -std=c11 -O2 -Wall -Wextra -Werror "${includes[@]}" tools/gpu_capabilities.c "${libraries[@]}" -o out/bb-gpu-capabilities
if [[ ${1:-} == --test ]]; then
    "${cc[@]}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${includes[@]}" -I. -Isrc tests/test_pad.c "${libraries[@]}" -o out/pad-test
    out/pad-test
    "${cc[@]}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread ${testpie[@]+"${testpie[@]}"} "${includes[@]}" -I. -Isrc tests/test_runtime.c "${runtime[@]}" out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o out/runtime-test
    out/runtime-test
    "${cc[@]}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${includes[@]}" -Isrc tests/test_file_mods.c -o out/file-mods-test
    out/file-mods-test
    "${cc[@]}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread ${testpie[@]+"${testpie[@]}"} "${includes[@]}" -I. -Isrc tests/test_sema.c "${runtime[@]}" out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o out/sema-test
    out/sema-test
    "${cc[@]}" -std=c11 -D_GNU_SOURCE -O2 -g -Wall -Wextra -Werror "${includes[@]}" -I. -Isrc tests/test_content.c src/runtime_content.c -o out/content-test
    out/content-test
    "${cc[@]}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread ${testpie[@]+"${testpie[@]}"} "${includes[@]}" -Isrc tests/test_cpu_patch.c src/cpu_patch.c "${zydis[@]}" -o out/cpu-patch-test
    out/cpu-patch-test
    if [[ $(uname -s) == Darwin ]]; then
        # Rosetta 2: AVX/AVX2/FMA/BMI1/LZCNT/MOVBE/POPCNT must execute; the guest address space must be reservable.
        "${cc[@]}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread tests/test_avx_rosetta.c -o out/avx-rosetta-test
        out/avx-rosetta-test
        "${cc[@]}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${testpie[@]}" "${includes[@]}" -I. -Isrc tests/test_memory_darwin.c "${runtime[@]}" out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o out/memory-darwin-test
        out/memory-darwin-test
        "${cc[@]}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${testpie[@]}" "${includes[@]}" -I. -Isrc tests/test_lowalloc_darwin.c "${runtime[@]}" out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o out/lowalloc-darwin-test
        out/lowalloc-darwin-test
        "${cc[@]}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${testpie[@]}" "${includes[@]}" -I. -Isrc tests/test_tls_darwin.c "${runtime[@]}" out/libatrac9.a -lm "${gpu[@]}" "${libraries[@]}" -o out/tls-darwin-test
        out/tls-darwin-test
        # test_signals_darwin.c includes src/probe.c (fault() and watchdog() as shipped): linked like bb-probe.
        "${cc[@]}" -std=c11 -O2 -g -Wall -Wextra -Werror -pthread "${testpie[@]}" "${includes[@]}" -I. -Isrc tests/test_signals_darwin.c src/cpu_patch.c "${runtime[@]}" src/vulkan_smoke.c out/libatrac9.a -lm "${zydis[@]}" "${gpu[@]}" "${libraries[@]}" -o out/signals-darwin-test
        out/signals-darwin-test
    fi
fi
