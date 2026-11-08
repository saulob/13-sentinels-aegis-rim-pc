<#
.SYNOPSIS
  Builds and runs the focused headless tests: no window, no game launch, no audio, no network.
.DESCRIPTION
  Needs the build configured once by tools\build.ps1. Only the test targets of the chosen groups are built, incrementally
  in the existing build folder (never cleaned); the game executable and the video core are not rebuilt. A test whose
  target did not build fails without running, so an older executable can never pass in its place.

  Every test runs even after one fails; the exit code is non-zero when any of them failed. A step that cannot run here
  (no installed game, no Python) is SKIPPED with the reason; checks a test leaves out are listed as notes. Nothing is
  PASS unless it ran.

  The tests only read the installed game (game\prepared, build\image) and the user's save (user\savedata): the save
  test works on a copy in the build folder. TSR_TEST_PACKAGES names a folder with the real USA packages, which are only
  opened; TSR_TEST_INSTALL adds a full install into that folder.
.PARAMETER Group
  Runtime, CPU, SaveData, Packages, Launcher, Updater, Features, Audio, Video, Graphics, Imports, or All (the default).
  Several can be given.
.PARAMETER TimeoutSeconds
  Fails a test that runs longer than this.
.EXAMPLE
  .\tools\test.ps1
  .\tools\test.ps1 -Group SaveData
  .\tools\test.ps1 -Group Runtime,Audio
#>
[CmdletBinding()]
param(
    [string[]]$Group = @('All'),
    [int]$TimeoutSeconds = 300
)

