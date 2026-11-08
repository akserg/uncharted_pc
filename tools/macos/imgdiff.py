#!/usr/bin/env python3
"""imgdiff.py <a.raw> <b.raw>: crude comparison of two BB_FRAME_DUMP_TRIGGER display dumps of equal size
(sampled every 7th pixel): mean absolute channel difference and the share of pixels differing by more than 24."""
import sys
a, b = open(sys.argv[1], 'rb').read(), open(sys.argv[2], 'rb').read()
if len(a) != len(b):
    sys.exit('sizes differ')
total = big = n = 0
for o in range(0, len(a) - 3, 28):
    d = abs(a[o] - b[o]) + abs(a[o + 1] - b[o + 1]) + abs(a[o + 2] - b[o + 2])
    total += d; n += 1
    if d > 24: big += 1
print(f'mean_abs_diff={total / (3 * n):.2f} pixels_over_24={100.0 * big / n:.2f}%')
