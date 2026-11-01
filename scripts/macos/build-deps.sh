#!/usr/bin/env bash
# Builds the third-party libraries uncharted_pc needs as x86_64 macOS binaries (they run under
# Rosetta 2) into $BB_DEPS. Idempotent: a library whose marker file already exists is skipped.
# Build tools (cmake, ninja, pkg-config, nasm) may come from arm64 Homebrew; no Homebrew library
# is ever linked. Vulkan loader/MoltenVK/glslang come from the LunarG SDK, not from here.
# See docs/macos/README.md "Toolchain" and docs/macos/M1-host-compiles.md.
set -euo pipefail

export VULKAN_SDK=${VULKAN_SDK:-$HOME/VulkanSDK/1.4.363.0/macOS}
export BB_DEPS=${BB_DEPS:-$HOME/.local/uncharted-x86_64}
# LIBDIR (not PATH): never fall back to /opt/homebrew/lib/pkgconfig.
export PKG_CONFIG_LIBDIR=$BB_DEPS/lib/pkgconfig
export CMAKE_PREFIX_PATH=$BB_DEPS
export MACOSX_DEPLOYMENT_TARGET=15.4
JOBS=${BB_JOBS:-$(sysctl -n hw.ncpu)}

[[ $(uname -s) == Darwin ]] || { echo 'macOS only.' >&2; exit 1; }
for tool in cmake ninja pkg-config nasm curl tar git make; do
    command -v "$tool" >/dev/null || { echo "Need $tool (build tool, arm64 Homebrew is fine)." >&2; exit 1; }
done

SRC=$BB_DEPS/src
mkdir -p "$SRC" "$BB_DEPS/lib" "$BB_DEPS/include"

# Common CMake flags: x86_64 only, Rosetta-era deployment target, absolute install names so the
# dylibs resolve without any rpath (the rpaths of bb-probe/libbbgpu stay minimal), and a policy
# floor so old projects (protobuf 3.21, fmt 9) configure with CMake 4.
cmake_flags=(-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.4 -DCMAKE_INSTALL_PREFIX="$BB_DEPS"
    -DCMAKE_INSTALL_NAME_DIR="$BB_DEPS/lib" -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_PREFIX_PATH="$BB_DEPS"
    -DCMAKE_IGNORE_PREFIX_PATH='/opt/homebrew;/usr/local')
arch_flags=(-arch x86_64 -mmacosx-version-min=15.4)

step() { echo; echo "=== $* ($(date +%H:%M:%S))"; }

# fetch <name> <url>: downloads and extracts into $SRC/<name> (strip one directory level).
fetch() {
    local name=$1 url=$2 dir=$SRC/$1
    if [[ ! -d $dir ]]; then
        local tmp=$SRC/$name.download
        rm -rf "$tmp" && mkdir -p "$tmp"
        curl -fL --retry 3 -o "$tmp/archive" "$url"
        mkdir "$dir" && tar -xf "$tmp/archive" -C "$dir" --strip-components=1
        rm -rf "$tmp"
    fi
}

# cmake_lib <name> <source subdir> [extra cmake args...]
cmake_lib() {
    local name=$1 sub=$2; shift 2
    cmake -S "$SRC/$name/$sub" -B "$SRC/$name/build-x86_64" "${cmake_flags[@]}" "$@"
    cmake --build "$SRC/$name/build-x86_64" -j "$JOBS"
    cmake --install "$SRC/$name/build-x86_64"
}

# --- SDL3 3.4.18 (shared) -------------------------------------------------------------------
if [[ ! -f $BB_DEPS/lib/pkgconfig/sdl3.pc ]]; then
    step SDL3 3.4.18
    fetch SDL3 https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    cmake_lib SDL3 . -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_INSTALL_TESTS=OFF \
        -DSDL_EXAMPLES=OFF -DSDL_TEST_LIBRARY=OFF
fi

# --- fmt 9.1.0 (static, PIC) ----------------------------------------------------------------
if [[ ! -f $BB_DEPS/lib/cmake/fmt/fmt-config.cmake ]]; then
    step fmt 9.1.0
    fetch fmt https://github.com/fmtlib/fmt/archive/refs/tags/9.1.0.tar.gz
    cmake_lib fmt . -DBUILD_SHARED_LIBS=OFF -DFMT_TEST=OFF -DFMT_DOC=OFF -DFMT_INSTALL=ON
fi