. (Join-Path $PSScriptRoot '_common.ps1')
$root = $script:RepoRoot
$buildDir = Join-Path $root 'build\cmake\clang64'
$groupOrder = @('Runtime', 'CPU', 'SaveData', 'Packages', 'Launcher', 'Updater', 'Features', 'Audio', 'Video', 'Graphics', 'Imports')
# powershell -File passes "-Group A,B" as one string.
$Group = @($Group | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($name in $Group) {
    if ($name -ne 'All' -and $groupOrder -notcontains $name) { Stop-Tool "Unknown group '$name'. Groups: All, $($groupOrder -join ', ')." }
}

function Step([string]$GroupName, [string]$Title, [string]$Target = '', [scriptblock]$Block = $null) {
    [pscustomobject]@{ Group = $GroupName; Title = $Title; Target = $Target; Block = $Block }
}
function Result([string]$State, [string]$Reason = '', [string[]]$Notes = @()) {
    [pscustomobject]@{ State = $State; Reason = $Reason; Notes = $Notes }
}

# Windows PowerShell turns a native program's stderr into errors, which 'Stop' would make fatal.
function Invoke-Native([scriptblock]$Command) {
    $ErrorActionPreference = 'Continue'
    $lines = @(& $Command 2>&1 | ForEach-Object { "$_" })
    [pscustomobject]@{ Lines = $lines; Code = $LASTEXITCODE }
}

function Invoke-Imports([string]$Version, [bool]$Required) {
    $game = Join-Path $root "game\prepared\CUSA19610-$Version"
    $source = "game\prepared\CUSA19610-$Version"
    $view = Join-Path $root "game\work\view-$Version"
    if (-not (Test-Path -LiteralPath (Join-Path $game 'eboot.bin')) -and (Test-Path -LiteralPath (Join-Path $view 'eboot.bin'))) {
        $game = $view
        $source = "game\work\view-$Version"
    }
    if (-not (Test-Path -LiteralPath (Join-Path $game 'eboot.bin'))) {
        $why = "USA $Version is not installed (game\prepared\CUSA19610-$Version)"
        if ($Required) { return Result 'SKIPPED' $why }
        return Result 'SKIPPED' "$why; optional"
    }
    $python = Get-Python
    if (-not $python) { return Result 'SKIPPED' 'Python 3 not found (tools\setup.ps1)' }
    $coverage = Join-Path $root 'scripts\import_coverage.py'
    $eboot = Invoke-Native { & $python.Exe @($python.Args) '-I' $coverage $game '--eboot-only' }
    $all = Invoke-Native { & $python.Exe @($python.Args) '-I' $coverage $game }
    $ebootSummary = Get-ImportSummary $eboot
    $allSummary = Get-ImportSummary $all
    if (-not $ebootSummary -or -not $allSummary) {
        return Result 'FAIL' "import_coverage.py failed: $(($eboot.Lines + $all.Lines) -join ' | ')"
    }
    # The eboot's imports must all bind; what only the bundled modules import stops the game only if those paths run.
    if ($ebootSummary.Unbound -ne 0) {
        return Result 'FAIL' "$($ebootSummary.Unbound) eboot imports have no binding: $(($eboot.Lines | Select-Object -Skip 1) -join ' | ')"
    }
    $libraries = @($all.Lines | Where-Object { $_ -match '^\s+(lib\w+): (\d+)$' } | ForEach-Object { $_.Trim() })
    return Result 'PASS' '' @("$($source): eboot: $($ebootSummary.Text)",
                              "with bundled modules: $($allSummary.Unbound) unbound ($($libraries -join ', '))")
}
function Get-ImportSummary($Run) {
    $line = $Run.Lines | Where-Object { $_ -match 'imports: .* (\d+) unbound$' } | Select-Object -Last 1
    if ($Run.Code -ne 0 -or -not $line) { return $null }
    $null = $line -match '(\d+) unbound$'
    [pscustomobject]@{ Unbound = [int]$Matches[1]; Text = $line.Substring($line.IndexOf(': ') + 2) }
}

function Invoke-PromptArt {
    $folder = Join-Path $root 'build\test-prompt-art'
    if (-not (Test-Path -LiteralPath (Join-Path $folder '100'))) {
        return Result 'SKIPPED' 'no exported entries: run this group with TSR_TEST_PACKAGES set'
    }
    $python = Get-Python
    if (-not $python) { return Result 'SKIPPED' 'Python 3 not found (tools\setup.ps1)' }
    $modules = Invoke-Native { & $python.Exe @($python.Args) '-I' '-c' 'import numpy, PIL, scipy' }
    if ($modules.Code -ne 0) { return Result 'SKIPPED' 'numpy, Pillow and scipy are not installed (tools\setup.ps1 -InstallPython)' }
    $run = Invoke-Native { & $python.Exe @($python.Args) '-I' (Join-Path $root 'tests\prompt_art_tests.py') $folder }
    $notes = @($run.Lines | Where-Object { $_ -match '^prompt art' })
    if ($run.Code -eq 2) { return Result 'SKIPPED' (($run.Lines | Select-Object -Last 1) -join ' ') }
    if ($run.Code -ne 0) {
        $run.Lines | ForEach-Object { Write-Host "    $_" }
        return Result 'FAIL' (@($run.Lines | Where-Object { $_ -match '^FAIL|Error' } | Select-Object -First 3) -join '; ') $notes
    }
    return Result 'PASS' '' $notes
}

function Invoke-ExitGameLayout {
    $base = Join-Path $root 'game\prepared\CUSA19610-1.00'
    $update = Join-Path $root 'game\prepared\CUSA19610-1.02'
    if (-not (Test-Path -LiteralPath (Join-Path $base 'ROBO_US.CPK'))) { return Result 'SKIPPED' 'no prepared USA 1.00 game (game\prepared\CUSA19610-1.00)' }
    $python = Get-Python
    if (-not $python) { return Result 'SKIPPED' 'Python 3 not found (tools\setup.ps1)' }
    $modules = Invoke-Native { & $python.Exe @($python.Args) '-I' '-c' 'import numpy, PIL, scipy' }
    if ($modules.Code -ne 0) { return Result 'SKIPPED' 'numpy, Pillow and scipy are not installed (tools\setup.ps1 -InstallPython)' }
    $run = Invoke-Native { & $python.Exe @($python.Args) '-I' (Join-Path $root 'tests\exit_game_layout_tests.py') $base $update }
    $notes = @($run.Lines | Where-Object { $_ -match '^exit game' })
    if ($run.Code -eq 2) { return Result 'SKIPPED' (($run.Lines | Select-Object -Last 1) -join ' ') }
    if ($run.Code -ne 0) {
        $run.Lines | Where-Object { $_ -match '^FAIL|Error' } | Select-Object -First 12 | ForEach-Object { Write-Host "    $_" }
        return Result 'FAIL' (@($run.Lines | Where-Object { $_ -match '^FAIL|Error' } | Select-Object -First 3) -join '; ') $notes
    }
    return Result 'PASS' '' $notes
}

function Invoke-BugReportService {
    $node = Get-Command node -ErrorAction SilentlyContinue
    if (-not $node) { return Result 'SKIPPED' 'Node.js 22 or newer not found' }
    $request = Join-Path $buildDir 'bin\bug-report-request.json'
    if (-not (Test-Path -LiteralPath $request)) { return Result 'FAIL' 'the launcher test did not leave its request (bin\bug-report-request.json)' }
    $folder = Join-Path $root 'server\13-sentinels-bug-reports'
    $env:TSR_CPP_REQUEST = $request
    Push-Location $folder
    try { $run = Invoke-Native { & $node.Source '--test' '--test-reporter=tap' 'test/*.test.js' } } finally { Pop-Location; Remove-Item Env:\TSR_CPP_REQUEST -ErrorAction SilentlyContinue }
    $summary = @($run.Lines | Where-Object { $_ -match '^# (tests|pass|fail) ' })
    if ($run.Code -ne 0) {
        $run.Lines | Where-Object { $_ -match '^\s*not ok|Error' } | Select-Object -First 12 | ForEach-Object { Write-Host "    $_" }
        return Result 'FAIL' "node --test exited with $($run.Code)"
    }
    $summary | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
    return Result 'PASS'
}

function Invoke-Script([string]$Relative) {
    $run = Invoke-Native { & powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File (Join-Path $root $Relative) }
    if ($run.Code -ne 0) {
        $run.Lines | ForEach-Object { Write-Host "    $_" }
        $fails = @($run.Lines | Where-Object { $_ -match '^FAIL' } | Select-Object -First 3)
        return Result 'FAIL' $(if ($fails) { $fails -join '; ' } else { "exit code $($run.Code)" })
    }
    $run.Lines | Select-Object -Last 1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
    return Result 'PASS'
}

$steps = @(
    (Step 'Runtime' 'threads, join/detach, handles, mutexes, condition variables, wait states' 'tsr-runtime-tests'),
    (Step 'Runtime' 'guest memory (sparse backing, commitment, aliases, release)' 'tsr-memory-windows-tests'),
    (Step 'Runtime' 'log rotation and closed standard streams' 'tsr-log-tests'),
    (Step 'Runtime' 'crash report module history' 'tsr-module-history-tests'),
    (Step 'CPU' 'MOVBE and BMI1 emulation, CPU requirements, LZCNT rewrite of every site against the processor and on the real 1.00/1.01/1.02 images' 'tsr-isa-tests'),
    (Step 'SaveData' 'mounts, files, handles, errors, interrupted writes, metadata, user save copy' 'tsr-savedata-tests'),
    (Step 'Packages' 'identities 1.00/1.01/1.02, real packages, installs, highest-version precedence, update removal, isolation, voice banks, boot images, prompt art export' 'tsr-game-profile-tests'),
    (Step 'Packages' 'Xbox prompt art coverage (Python, on the entries the step above exported)' -Block { Invoke-PromptArt }),
    (Step 'Packages' 'release scripts (static): ZIP and executable hashes, nothing uploaded or published, quiet run with a log file' -Block { Invoke-Script 'tests\release_script_tests.ps1' }),
    (Step 'Launcher' 'tsr.ini persistence, validation, launcher gate, keys, languages, window sizes' 'tsr-launcher-tests'),
    (Step 'Launcher' 'Bug Report: JSON, service address, log choice, privacy filter, report and preview, client against a stand-in transport, My Reports, nothing sent on its own' 'tsr-bug-report-tests'),
    (Step 'Launcher' 'Bug Report service (Node, disposable database): submission, receipts, limits, retention, admin, dashboard, and the launcher''s own request' -Block { Invoke-BugReportService }),
    (Step 'Launcher' 'application icon and launcher palette (static): SVG, ICO, PNGs, resources, relink, license, action and link colors' -Block { Invoke-Script 'testspp_icon_tests.ps1' }),
    (Step 'Launcher' 'windowed-mode size and centring' 'tsr-window-tests'),
    (Step 'Launcher' 'SDL gamepad defaults' 'tsr-sdl-compat-tests'),
    (Step 'Updater' 'versions, release JSON, SHA-256, ZIP safety, staging, protected game and user data, jobs, install, rollback, the 13S release contract' 'tsr-update-tests'),
    (Step 'Updater' 'loopback HTTP server only: 404, 500, 504, dropped and stalled transfers, cancellation, redirects, address policy, retries' 'tsr-update-network-tests'),
    (Step 'Updater' 'trg-updater.exe end to end on temporary folders: waits for its parent, installs, restarts a marker program' 'tsr-update-helper-tests'),
    (Step 'Features' 'Skip Intro hook sites, refusals, hook run on synthetic code' 'tsr-game-hook-tests'),
    (Step 'Features' 'Exit Game: label data, texture and layout patches, hooks, sites and install on the real 1.00/1.01/1.02 images' 'tsr-exit-game-tests'),
    (Step 'Features' 'Exit Game on the real title files of the five languages (Python, game\prepared)' -Block { Invoke-ExitGameLayout }),
    (Step 'Features' 'Xbox patches, real archive switching, refusals, rollback, shared links, languages, build status, settings' 'tsr-game-feature-tests'),
    (Step 'Audio' 'Ajm contexts, instances, batch slots, pending batches, errors' 'tsr-ajm-tests'),
    (Step 'Video' 'presentation pacing' 'tsr-frame-pacing-tests'),
    (Step 'Video' 'renderer selection (Vulkan default, Auto, OpenGL, Direct3D 11, requirements, launcher and Options menu)' 'tsr-renderer-selection-tests'),
    (Step 'Video' 'OpenGL backend (detiler, emitted GLSL through glslang, no context)' 'tsr-opengl-backend-tests'),
    (Step 'Video' 'OpenGL fast paths (write watch with real faults, state tracker, stream ring, no context)' 'tsr-opengl-perf-tests'),
    (Step 'Video' 'ANGLE dependency (pin, cache, extraction, DLL staging, package checks, no network)' -Block { Invoke-Script 'tests\angle_dependency_tests.ps1' }),
    (Step 'Video' 'H.264 decoder (Videodec2)' -Block { Result 'SKIPPED' 'lives in the video core library; no headless test target yet' }),
    (Step 'Graphics' 'render resolution sites on the real 1.00/1.01/1.02 images, refusals, 16:9 presentation, downsample filter, settings, menus, backends' 'tsr-graphics-tests'),
    (Step 'Imports' 'USA 1.00 import coverage' -Block { Invoke-Imports '1.00' $true }),
    (Step 'Imports' 'USA 1.01 import coverage' -Block { Invoke-Imports '1.01' $false }),
    (Step 'Imports' 'USA 1.02 import coverage' -Block { Invoke-Imports '1.02' $false })
)

$wanted = @(if ($Group -contains 'All') { $groupOrder } else { $groupOrder | Where-Object { $Group -contains $_ } })
$steps = @($steps | Where-Object { $wanted -contains $_.Group })
$targets = @($steps | Where-Object { $_.Target } | ForEach-Object { $_.Target } | Select-Object -Unique)

function Invoke-Captured([string]$Exe) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    # One stream, so the output keeps its order.
    $info.FileName = Join-Path $env:SystemRoot 'System32\cmd.exe'
    $info.Arguments = "/d /s /c `"`"$Exe`" 2>&1`""
    $info.WorkingDirectory = Split-Path $Exe -Parent
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::Start($info)
    $out = $process.StandardOutput.ReadToEndAsync()
    $err = $process.StandardError.ReadToEndAsync()
    $finished = $process.WaitForExit($TimeoutSeconds * 1000)
    if (-not $finished) { $process.Kill(); $process.WaitForExit() }
    $lines = @(($out.Result + $err.Result) -split "`r?`n" | Where-Object { $_ -ne '' })
    [pscustomobject]@{ Code = $process.ExitCode; Lines = $lines; TimedOut = -not $finished }
}

