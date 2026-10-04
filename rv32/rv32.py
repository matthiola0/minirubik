#!/usr/bin/env python3
"""Build, run and measure the RV32I minirubik programs on Ripes.

  python rv32/rv32.py build
  python rv32/rv32.py run PROGRAM [--d11 | --states FILE | --state S ...]
  python rv32/rv32.py emit SOURCE -o OUT [--state S] [--render]
  python rv32/rv32.py size PROGRAM

PROGRAM is the C reference rv32/build/solver_ref.elf or an assembly source
such as rv32/solver.s. `run` checks every result against the exit code and
path length of the host solver and replays the path in a separate Python
model of the cube.
"""
import argparse
import concurrent.futures
import csv
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
BUILD = HERE / 'build'
EXE = '.exe' if os.name == 'nt' else ''
PLACEHOLDER = '21345671111111'
REQUIRED_VECTOR = '21345671111111'
STATIC_LIMIT = 128 * 1024
HOST_FLAGS = ['-O2', '-std=c99', '-Wall', '-Wextra', '-Wpedantic',
              '-Wno-sign-compare']
TARGET_FLAGS = ['-O2', '-march=rv32i', '-mabi=ilp32', '-std=c99', '-Wall',
                '-Wextra', '-Wpedantic', '-ffreestanding', '-nostdlib',
                '-nostartfiles', '-Wl,--no-relax']

# The cube model of solver.c, for an independent replay of every path.
SOURCES = ((1, 4, 2, 0, 3, 5, 6), (0, 1, 2, 4, 5, 6, 3),
           (0, 2, 5, 3, 1, 4, 6))
TWISTS = ((1, 2, 0, 2, 1, 0, 0), (0, 0, 0, 1, 2, 1, 2),
          (0, 0, 0, 0, 0, 0, 0))
NAMES = ("R", "R2", "R'", "B", "B2", "B'", "D", "D2", "D'")


def fail(message):
    sys.exit(f'rv32.py: {message}')


def run_checked(command):
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        fail(f'command failed ({result.returncode}): {" ".join(command)}\n'
             f'{result.stdout}{result.stderr}')
    if result.stderr.strip():
        print(result.stderr.strip())
    return result.stdout


def replays_to_solved(state, moves):
    p = [int(c) - 1 for c in state[:7]]
    o = [int(c) - 1 for c in state[7:]]
    for name in moves:
        face, turns = divmod(NAMES.index(name), 3)
        for _ in range(turns + 1):
            p = [p[i] for i in SOURCES[face]]
            o = [(o[i] + t) % 3 for i, t in zip(SOURCES[face], TWISTS[face])]
    return p == list(range(7)) and o == [0] * 7


def find_ripes(argument):
    candidates = (argument, os.environ.get('RIPES'),
                  str(REPO.parent / 'tools' / 'Ripes' / f'Ripes{EXE}'),
                  shutil.which('Ripes'))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    fail('Ripes not found; pass --ripes PATH or set RIPES')


def section_sizes(path, size_tool):
    """.text bytes and .data + .bss + .rodata bytes (with small variants)."""
    sizes = {}
    for line in run_checked([size_tool, '-A', str(path)]).splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[0].startswith('.') and parts[1].isdigit():
            sizes[parts[0]] = int(parts[1])
    text = sum(v for k, v in sizes.items() if k == '.text' or
               k.startswith('.text.'))
    data = sum(v for k, v in sizes.items() if k.startswith(
        ('.data', '.sdata', '.bss', '.sbss', '.rodata', '.srodata')))
    return text, data, sizes