# --- Zydis 4.0.0 + Zycore (shared; bb-probe links -lZydis). Zydis only installs itself, so the
# Zycore submodule (headers, find_package(Zycore)) is built and installed first, as its own project.
if [[ ! -f $BB_DEPS/lib/cmake/zycore/zycore-config.cmake || ! -f $BB_DEPS/lib/libZydis.dylib ]]; then
    step Zycore+Zydis v4.0.0
    if [[ ! -d $SRC/zydis ]]; then
        git clone --quiet --depth 1 --branch v4.0.0 --recurse-submodules --shallow-submodules \
            https://github.com/zyantific/zydis.git "$SRC/zydis"
    fi
    rm -rf "$SRC/zydis/build-x86_64" "$SRC/zydis/build-zycore-x86_64"
    cmake -S "$SRC/zydis/dependencies/zycore" -B "$SRC/zydis/build-zycore-x86_64" "${cmake_flags[@]}" \
        -DZYCORE_BUILD_SHARED_LIB=ON -DZYCORE_BUILD_EXAMPLES=OFF -DZYCORE_BUILD_TESTS=OFF
    cmake --build "$SRC/zydis/build-zycore-x86_64" -j "$JOBS"
    cmake --install "$SRC/zydis/build-zycore-x86_64"
    # With a system Zycore, Zydis links the bare name "Zycore" (no imported target): give the
    # compiler and linker the prefix explicitly.
    cmake_lib zydis . -DCMAKE_C_FLAGS="-I$BB_DEPS/include" -DCMAKE_SHARED_LINKER_FLAGS="-L$BB_DEPS/lib" \
        -DZYAN_SYSTEM_ZYCORE=ON -DZYDIS_BUILD_SHARED_LIB=ON -DZYDIS_BUILD_EXAMPLES=OFF \
        -DZYDIS_BUILD_TOOLS=OFF -DZYDIS_BUILD_DOXYGEN=OFF
fi

# --- xxHash 0.8.2 (static only) -------------------------------------------------------------
if [[ ! -f $BB_DEPS/lib/pkgconfig/libxxhash.pc ]]; then
    step xxHash v0.8.2
    fetch xxhash https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.2.tar.gz
    make -C "$SRC/xxhash" clean >/dev/null
    # Static archive only (nothing needs the dylib, and its Makefile rule rejects -arch flags).
    make -C "$SRC/xxhash" -j "$JOBS" CC=clang CFLAGS="-O3 -fPIC ${arch_flags[*]}" \
        LDFLAGS="${arch_flags[*]}" PREFIX="$BB_DEPS" libxxhash.a libxxhash.pc
    mkdir -p "$BB_DEPS/lib/pkgconfig"
    cp "$SRC/xxhash/libxxhash.a" "$BB_DEPS/lib/"
    cp "$SRC/xxhash/xxhash.h" "$SRC/xxhash/xxh3.h" "$BB_DEPS/include/"
    cp "$SRC/xxhash/libxxhash.pc" "$BB_DEPS/lib/pkgconfig/"
fi

# --- miniupnpc 2.2.6 (static; gpu/CMakeLists.txt prefers libminiupnpc.a) ----------------------
if [[ ! -f $BB_DEPS/lib/libminiupnpc.a ]]; then
    step miniupnpc 2.2.6
    fetch miniupnp https://github.com/miniupnp/miniupnp/archive/refs/tags/miniupnpc_2_2_6.tar.gz
    cmake_lib miniupnp miniupnpc -DUPNPC_BUILD_SHARED=OFF -DUPNPC_BUILD_STATIC=ON \
        -DUPNPC_BUILD_TESTS=OFF -DUPNPC_BUILD_SAMPLE=OFF -DUPNPC_NO_INSTALL=OFF
fi

# --- OpenSSL 3.0.15 (shared; zlib comes from the macOS SDK) ---------------------------------
if [[ ! -f $BB_DEPS/lib/libssl.dylib ]]; then
    step OpenSSL 3.0.15
    fetch openssl https://github.com/openssl/openssl/releases/download/openssl-3.0.15/openssl-3.0.15.tar.gz
    ( cd "$SRC/openssl"
      ./Configure darwin64-x86_64-cc shared no-tests --prefix="$BB_DEPS" \
          --openssldir="$BB_DEPS/ssl" --libdir=lib -mmacosx-version-min=15.4
      make -j "$JOBS" build_sw
      make install_sw )