function Invoke-TestExe([string]$Target, [hashtable]$Built) {
    if (-not $Built[$Target]) { return Result 'FAIL' "$Target did not build (see the build output above); the old executable was not run" }
    $exe = Join-Path $buildDir "bin\$Target.exe"
    if (-not (Test-Path -LiteralPath $exe)) { return Result 'FAIL' "$exe is missing after the build" }
    $run = Invoke-Captured $exe
    $notes = @($run.Lines | Where-Object { $_ -match '^(skipped|not covered|note):' -or $_ -match '^(user save copy|local install|real archive|real boot images|real install|removal|packages:|package |prompt art|voice table|CUSA19610 1\.0[12] USA boot image)' })
    if ($run.TimedOut) { return Result 'FAIL' "timed out after $TimeoutSeconds s" $notes }
    if ($run.Code -ne 0) {
        $run.Lines | ForEach-Object { Write-Host "    $_" }
        $fails = @($run.Lines | Where-Object { $_ -match '^FAIL' } | Select-Object -First 3)
        $why = if ($fails) { $fails -join '; ' } else { "exit code $($run.Code)" }
        return Result 'FAIL' $why $notes
    }
    $last = $run.Lines | Select-Object -Last 1
    if ($last) { Write-Host "    $last" -ForegroundColor DarkGray }
    return Result 'PASS' '' $notes
}

