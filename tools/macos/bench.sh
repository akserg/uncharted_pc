#!/usr/bin/env bash
# macOS benchmark (M5b): U1 "Continue" checkpoint, then the owner's recorded pad route (user/bench-route.pad,
# BB_PAD_REPLAY) with BB_PERF_LOG=1. One run writes out/bench/<label>-<n>.txt: mean fps, frame-time p50/p99,
# GPU-command-thread CPU and peak footprint over the replay window only. The game is quit afterwards.
#
#   tools/macos/bench.sh <label> [runs=1]
#
# Environment (all optional):
#   BB_GAME_DIR          game dump (default /Users/akserg/Projects/CUSA02344)
#   BENCH_BIN            directory with bb-probe and gpu/libbbgpu.dylib (default <repo>/out); lets variants
#                        built earlier keep their binaries (out/variants/<name>)
#   BENCH_ROUTE          pad recording (default user/bench-route.pad)
#   BENCH_FRAME_AT       replay second of the reference frame dump (default 60; 0 = none)
#   BENCH_SAMPLE_AT      replay second at which `sample <pid> 10` runs (default off); the report goes to
#                        BENCH_SAMPLE_FILE (default out/bench/<label>-<n>.sample.txt)
#   BENCH_ALLOW_BUSY=1   run although another bb-probe is running (exploration only: the numbers are not valid)
# A measured run refuses to start next to another bb-probe (waits, re-checking every 60 s); the Unity Editor state
# is recorded in the result file. BB_PAD_FILE makes the game ignore the host controller (BB_PAD_HOST unset).
#   any BB_* variable is passed on to the game (settings sweeps).
# The save data of user/ is restored from a snapshot before every run (taken on the first run), so every run
# starts from the same checkpoint.
set -uo pipefail
export LC_ALL=C
PORT=$(cd -- "$(dirname -- "$0")/../.." && pwd)
label=${1:?usage: bench.sh <label> [runs]}
runs=${2:-1}
BIN=${BENCH_BIN:-$PORT/out}
GAME=${BB_GAME_DIR:-/Users/akserg/Projects/CUSA02344}
ROUTE=${BENCH_ROUTE:-$PORT/user/bench-route.pad}
USER_DIR=$PORT/user
OUT=$PORT/out/bench
mkdir -p "$OUT"
[[ -x $BIN/bb-probe && -f $BIN/gpu/libbbgpu.dylib || -x $BIN/bb-probe && $BIN == "$PORT/out" ]] || { echo "no bb-probe in $BIN" >&2; exit 1; }
[[ -f $ROUTE ]] || { echo "no pad recording $ROUTE" >&2; exit 1; }
route_ms=$(awk 'END {print $1}' "$ROUTE")

# Same environment as run.sh.
export ROSETTA_ADVERTISE_AVX=1 BB_GPU_ID=${BB_GPU_ID:-0} BB_PERF_LOG=1
export BB_CONFIG=${BB_CONFIG:-$PORT/bbport.ini} BB_INPUT_CONFIG=${BB_INPUT_CONFIG:-$PORT/input.ini}
export BB_GPU_USER_DIR=$USER_DIR BB_VBLANK_HZ=${BB_VBLANK_HZ:-60}
export BB_VELOCITY_PS=${BB_VELOCITY_PS:-f4b18264} BB_UPSCALE_BEFORE_CS=${BB_UPSCALE_BEFORE_CS:-aec539f0}
export BB_HUDLESS_PS=${BB_HUDLESS_PS:-484cdbf5} BB_LOADEXEC_FILE=$PORT/out/loadexec.txt
export BB_PAD_REPLAY=$ROUTE

if [[ ! -d $OUT/save-snapshot ]]; then
    cp -R "$USER_DIR/savedata" "$OUT/save-snapshot"
    echo "save data snapshot taken: $OUT/save-snapshot"
fi

now() { python3 -c 'import time; print(time.time())'; }
lines() { wc -l < "$1" | tr -d ' '; }
press() { # press <tokens> : hold 0.3 s, release
    echo "$1" > "$PAD"; sleep 0.3; : > "$PAD"
}

