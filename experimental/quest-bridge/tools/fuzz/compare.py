"""Compares the fuzzer's output from the device with the interpreter's.

    python tools/fuzz/compare.py <out dir>

Reads native.txt and guest.txt (and guest.err for skipped instructions) from
the directory, and prints each mismatching test with its instruction and the
registers that differ, grouped by mnemonic so a single bad case shows up as
one line rather than a hundred.
"""
import collections
import re
import sys

out = sys.argv[1]
names = [f'x{i}' for i in range(31)] + ['nzcv'] + [f'v{i // 2}.{"lo" if i % 2 == 0 else "hi"}' for i in range(64)] + ['mem']

def load(path):
    rows = {}
    for line in open(path, errors='replace'):
        if line.startswith('T'):
            parts = line.split()
            rows[int(parts[0][1:])] = parts[1:]
    return rows

text = []
for line in open(out + '/fuzz_list.h'):
    m = re.match(r'\s+"([0-9a-f]{8}) (.*)",$', line)
    if m:
        text.append((m.group(1), m.group(2)))

native = load(out + '/native.txt')
guest = load(out + '/guest.txt')
skipped = set()
try:
    for line in open(out + '/guest.err', errors='replace'):
        if line.startswith('skip: '):
            skipped.add(line.split()[1])
except FileNotFoundError:
    pass

by_mnemonic = collections.defaultdict(list)
unimplemented = collections.Counter()
matched = 0
for i, (word, disasm) in enumerate(text):
    if i not in native or i not in guest:
        continue
    mnemonic = disasm.split()[0]
    if word in skipped:
        unimplemented[mnemonic] += 1
        continue
    a, b = native[i], guest[i]
    diffs = [(names[k], a[k], b[k]) for k in range(min(len(a), len(b))) if a[k] != b[k]]
    if not diffs:
        matched += 1
        continue
    by_mnemonic[mnemonic].append((i, word, disasm, diffs))

print(f'{len(text)} tests: {matched} match, {sum(len(v) for v in by_mnemonic.values())} differ, '
      f'{sum(unimplemented.values())} not implemented')
print()
for mnemonic, cases in sorted(by_mnemonic.items(), key=lambda kv: -len(kv[1])):
    i, word, disasm, diffs = cases[0]
    shown = ', '.join(f'{n}: {x} vs {y}' for n, x, y in diffs[:3])
    print(f'{len(cases):4d}  {disasm:48s} [{word}] {shown}')
print()
print('not implemented:', ', '.join(f'{m} ({n})' for m, n in unimplemented.most_common()))