$tc = Resolve-Toolchain 'auto'
$env:PATH = "$($tc.Bin);$env:PATH"
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'build.ninja'))) { Stop-Tool 'Run tools\build.ps1 once first: the tests reuse its build folder.' }

$started = Get-Date
$built = @{}
if ($targets.Count) {
    Sync-GpuDependencies
    Write-Step "Building $($targets.Count) test target(s) incrementally"
    $build = Invoke-Native { & ninja -C $buildDir -k 0 @targets }
    if ($build.Code -ne 0) { $build.Lines | Where-Object { $_ -notmatch 'Deprecation|cmake_minimum_required|compatibility with|^\s*$' } | ForEach-Object { Write-Host "    $_" } }
    else { $build.Lines | Select-Object -Last 1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray } }
    foreach ($target in $targets) {
        $built[$target] = $build.Code -eq 0 -or (Invoke-Native { & ninja -C $buildDir $target }).Code -eq 0
    }
}

$results = foreach ($step in $steps) {
    Write-Step "$($step.Group): $($step.Title)"
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $result = if ($step.Block) { & $step.Block } else { Invoke-TestExe $step.Target $built }
    $clock.Stop()
    switch ($result.State) {
        'PASS' { Write-Host "    PASS ($([math]::Round($clock.Elapsed.TotalSeconds, 1)) s)" -ForegroundColor Green }
        'FAIL' { Write-Host "    FAIL: $($result.Reason)" -ForegroundColor Red }
        default { Write-Host "    SKIPPED: $($result.Reason)" -ForegroundColor Yellow }
    }
    [pscustomobject]@{ Step = $step; Result = $result; Seconds = $clock.Elapsed.TotalSeconds }
}

