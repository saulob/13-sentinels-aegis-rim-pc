# Static checks of the application icon and the launcher palette: the canonical SVG, the generated ICO and PNGs, the
# Windows resources, the relink dependency, the license entry and the semantic launcher colors. Nothing is rendered,
# built, launched or sent.
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
function Join-Root([string]$Relative) { Join-Path $root $Relative }
function Read-Source([string]$Relative) { Get-Content -LiteralPath (Join-Root $Relative) -Raw }
function Get-Channels([string]$Source, [string]$Name) {
    $values = foreach ($channel in 'Red', 'Green', 'Blue') {
        $match = [regex]::Match($Source, "k$Name$channel = 0x([0-9A-Fa-f]{2});")
        if ($match.Success) { $match.Groups[1].Value.ToUpper() } else { '??' }
    }
    '#' + ($values -join '')
}
function Read-BigEndian([byte[]]$Bytes, [int]$Offset) {
    ([int]$Bytes[$Offset] -shl 24) -bor ([int]$Bytes[$Offset + 1] -shl 16) -bor ([int]$Bytes[$Offset + 2] -shl 8) -bor [int]$Bytes[$Offset + 3]
}

$palette = Read-Source 'src\tsr_accent.h'
$teal = Get-Channels $palette 'Teal'
$lightCyan = Get-Channels $palette 'LightCyan'
$accent = Get-Channels $palette 'Accent'
Check ($teal -eq '#1990A7') "dark teal is $teal, expected #1990A7"
Check ($lightCyan -eq '#90E0EF') "light cyan is $lightCyan, expected #90E0EF"
Check ($accent -eq '#4FC3E0') "bright cyan accent and action is $accent, expected #4FC3E0"

Check (-not (Test-Path -LiteralPath (Join-Root 'game-console-ecommerce-svgrepo-com.svg'))) 'the root source SVG copy is gone'
Check (Test-Path -LiteralPath (Join-Root 'packaging\icons\app-icon.svg')) 'packaging\icons\app-icon.svg exists'
$svg = Read-Source 'packaging\icons\app-icon.svg'
Check ($svg.IndexOf($teal, [StringComparison]::OrdinalIgnoreCase) -ge 0) "app-icon.svg uses $teal"
Check ($svg.IndexOf($lightCyan, [StringComparison]::OrdinalIgnoreCase) -ge 0) "app-icon.svg uses $lightCyan"
Check ($svg -notmatch 'rgb\(') 'app-icon.svg colors are explicit hex values'
Check (([regex]::Matches($svg, '<path ')).Count -eq 2) 'app-icon.svg keeps its two paths'

foreach ($size in 16, 24, 32, 48, 64, 128, 256, 512) {
    $path = Join-Root "packaging\icons\app-icon-$size.png"
    if (-not (Test-Path -LiteralPath $path)) { Check $false "app-icon-$size.png exists"; continue }
    $bytes = [IO.File]::ReadAllBytes($path)
    Check ($bytes.Length -gt 33 -and [BitConverter]::ToString($bytes[0..3]) -eq '89-50-4E-47') "app-icon-$size.png is a PNG"
    Check ((Read-BigEndian $bytes 16) -eq $size -and (Read-BigEndian $bytes 20) -eq $size) "app-icon-$size.png is ${size}x${size}"
    Check ($bytes[24] -eq 8 -and $bytes[25] -eq 6) "app-icon-$size.png is 8-bit RGBA"
}

$icoPath = Join-Root 'packaging\icons\app-icon.ico'
Check (Test-Path -LiteralPath $icoPath) 'app-icon.ico exists'
$ico = [IO.File]::ReadAllBytes($icoPath)
$frames = [BitConverter]::ToUInt16($ico, 4)
$sizes = @(0..($frames - 1) | ForEach-Object { $w = $ico[6 + 16 * $_]; if ($w -eq 0) { 256 } else { [int]$w } })
Check (($sizes -join ',') -eq '16,24,32,48,64,128,256') "app-icon.ico frames are $($sizes -join ',')"

Check ((Read-Source 'src\13_sentinels_aegis_rim.rc') -match '(?m)^1 ICON "\.\./packaging/icons/app-icon\.ico"') 'src\13_sentinels_aegis_rim.rc references app-icon.ico'
$resources = @(Get-ChildItem -LiteralPath (Join-Root 'src') -Filter '*.rc' | ForEach-Object { $_.Name })
Check (($resources -join ',') -eq '13_sentinels_aegis_rim.rc') "the one program has the one resource file (found $($resources -join ', '))"
$cmake = Read-Source 'CMakeLists.txt'
Check ($cmake -match 'src/13_sentinels_aegis_rim\.rc PROPERTIES OBJECT_DEPENDS\s+"[^"]*packaging/icons/app-icon\.ico') 'the program resource relinks when app-icon.ico changes'

$licenses = Read-Source 'packaging\licenses\THIRD-PARTY-LICENSES.txt'
Check ($licenses -match 'Application icon: Game Console Ecommerce') 'the icon license entry exists'
Check ($licenses -match 'CC0' -and $licenses -match 'svgrepo\.com/svg/425126') 'the icon license entry names CC0 and the SVG Repo source'
Check ($licenses -match '#1990A7' -and $licenses -match '#90E0EF') 'the icon license entry states the launcher palette colors'

$launcher = Read-Source 'launcher\launcher.cpp'
Check ($launcher -match 'WithAccent\(kAccent\)\.WithAction\(kAccent, kActionEdge, kActionLink\)') 'the launcher theme sets action, edge and link colors'
Check ($launcher -match 'kActionEdge = ImVec4\(kTealRed') 'the action edge is the dark teal'
Check ($launcher -match 'kActionLink = ImVec4\(kLightCyanRed') 'the action link is the light cyan'
Check ($launcher -match 'theme\.warn = ImVec4\(0\.47f, 0\.18f, 0\.10f, 1\.0f\)' -and $launcher -match 'theme\.warn_dot = ImVec4\(1\.0f, 0\.45f, 0\.30f, 1\.0f\)') 'warning colors are unchanged'

$patch = Read-Source 'gpu\patches\trg-launcher\0018-action-colors.patch'
$changed = @($patch -split "`n" | Where-Object { $_ -match '^[+-][^+-]' }) -join "`n"
$added = @($patch -split "`n" | Where-Object { $_ -match '^\+[^+]' }) -join "`n"
Check ($changed -notmatch '(?i)sponsor') 'the action-color patch leaves the Sponsor colors alone'
Check ($added -match 'c\[ImGuiCol_CheckMark\] = t\.link' -and $added -match 'c\[ImGuiCol_NavCursor\] = t\.link') 'check marks and focus use the link color'
Check ($added -match 'Col\(selected \? t\.action' -and $added -match 'Col\(hot \? t\.action_hot : t\.action\)') 'the selected page and PLAY use the action color'
Check ($changed -notmatch '\bt\.(good|warn)(_dot)?\s*=') 'the action-color patch leaves good and warning colors alone'

$python = Get-Command python -ErrorAction SilentlyContinue
if ($python) {
    $run = & $python.Source -I (Join-Root 'tools\make-app-icon.py') --check 2>&1
    Check ($LASTEXITCODE -eq 0) "tools\make-app-icon.py --check passes: $($run | Select-Object -Last 1)"
}

if ($script:failures -ne 0) {
    Write-Host "$script:failures of $script:checks icon checks failed" -ForegroundColor Red
    exit 1
}
Write-Host "app icon: $script:checks checks passed"
