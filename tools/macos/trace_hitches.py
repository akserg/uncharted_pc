#!/usr/bin/env python3
"""trace_hitches.py <frames-trace.txt> [more files]: hitch analysis of BB_FRAME_TRACE output (gpu/shim/bbport_frame_trace.h).
Per file: frames in the replay window (replay_s >= 0), the frames over 25 ms (a missed vblank), their slot sums vs normal
frames, the share of hitch frames in which each slot is large, and the hitch count per 10 s of replay time."""
import sys
from collections import Counter

names = ['drain', 'rec', 'submit', 'present', 'pipe']
for path in sys.argv[1:]:
    rows = []
    for line in open(path):
        p = line.replace('|', ' ').split()
        if len(p) < 14: continue
        v = list(map(float, p))
        if v[2] < 0: continue
        rows.append({'n': int(v[0]), 'rt': v[2], 'ft': v[3], 'tot': v[4:14:2], 'max': v[5:14:2]})
    hit = [r for r in rows if r['ft'] > 25]
    ok = [r for r in rows if r['ft'] <= 20]
    print(f'== {path}: {len(rows)} frames, {len(hit)} over 25 ms ({100*len(hit)/len(rows):.2f} %), '
          f'{sum(1 for r in rows if r["ft"] > 20)} over 20 ms')
    print('   slot sum per frame, ms        ' + ' '.join(f'{n:>8}' for n in names))
    for label, g in (('normal (<=20 ms) mean', ok), ('hitch (>25 ms) mean', hit)):
        if g: print(f'   {label:<28}' + ' '.join(f'{sum(r["tot"][i] for r in g)/len(g):8.2f}' for i in range(5)))
    for label, g in (('normal max-single mean', ok), ('hitch max-single mean', hit)):
        if g: print(f'   {label:<28}' + ' '.join(f'{sum(r["max"][i] for r in g)/len(g):8.2f}' for i in range(5)))
    # which slot has a single event >= 8 ms in the hitch frame
    big = Counter()
    for r in hit:
        s = [names[i] for i in range(5) if r['max'][i] >= 8]
        big[','.join(s) or 'none'] += 1
    print('   hitch frames by slot(s) with a single event >= 8 ms:', dict(big.most_common()))
    bins = Counter(int(r['rt'] // 10) * 10 for r in hit)
    print('   hitches per 10 s of replay:', ' '.join(f'{k}:{bins[k]}' for k in sorted(bins)))
    print('   worst 8 frames (n replay_s ft | drain rec submit present pipe, sums):')
    for r in sorted(hit, key=lambda r: -r['ft'])[:8]:
        print(f'     {r["n"]} {r["rt"]:.2f} {r["ft"]:.1f} | ' + ' '.join(f'{x:.1f}' for x in r['tot']))