def preprocess(text, state, render):
    """Resolve .if/.else/.endif, set RENDER and replace the input digits."""
    values, stack, out, active, inputs = {}, [], [], True, 0
    for number, line in enumerate(text.splitlines(), 1):
        code = line.split('#', 1)[0].strip()
        match = re.fullmatch(r'\.if\s+(!?)\s*(\w+)', code)
        if match:
            name = match.group(2)
            value = int(name, 0) if name[0].isdigit() else values.get(name)
            if value is None:
                fail(f'line {number}: .if uses unknown symbol {name}')
            taken = bool(value) != (match.group(1) == '!')
            stack.append((active, taken))
            active = active and taken
            continue
        if code in ('.else', '.endif'):
            if not stack:
                fail(f'line {number}: {code} without .if')
            parent, taken = stack[-1]
            if code == '.else':
                active = parent and not taken
            else:
                active = parent
                stack.pop()
            continue
        if not active:
            continue
        match = re.fullmatch(r'\.equ\s+(\w+)\s*,\s*(\S+)', code)
        if match and match.group(1) == 'RENDER':
            line = f'.equ RENDER, {int(render)}'
            values['RENDER'] = int(render)
        elif match:
            try:
                values[match.group(1)] = int(match.group(2), 0)
            except ValueError:
                pass
        match = re.match(r'(\s*input:\s*\.(?:string|asciz)\s*")([^"]*)(".*)',
                         line)
        if match:
            line = match.group(1) + state + match.group(3)
            inputs += 1
        out.append(line)
    if stack:
        fail('.if without .endif')
    if inputs != 1:
        fail(f'expected one `input: .string "..."` line, found {inputs}')
    return '\n'.join(out) + '\n'


def assembly_text(source, state, render):
    tables = BUILD / 'tables.s'
    if not tables.is_file():
        fail('rv32/build/tables.s is missing; run `rv32.py build` first')
    return (preprocess(source.read_text(), state, render) + '\n' +
            tables.read_text())


def check_state_text(state):
    if '"' in state or '\\' in state or '\n' in state:
        fail(f'state {state!r} contains a character that cannot be inlined')


# ---- build -----------------------------------------------------------------

def command_build(args):
    BUILD.mkdir(exist_ok=True)
    generator = BUILD / f'gen_tables{EXE}'
    run_checked([args.cc, *HOST_FLAGS, str(HERE / 'gen_tables.c'), '-o',
                 str(generator)])
    run_checked([str(generator), str(BUILD)])
    run_checked([args.cc, *HOST_FLAGS, str(REPO / 'solver_ida.c'), '-o',
                 str(BUILD / f'oracle{EXE}')])
    elf = BUILD / 'solver_ref.elf'
    run_checked([args.rvcc, *TARGET_FLAGS, f'-I{BUILD}',
                 str(HERE / 'solver_ref.c'), '-o', str(elf)])
    if elf.read_bytes().count(PLACEHOLDER.encode() + b'\0') != 1:
        fail('the input string does not appear exactly once in the ELF')
    text, data, _ = section_sizes(elf, args.size)
    print(f'built {BUILD.relative_to(REPO)}: tables.s, tables.h, '
          f'states_d11.txt, oracle{EXE}, solver_ref.elf')
    print(f'solver_ref.elf: .text {text} bytes; .data + .bss + .rodata '
          f'{data} bytes (limit {STATIC_LIMIT})')


# ---- run -------------------------------------------------------------------

def load_states(args):
    if args.d11:
        path = BUILD / 'states_d11.txt'
        if not path.is_file():
            fail('rv32/build/states_d11.txt is missing; run `rv32.py build`')
        states = path.read_text().split()
        if len(states) != 2644 or len(set(states)) != 2644:
            fail('distance-11 suite must contain 2,644 distinct states')
        return [(s, (0, 11)) for s in states]
    states = list(args.state or [])
    if args.states:
        for line in Path(args.states).read_text().splitlines():
            line = line.split('#', 1)[0].strip()
            if line:
                states.append(line)
    if not states:
        fail('give --d11, --states FILE or --state S')
    oracle = BUILD / f'oracle{EXE}'
    if not oracle.is_file():
        fail(f'{oracle} is missing; run `rv32.py build` first')
    expected = []
    for state in states:
        check_state_text(state)
        result = subprocess.run([str(oracle), state], capture_output=True,
                                text=True)
        if result.returncode == 0:
            expected.append((state, (0, len(result.stdout.split()))))
        elif result.returncode == 2:
            expected.append((state, (2, None)))
        else:
            fail(f'host solver failed on {state}')
    return expected


