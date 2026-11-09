<#
.SYNOPSIS
  Prepares the complete release in dist\: the Windows x64 ZIP and the Linux x86_64 AppImage, each with its .sha256
  file, and dist\release-notes-v<version>.md. Local only: nothing is published, tagged or uploaded.
.DESCRIPTION
  1. Windows: tools\package.ps1 (build, package, release folder checks), tools\test.ps1 (every headless test) and the
     Windows feature contract (tsr-platform-parity-tests --contract).
  2. Linux: tools/package-release-linux.sh through wsl.exe (build, every headless Linux test, the Linux feature contract,
     the packaging script's self-test, tools/package-linux.sh with its AppDir and AppImage checks).
  3. Gates: both .sha256 files match their artifacts, the Windows release folder is checked again, every name and
     contract carries the one project VERSION, and the Windows and Linux feature contracts are identical.
  4. dist\release-notes-v<version>.md from packaging\release-notes.md, with the SHA-256 of every file to scan and each
     VirusTotal entry PENDING.
  Both platforms run even if the first fails, so one run shows every problem; the release counts as prepared only when
  everything passed. VirusTotal comes next, by hand (docs\RELEASE_SECURITY.md): do not publish before it is complete.

  The Linux side needs a WSL2 Ubuntu 20.04 distribution (the AppImage's glibc baseline) prepared once by
  packaging/linux/ci-deps.sh, as docs\LINUX.md describes. Nothing is installed or changed in WSL.

  Inputs kept in the ignored cache\ folder (tools\release-inputs.ps1): cache\test-images\<build>\boot.bin, the real
  boot images of the nine builds, completed from game\image and build\image; and cache\appimage-runtime\runtime-x86_64,
  the reviewed AppImage runtime, used only with the SHA-256 pinned in patches\appimage-runtime\baseline.env and, when
  missing, copied from a WSL packaging cache that holds the same file.
.PARAMETER TestImages
  Another folder with the nine builds' boot images (<folder>\CUSA19610-1.00\boot.bin and so on), every one present.
  Defaults to $env:TSR_TEST_IMAGES, else cache\test-images.
.PARAMETER NoTestImages
  Prepare the release without the real-image checks (not for a published release).
.PARAMETER Clean
  Clean build for both platforms.
.PARAMETER SkipBuild
  Package the existing builds of both platforms (their tests are not run again).
.PARAMETER WslDistro
  The WSL distribution to build in.
.EXAMPLE
  .\tools\package-release.ps1
#>
[CmdletBinding()]
param(
    [string]$TestImages = $env:TSR_TEST_IMAGES,
    [switch]$NoTestImages,
    [switch]$Clean,
    [switch]$SkipBuild,
    [string]$WslDistro = 'DcpFocalBuild'
)

. (Join-Path $PSScriptRoot '_common.ps1')
. (Join-Path $PSScriptRoot 'release-inputs.ps1')

$root = $script:RepoRoot
$cmake = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmake -notmatch '(?m)^project\(thirteen_sentinels_aegis_rim_pc VERSION ([0-9]+\.[0-9]+\.[0-9]+)') { Stop-Tool 'project VERSION not found in CMakeLists.txt' }
$version = $Matches[1]
$dist = Join-Path $root 'dist'
$release = Join-Path $root 'build\release'
$windowsName = "13SentinelsAegisRimPC-v$version-windows-x64"
$linuxName = "13SentinelsAegisRimPC-v$version-x86_64"
$windowsFiles = @("$windowsName.zip", "$windowsName.zip.sha256")
$linuxFiles = @("$linuxName.AppImage", "$linuxName.AppImage.sha256")
$windowsContract = Join-Path $release 'feature-contract-windows.txt'
$linuxContract = Join-Path $release 'feature-contract-linux.txt'
$notesFile = Join-Path $dist "release-notes-v$version.md"
$stage = Join-Path $dist $windowsName
$programs = @('13_sentinels_aegis_rim.exe', 'trg-updater.exe')

$nineBuilds = $script:NineBuilds
if ($NoTestImages) {
    $TestImages = ''
} else {
    try { $TestImages = Resolve-TestImages $root $TestImages } catch { Stop-Tool $_.Exception.Message }
    if (-not $TestImages) {
        $absent = Get-MissingTestImages (Join-Path $root 'cache\test-images')
        Stop-Tool "No real boot image of $($absent -join ', ') in cache\test-images, game\image or build\image. Install those builds with the launcher, which makes their boot images, or give -TestImages."
    }
}

function Test-Checksum([string]$Artifact, [string]$Checksum) {
    $line = (Get-Content -LiteralPath $Checksum -TotalCount 1)
    if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { return "$(Split-Path $Checksum -Leaf) is not '<sha256>  <file name>'" }
    if ($Matches[2] -ne (Split-Path $Artifact -Leaf)) { return "$(Split-Path $Checksum -Leaf) names $($Matches[2])" }
    $actual = (Get-FileHash -LiteralPath $Artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $Matches[1]) { return "$(Split-Path $Checksum -Leaf) does not match the file" }
    return $null
}

function Test-Artifacts([string[]]$Files) {
    $problems = New-Object System.Collections.Generic.List[string]
    foreach ($file in $Files) {
        if (-not (Test-Path -LiteralPath (Join-Path $dist $file) -PathType Leaf)) { $problems.Add("missing: $file") }
    }
    if ($problems.Count -eq 0) {
        $problem = Test-Checksum (Join-Path $dist $Files[0]) (Join-Path $dist $Files[1])
        if ($problem) { $problems.Add($problem) }
    }
    return $problems
}

function Remove-Artifacts([string[]]$Files) {
    foreach ($file in $Files) { Remove-Item -LiteralPath (Join-Path $dist $file) -ErrorAction SilentlyContinue }
}

function Get-Sha256([string]$Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

$results = [ordered]@{ 'Windows package' = $false; 'Windows tests' = $false; 'Linux AppImage' = $false; 'Parity gate' = $false; 'Release notes' = $false }
$notes = New-Object System.Collections.Generic.List[string]
$powershell = (Get-Process -Id $PID).Path

Write-Step "13 Sentinels: Aegis Rim PC Port $version - release packages for Windows and Linux (local only)"
if ($TestImages) { Write-Host "Real boot images: $TestImages" } else { Write-Warn 'Real boot images: not checked (-NoTestImages)' }
New-Item -ItemType Directory -Force $dist, $release | Out-Null
Remove-Item -LiteralPath $windowsContract, $linuxContract, $notesFile -ErrorAction SilentlyContinue

# Windows x64
Write-Step 'Windows x64 ZIP (tools\package.ps1)'
Remove-Artifacts $windowsFiles
$windowsArgs = @()
if ($Clean) { $windowsArgs += '-Clean' }
if ($SkipBuild) { $windowsArgs += '-SkipBuild' }
& $powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'package.ps1') @windowsArgs
$windowsProblems = @()
if ($LASTEXITCODE -ne 0) { $windowsProblems += "tools\package.ps1 failed (exit $LASTEXITCODE)" }
else {
    $windowsProblems = @(Test-Artifacts $windowsFiles)
    & $powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'package.ps1') -VerifyOnly $stage
    if ($LASTEXITCODE -ne 0) { $windowsProblems += "the release folder $stage did not pass tools\package.ps1 -VerifyOnly" }
    foreach ($program in $programs) {
        if (-not (Test-Path -LiteralPath (Join-Path $stage $program) -PathType Leaf)) { $windowsProblems += "missing in the release folder: $program" }
    }
    $built = (Get-Item -LiteralPath (Join-Path $stage '13_sentinels_aegis_rim.exe') -ErrorAction SilentlyContinue).VersionInfo.ProductVersion
    if ($built -ne $version) { $windowsProblems += "13_sentinels_aegis_rim.exe is version '$built', CMakeLists.txt says $version" }
}
$results['Windows package'] = ($windowsProblems.Count -eq 0)
foreach ($problem in $windowsProblems) { $notes.Add("Windows: $problem") }

