# Static checks of the release and run scripts: which artifacts tools\package.ps1 hashes, that nothing is uploaded or
# published, that docs\RELEASE_SECURITY.md lists the same files, and that tools\build.ps1 -Run keeps the terminal quiet while
# the runtime still writes its log file. Nothing is built, packaged, launched or sent.
[CmdletBinding()]
param()

$root = Split-Path -Parent $PSScriptRoot
$ErrorActionPreference = 'Stop'

$script:checks = 0
$script:failures = 0
function Check([bool]$Passed, [string]$Name) {
    $script:checks++
    if (-not $Passed) {
        $script:failures++
        Write-Host "FAIL: $Name" -ForegroundColor Red
    }
}
function Read-Source([string]$Relative) { Get-Content -LiteralPath (Join-Path $root $Relative) -Raw }

$package = Read-Source 'tools\package.ps1'
$build = Read-Source 'tools\build.ps1'
$security = Read-Source 'docs\RELEASE_SECURITY.md'
$runtimeLog = Read-Source 'src\runtime_log.c'

# The executables the package script stages at the top of the folder.
$null = $package -match '(?m)^\$RootFiles = @\((.+)\)\s*$'
$rootExes = @([regex]::Matches($Matches[1], "'([^']+\.exe)'") | ForEach-Object { $_.Groups[1].Value })
$null = $package -match "foreach \(\`$program in @\(([^)]*)\)\)"
$hashed = @([regex]::Matches($Matches[1], "'([^']+)'") | ForEach-Object { $_.Groups[1].Value })

function Test-ReleaseSecurity {
    Check ($rootExes.Count -ge 1) 'the package script stages at least the main executable'
    Check ($rootExes -contains '13_sentinels_aegis_rim.exe') 'the main 13 Sentinels executable is staged'
    Check ((($hashed | Sort-Object) -join ' ') -eq (($rootExes | Sort-Object) -join ' ')) 'every staged executable, and only those, is hashed'
    Check ($package -match 'Get-FileHash -LiteralPath \(Join-Path \$stage \$program\) -Algorithm SHA256') 'each executable hash is read from the staged copy that goes into the ZIP'
    Check ($package -match 'Windows ZIP SHA-256:\s+\$hash') 'the final ZIP hash is printed'
    Check ($package -match "\{0\} SHA-256: \{1\}") 'each executable hash is printed with its name'
    Check ($package -match 'docs\\RELEASE_SECURITY\.md') 'the final output points at the release security notes'
    Check ($package.IndexOf('Windows ZIP SHA-256') -gt $package.IndexOf('RELEASE PACKAGE READY')) 'the hashes are printed in the final summary'

    $hasUpdater = (Test-Path -LiteralPath (Join-Path $root 'launcher\updater')) -or ($rootExes | Where-Object { $_ -match 'updater' })
    if ($hasUpdater) {
        Check ([bool]($hashed | Where-Object { $_ -match 'updater' })) 'an updater the project ships is hashed'
    } else {
        Check (-not ($hashed | Where-Object { $_ -match 'updater' })) 'no updater hash is printed: the project ships no updater'
        Check ($package -notmatch 'trg-updater' -and $security -notmatch 'trg-updater') 'no updater is named in the package script or the notes'
    }
    $hasLinux = (Test-Path -LiteralPath (Join-Path $root 'packaging\linux')) -or (Test-Path -LiteralPath (Join-Path $root 'tools\package-linux.sh'))
    $tableRows = @($security -split "`r?`n" | Where-Object { $_ -match '^\|' })
    if ($hasLinux) {
        $combined = Read-Source 'tools\package-release.ps1'
        $linux = Read-Source 'tools\package-linux.sh'
        $notes = Read-Source 'packaging\release-notes.md'
        Check ($combined -match 'Linux AppImage SHA-256:' -and $linux -match 'Linux AppImage SHA-256:') 'the Linux AppImage the project builds is hashed'
        Check ([bool]($tableRows | Where-Object { $_ -match 'x86_64\.AppImage' })) 'the AppImage is listed in the release security notes'
        Check ($security.Contains('13SentinelsAegisRimPC-v<version>-x86_64.AppImage')) 'the release notes list the AppImage'
        $virusTotal = if ($notes -match '(?s)## VirusTotal\s*\n(.*?)(\n## |\z)') { $Matches[1] } else { '' }
        $entries = @([regex]::Matches($virusTotal, '(?m)^- ([^(:]+?)\s*(\(|:)') | ForEach-Object { $_.Groups[1].Value })
        Check (($entries -join ',') -eq 'Windows ZIP,Linux AppImage,Windows executable,Updater') "the release notes list exactly the VirusTotal entries ZIP, AppImage, executable, updater (found $($entries -join ', '))"
        foreach ($entry in $entries) {
            Check ($virusTotal -match "(?m)^- $([regex]::Escape($entry))\b.*: PENDING$") "the release notes leave VirusTotal '$entry' PENDING"
        }
        Check ($notes -notmatch '(?i)launcher\.exe|_launcher\b|Windows launcher') 'the release notes name no separate launcher executable'
        Check ($notes -match 'run \*\*13_sentinels_aegis_rim\.exe\*\*') 'the release notes tell Windows players to run 13_sentinels_aegis_rim.exe'
        Check (-not $combined.Contains('LAUNCHER_EXE_SHA256') -and -not $notes.Contains('LAUNCHER_EXE_SHA256')) 'no launcher hash placeholder is filled or expected'
        Check ($combined -match 'Parity gate' -and $combined -match 'feature-contract-windows\.txt' -and $combined -match 'feature-contract-linux\.txt') 'the combined release compares the Windows and Linux feature contracts'
        Check ($combined -match "RELEASE NOT PREPARED") 'the combined release stops when a platform fails'
    } else {
        Check ($package -notmatch 'AppImage') 'no AppImage hash is printed: the project has no Linux packaging'
        Check (-not ($tableRows | Where-Object { $_ -match 'AppImage' })) 'no AppImage row in the release notes table'
    }

    foreach ($file in @('13SentinelsAegisRimPC-v<version>-windows-x64.zip') + $rootExes) {
        Check ($security.Contains($file)) "the release notes list $file"
    }
    Check ($security -match 'does not cover the programs inside it') 'the notes say a ZIP scan alone is not enough'
    Check ($security -match 'Uploads stay manual') 'the notes keep uploads manual'
}