unity_cpu() { ps -axo pcpu,command | awk '/Unity\.app\/Contents\/MacOS\/Unity( |$)/ && !/awk/ {cpu+=$1} END {printf "%.0f", cpu}'; }
good=0
while (( good < runs )); do
    n=1; while [[ -e $OUT/$label-$n.txt || -e $OUT/$label-$n.invalid.txt ]]; do ((n++)); done
    tag=$OUT/$label-$n
    PAD=$tag.pad; LOG=$tag.log; FRAMES=$tag.frames; DUMP=$tag.dump
    : > "$PAD"; rm -rf "$DUMP"; mkdir -p "$DUMP"; rm -f "$FRAMES"
    # No measured run next to another game copy: any bb-probe (the owner's included) makes us wait, re-checked every 60 s.
    if [[ ${BENCH_ALLOW_BUSY:-0} != 1 ]]; then
        while [[ -n $(pgrep -f bb-probe) ]]; do
            echo "$(date +%H:%M:%S) another bb-probe is running, waiting 60 s:"; pgrep -fl bb-probe | cut -c1-100
            sleep 60
        done
    fi
    # Unity Editor: a busy one (compiling, > 100% CPU) delays the run too, re-checked every 60 s; its state is recorded.
    if [[ ${BENCH_ALLOW_BUSY:-0} != 1 ]]; then
        while (( $(unity_cpu) > 100 )); do echo "$(date +%H:%M:%S) Unity Editor busy ($(unity_cpu)% CPU), waiting 60 s"; sleep 60; done
    fi
    unity_state=$(ps -axo pid,pcpu,comm | awk '/Unity\.app\/Contents\/MacOS\/Unity( |$)/ && !/awk/ {c++; cpu+=$2} END {printf "%d process(es), %.0f%% CPU", c, cpu}')
    load_before=$(uptime | sed 's/.*load averages*: //')
    rsync -a --delete "$OUT/save-snapshot/" "$USER_DIR/savedata/"
    echo "== $label run $n: $BIN"
    export BB_PAD_FILE=$PAD BB_PERF_FRAMES=$FRAMES BB_FRAME_DUMP_TRIGGER=$tag.trigger BB_DUMP_DIR=$DUMP
    "$BIN/bb-probe" "$PORT/out/eboot/boot-linked.bin" --content-profile "$PORT/out/eboot/content.bin" \
        --app0 "$GAME" --user "$USER_DIR" --timeout 0 > "$LOG" 2>&1 < /dev/null &
    pid=$!
    start=$(now)
    # Contention monitor for the whole run: a busy Unity Editor (> 100% CPU) or a second bb-probe makes it invalid.
    ( while kill -0 $pid 2>/dev/null; do
          echo "$(unity_cpu) $(pgrep -f 'bb-probe .*boot-linked' | grep -vc "^$pid$")" >> "$tag.mon"; sleep 3
      done ) &
    monpid=$!
    elapsed() { python3 -c "import time; print(int(time.time()-$start))"; }
    # Menus: title ("Press X to Begin") appears ~65 s after launch; Continue, Begin, Continue, Friend Stats OK.
    while (( $(elapsed) < 85 )); do kill -0 $pid 2>/dev/null || break; sleep 1; done
    for gap in 12 12 12 18; do press cross; sleep "$gap"; done
    press cross; sleep 40
    if ! kill -0 $pid 2>/dev/null; then echo "game exited before the replay"; tail -5 "$LOG"; exit 1; fi
    touch "$tag.trigger"; sleep 3; raw=$(ls "$DUMP"/display_*.raw 2>/dev/null | head -1)
    [[ -n $raw ]] && python3 "$PORT/tools/macos/raw2png.py" "$raw" "$tag-start.png" --half; rm -f "$DUMP"/*.raw
    first=$(lines "$FRAMES")
    t_replay=$(now)
    echo replay > "$PAD"
    until grep -aq "pad replay of" "$LOG" 2>/dev/null; do sleep 0.2; done
    grep -a "pad replay of" "$LOG"
    framed=0; sampled=0
    until grep -aq "pad replay finished" "$LOG"; do
        kill -0 $pid 2>/dev/null || { echo "game exited during the replay"; break; }
        at=$(python3 -c "print(int(time.time()-$t_replay))" 2>/dev/null || python3 -c "import time; print(int(time.time()-$t_replay))")
        if (( ${BENCH_FRAME_AT:-60} > 0 && framed == 0 && at >= ${BENCH_FRAME_AT:-60} )); then touch "$tag.trigger"; framed=1; fi
        if [[ -n ${BENCH_SAMPLE_AT:-} ]] && (( sampled == 0 && at >= BENCH_SAMPLE_AT )); then
            sampled=1
            sample $pid 10 -file "${BENCH_SAMPLE_FILE:-$tag.sample.txt}" > "$tag.sample.out" 2>&1
        fi
        if (( at > route_ms / 1000 + 90 )); then echo "replay did not finish"; break; fi
        sleep 0.5
    done
    last=$(lines "$FRAMES")
    # the reference frame (guest display buffer, 1920x1080)
    raw=$(ls "$DUMP"/display_*.raw 2>/dev/null | head -1)
    if [[ -n $raw ]]; then
        python3 "$PORT/tools/macos/raw2png.py" "$raw" "$tag.png" --half
        [[ -f $OUT/reference-frame.raw ]] || cp "$raw" "$OUT/reference-frame.raw"
        python3 "$PORT/tools/macos/imgdiff.py" "$OUT/reference-frame.raw" "$raw" > "$tag.imgdiff" 2>&1
    fi
    kill -TERM $pid 2>/dev/null; wait $pid 2>/dev/null; wait $monpid 2>/dev/null
    rm -rf "$DUMP" "$tag.trigger"
    {
        echo "label=$label run=$n"
        echo "bin=$BIN"
        echo "date=$(date '+%Y-%m-%d %H:%M:%S')"
        echo "unity=$unity_state (at start; load avg $load_before)"
        pgrep -f bb-probe | wc -l | awk '{print "bb_probe_processes_at_end=" $1}'
        python3 "$PORT/tools/macos/bench_report.py" "$FRAMES" "$LOG" "$first" "$last"
        [[ -f $tag.imgdiff ]] && echo "frame_vs_reference: $(cat "$tag.imgdiff")"
        awk '{if ($1 > u) u = $1; if ($2 > p) p = $2} END {printf "max_unity_cpu_pct=%d max_other_bb_probe=%d\n", u, p}' "$tag.mon"
    } > "$tag.txt"
    read -r max_unity max_other < <(awk '{if ($1 > u) u = $1; if ($2 > p) p = $2} END {print u + 0, p + 0}' "$tag.mon")
    if [[ ${BENCH_ALLOW_BUSY:-0} != 1 ]] && (( max_unity > 100 || max_other > 0 )); then
        mv "$tag.txt" "$tag.invalid.txt"
        echo "run $n INVALID (Unity up to ${max_unity}% CPU, other bb-probe: $max_other): redone"
        (( good == 0 )) && rm -f "$OUT/reference-frame.raw"
    else
        cat "$tag.txt"; ((good++))
    fi
done
