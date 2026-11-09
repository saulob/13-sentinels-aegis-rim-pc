# Static checks of the one-program layout: 13_sentinels_aegis_rim.exe (Linux: usr/bin/13_sentinels_aegis_rim in the
# AppImage) opens the launcher and runs the game, its game process and its update restart; no separate launcher program
# is built, packaged or named; the Windows program is a GUI-subsystem PE. Nothing is built, packaged, launched or sent.
[CmdletBinding()]
param()

$root = Split-Path -Parent $PSScriptRoot
$ErrorActionPreference = 'Stop'
. (Join-Path $root 'tools\_common.ps1')

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

$program = '13_sentinels_aegis_rim'
$retired = '13_sentinels_' + 'launcher'
$self = $MyInvocation.MyCommand.Path

function Test-BuildTargets {
    $cmake = Read-Source 'CMakeLists.txt'
    $launcherCmake = Read-Source 'launcher\CMakeLists.txt'
    $shipped = foreach ($text in @($cmake, $launcherCmake)) {
        foreach ($match in [regex]::Matches($text, 'add_executable\(\s*([\w-]+)\s+([^\s)]+)')) {
            if ($match.Groups[1].Value -ne '${name}' -and $match.Groups[2].Value -ne 'EXCLUDE_FROM_ALL') { $match.Groups[1].Value }
        }
    }
    $shipped = @($shipped | Sort-Object -Unique)
    Check (($shipped -join ',') -eq 'trg-updater,tsr_game') "the default build makes only the program and the update helper (found $($shipped -join ', '))"
    Check ($cmake -match '(?s)if \(WIN32\)\s*\n(\s*#[^\n]*\n)*\s*add_executable\(tsr_game WIN32 \$\{TSR_GAME_SOURCES\}\)') 'the Windows program is a WIN32 (GUI subsystem) target'
    Check ($cmake.Contains('set_target_properties(tsr_game PROPERTIES OUTPUT_NAME "13_sentinels_aegis_rim")')) 'the program is named 13_sentinels_aegis_rim'
    Check ($launcherCmake -match 'add_executable\(trg-updater WIN32 ') 'the update helper is a WIN32 (GUI subsystem) target'
    foreach ($text in @($cmake, $launcherCmake)) {
        Check ($text -notmatch '(?i)-mconsole|subsystem[,:= ]+console') 'no CMake file links a console-subsystem program'
        Check (-not $text.Contains($retired) -and -not $text.Contains('launcher_entry')) 'no CMake file builds a separate launcher'
    }
    foreach ($file in @('src\launcher_entry.c', "src\$retired.rc")) {
        Check (-not (Test-Path -LiteralPath (Join-Path $root $file))) "$file is gone"
    }
}

function Test-StartupModes {
    $loader = Read-Source 'src\loader.c'
    Check ($loader -match 'int main\(int argc, char \*\*argv\)') 'the program has one entry point, main'
    Check ($loader.Contains('else if (!strcmp(argv[i], "--launcher")) force_launcher = 1;')) '--launcher opens the launcher of the same program'
    Check ($loader.Contains('else if (!strcmp(argv[i], "--tsr-game")) game_child = 1;')) '--tsr-game selects the game process of the same program'
    Check ($loader.Contains('TsrLauncherResult launched=tsr_launcher_run(&launcher);')) 'a normal start goes through the integrated launcher'
    Check ((Read-Source 'launcher\game_process.h').Contains('inline constexpr char kGameProcessArgument[] = "--tsr-game";')) 'the game process argument is --tsr-game'
    $launcher = Read-Source 'launcher\launcher.cpp'
    Check ($launcher.Contains('if (!CreateGameProcess(CurrentExecutablePath(), arguments, error)) {')) 'Allow overlay programs starts this same executable again'
    Check ($launcher -notmatch '\.exe"') 'the launcher names no other executable to start'
    $update = Read-Source 'launcher\update_config.cpp'
    Check ($update.Contains('config.restart_executable = install_dir / "13_sentinels_aegis_rim.exe";')) 'a Windows update restarts 13_sentinels_aegis_rim.exe'
    Check ($update.Contains('arguments.emplace_back("--launcher");')) 'an update restart opens the launcher'
    Check ($update.Contains('config.required_staged_files = {"13_sentinels_aegis_rim.exe", "trg-updater.exe", "SDL3.dll", "libc++.dll"};')) 'a later package must hold the program and the helper, and no launcher program'
    foreach ($source in @('src', 'launcher', 'gpu\shim')) {
        $files = Get-ChildItem -LiteralPath (Join-Path $root $source) -Recurse -File -Include '*.c', '*.cpp', '*.h' |
            Where-Object { $_.FullName -notmatch '\\third_party\\' }
        $console = @($files | Select-String -Pattern '\b(AllocConsole|AttachConsole|FreeConsole|GetConsoleWindow)\b' -List)
        Check ($console.Count -eq 0) "no console API is called in $source ($(@($console | ForEach-Object { $_.Path }) -join ', '))"
    }
}

function Test-LogWithoutConsole {
    $log = Read-Source 'src\runtime_log.c'
    Check ($log.Contains('if (_fileno(stdin) < 0 && freopen("NUL", "r", stdin))')) 'a GUI-subsystem start with closed standard streams still gets valid streams'
    Check ($log.Contains('freopen(path, append ? "a" : "w", stdout)') -and $log.Contains('freopen(path, "a", stderr)')) 'stdout and stderr go to the log file'
    Check ((Read-Source 'tools\test.ps1') -match "'log rotation and closed standard streams' 'tsr-log-tests'") 'the closed-stream log test runs'
}

