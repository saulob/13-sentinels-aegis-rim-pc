# The release's local inputs (tools\release-inputs.ps1) on a scratch tree: where the nine real boot images are found,
# and that only the AppImage runtime with the pinned SHA-256 is ever used. Also that tools\package-release.ps1 needs no
# argument and names no temporary or other project's path. Nothing is built, packaged, launched or sent.
[CmdletBinding()]
param()

$root = Split-Path -Parent $PSScriptRoot
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $root 'tools\release-inputs.ps1')

$script:checks = 0
$script:failures = 0
function Check([bool]$Passed, [string]$Name) {
    $script:checks++
    if (-not $Passed) {
        $script:failures++
        Write-Host "FAIL: $Name" -ForegroundColor Red
    }
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) ("tsr-release-inputs-" + [Guid]::NewGuid().ToString('N'))
function Write-Image([string]$Folder, [string]$Key) {
    New-Item -ItemType Directory -Force (Join-Path $Folder $Key) | Out-Null
    [IO.File]::WriteAllText((Join-Path $Folder "$Key\boot.bin"), "TSRBOOT1 $Key")
}

try {
    New-Item -ItemType Directory -Force $scratch | Out-Null

    # The images: a given folder must be complete; otherwise cache\test-images, completed from game\image and build\image.
    $given = Join-Path $scratch 'given'
    foreach ($key in $script:NineBuilds) { Write-Image $given $key }
    Check ((Resolve-TestImages $scratch $given) -eq (Resolve-Path $given).Path) 'a complete given folder is used'
    Remove-Item -LiteralPath (Join-Path $given 'CUSA18093-1.00') -Recurse
    $threw = $false
    try { Resolve-TestImages $scratch $given | Out-Null } catch { $threw = $_.Exception.Message -match 'CUSA18093-1\.00' }
    Check $threw 'a given folder missing a build fails and names it'

    Check ($null -eq (Resolve-TestImages $scratch '')) 'no images anywhere: nothing is resolved'
    foreach ($key in $script:NineBuilds[0..3]) { Write-Image (Join-Path $scratch 'build\image') $key }
    foreach ($key in $script:NineBuilds[3..7]) { Write-Image (Join-Path $scratch 'game\image') $key }
    Check ($null -eq (Resolve-TestImages $scratch '')) 'eight of nine builds: nothing is resolved'
    $missing = @(Get-MissingTestImages (Join-Path $scratch 'cache\test-images'))
    Check ($missing.Count -eq 1 -and $missing[0] -eq 'CUSA18093-1.00') 'the cache lacks only the build found nowhere'
    Write-Image (Join-Path $scratch 'build\image') 'CUSA18093-1.00'
    $cache = Resolve-TestImages $scratch ''
    Check ($cache -eq (Join-Path $scratch 'cache\test-images')) 'the completed cache is used'
    Check ((Get-Content -LiteralPath (Join-Path $cache 'CUSA10602-1.00\boot.bin') -Raw) -eq 'TSRBOOT1 CUSA10602-1.00') 'game\image is copied'
    Remove-Item -LiteralPath (Join-Path $scratch 'build\image'), (Join-Path $scratch 'game\image') -Recurse
    Check ((Resolve-TestImages $scratch '') -eq $cache) 'the cache stays complete once the sources are gone'

    # The runtime: only the pinned SHA-256, from the cache or an import, never anything else.
    $runtimeFile = Join-Path $scratch 'reviewed-runtime'
    [IO.File]::WriteAllText($runtimeFile, 'reviewed runtime')
    $pinned = (Get-FileHash -LiteralPath $runtimeFile -Algorithm SHA256).Hash.ToLowerInvariant()
    New-Item -ItemType Directory -Force (Join-Path $scratch 'patches\appimage-runtime') | Out-Null
    [IO.File]::WriteAllText((Join-Path $scratch 'patches\appimage-runtime\baseline.env'), "RUNTIME_TAG=x`nRUNTIME_SHA256=$pinned`n")
    Check ((Get-PinnedRuntimeSha256 $scratch) -eq $pinned) 'the pin is read from baseline.env'
    $cached = Join-Path $scratch 'cache\appimage-runtime\runtime-x86_64'

    $script:imports = 0
    $good = { param($File, $Sha256) $script:imports++; Copy-Item -LiteralPath $runtimeFile $File }
    $bad = { param($File, $Sha256) $script:imports++; [IO.File]::WriteAllText($File, 'a rebuilt runtime') }
    $none = { param($File, $Sha256) $script:imports++ }

    Check ($null -eq (Resolve-ReviewedRuntime $scratch $none)) 'no copy anywhere: no runtime'
    Check ($null -eq (Resolve-ReviewedRuntime $scratch $bad) -and -not (Test-Path -LiteralPath $cached)) 'an imported file with another hash is refused and removed'
    Check ((Resolve-ReviewedRuntime $scratch $good) -eq $cached) 'an imported reviewed runtime is cached'
    $script:imports = 0
    Check ((Resolve-ReviewedRuntime $scratch $bad) -eq $cached -and $script:imports -eq 0) 'a cached reviewed runtime is used without importing'
    [IO.File]::WriteAllText($cached, 'replaced by something else')
    Check ($null -eq (Resolve-ReviewedRuntime $scratch $none) -and -not (Test-Path -LiteralPath $cached)) 'a cached file with another hash is refused and removed'
    Check ($null -eq (Resolve-ReviewedRuntime $scratch $null)) 'without an import nothing is used'

    # The single release command: no mandatory argument, no temporary or other project's path.
    $release = Get-Content -LiteralPath (Join-Path $root 'tools\package-release.ps1') -Raw
    Check ($release -notmatch '(?i)Mandatory') 'tools\package-release.ps1 takes no mandatory argument'
    Check ($release -notmatch '(?i)AppData\\Local\\Temp|\\Temp\\claude|scratchpad') 'tools\package-release.ps1 names no temporary folder'
    Check ($release -notmatch 'dragons-crown-pro-pc|odin-sphere') "tools\package-release.ps1 names no other project's cache"
    Check ($release -match 'Resolve-TestImages \$root' -and $release -match 'Resolve-ReviewedRuntime \$root') 'tools\package-release.ps1 resolves both inputs'
    Check ((Get-Content -LiteralPath (Join-Path $root '.gitignore') -Raw) -match '(?m)^/cache/$') 'cache\ is ignored by Git'
    $import = Get-Content -LiteralPath (Join-Path $root 'tools\import-appimage-runtime.sh') -Raw
    Check ($import -match 'sha256sum -- "\$candidate"' -and $import -match 'sha256sum -- "\$target\.part"') 'the WSL import checks the hash before and after copying'
    $build = Get-Content -LiteralPath (Join-Path $root 'tools\build-appimage-runtime.sh') -Raw
    Check ($build -match 'not the reviewed \$RUNTIME_SHA256') 'a runtime built or given with another hash is refused'
    foreach ($file in 'tools\package-linux.sh', 'tools\package-release-linux.sh') {
        Check ((Get-Content -LiteralPath (Join-Path $root $file) -Raw) -match "--exclude '/cache'|\|cache\|") "$file keeps cache\ out of the mirror and the package"
    }
} finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures) {
    Write-Host "FAILED: $script:failures of $script:checks checks" -ForegroundColor Red
    exit 1
}
Write-Host "release inputs: $script:checks checks passed (nine real images from the cache, the pinned AppImage runtime only, one command)"
exit 0
