# RV32I solver

| File | Contents |
| --- | --- |
| `solver.s` | Hand-written RV32I IDA* solver |
| `solver_ref.c` | The same search in freestanding C, the GCC reference |
| `gen_tables.c` | Writes the constant tables and the 2,644 distance-11 states from `solver_ida.c` |
| `verify_ref.c` | Checks the C reference against the full BFS oracle on every state |
| `rv32.py` | Builds, runs on Ripes, checks paths and measures section sizes |
| `run.ps1` | Runs every suite and saves the logs in `results/` |
| `tests.txt`, `regression.txt` | Test states |
| `variants/` | Earlier measured versions of `solver.s` |
| `delivery/` | Single-file sources ready to load in Ripes |

## Requirements

Python 3, host GCC and GNU `riscv64-unknown-elf-{gcc,as,ld,size,nm,objdump}`
with RV32I/ILP32 support, all on PATH, and Ripes v2.2.6-106-g5b8a616. Set
`RIPES` to the Ripes executable if it is not at `../tools/Ripes/Ripes.exe`.
`results/environment.txt` records the exact versions and the Ripes SHA256.

## Usage

From the repository root:

```powershell
python rv32/rv32.py build
python rv32/rv32.py run rv32/solver.s --d11 --csv rv32/results/assembly-d11.csv
python rv32/rv32.py run rv32/build/solver_ref.elf --d11 --csv rv32/results/reference-d11.csv
python rv32/rv32.py run rv32/solver.s --states rv32/tests.txt
python rv32/rv32.py size rv32/solver.s
python rv32/rv32.py size rv32/build/solver_ref.elf
```

`build` writes the tables, the host oracle, the distance-11 list and the C ELF
into `build/`, which is not tracked; the target programs link the tables as
constants and never build them. `run` inlines each state, runs it with
`Ripes --mode cli --iret`, and passes a result only if the exit code matches the
host oracle and a valid path has the optimal length and solves the cube in a
separate Python model. A timeout, a missing `--iret` count or more than 50
million instructions also fails. `size` assembles and links with GNU `as` and
`ld`, renderer removed, and reports `.text` and `.data + .bss + .rodata`.

`./rv32/run.ps1` runs all suites, including the regression set, `RV32_5S`, the
earlier versions and `verify_ref`, and emits the GUI sources. The
exhaustive suites take several minutes.

The C reference is built with `-O2 -march=rv32i -mabi=ilp32 -ffreestanding
-nostdlib -nostartfiles -Wl,--no-relax` and links no libgcc. `run` patches its
14-byte input string in the ELF.

## Input and output

`input: .string "..."` holds the 14-character state: seven distinct permutation
digits 1 to 7, then seven twist digits 1 to 3 whose sum is 0 modulo 3. The
program prints the optimal HTM moves separated by spaces and a newline (an
empty line when solved) and exits through ecall 93 with 0 after replay verifies
the path, 1 on a search or replay failure, or 2 on invalid input.

## Ripes GUI

```powershell
python rv32/rv32.py emit rv32/solver.s --render --state 23615472113321 -o rv32/build/gui-short.s
python rv32/rv32.py emit rv32/solver.s --render --state 21345671111111 -o rv32/build/gui.s
python rv32/rv32.py emit rv32/solver.s --state 23615472113321 -o rv32/build/walkthrough.s
```

Add an LED Matrix in the I/O tab (width 35, height 25), select RV32I with M and
C unchecked, load the file with File > Load program > source file, and run.
`gui-short.s` animates the returned `D' R'`, and `gui.s` the 11-move solution
of the required vector. `walkthrough.s` has no renderer and is the program used
for the pipeline walkthrough on `RV32_5S`. `delivery/` holds the same kind of
files, ready to load. `run` and `size` always remove the renderer.

## Ripes assembler notes

This Ripes build has no `.if`, `.section`, `.space` or `.set`, so `rv32.py`
resolves `.if RENDER` blocks and appends the generated `tables.s` itself. It
also leaves forward table labels in `.word` unresolved, so `solver.s` adds six
numeric row offsets to the table bases. The programs print with ecall 11, and
the runner drops the NULs Ripes prints.