Write-Step 'Windows headless tests (tools\test.ps1) and the Windows feature contract'
$testProblems = @()
$previousImages = $env:TSR_TEST_IMAGES
try {
    if ($TestImages) { $env:TSR_TEST_IMAGES = $TestImages } else { Remove-Item Env:\TSR_TEST_IMAGES -ErrorAction SilentlyContinue }
    if ($SkipBuild) {
        Write-Host 'Skipped with -SkipBuild: the existing Windows tests are not run again.'
    } else {
        & $powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'test.ps1')
        if ($LASTEXITCODE -ne 0) { $testProblems += "tools\test.ps1 failed (exit $LASTEXITCODE)" }
    }
    $parity = Join-Path $root 'build\cmake\clang64\bin\tsr-platform-parity-tests.exe'
    if (-not (Test-Path -LiteralPath $parity)) { $testProblems += "missing $parity (run without -SkipBuild)" }
    else {
        $tc = Resolve-Toolchain 'auto'
        $savedPath = $env:PATH
        $env:PATH = "$($tc.Bin);$env:PATH"
        Push-Location (Split-Path $parity -Parent)
        try { & $parity --contract $windowsContract | Out-Host } finally { Pop-Location; $env:PATH = $savedPath }
        if ($LASTEXITCODE -ne 0) { $testProblems += "tsr-platform-parity-tests failed on Windows (exit $LASTEXITCODE)" }
    }
} finally {
    if ($null -eq $previousImages) { Remove-Item Env:\TSR_TEST_IMAGES -ErrorAction SilentlyContinue } else { $env:TSR_TEST_IMAGES = $previousImages }
}
$results['Windows tests'] = ($testProblems.Count -eq 0)
foreach ($problem in $testProblems) { $notes.Add("Windows: $problem") }

