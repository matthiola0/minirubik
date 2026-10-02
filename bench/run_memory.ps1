# Host memory of Ripes vs. guest bytes written by store_fill.s.
# From the repository root:
# Usage: ./bench/run_memory.ps1 -Ripes ../tools/Ripes/Ripes.exe
param(
    [Parameter(Mandatory)] [string] $Ripes,
    [long[]] $Sizes = @(4, 1MB, 4MB, 16MB),
    [string] $Proc = 'RV32_ISS',
    [int] $TimeoutMs = 30000
)
$ErrorActionPreference = 'Stop'
$source = Get-Content (Join-Path $PSScriptRoot 'store_fill.s') -Raw
$results = foreach ($size in $Sizes) {
    $asm = Join-Path ([IO.Path]::GetTempPath()) "store_fill_$size.s"
    ($source -replace '(?m)^\.equ BYTES, .*$', ".equ BYTES, $size") |
        Set-Content -NoNewline $asm
    $p = Start-Process -FilePath $Ripes -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput "$asm.out" -ArgumentList @(
            '--mode', 'cli', '--src', $asm, '-t', 'asm', '--proc', $Proc,
            '--timeout', $TimeoutMs)
    $peakPrivate = 0L; $peakWorkingSet = 0L
    while (-not $p.HasExited) {
        try {
            $p.Refresh()
            $peakPrivate = [Math]::Max($peakPrivate, $p.PrivateMemorySize64)
            $peakWorkingSet = [Math]::Max($peakWorkingSet, $p.PeakWorkingSet64)
        } catch {}
        Start-Sleep -Milliseconds 50
    }
    Remove-Item $asm, "$asm.out" -ErrorAction SilentlyContinue
    [pscustomobject]@{
        GuestBytes = $size
        PeakPrivateBytes = $peakPrivate
        PeakWorkingSetBytes = $peakWorkingSet
    }
}
$control = $results | Where-Object GuestBytes -eq ($Sizes | Measure-Object -Minimum).Minimum
$results | ForEach-Object {
    $delta = $_.PeakPrivateBytes - $control.PeakPrivateBytes
    $_ | Add-Member HostDeltaBytes $delta -PassThru |
        Add-Member HostPerGuestByte ([Math]::Round($delta / [Math]::Max(1, $_.GuestBytes - $control.GuestBytes), 2)) -PassThru
} | Format-Table -AutoSize