Write-Host ''
Write-Host '13 Sentinels: Aegis Rim - Test Suite' -ForegroundColor Cyan
Write-Host ''
$counts = @{ PASS = 0; FAIL = 0; SKIPPED = 0; PARTIAL = 0 }
foreach ($name in $wanted) {
    $mine = @($results | Where-Object { $_.Step.Group -eq $name })
    $failed = @($mine | Where-Object { $_.Result.State -eq 'FAIL' })
    $skipped = @($mine | Where-Object { $_.Result.State -eq 'SKIPPED' })
    $state = if ($failed) { 'FAIL' } elseif ($skipped.Count -eq $mine.Count) { 'SKIPPED' } elseif ($skipped) { 'PARTIAL' } else { 'PASS' }
    $counts[$state]++
    $seconds = ($mine | Measure-Object -Property Seconds -Sum).Sum
    $color = @{ PASS = 'Green'; FAIL = 'Red'; SKIPPED = 'Yellow'; PARTIAL = 'Yellow' }[$state]
    Write-Host ('{0,-12}{1,-9}{2,6:N1} s' -f $name, $state, $seconds) -ForegroundColor $color
    foreach ($entry in $failed) { Write-Host "    failed: $($entry.Step.Title): $($entry.Result.Reason)" -ForegroundColor Red }
    foreach ($entry in $skipped) { Write-Host "    skipped: $($entry.Step.Title): $($entry.Result.Reason)" -ForegroundColor Yellow }
    foreach ($entry in $mine) { foreach ($note in $entry.Result.Notes) { Write-Host "    note: $note" -ForegroundColor DarkGray } }
}
Write-Host ''
Write-Host "Total: $($wanted.Count) groups"
Write-Host "Passed: $($counts.PASS)"
if ($counts.PARTIAL) { Write-Host "Passed with skipped steps: $($counts.PARTIAL)" }
Write-Host "Failed: $($counts.FAIL)"
Write-Host "Skipped: $($counts.SKIPPED)"
Write-Host ("Duration: {0:N1} s" -f ((Get-Date) - $started).TotalSeconds)
if ($counts.FAIL) { exit 1 }
exit 0