# Linux x86_64 through WSL
Write-Step 'Linux x86_64 AppImage (tools\package-release-linux.sh in WSL)'
Remove-Artifacts $linuxFiles
$wslArgs = @('-d', $WslDistro)
$linuxProblems = @()
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    $linuxProblems += 'wsl.exe was not found. The AppImage is built in WSL2, in an Ubuntu 20.04 distribution prepared as docs\LINUX.md describes.'
} else {
    $probe = & wsl.exe @wslArgs --exec /bin/sh -c '. /etc/os-release; echo "$(uname -sm) $VERSION_ID"' 2>&1
    if ($LASTEXITCODE -ne 0 -or "$probe" -notmatch 'Linux x86_64 20\.04') {
        $linuxProblems += "WSL distribution $WslDistro is not a usable Ubuntu 20.04 ($(("$probe" -replace '\0', '').Trim())). Create it as docs\LINUX.md describes, or name another with -WslDistro."
    } else {
        $repoWsl = (& wsl.exe @wslArgs --exec /usr/bin/wslpath -a -u $root | Out-String).Trim()
        $imagesWsl = if ($TestImages) { (& wsl.exe @wslArgs --exec /usr/bin/wslpath -a -u $TestImages | Out-String).Trim() } else { '' }
        if (-not $repoWsl -or ($TestImages -and -not $imagesWsl)) {
            $linuxProblems += 'wslpath could not convert the repository or test image path.'
        } else {
            $importRuntime = {
                param($File, $Sha256)
                $target = (& wsl.exe @wslArgs --exec /usr/bin/wslpath -a -u $File | Out-String).Trim()
                & wsl.exe @wslArgs --exec /bin/bash "$repoWsl/tools/import-appimage-runtime.sh" $Sha256 $target | Out-Host
            }
            $runtime = Resolve-ReviewedRuntime $root $importRuntime
            $runtimeWsl = ''
            if ($runtime) {
                $runtimeWsl = (& wsl.exe @wslArgs --exec /usr/bin/wslpath -a -u $runtime | Out-String).Trim()
                Write-Host "AppImage runtime: $runtime (the pinned SHA-256 $(Get-PinnedRuntimeSha256 $root))"
            } else {
                Write-Warn 'AppImage runtime: no reviewed copy found; tools/build-appimage-runtime.sh builds one and refuses it unless it has the pinned SHA-256.'
            }
            $linuxArgs = @()
            if ($Clean) { $linuxArgs += '--clean' }
            if ($SkipBuild) { $linuxArgs += '--skip-build' }
            & wsl.exe @wslArgs --exec /usr/bin/env "TSR_TEST_IMAGES=$imagesWsl" "TSR_APPIMAGE_RUNTIME=$runtimeWsl" /bin/bash "$repoWsl/tools/package-release-linux.sh" @linuxArgs
            if ($LASTEXITCODE -ne 0) {
                $linuxProblems += "tools/package-release-linux.sh failed (exit $LASTEXITCODE). If it listed missing tools, prepare WSL once with: wsl -d $WslDistro -u root --exec /bin/bash $repoWsl/packaging/linux/ci-deps.sh"
            } else {
                $linuxProblems = @(Test-Artifacts $linuxFiles)
            }
        }
    }
}
$results['Linux AppImage'] = ($linuxProblems.Count -eq 0)
foreach ($problem in $linuxProblems) { $notes.Add("Linux: $problem") }

