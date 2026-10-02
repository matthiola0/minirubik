"""Independent cubie-model check of the native CLI; run from repository root."""
import subprocess
from pathlib import Path

SOURCES = ((1, 4, 2, 0, 3, 5, 6),
           (0, 1, 2, 4, 5, 6, 3),
           (0, 2, 5, 3, 1, 4, 6))
TWISTS = ((1, 2, 0, 2, 1, 0, 0),
          (0, 0, 0, 1, 2, 1, 2),
          (0, 0, 0, 0, 0, 0, 0))
NAMES = ("R", "R2", "R'", "B", "B2", "B'", "D", "D2", "D'")
SOLVED = (tuple(range(7)), (0,) * 7)


def move(state, name):
    index = NAMES.index(name)
    face, turns = index // 3, index % 3 + 1
    p, o = state
    for _ in range(turns):
        p = tuple(p[i] for i in SOURCES[face])
        o = tuple((o[i] + twist) % 3
                  for i, twist in zip(SOURCES[face], TWISTS[face]))
    return p, o


def encode(state):
    return ''.join(str(x + 1) for part in state for x in part)


binary = './solver_ida.exe' if Path('solver_ida.exe').exists() else './solver_ida'


def check(text, distance):
    result = subprocess.run([binary, text], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    path = result.stdout.split()
    assert len(path) == distance, (text, path, distance)
    state = (tuple(int(x) - 1 for x in text[:7]),
             tuple(int(x) - 1 for x in text[7:]))
    for name in path:
        state = move(state, name)
    assert state == SOLVED, (text, path, state)


for name in NAMES:
    check(encode(move(SOLVED, name)), 1)
for line in Path('tests/solutions.txt').read_text().splitlines():
    if line and not line.startswith('#'):
        text, path = line.split('|')
        check(text, len(path.split()))

invalid = ('1234567111111', '123456711111111', '02345671111111',
           '82345671111111', '12345671111110', '12345671111114',
           '1234567111111a', '11345671111111', '12345671111112')
for args in ([text] for text in invalid):
    assert subprocess.run([binary, *args], capture_output=True).returncode == 2
for args in ([], [encode(SOLVED)] * 2):
    assert subprocess.run([binary, *args], capture_output=True).returncode == 2
print('PASS: 9 one-move cases, 8 upstream vectors, 11 invalid CLI cases; '
      'paths replayed in an independent Python model')
