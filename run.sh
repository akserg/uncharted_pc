#!/usr/bin/env bash
# Adaptação Linux do run.py do uncharted_pc (Windows)

PORT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GAME="${BB_GAME_DIR:?Defina BB_GAME_DIR com a pasta do jogo (com eboot.bin)}"
EXE_NAME="${BB_EXE:-u1}"

case "$EXE_NAME" in
    u1) EXE="eboot.bin" ;;
    u2) EXE="big2-ps4_Shipping.elf" ;;
    u3) EXE="big3-ps4_Shipping.elf" ;;
    *)  EXE="$EXE_NAME" ;;
esac

USER_DIR="${BB_USER_DIR:-$PORT/user}"
OUT_ROOT="$PORT/out"
OUT="$OUT_ROOT/${EXE%.*}"
PROBE="$PORT/out/bb-probe"

if [ ! -f "$GAME/eboot.bin" ]; then
    echo "ERRO: não achei eboot.bin em $GAME" >&2
    exit 1
fi
if [ ! -x "$PROBE" ]; then
    echo "ERRO: bb-probe não encontrado em $PROBE" >&2
    exit 1
fi

mkdir -p "$OUT" "$USER_DIR"

if [ "$(uname -s)" = Darwin ]; then
    # macOS: bb-probe is an x86_64 binary under Rosetta 2; without this cpuid hides AVX and bb-probe stops
    # (Rosetta translates the instructions regardless; docs/macos/M2-memory.md).
    export ROSETTA_ADVERTISE_AVX=1

    # The same default as build.sh: the LunarG SDK for macOS. Needed at run time for libvulkan (the loader).
    export VULKAN_SDK="${VULKAN_SDK:-$HOME/VulkanSDK/1.4.363.0/macOS}"
    if [ ! -d "$VULKAN_SDK" ]; then
        echo "ERROR: Vulkan SDK not found at $VULKAN_SDK" >&2
        echo "Install the LunarG Vulkan SDK 1.4.363.0 for macOS in ~/VulkanSDK, or set VULKAN_SDK to its macOS folder" >&2
        echo "(e.g. VULKAN_SDK=/path/to/VulkanSDK/<version>/macOS ./run.sh). See docs/MACOS_BUILD.md." >&2
        exit 1
    fi

    # The Vulkan loader finds no driver on its own ("ErrorIncompatibleDriver"): the SDK's setup-env.sh points it
    # at the SDK's ICD folder. Point it at MoltenVK only (the folder also has KosmicKrisp, which is arm64-only
    # and cannot load into this x86_64 process). VK_ICD_FILENAMES is the name older loaders read. Neither is
    # set if the caller already chose a driver with either variable.
    # Not set, because the game does not need them: DYLD_LIBRARY_PATH (bb-probe and libbbgpu carry an rpath to
    # $VULKAN_SDK/lib from build time), VK_ADD_LAYER_PATH (only for validation layers, which stay off) and PATH.
    MVK_ICD="$VULKAN_SDK/share/vulkan/icd.d/MoltenVK_icd.json"
    if [ -z "${VK_DRIVER_FILES:-}" ] && [ -z "${VK_ICD_FILENAMES:-}" ]; then
        if [ ! -f "$MVK_ICD" ]; then
            echo "ERROR: MoltenVK driver file not found: $MVK_ICD" >&2
            echo "The Vulkan SDK at $VULKAN_SDK looks incomplete; reinstall it, or set VK_DRIVER_FILES to a Vulkan driver (.json)." >&2
            exit 1
        fi
        export VK_DRIVER_FILES="$MVK_ICD"
        export VK_ICD_FILENAMES="$MVK_ICD"
        ICD_STATE=found
    else
        ICD_STATE="set by caller (${VK_DRIVER_FILES:-$VK_ICD_FILENAMES})"
    fi

    # MoltenVK is GPU 0; KosmicKrisp (arm64-only) is not usable under Rosetta.
    export BB_GPU_ID="${BB_GPU_ID:-0}"
    echo "macOS: VULKAN_SDK=$VULKAN_SDK, MoltenVK ICD $ICD_STATE, BB_GPU_ID=$BB_GPU_ID"
fi
export PYTHONUTF8=1
export PYTHONIOENCODING=utf-8
export BB_CONFIG="${BB_CONFIG:-$PORT/bbport.ini}"
export BB_INPUT_CONFIG="${BB_INPUT_CONFIG:-$PORT/input.ini}"
export BB_GPU_USER_DIR="$USER_DIR"
export BB_VBLANK_HZ="${BB_VBLANK_HZ:-60}"
export BB_VELOCITY_PS="${BB_VELOCITY_PS:-f4b18264}"
export BB_UPSCALE_BEFORE_CS="${BB_UPSCALE_BEFORE_CS:-aec539f0}"
export BB_HUDLESS_PS="${BB_HUDLESS_PS:-484cdbf5}"
export BB_LOADEXEC_FILE="$OUT_ROOT/loadexec.txt"

prepare_all() {
    echo "Preparando $EXE ..."
    python3 "$PORT/scripts/prepare.py" "$GAME" --out "$OUT" --exe "$EXE"
    python3 "$PORT/scripts/link_modules.py" "$GAME" --out "$OUT" --exe "$EXE"
    python3 "$PORT/scripts/content_profile.py" "$GAME" --out "$OUT" --sku "${BB_CONTENT_SKU:-full}"
}

prepare_all

# macOS: the probe output is also written to $USER_DIR/last_run.log (one file per run.sh call,
# appended across loadexec switches).
LAST_RUN_LOG="$USER_DIR/last_run.log"
[ "$(uname -s)" = Darwin ] && : > "$LAST_RUN_LOG"

while true; do
    rm -f "$BB_LOADEXEC_FILE"
    if [ "$(uname -s)" = Darwin ]; then
        "$PROBE" "$OUT/boot-linked.bin" \
            --content-profile "$OUT/content.bin" \
            --app0 "$GAME" \
            --user "$USER_DIR" \
            --timeout "${BB_TIMEOUT:-0}" \
            "$@" 2>&1 | tee -a "$LAST_RUN_LOG"
        code=${PIPESTATUS[0]}
    else
        "$PROBE" "$OUT/boot-linked.bin" \
            --content-profile "$OUT/content.bin" \
            --app0 "$GAME" \
            --user "$USER_DIR" \
            --timeout "${BB_TIMEOUT:-0}" \
            "$@"
        code=$?
    fi

    if [ "$code" -ne 101 ] || [ ! -f "$BB_LOADEXEC_FILE" ]; then
        exit "$code"
    fi

    NEW_EXE=$(basename "$(head -n 1 "$BB_LOADEXEC_FILE" | tr -d '\r')")
    if [ -z "$NEW_EXE" ]; then
        echo "ERRO: loadexec.txt vazio" >&2
        exit 1
    fi
    echo ">>> Trocando para $NEW_EXE"
    EXE="$NEW_EXE"
    OUT="$OUT_ROOT/${EXE%.*}"
    mkdir -p "$OUT"
    prepare_all
done
