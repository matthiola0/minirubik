# Compare search operation counts and run solver checks from the repository root.
# Usage: ./bench/run_efficiency.ps1 [-Compiler gcc]
param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
Push-Location (Join-Path $PSScriptRoot '..')
try {
    $results = 'bench/results'
    if (-not (Test-Path $results)) {
        New-Item -ItemType Directory $results | Out-Null
    }
    $flags = @('-O3', '-std=c99', '-Wall', '-Wextra', '-Wpedantic',
               '-Wno-sign-compare')
    & $Compiler --version | Set-Content "$results/compiler.txt"
    if ($LASTEXITCODE -ne 0) { throw 'Compiler version check failed' }
    & $Compiler @flags solver_ida.c -o solver_ida.exe
    if ($LASTEXITCODE -ne 0) { throw 'Solver build failed' }
    python tests/check_ida_cli.py | Tee-Object "$results/cli.txt"
    if ($LASTEXITCODE -ne 0) { throw 'CLI checks failed' }
    foreach ($mode in 0,1,2) {
        $variant = @('eager', 'p-first', 'o-first')[$mode]
        & $Compiler @flags "-DIDA_HEURISTIC_ORDER=$mode" `
            bench/c_efficiency.c -o "bench/$variant.exe"
        if ($LASTEXITCODE -ne 0) { throw "$variant build failed" }
        & ".\bench\$variant.exe" "$results/$variant.csv" |
            Tee-Object "$results/$variant.txt"
        if ($LASTEXITCODE -ne 0) { throw "$variant checks failed" }
    }
    # The default variant again at -O2, which the RV32I reference build uses.
    $o2flags = @('-O2') + $flags[1..($flags.Count - 1)]
    & $Compiler @o2flags bench/c_efficiency.c `
        -o bench/p-first-o2.exe
    if ($LASTEXITCODE -ne 0) { throw 'p-first -O2 build failed' }
    .\bench\p-first-o2.exe | Tee-Object "$results/p-first-o2.txt"
    if ($LASTEXITCODE -ne 0) { throw 'p-first -O2 checks failed' }
    $timer = [Diagnostics.Stopwatch]::StartNew()
    .\solver_ida.exe --self-test | Tee-Object "$results/self-test.txt"
    $testExit = $LASTEXITCODE
    $timer.Stop()
    $seconds = $timer.Elapsed.TotalSeconds.ToString(
        'F3', [Globalization.CultureInfo]::InvariantCulture)
    "wall_seconds=$seconds exit=$testExit" |
        Tee-Object -Append "$results/self-test.txt"
    if ($testExit -ne 0) { throw 'Exhaustive self-test failed' }
} finally {
    Pop-Location
}