def program_for(state, program, base, workdir, index, render):
    if program.suffix == '.elf':
        if len(state.encode()) > len(PLACEHOLDER):
            raise ValueError('state longer than 14 characters cannot be '
                             'patched into the ELF')
        patched = base.replace(PLACEHOLDER.encode() + b'\0',
                               state.encode().ljust(15, b'\0'), 1)
        path = workdir / f'{index}.elf'
        path.write_bytes(patched)
        return path, 'elf'
    path = workdir / f'{index}.s'
    path.write_text(assembly_text(program, state, render))
    return path, 'asm'


def run_one(task):
    index, state, (want_exit, want_length), program, base, workdir, args = task
    row = {'state': state, 'expected_exit': want_exit,
           'expected_length': '' if want_length is None else want_length,
           'exit': '', 'length': '', 'iret': '', 'status': '', 'moves': ''}
    try:
        path, kind = program_for(state, program, base, workdir, index, False)
    except ValueError as error:
        row['status'] = str(error)
        return row
    try:
        result = subprocess.run(
            [args.ripes, '--mode', 'cli', '--src', str(path), '-t', kind,
             '--proc', args.proc, '--iret', '--timeout', str(args.timeout)],
            capture_output=True, text=True, errors='replace',
            timeout=args.timeout / 1000 + 30)
    except subprocess.TimeoutExpired:
        row['status'] = 'Ripes process timed out'
        return row
    path.unlink()
    out = result.stdout.replace('\r', '').replace('\0', '')
    exited = re.search(r'Program exited with code: (-?\d+)', out)
    retired = re.search(r'instructions retired\n(\d+)', out)
    if retired:
        row['iret'] = int(retired.group(1))
    if result.returncode != 0 or not retired:
        row['status'] = f'Ripes failed or missing --iret: {result.stderr[:200]}'
        return row
    if not exited:
        detail = ' '.join(out.split())[:200] or result.stderr.strip()[:200]
        row['status'] = f'no exit: {detail}'
        return row
    row['exit'] = int(exited.group(1))
    lines = out[:exited.start()].split('\n')
    moves = lines[0].split() if lines else []
    row['moves'] = ' '.join(moves)
    if row['exit'] != want_exit:
        row['status'] = f'exit {row["exit"]}, expected {want_exit}'
    elif want_exit == 0:
        row['length'] = len(moves)
        if any(m not in NAMES for m in moves):
            row['status'] = 'output is not a move list'
        elif not replays_to_solved(state, moves):
            row['status'] = 'path does not solve the cube'
        elif len(moves) != want_length:
            row['status'] = f'length {len(moves)}, expected {want_length}'
        elif args.proc == 'RV32_ISS' and row['iret'] > args.max_iret:
            row['status'] = f'instruction budget exceeded: {row["iret"]}'
    row['status'] = row['status'] or 'ok'
    return row