fi

# --- protobuf 3.21.12 (v21.12; newer needs abseil). protoc is x86_64 and runs under Rosetta. --
if [[ ! -f $BB_DEPS/lib/cmake/protobuf/protobuf-config.cmake ]]; then
    step protobuf v21.12
    fetch protobuf https://github.com/protocolbuffers/protobuf/releases/download/v21.12/protobuf-cpp-3.21.12.tar.gz
    cmake_lib protobuf cmake -Dprotobuf_BUILD_TESTS=OFF -Dprotobuf_BUILD_SHARED_LIBS=OFF \
        -Dprotobuf_BUILD_EXAMPLES=OFF -Dprotobuf_WITH_ZLIB=ON -Dprotobuf_MSVC_STATIC_RUNTIME=OFF
fi

# --- FFmpeg 6.1.1 (shared; avformat avcodec avutil swscale swresample) -----------------------
if [[ ! -f $BB_DEPS/lib/pkgconfig/libavcodec.pc ]]; then
    step FFmpeg 6.1.1
    fetch ffmpeg https://ffmpeg.org/releases/ffmpeg-6.1.1.tar.xz
    # Decoders are left at their defaults: avplayer opens arbitrary streams (codec chosen from the
    # stream), videodec needs H.264/HEVC, runtime_ajm.c needs MP3/AAC.
    ( cd "$SRC/ffmpeg"
      ./configure --prefix="$BB_DEPS" --arch=x86_64 --target-os=darwin --enable-cross-compile \
          --cc="clang -arch x86_64" --cxx="clang++ -arch x86_64" --x86asmexe="$(command -v nasm)" \
          --extra-cflags="${arch_flags[*]}" --extra-ldflags="${arch_flags[*]}" \
          --install-name-dir="$BB_DEPS/lib" --enable-shared --disable-static --disable-programs \
          --disable-doc --disable-network --disable-autodetect --enable-zlib --disable-iconv \
          --disable-avdevice --disable-avfilter --enable-pic
      make -j "$JOBS"
      make install )
fi

# --- Header-only: Boost 1.83.0 (headers), robin-map 1.2.1, VMA 3.0.1, nlohmann_json 3.11.3 ----
if [[ ! -f $BB_DEPS/include/boost/version.hpp ]]; then
    step Boost 1.83.0 headers
    fetch boost https://archives.boost.io/release/1.83.0/source/boost_1_83_0.tar.gz
    cp -R "$SRC/boost/boost" "$BB_DEPS/include/boost"
fi
if [[ ! -d $BB_DEPS/share/cmake/tsl-robin-map ]]; then
    step robin-map v1.2.1
    fetch robin-map https://github.com/Tessil/robin-map/archive/refs/tags/v1.2.1.tar.gz
    cmake_lib robin-map . -DBUILD_TESTING=OFF
fi
if [[ ! -f $BB_DEPS/include/vma/vk_mem_alloc.h ]]; then
    step VulkanMemoryAllocator v3.0.1
    fetch vma https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.0.1.tar.gz
    # gpu/CMakeLists.txt falls back to find_path(vk_mem_alloc.h PATH_SUFFIXES vma).
    mkdir -p "$BB_DEPS/include/vma"
    cp "$SRC/vma/include/vk_mem_alloc.h" "$BB_DEPS/include/vma/"
fi
if [[ ! -f $BB_DEPS/share/cmake/nlohmann_json/nlohmann_jsonConfig.cmake ]]; then
    step nlohmann_json v3.11.3
    fetch nlohmann_json https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
    cmake_lib nlohmann_json . -DJSON_BuildTests=OFF -DJSON_Install=ON
fi

# --- Verification ---------------------------------------------------------------------------
step "Verifying architectures"
bad=0
while IFS= read -r f; do
    archs=$(lipo -archs "$f" 2>/dev/null || true)
    [[ -z $archs ]] && continue
    printf '%-60s %s\n' "${f#"$BB_DEPS"/}" "$archs"
    [[ $archs == x86_64 ]] || bad=1
done < <(find "$BB_DEPS/lib" "$BB_DEPS/bin" -type f \( -name '*.dylib' -o -name '*.a' -o -perm -u+x \) 2>/dev/null)
[[ $bad == 0 ]] || { echo 'Non-x86_64 artefact found in BB_DEPS.' >&2; exit 1; }
echo "Dependencies ready in $BB_DEPS"
