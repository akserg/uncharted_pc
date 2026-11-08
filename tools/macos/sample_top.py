#!/usr/bin/env python3
"""sample_top.py <sample.txt> <thread-name-substring> [N=20]: self-time (top of stack) and inclusive time of one thread
from a macOS `sample` call-graph report, as % of that thread's samples."""
import re, sys
from collections import Counter

path, want = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 20
lines = open(path, errors='replace').read().split('\n')
start = next(i for i, l in enumerate(lines) if re.match(r'\s+\d+ Thread_\d+:', l) and want in l)
end = next((i for i in range(start + 1, len(lines)) if re.match(r'\s+\d+ Thread_\d+:', lines[i]) or lines[i].startswith('Total number')), len(lines))
total = int(lines[start].split()[0])
node = re.compile(r'^(\s*[+!:| ]*?)(\d+) (.*)$')
stack = []   # (depth, name, count, children_sum_index)
self_t, incl = Counter(), Counter()
def name_of(text):
    t = re.sub(r'\s+\+ \d+\s+\[0x[0-9a-f]+\].*$', '', text)          # "+ off [addr] file:line"
    loc = re.search(r'\s+\[0x[0-9a-f]+\]\s*(\S+:\d+)?$', text)
    t = re.sub(r'\s+\[0x[0-9a-f]+\].*$', '', t)
    t = re.sub(r'\s+\(in ([^)]*)\).*$', r' [\1]', t)
    return t[:150]
entries = []
for l in lines[start + 1:end]:
    m = node.match(l)
    if not m: continue
    entries.append((len(m.group(1)), int(m.group(2)), name_of(m.group(3))))
# self time of a node = its count - its direct children; unsymbolized frames (???, Rosetta) are credited to the
# nearest symbolized ancestor. Depth comes from the tree-prefix width.
stack = []   # [depth, count, name, children_sum, effective_name]
def close(upto):
    while stack and stack[-1][0] >= upto:
        d, c, n, kids, eff = stack.pop()
        self_t[eff] += max(0, c - kids)
for d, c, n in entries:
    close(d)
    parent_eff = stack[-1][4] if stack else n
    eff = parent_eff if n.startswith('???') and stack else n
    if stack: stack[-1][3] += c
    stack.append([d, c, n, 0, eff])
    incl[eff] = max(incl[eff], c)
close(-1)
print(f'thread: {lines[start].strip()}  (samples {total})')
print('\nTop of stack (self), % of the thread:')
for n, c in self_t.most_common(top): print(f'{100.0 * c / total:6.2f}%  {c:6d}  {n}')
print('\nInclusive (max single node), % of the thread:')
for n, c in incl.most_common(top): print(f'{100.0 * c / total:6.2f}%  {c:6d}  {n}')
