#!/usr/bin/env python3
"""M4 task 9: scan a linked image (out/<exe>/boot-linked.bin, BBPROBE5) for gs:/fs: segment-prefixed operands.

  clang -arch x86_64 -O2 -I$BB_DEPS/include tools/scan_gs_fs.c -L$BB_DEPS/lib -lZydis -Wl,-rpath,$BB_DEPS/lib -o out/scan-gs-fs
  python3 tools/scan_gs_fs.py out/eboot/boot-linked.bin out/scan-gs-fs

Prints, per module (eboot, libc.prx, libSceFios2.prx), the exact patched form `mov rax, gs:[0]` (what probe.c
patch_tcb_loads rewrites) and every other gs:/fs: access Zydis decodes in the executable segments. A linear sweep
also decodes read-only data that sits in the same segment (import stubs, NID tables, ASCII), so hits at or after the
first import stub are data; look at the lowest hit and at absolute `fs:[small]` forms, which are the TLS shapes."""
import os, re, struct, subprocess, sys, tempfile

def main(path, scan):
    data = open(path, 'rb').read()
    if data[:8] != b'BBPROBE5':
        sys.exit('only BBPROBE5 (multi-module) images are handled')
    p = 8
    def r():
        nonlocal p
        v = struct.unpack_from('<Q', data, p)[0]; p += 8; return v
    size, entry, ns, nr, imp = r(), r(), r(), r(), r(); r(); r(); [r() for _ in range(4)]
    mods = []
    for _ in range(r()):
        base, sz = r(), r(); [r() for _ in range(5)]; mods.append((base, sz))
    nb = r()
    p += nb * 24
    segs = [(r(), r(), r()) for _ in range(ns)]
    img = data[-size:]
    stub = re.search(rb'\xff\x25....\x68\x00\x00\x00\x00\xe9', img)
    with tempfile.NamedTemporaryFile(suffix='.bin') as f:
        f.write(img); f.flush()
        out = subprocess.run([scan, f.name, ','.join(f'{b}:{s}' for b, s in mods)] +
                             [f'{a}:{s}' for a, s, fl in segs if fl & 1], capture_output=True, text=True).stdout
    hits = [int(m.group(1), 16) for m in re.finditer(r'other: image\+(0x[0-9a-f]+)', out)]
    print(f'{path}: entry {entry:#x}, modules {[(hex(b), hex(s)) for b, s in mods]}')
    print(f'first import stub at {stub.start():#x}' if stub else 'no import stub table found')
    print(f'{len(hits)} other gs:/fs: decodes, lowest {min(hits):#x}' if hits else 'no other gs:/fs: decodes')
    small = [l for l in out.splitlines() if re.search(r'(fs|gs):\[0x0000000000[0-9A-F]{4}\]', l) and 'other' in l]
    print(f'absolute fs/gs with displacement < 0x1000000 (TLS shape): {len(small)}')
    print('\n'.join(l for l in out.splitlines() if not l.startswith('  other')))

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
