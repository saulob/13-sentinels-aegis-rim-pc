#!/usr/bin/env bash
# The Linux half of tools\package-release.ps1, which starts it through wsl.exe: builds and tests the checkout that lives
# on a Windows drive, writes the Linux feature contract, packages the AppImage and copies the AppImage, its .sha256,
# the debug symbols and the contract back into the checkout. Local only: nothing is published, tagged or uploaded.
#
#   tools/package-release-linux.sh [--clean] [--skip-build]
#
# A Windows drive is slow to build on and has no Unix file modes, so the sources are mirrored (only what changed) into
# ${TSR_WSL_BUILD_DIR:-~/.cache/13-sentinels-aegis-rim-pc-build}. There tools/build.sh --test builds and runs every
# headless Linux test (TSR_TEST_IMAGES names the real boot images of the nine builds), tsr-platform-parity-tests writes
# the contract and tools/package-linux.sh packages. Needs the build environment of packaging/linux/ci-deps.sh and
# rsync; nothing is installed here.
set -euo pipefail
source=$(cd -- "$(dirname -- "$0")/.." && pwd)
clean=0 skip_build=0
while (($#)); do
    case $1 in
        --clean) clean=1 ;;
        --skip-build) skip_build=1 ;;
        -h|--help) sed -n '2,12p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

step() { printf '\n==> %s\n' "$*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || die 'This needs Linux x86_64 (WSL2 on a 64-bit PC).'
command -v rsync >/dev/null || die 'rsync is missing. Run packaging/linux/ci-deps.sh, which installs it.'
if [[ -n ${TSR_TEST_IMAGES:-} && ! -d $TSR_TEST_IMAGES ]]; then
    die "TSR_TEST_IMAGES names $TSR_TEST_IMAGES, which is not a folder"
fi

mirror=${TSR_WSL_BUILD_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/13-sentinels-aegis-rim-pc-build}
mkdir -p "$mirror"
mirror=$(cd -- "$mirror" && pwd)
[[ $mirror != "$source" && $mirror != "$source"/* ]] || die "The build folder $mirror is inside the checkout."

step "Mirroring the sources into $mirror"
# Content, not timestamps, decides what is copied: a restored older file must still rebuild.
rsync -rl --checksum --no-times --delete --chmod=D755,F644 \
    --exclude '/.git' --exclude '/game' --exclude '/user' --exclude '/cache' --exclude '/logs' --exclude '/build' \
    --exclude '/dist' --exclude '/out' --exclude '/.venv' --exclude '__pycache__' "$source/" "$mirror/"
chmod +x "$mirror"/tools/*.sh "$mirror"/packaging/linux/*.sh "$mirror"/packaging/linux/AppRun

cd "$mirror"
((clean)) && rm -rf build/cmake/linux
rm -rf dist
if ((skip_build)); then
    step 'Using the existing Linux build (--skip-build): its tests are not run again'
else
    step "Building and running every headless Linux test${TSR_TEST_IMAGES:+ (real boot images: $TSR_TEST_IMAGES)}"
    bash tools/build.sh --test
fi
parity=build/cmake/linux/bin/tsr-platform-parity-tests
[[ -x $parity ]] || die "Missing $parity (run without --skip-build)"
mkdir -p build/release
(cd build/cmake/linux/bin && ./tsr-platform-parity-tests --contract "$mirror/build/release/feature-contract-linux.txt")

step 'Release checks of the packaging script (fixtures)'
bash tools/package-linux.sh --self-test
TSR_SOURCE_CHECKOUT=$source bash tools/package-linux.sh --skip-build

step "Copying the AppImage into $source/dist"
mkdir -p "$source/dist" "$source/build/release"
shopt -s nullglob
images=(dist/13SentinelsAegisRimPC-v*-x86_64.AppImage)
((${#images[@]} == 1)) || die "Expected one AppImage in $mirror/dist, found ${#images[@]}"
name=$(basename -- "${images[0]}" .AppImage)
rm -rf "$source/dist/$name.AppImage" "$source/dist/$name.AppImage.sha256" "$source/dist/$name-symbols"
cp -- "dist/$name.AppImage" "dist/$name.AppImage.sha256" "$source/dist/"
cp -r -- "dist/$name-symbols" "$source/dist/"
cp -- build/release/feature-contract-linux.txt "$source/build/release/feature-contract-linux.txt"
(cd "$source/dist" && sha256sum -c "$name.AppImage.sha256")
printf '\nLinux AppImage: %s\n' "$source/dist/$name.AppImage"
