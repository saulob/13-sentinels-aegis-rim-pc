# The local inputs of a release that are not in Git: the real boot images of the nine builds and the reviewed AppImage
# runtime. Both are kept in cache\ (ignored, never mirrored or packaged). Dot-sourced by tools\package-release.ps1 and
# tests\release_inputs_tests.ps1.

$script:NineBuilds = @('CUSA19610-1.00', 'CUSA19610-1.01', 'CUSA19610-1.02', 'CUSA19620-1.00', 'CUSA19620-1.01',
    'CUSA10602-1.00', 'CUSA10602-1.03', 'CUSA10602-1.04', 'CUSA18093-1.00')

function Get-MissingTestImages([string]$Folder) {
    @($script:NineBuilds | Where-Object { -not (Test-Path -LiteralPath (Join-Path $Folder "$_\boot.bin") -PathType Leaf) })
}

# The given folder (it must be complete), else cache\test-images, completed from the boot images the launcher and
# tools\build.ps1 make (game\image, build\image). $null when a build's image exists nowhere.
function Resolve-TestImages([string]$Root, [string]$Given) {
    if ($Given) {
        $path = (Resolve-Path -LiteralPath $Given -ErrorAction Stop).Path
        $missing = @(Get-MissingTestImages $path)
        if ($missing.Count) { throw "Missing boot images in ${path}: $($missing -join ', ')" }
        return $path
    }
    $cache = Join-Path $Root 'cache\test-images'
    foreach ($key in Get-MissingTestImages $cache) {
        foreach ($source in @((Join-Path $Root "game\image\$key"), (Join-Path $Root "build\image\$key"))) {
            if (-not (Test-Path -LiteralPath (Join-Path $source 'boot.bin') -PathType Leaf)) { continue }
            New-Item -ItemType Directory -Force (Join-Path $cache $key) | Out-Null
            Copy-Item -LiteralPath (Join-Path $source 'boot.bin') (Join-Path $cache "$key\boot.bin")
            break
        }
    }
    if (@(Get-MissingTestImages $cache).Count) { return $null }
    return $cache
}

function Get-PinnedRuntimeSha256([string]$Root) {
    $baseline = Get-Content -LiteralPath (Join-Path $Root 'patches\appimage-runtime\baseline.env') -Raw
    if ($baseline -notmatch '(?m)^RUNTIME_SHA256=([0-9a-f]{64})\s*$') { throw 'RUNTIME_SHA256 not found in patches\appimage-runtime\baseline.env' }
    return $Matches[1]
}

function Test-PinnedRuntime([string]$File, [string]$Sha256) {
    (Test-Path -LiteralPath $File -PathType Leaf) -and (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant() -eq $Sha256
}

# cache\appimage-runtime\runtime-x86_64 when it has the pinned SHA-256. Otherwise $Import is asked to place a copy there
# (from a WSL packaging cache); a file with any other hash is deleted and $null returned, so nothing unreviewed is used.
function Resolve-ReviewedRuntime([string]$Root, [scriptblock]$Import) {
    $sha256 = Get-PinnedRuntimeSha256 $Root
    $file = Join-Path $Root 'cache\appimage-runtime\runtime-x86_64'
    if (Test-PinnedRuntime $file $sha256) { return $file }
    Remove-Item -LiteralPath $file -ErrorAction SilentlyContinue
    if ($Import) {
        New-Item -ItemType Directory -Force (Split-Path $file -Parent) | Out-Null
        & $Import $file $sha256
        if (Test-PinnedRuntime $file $sha256) { return $file }
        Remove-Item -LiteralPath $file -ErrorAction SilentlyContinue
    }
    return $null
}