# The two platforms offer the same features: their contracts, computed from the same tables, must be identical.
Write-Step 'Parity gate: the Windows and Linux feature contracts'
$parityProblems = @()
foreach ($contract in @($windowsContract, $linuxContract)) {
    if (-not (Test-Path -LiteralPath $contract -PathType Leaf)) { $parityProblems += "missing $(Split-Path $contract -Leaf)" }
}
if ($parityProblems.Count -eq 0) {
    $windowsText = [IO.File]::ReadAllText($windowsContract)
    $linuxText = [IO.File]::ReadAllText($linuxContract)
    if ($windowsText -ne $linuxText) {
        $difference = Compare-Object ($windowsText -split "`n") ($linuxText -split "`n") | Select-Object -First 8
        foreach ($line in $difference) { $parityProblems += "only on $(if ($line.SideIndicator -eq '<=') { 'Windows' } else { 'Linux' }): $($line.InputObject)" }
    }
    if (-not $windowsText.StartsWith("port $version`n")) { $parityProblems += "the feature contract is not for version $version" }
    $builds = @([regex]::Matches($windowsText, '(?m)^build (\S+)') | ForEach-Object { $_.Groups[1].Value })
    if ((($builds | Sort-Object) -join ',') -ne (($nineBuilds | Sort-Object) -join ',')) { $parityProblems += "the contract covers $($builds -join ', ')" }
    $imageLines = ([regex]::Matches($windowsText, '(?m)^  image\.identity ')).Count
    if ($TestImages -and $imageLines -ne $nineBuilds.Count) { $parityProblems += "the contract holds real-image checks of $imageLines of $($nineBuilds.Count) builds" }
    if ($parityProblems.Count -eq 0) { Write-Ok "Windows and Linux feature contracts are identical (sha256 $((Get-FileHash -LiteralPath $windowsContract -Algorithm SHA256).Hash.ToLowerInvariant()))" }
}
$results['Parity gate'] = ($parityProblems.Count -eq 0)
foreach ($problem in $parityProblems) { $notes.Add("Parity: $problem") }

$hashes = [ordered]@{}
if ($results['Windows package'] -and $results['Linux AppImage']) {
    Write-Step "Release notes ($(Split-Path $notesFile -Leaf))"
    $hashes["$windowsName.zip"] = Get-Sha256 (Join-Path $dist "$windowsName.zip")
    $hashes["$linuxName.AppImage"] = Get-Sha256 (Join-Path $dist "$linuxName.AppImage")
    foreach ($program in $programs) { $hashes[$program] = Get-Sha256 (Join-Path $stage $program) }
    $values = [ordered]@{
        '@VERSION@' = $version
        '@WINDOWS_ZIP_SHA256@' = $hashes["$windowsName.zip"]
        '@LINUX_APPIMAGE_SHA256@' = $hashes["$linuxName.AppImage"]
        '@GAME_EXE_SHA256@' = $hashes['13_sentinels_aegis_rim.exe']
        '@UPDATER_EXE_SHA256@' = $hashes['trg-updater.exe']
    }
    $text = Get-Content -LiteralPath (Join-Path $root 'packaging\release-notes.md') -Raw
    foreach ($key in $values.Keys) { $text = $text.Replace($key, $values[$key]) }
    if ($text -match '@[A-Z_0-9]+@') { $notes.Add("Release notes: unreplaced placeholder $($Matches[0])") }
    else {
        [IO.File]::WriteAllText($notesFile, ($text -replace "\r?\n", "`n"), (New-Object Text.UTF8Encoding $false))
        $results['Release notes'] = $true
    }
} else {
    $notes.Add('Release notes: not written, a platform failed')
}

Write-Host ''
foreach ($key in $results.Keys) {
    if ($results[$key]) { Write-Host ("{0}: OK" -f $key) -ForegroundColor Green }
    else { Write-Host ("{0}: FAILED" -f $key) -ForegroundColor Red }
}
if ($results.Values -contains $false) {
    Write-Host ''
    foreach ($note in $notes) { Write-Host "  $note" -ForegroundColor Red }
    Write-Host ''
    Write-Host 'RELEASE NOT PREPARED' -ForegroundColor Red
    exit 1
}

Write-Host ''
Write-Host 'RELEASE PREPARED (nothing was published)' -ForegroundColor Green
foreach ($file in $windowsFiles + $linuxFiles) { Write-Host "  $(Join-Path $dist $file)" }
Write-Host "  $notesFile"
Write-Host "  Symbols (not for release): $(Join-Path $dist "$windowsName-symbols"), $(Join-Path $dist "$linuxName-symbols")"
Write-Host ''
Write-Host 'SHA-256 of the files to scan on VirusTotal (each on its own; uploads are manual):'
Write-Host "  Windows ZIP SHA-256:                $($hashes["$windowsName.zip"])"
Write-Host "  Linux AppImage SHA-256:             $($hashes["$linuxName.AppImage"])"
foreach ($program in $programs) { Write-Host ("  {0} SHA-256: {1}" -f $program.PadRight(28), $hashes[$program]) }
Write-Host ''
Write-Host "Next: scan these files on VirusTotal and replace each PENDING in $(Split-Path $notesFile -Leaf). Do not publish before that." -ForegroundColor Yellow