function Test-NothingIsSent {
    foreach ($forbidden in @('Invoke-WebRequest', 'Invoke-RestMethod', 'curl ', 'wget ', 'Start-BitsTransfer', 'Net.WebClient',
            'virustotal.com', 'gh release', 'gh api', 'git tag', 'git push', 'git commit', 'Publish-')) {
        Check (-not $package.Contains($forbidden)) "package.ps1 never uses $forbidden"
    }
    foreach ($script in @('tools\package-release.ps1', 'tools\package-linux.sh', 'tools\package-release-linux.sh')) {
        if (-not (Test-Path -LiteralPath (Join-Path $root $script))) { continue }
        $text = Read-Source $script
        foreach ($forbidden in @('Invoke-WebRequest', 'Invoke-RestMethod', 'virustotal.com', 'gh release', 'gh api', 'git tag',
                'git push', 'git commit', 'git add', 'Publish-', 'api.github.com', 'uploads.github.com')) {
            Check (-not $text.Contains($forbidden)) "$script never uses $forbidden"
        }
        Check ($text -match 'nothing is published, tagged or uploaded') "$script states that it publishes nothing"
    }
    Check ($package -match 'nothing is published, tagged or uploaded') 'package.ps1 states that it publishes nothing'
    Check ($package -match 'RELEASE PACKAGE READY \(nothing was published\)') 'the summary states that nothing was published'
    Check ($package -match 'uploads are manual') 'the summary says uploads are manual'
}

function Test-QuietRun {
    Check ($build -match '\[switch\]\$VerboseRuntime') 'build.ps1 has -VerboseRuntime'
    Check ($build.Contains('$quiet = -not ($VerboseRuntime -or $CheckImports -or $TimeoutSeconds -gt 0)')) 'the terminal is quiet unless verbose, import checks or a timed run'
    Check ($build.Contains("RedirectStandardOutput = 'NUL'")) 'a quiet run sends the inherited stdout to NUL'
    Check ($build -match 'RedirectStandardError = ''\\\.\\NUL''') 'a quiet run sends stderr to NUL under a spelling Start-Process accepts'
    Check ($build.Contains('Write-Host "Runtime logs: $logsDir"')) 'the log folder is printed'
    Check ($build.Contains('Write-Warn "Abnormal exit; see the runtime logs in $logsDir"')) 'an abnormal exit points at the log folder'
    Check ($build.Contains("'--logs'")) 'a custom --logs folder is the one reported'
    Check ($build -match 'if \(\$quiet\) \{ \$start\.Redirect') 'only a quiet run redirects'
    Check ($runtimeLog.Contains('freopen(path, append ? "a" : "w", stdout)')) 'the runtime still writes its log file'
    Check ($runtimeLog.Contains('freopen(path, "a", stderr)')) 'stderr, where errors and crashes go, still reaches the log file'
    Check (-not $build.Contains('-VerboseRuntime') -or $build.Contains('streams the runtime output live')) 'the verbose switch is documented'
}

function Test-PackagingReadme {
    $readme = Read-Source 'packaging\README.txt'
    Check ($readme -notmatch '(?i)game version') 'the package README names no Game version selector'
    Check ($readme -notmatch '(?i)chooses which starts') 'the package README does not say a choice decides which version starts'
    foreach ($version in @('1.00', '1.01', '1.02')) {
        Check ($readme.Contains($version)) "the package README names USA $version"
    }
    Check ($readme -match 'newest supported update') 'the package README says the newest installed supported update starts'
    Check ($readme -match 'detects the\s+installed builds by itself') 'the package README says installed builds are detected automatically'
    Check ($readme -match 'no version to pick') 'the package README says the version is not picked'
}

Test-ReleaseSecurity
Test-NothingIsSent
Test-QuietRun
Test-PackagingReadme

if ($script:failures) {
    Write-Host "FAILED: $script:failures of $script:checks checks" -ForegroundColor Red
    exit 1
}
Write-Host "release scripts: $script:checks checks passed (hashes of the ZIP and every staged executable, nothing sent, quiet run with a log file)"
exit 0
