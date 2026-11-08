#!/usr/bin/env python3
"""bench_report.py <frames.txt> <run.log> <first_frame_index> <last_frame_index> [label]

Statistics of one benchmark replay (tools/macos/bench.sh). frames.txt is BB_PERF_FRAMES output
("<t s> <frame ms>" per guest flip); the replay window is the flips with index in [first, last).
PERF lines of run.log whose whole 5 s window lies inside the replay give GPU-thread CPU and footprint.
Prints key=value lines."""
import re, sys

def pct(sorted_values, p):
    return sorted_values[min(len(sorted_values) - 1, int(p * (len(sorted_values) - 1) + 0.5))]

def main():
    frames_path, log_path, first, last = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
    rows = [tuple(map(float, line.split())) for line in open(frames_path) if line.strip()]
    window = rows[first:last]
    if len(window) < 10:
        sys.exit(f'only {len(window)} frames in the replay window')
    t0, t1 = window[0][0], window[-1][0]
    ft = sorted(r[1] for r in window)
    duration = t1 - t0
    out = {
        'frames': len(window), 'replay_s': round(duration, 2),
        'mean_fps': round((len(window) - 1) / duration, 2),
        'ft_mean_ms': round(sum(ft) / len(ft), 2),
        'ft_p50_ms': round(pct(ft, 0.50), 2), 'ft_p99_ms': round(pct(ft, 0.99), 2),
        'ft_p999_ms': round(pct(ft, 0.999), 2),
        'frames_over_20ms_pct': round(100.0 * sum(1 for v in ft if v > 20.0) / len(ft), 2),
    }
    cpu, foot, n = 0.0, 0.0, 0
    pat = re.compile(r'^PERF: t=(\d+) .*gpu_thread_cpu=([-\d.]+)% footprint_mb=(\d+)')
    for line in open(log_path, errors='replace'):
        m = pat.match(line)
        if m and t0 + 5.5 <= float(m.group(1)) <= t1 + 0.5:
            cpu += float(m.group(2)); foot = max(foot, float(m.group(3))); n += 1
    out['perf_windows'] = n
    out['gpu_thread_cpu_pct'] = round(cpu / n, 1) if n else -1
    out['footprint_peak_mb'] = int(foot)
    for k, v in out.items():
        print(f'{k}={v}')

main()