def command_run(args):
    args.ripes = find_ripes(args.ripes)
    program = Path(args.program)
    if not program.is_file():
        fail(f'{program} not found')
    states = load_states(args)
    base = program.read_bytes() if program.suffix == '.elf' else None
    if base is not None and base.count(PLACEHOLDER.encode() + b'\0') != 1:
        fail('the input string does not appear exactly once in the ELF')
    rows = []
    with tempfile.TemporaryDirectory(prefix='rv32-') as workdir:
        tasks = [(i, s, e, program, base, Path(workdir), args)
                 for i, (s, e) in enumerate(states)]
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            for done, row in enumerate(pool.map(run_one, tasks), 1):
                rows.append(row)
                if done % 200 == 0:
                    print(f'{done}/{len(tasks)} states', file=sys.stderr)
    if args.csv:
        with open(args.csv, 'w', newline='') as out:
            writer = csv.DictWriter(out, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    failed = [r for r in rows if r['status'] != 'ok']
    measured = [r for r in rows if r['iret'] != '' and r['status'] == 'ok']
    print(f'{program.name} on {args.proc}: {len(rows) - len(failed)} of '
          f'{len(rows)} states passed')
    for row in failed[:10]:
        print(f'  FAIL {row["state"]}: {row["status"]}')
    if len(rows) <= 20:
        for row in rows:
            print(f'  {row["state"]}  exit {row["exit"]}  iret {row["iret"]}'
                  f'  {row["moves"]}')
    if measured:
        worst = max(measured, key=lambda r: r['iret'])
        mean = sum(r['iret'] for r in measured) / len(measured)
        print(f'retired instructions: max {worst["iret"]} at {worst["state"]}'
              f', mean {mean:.1f}, min {min(r["iret"] for r in measured)}')
        for row in measured:
            if row['state'] == REQUIRED_VECTOR:
                print(f'{REQUIRED_VECTOR}: {row["iret"]}')
    return 1 if failed else 0


# ---- emit and size ---------------------------------------------------------

def command_emit(args):
    state = args.state or PLACEHOLDER
    check_state_text(state)
    Path(args.output).write_text(assembly_text(Path(args.source), state,
                                               args.render))
    print(f'wrote {args.output} (RENDER={int(args.render)}, input {state})')


def command_size(args):
    program = Path(args.program)
    if program.suffix == '.elf':
        text, data, _ = section_sizes(program, args.size)
    else:
        with tempfile.TemporaryDirectory(prefix='rv32-') as workdir:
            source = Path(workdir) / 'program.s'
            obj = Path(workdir) / 'program.o'
            source.write_text(assembly_text(program, PLACEHOLDER, False))
            run_checked([args.rvas, '-march=rv32i', '-mabi=ilp32',
                         '-mno-relax', str(source), '-o', str(obj)])
            elf = Path(workdir) / 'program.elf'
            run_checked([args.rvld, '-m', 'elf32lriscv', '--no-relax',
                         '-e', '_start', str(obj), '-o', str(elf)])
            text, data, _ = section_sizes(elf, args.size)
    print(f'{program.name}: .text {text} bytes; .data + .bss + .rodata '
          f'{data} bytes (limit {STATIC_LIMIT})')
    return int(data > STATIC_LIMIT)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest='command', required=True)

    build = sub.add_parser('build', help='generate tables, build reference')
    build.add_argument('--cc', default='gcc')
    build.add_argument('--rvcc', default='riscv64-unknown-elf-gcc')
    build.add_argument('--size', default='riscv64-unknown-elf-size')
    build.set_defaults(func=command_build)

    run = sub.add_parser('run', help='run and check a program on Ripes')
    run.add_argument('program')
    run.add_argument('--d11', action='store_true',
                     help='all 2,644 distance-11 states')
    run.add_argument('--states', help='file with one state per line')
    run.add_argument('--state', action='append', help='a single state')
    run.add_argument('--proc', default='RV32_ISS')
    run.add_argument('--jobs', type=int,
                     default=max(1, min(8, (os.cpu_count() or 2) - 1)))
    run.add_argument('--timeout', type=int, default=300000,
                     help='Ripes simulation timeout in ms')
    run.add_argument('--csv', help='write per-state results')
    run.add_argument('--max-iret', type=int, default=50_000_000,
                     help='instruction ceiling for valid ISS queries')
    run.add_argument('--ripes')
    run.set_defaults(func=command_run)

    emit = sub.add_parser('emit', help='write one source file for Ripes')
    emit.add_argument('source')
    emit.add_argument('-o', '--output', required=True)
    emit.add_argument('--state')
    emit.add_argument('--render', action='store_true',
                      help='keep .if RENDER blocks (GUI build)')
    emit.set_defaults(func=command_emit)

    size = sub.add_parser('size', help='section sizes, renderer removed')
    size.add_argument('program')
    size.add_argument('--rvas', default='riscv64-unknown-elf-as')
    size.add_argument('--size', default='riscv64-unknown-elf-size')
    size.add_argument('--rvld', default='riscv64-unknown-elf-ld')
    size.set_defaults(func=command_size)

    args = parser.parse_args()
    sys.exit(args.func(args))


if __name__ == '__main__':
    main()
