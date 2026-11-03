#!/usr/bin/env python3
"""M5 triage: extract the image of a BBPROBE5 linked image, then disassemble or scan for 0x10'0000'0000-derived constants.
  clang -arch x86_64 -O2 -I$BB_DEPS/include tools/scan_va_imm.c -L$BB_DEPS/lib -lZydis -lZycore -Wl,-rpath,$BB_DEPS/lib -o out/scan-va-imm
  python3 tools/scan_va_imm.py out/eboot/boot-linked.bin out/scan-va-imm dis 0x7e200 0x7e260
  python3 tools/scan_va_imm.py out/eboot/boot-linked.bin out/scan-va-imm imm"""
import struct, subprocess, sys, tempfile
path, tool, mode = sys.argv[1:4]
data = open(path, 'rb').read()
assert data[:8] == b'BBPROBE5'
p = 8
def r():
    global p
    v = struct.unpack_from('<Q', data, p)[0]; p += 8; return v
size, entry, ns, nr, imp = r(), r(), r(), r(), r(); r(); r(); [r() for _ in range(4)]
for _ in range(r()): [r() for _ in range(7)]
nb = r(); p += nb * 24
segs = [(r(), r(), r()) for _ in range(ns)]
img = data[-size:]
with tempfile.NamedTemporaryFile(suffix='.raw') as f:
    f.write(img); f.flush()
    if mode == 'dis':
        args = [tool, 'dis', f.name, sys.argv[4], sys.argv[5]]
    else:
        args = [tool, 'imm', f.name] + [f'{a}:{s}' for a, s, fl in segs if fl & 1]
    sys.stdout.write(subprocess.run(args, capture_output=True, text=True).stdout)
