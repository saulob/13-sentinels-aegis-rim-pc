<#
.SYNOPSIS
  Builds and runs the focused headless tests: no window, no game launch, no network.
.DESCRIPTION
  Needs the build configured once by tools\build.ps1. Only the test targets are built, incrementally. Every test runs even
  after one fails; the exit code is non-zero when any of them failed. Set TSR_TEST_PACKAGES to the folder of the USA
  packages to include the checks on the real package headers (they are only opened).
#>
[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot '_common.ps1')
$tc = Resolve-Toolchain 'auto'
$env:PATH = "$($tc.Bin);$env:PATH"
$buildDir = Join-Path $script:RepoRoot 'build\cmake\clang64'
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'build.ninja'))) { Stop-Tool 'Run tools\build.ps1 once first.' }

$tests = [ordered]@{
    'tsr-game-profile-tests'   = 'Package identity, base and update, staging'
    'tsr-memory-windows-tests' = 'Sparse guest memory'
    'tsr-log-tests'            = 'Log rotation and closed standard streams'
    'tsr-module-history-tests' = 'Crash report module history'
    'tsr-sdl-compat-tests'     = 'SDL gamepad defaults'
    'tsr-window-tests'         = 'Windowed-mode size and centring'
    'tsr-frame-pacing-tests'   = 'Presentation pacing'
}
& ninja -C $buildDir @($tests.Keys)
if ($LASTEXITCODE -ne 0) { Stop-Tool 'Building the tests failed' }

$failed = [System.Collections.Generic.List[string]]::new()
foreach ($name in $tests.Keys) {
    Write-Step $tests[$name]
    & (Join-Path $buildDir "bin\$name.exe")
    if ($LASTEXITCODE -ne 0) { $failed.Add($name) }
}
if ($failed.Count) { Stop-Tool "Failed: $($failed -join ', ')" }
Write-Host 'All focused tests passed.' -ForegroundColor Green
