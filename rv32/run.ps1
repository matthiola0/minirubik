$ErrorActionPreference = 'Stop'
$env:PYTHONIOENCODING = 'utf-8'
Set-Location (Split-Path $PSScriptRoot -Parent)
function Run-Logged([string]$Name, [string[]]$Arguments) {
    & python @Arguments 2>&1 | Tee-Object -FilePath "rv32/results/$Name.txt"
    if ($LASTEXITCODE -ne 0) { throw "$Name failed with exit $LASTEXITCODE" }
}
New-Item -ItemType Directory -Force rv32/results | Out-Null
& python rv32/rv32.py build
if ($LASTEXITCODE -ne 0) { throw 'build failed' }
Run-Logged 'assembly-d11' @('rv32/rv32.py','run','rv32/solver.s','--d11','--csv','rv32/results/assembly-d11.csv')
Run-Logged 'reference-d11' @('rv32/rv32.py','run','rv32/build/solver_ref.elf','--d11','--csv','rv32/results/reference-d11.csv')
Run-Logged 'regression' @('rv32/rv32.py','run','rv32/solver.s','--states','rv32/regression.txt','--state','123456711111111','--csv','rv32/results/regression.csv')
Run-Logged 'reference-regression' @('rv32/rv32.py','run','rv32/build/solver_ref.elf','--states','rv32/regression.txt','--csv','rv32/results/reference-regression.csv')
Run-Logged 'pipeline' @('rv32/rv32.py','run','rv32/solver.s','--states','rv32/tests.txt','--proc','RV32_5S','--csv','rv32/results/pipeline.csv')
foreach ($Variant in @('eager','short-circuit')) {
    Run-Logged $Variant @('rv32/rv32.py','run',"rv32/variants/$Variant.s",'--states','rv32/tests.txt','--state','54721631111111','--csv',"rv32/results/$Variant.csv")
}
& gcc -O2 -std=c99 -Wall -Wextra -Wpedantic -Wno-sign-compare -Irv32/build rv32/verify_ref.c -o rv32/build/verify_ref.exe
if ($LASTEXITCODE -ne 0) { throw 'host verifier build failed' }
$VerificationTimer = [Diagnostics.Stopwatch]::StartNew()
& ./rv32/build/verify_ref.exe 2>&1 | Tee-Object -FilePath rv32/results/host-verification.txt
if ($LASTEXITCODE -ne 0) { throw 'host verification failed' }
$VerificationTimer.Stop()
"wall_seconds=$($VerificationTimer.Elapsed.TotalSeconds) exit=0" | Add-Content rv32/results/host-verification.txt
foreach ($Program in @('rv32/solver.s','rv32/variants/eager.s','rv32/variants/short-circuit.s','rv32/build/solver_ref.elf')) {
    & python rv32/rv32.py size $Program
    if ($LASTEXITCODE -ne 0) { throw "size failed: $Program" }
}
& python rv32/rv32.py emit rv32/solver.s --render --state 23615472113321 -o rv32/build/gui-short.s
if ($LASTEXITCODE -ne 0) { throw 'short GUI emit failed' }
& python rv32/rv32.py emit rv32/solver.s --render --state 21345671111111 -o rv32/build/gui.s
if ($LASTEXITCODE -ne 0) { throw 'GUI emit failed' }
& riscv64-unknown-elf-as -march=rv32i -mabi=ilp32 -mno-relax --defsym LED_MATRIX_0_BASE=0xf0000000 --defsym LED_MATRIX_0_WIDTH=35 --defsym LED_MATRIX_0_HEIGHT=25 rv32/build/gui.s -o rv32/build/gui-size.o
if ($LASTEXITCODE -ne 0) { throw 'GUI RV32I assembly check failed' }
& riscv64-unknown-elf-ld -m elf32lriscv --no-relax -e _start rv32/build/gui-size.o -o rv32/build/gui-size.elf
if ($LASTEXITCODE -ne 0) { throw 'GUI link failed' }
& python rv32/rv32.py size rv32/build/gui-size.elf
if ($LASTEXITCODE -ne 0) { throw 'GUI static budget failed' }
& python rv32/rv32.py emit rv32/solver.s --state 23615472113321 -o rv32/build/walkthrough.s
if ($LASTEXITCODE -ne 0) { throw 'walkthrough emit failed' }