function Test-Linux {
    $appRun = Read-Source 'packaging\linux\AppRun'
    $execs = @([regex]::Matches($appRun, '(?m)^\s*exec .*$') | ForEach-Object { $_.Value.Trim() })
    Check (($execs -join '|') -eq "exec `"`$here/usr/bin/$program`" `"`$@`"") "AppRun starts only usr/bin/$program (found $($execs -join ' | '))"
    $desktop = Read-Source 'packaging\linux\13-sentinels-aegis-rim-pc.desktop'
    $desktopExecs = @([regex]::Matches($desktop, '(?m)^Exec=(.*)$') | ForEach-Object { $_.Groups[1].Value.Trim() })
    Check (($desktopExecs -join '|') -eq 'AppRun|AppRun --launcher') "the desktop entry and its Show launcher action start AppRun (found $($desktopExecs -join ' | '))"
    $linuxPackage = Read-Source 'tools\package-linux.sh'
    Check ($linuxPackage -match "(?m)^binary=$program$") 'the AppImage packages the one program'
    Check (-not $linuxPackage.Contains($retired)) 'the AppImage packages no launcher program'
}

function Test-Packaging {
    $package = Read-Source 'tools\package.ps1'
    Check ($package.Contains("`$Programs = @('13_sentinels_aegis_rim.exe', 'trg-updater.exe')")) 'the Windows package stages the program and the update helper only'
    Check ($package.Contains("`$RootFiles = @('13_sentinels_aegis_rim.exe', 'trg-updater.exe', 'README.txt', 'LICENSE.txt')")) 'no other executable may sit at the top of the package'
    Check ($package.Contains("`$RetiredPrograms = @('$retired.exe')") -and $package.Contains('separate launcher executable: $retired')) 'the release check refuses a separate launcher executable'
    Check ($package.Contains('$subsystem = Get-PeSubsystem $path') -and $package.Contains('if ($subsystem -ne 2)')) 'the release check refuses a staged program that is not a GUI-subsystem PE'
    Check ((Read-Source 'tools\package-release.ps1').Contains("`$programs = @('13_sentinels_aegis_rim.exe', 'trg-updater.exe')")) 'the combined release hashes the program and the helper only'
}

function Test-PeSubsystem {
    $system = $env:SystemRoot
    Check ((Get-PeSubsystem (Join-Path $system 'explorer.exe')) -eq 2) 'Get-PeSubsystem reads a Windows GUI program as 2'
    Check ((Get-PeSubsystem (Join-Path $system 'System32\cmd.exe')) -eq 3) 'Get-PeSubsystem reads a console program as 3'
    Check ((Get-PeSubsystem $self) -eq -1) 'Get-PeSubsystem refuses a file that is not a PE'
    $built = Join-Path $root 'build\cmake\clang64\bin'
    foreach ($exe in @("$program.exe", 'trg-updater.exe')) {
        $path = Join-Path $built $exe
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $subsystem = Get-PeSubsystem $path
            Check ($subsystem -eq 2) "the built $exe is a Windows GUI program (PE subsystem $subsystem)"
        }
    }
}

function Test-NoSeparateLauncherNamed {
    $folders = @('src', 'launcher', 'tools', 'tests', 'docs', 'packaging', 'server', 'gpu\shim')
    $files = foreach ($folder in $folders) {
        Get-ChildItem -LiteralPath (Join-Path $root $folder) -Recurse -File |
            Where-Object { $_.FullName -notmatch '\\(third_party|node_modules|__pycache__)\\' -and $_.Extension -notin @('.png', '.ico', '.ttf', '.otf', '.bin', '.dcpx') }
    }
    $files = @($files) + @(Get-Item -LiteralPath (Join-Path $root 'README.md'), (Join-Path $root 'CMakeLists.txt'))
    $hits = foreach ($file in $files) {
        if ($file.FullName -eq $self) { continue }
        foreach ($line in [IO.File]::ReadAllLines($file.FullName)) {
            if ($line.Contains($retired) -and -not $line.StartsWith('$RetiredPrograms = ')) { "$($file.FullName.Substring($root.Length + 1)): $($line.Trim())" }
        }
    }
    $hits = @($hits)
    foreach ($hit in $hits) { Write-Host "  $hit" }
    Check ($hits.Count -eq 0) "nothing outside the release check's refusal names $retired"
}

function Test-NoScanStaging {
    $scripts = @('tools\package.ps1', 'tools\package-release.ps1', 'tools\package-linux.sh', 'tools\package-release-linux.sh',
        'docs\RELEASE_SECURITY.md', 'docs\LINUX.md', 'packaging\release-notes.md')
    foreach ($script in $scripts) {
        $text = Read-Source $script
        Check ($text -notmatch '(?i)dist[\\/]+virustotal|SHA256SUMS|release candidates') "$script stages no copies for VirusTotal"
    }
    Check (-not (Test-Path -LiteralPath (Join-Path $root 'dist\virustotal'))) 'no dist\virustotal folder exists'
}

Test-BuildTargets
Test-StartupModes
Test-LogWithoutConsole
Test-Linux
Test-Packaging
Test-PeSubsystem
Test-NoSeparateLauncherNamed
Test-NoScanStaging

if ($script:failures) {
    Write-Host "FAILED: $script:failures of $script:checks checks" -ForegroundColor Red
    exit 1
}
Write-Host "one program: $script:checks checks passed (launcher, game process, overlay and update restart in 13_sentinels_aegis_rim; GUI subsystem; no launcher program built, packaged or named)"
exit 0
