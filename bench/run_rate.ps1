# Retired instructions per second of Ripes processor models on rate_loop.s.
# From the repository root:
# Usage: ./bench/run_rate.ps1 -Ripes ../tools/Ripes/Ripes.exe
param(
    [Parameter(Mandatory)] [string] $Ripes,
    [System.Collections.IDictionary] $Iterations = [ordered]@{
        RV32_ISS = 10000000; RV32_SS = 1000000; RV32_5S = 300000
    },
    [int] $Repeats = 3
)
$ErrorActionPreference = 'Stop'
$source = Get-Content (Join-Path $PSScriptRoot 'rate_loop.s') -Raw
$results = foreach ($proc in $Iterations.Keys) {
    $asm = Join-Path ([IO.Path]::GetTempPath()) "rate_loop_$proc.s"
    ($source -replace '(?m)^\.equ ITERS, .*$', ".equ ITERS, $($Iterations[$proc])") |
        Set-Content -NoNewline $asm
    foreach ($run in 1..$Repeats) {
        $report = "$asm.json"
        Start-Process -FilePath $Ripes -Wait -WindowStyle Hidden -ArgumentList @(
            '--mode', 'cli', '--src', $asm, '-t', 'asm', '--proc', $proc,
            '--iret', '--exectime', '--json', '--output', $report)
        $json = Get-Content $report -Raw
        Remove-Item $report
        $report = $json | ConvertFrom-Json
        $iret = [long] $report.'# instructions retired'
        $ms = [double] $report.'execution time (ms)'
        [pscustomobject]@{
            Proc = $proc; Run = $run; Retired = $iret; ModelMs = $ms
            InstrPerSec = [Math]::Round($iret / ($ms / 1000))
        }
    }
    Remove-Item $asm -ErrorAction SilentlyContinue
}
$results | Format-Table -AutoSize
