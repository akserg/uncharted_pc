#!/usr/bin/env python3
"""raw2png.py <in.raw> <out.png> [--bgra] [--half]: BB_FRAME_DUMP_TRIGGER raw 32-bit pixels
(name ends _<W>x<H>.raw) -> PNG, standard library only. --half halves the size (box filter)."""
import struct, sys, zlib

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    flags = {a for a in sys.argv[1:] if a.startswith('--')}
    src, dst = args
    w, h = map(int, src.rsplit('_', 1)[1].split('.')[0].split('x'))
    data = open(src, 'rb').read()
    if len(data) < w * h * 4:
        sys.exit(f'{src}: {len(data)} bytes, expected {w*h*4}')
    rows = []
    step = 2 if '--half' in flags else 1
    for y in range(0, h - step + 1, step):
        row = bytearray()
        base = y * w * 4
        for x in range(0, w - step + 1, step):
            o = base + x * 4
            p = data[o:o + 3]
            row += bytes((p[2], p[1], p[0])) if '--bgra' in flags else bytes(p)
        rows.append(b'\0' + bytes(row))
    ow, oh = w // step, h // step
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', ow, oh, 8, 2, 0, 0, 0)) \
        + chunk(b'IDAT', zlib.compress(b''.join(rows), 6)) + chunk(b'IEND', b'')
    open(dst, 'wb').write(png)

main()
