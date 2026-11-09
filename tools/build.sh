#!/usr/bin/env bash
# Builds 13_sentinels_aegis_rim on Linux x86_64 incrementally (Clang, CMake + Ninja): the Linux counterpart of
# tools\build.ps1. The one build configuration, RelWithDebInfo, is what is tested and what the AppImage contains.
#
#   tools/build.sh                   build build/cmake/linux/bin/13_sentinels_aegis_rim
#   tools/build.sh --clean           delete build/cmake/linux first
#   tools/build.sh --test            also build and run the focused headless tests (no window, game, audio or network);
#                                    TSR_TEST_IMAGES names the folder with the real boot images of the nine builds
#   tools/build.sh --run [args...]   start the loader that was just built (arguments after --run go to it)
#
# Missing tools or libraries are listed; packaging/linux/ci-deps.sh prepares Ubuntu 20.04 (the release baseline).
# FFmpeg is built from its pinned source into build/deps/linux by tools/build-ffmpeg-linux.sh.
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$root/build/cmake/linux
deps_dir=$root/build/deps/linux
config=RelWithDebInfo
clean=0 test=0 run=0
loader_args=()
while (($#)); do
    case $1 in
        --clean) clean=1 ;;
        --test) test=1 ;;
        --run) run=1; shift; loader_args=("$@"); break ;;
        -h|--help) sed -n '2,13p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done

# The headless tests of tools\test.ps1 that build on Linux. Windows-only by design: tsr-memory-windows-tests (the
# Windows memory API), tsr-log-tests (GUI-subsystem streams) and tsr-update-helper-tests (trg-updater.exe).
linux_tests=(
    tsr-runtime-tests
    tsr-module-history-tests
    tsr-isa-tests
    tsr-savedata-tests
    tsr-game-profile-tests
    tsr-build-parity-tests
    tsr-platform-parity-tests
    tsr-launcher-tests
    tsr-bug-report-tests
    tsr-window-tests
    tsr-sdl-compat-tests
    tsr-update-tests
    tsr-update-network-tests
    tsr-game-hook-tests
    tsr-exit-game-tests
    tsr-game-feature-tests
    tsr-ajm-tests
    tsr-frame-pacing-tests
    tsr-command-snapshot-tests
    tsr-renderer-selection-tests
    tsr-opengl-backend-tests
    tsr-opengl-perf-tests
    tsr-graphics-tests
)

step() { printf '\n==> %s\n' "$*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || die 'This script builds for Linux x86_64 only (Windows: tools\build.ps1).'

export CC=${CC:-clang} CXX=${CXX:-clang++}
missing=()
for tool in "$CC" "$CXX" cmake ninja pkg-config git make curl xz; do
    command -v "$tool" >/dev/null || missing+=("$tool")
done
command -v glslang >/dev/null || command -v glslangValidator >/dev/null || missing+=(glslang)
if command -v pkg-config >/dev/null; then
    for module in sdl3 freetype2 libpng zlib openssl libxxhash vulkan; do
        pkg-config --exists "$module" || missing+=("pkg-config:$module")
    done
fi
if ((${#missing[@]})); then
    echo "Missing: ${missing[*]}" >&2
    echo 'packaging/linux/ci-deps.sh prepares the Ubuntu 20.04 release build environment (docs/LINUX.md).' >&2
    exit 1
fi

step "Toolchain $($CC --version | head -1), $config"

step 'Third-party sources (gpu/deps.txt)'
patches_of() {
    local LC_ALL=C dir=$1 file
    PATCHES=()
    for file in "$dir"/*.patch; do
        [[ -f $file ]] || continue
        PATCHES+=("$file")
    done
}
# The same fingerprint as Get-PatchFingerprint in tools\_common.ps1, so a checkout fetched on Windows stays current.
patches_fingerprint() {
    local file hash
    (($#)) || return 0
    for file in "$@"; do
        hash=$(sha256sum < "$file" | cut -d' ' -f1)
        printf '%s:%s\n' "$(basename "$file")" "${hash^^}"
    done | sha256sum | cut -d' ' -f1 | tr 'a-f' 'A-F'
}
while read -r path url commit; do
    [[ -z ${path:-} || $path == \#* ]] && continue
    dir=$root/gpu/$path
    patches_of "$root/gpu/patches/$(basename "$path")"
    fingerprint=$(patches_fingerprint "${PATCHES[@]}")
    expected=$commit
    [[ -n $fingerprint ]] && expected=$commit$'\n'$fingerprint
    [[ -f $dir/.tsr_commit && $(tr -d '\r' < "$dir/.tsr_commit") == "$expected" ]] && continue
    echo "Fetching $path (${commit:0:7})"
    rm -rf "$dir"
    mkdir -p "$dir"
    git -C "$dir" init --quiet
    git -C "$dir" fetch --quiet --depth 1 "$url" "$commit"
    git -C "$dir" -c advice.detachedHead=false checkout --quiet FETCH_HEAD
    [[ -f $dir/.gitmodules ]] && git -C "$dir" submodule update --quiet --init --recursive --depth 1
    for patch in "${PATCHES[@]}"; do
        git -C "$dir" apply "$patch"
    done
    printf '%s' "$expected" > "$dir/.tsr_commit"
done < <(tr -d '\r' < "$root/gpu/deps.txt")

step 'FFmpeg (packaging/linux/ffmpeg.env)'
bash "$root/tools/build-ffmpeg-linux.sh" "$deps_dir"

((clean)) && { step 'Cleaning build/cmake/linux'; rm -rf "$build_dir"; }
if ! grep -qs "CMAKE_BUILD_TYPE:STRING=$config" "$build_dir/CMakeCache.txt" || [[ ! -f $build_dir/build.ninja ]]; then
    step 'Configuring'
    cmake -S "$root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=$config \
        -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_PREFIX_PATH="$deps_dir/ffmpeg"
fi
step 'Building'
targets=(tsr_game)
((test)) && targets+=("${linux_tests[@]}")
ninja -C "$build_dir" "${targets[@]}"
loader=$build_dir/bin/13_sentinels_aegis_rim
[[ -x $loader ]] || die "Missing $loader"

failed=()
if ((test)); then
    for name in "${linux_tests[@]}"; do
        step "Test $name"
        if (cd "$build_dir/bin" && "./$name"); then
            echo "    PASS"
        else
            echo "    FAIL ($name)"
            failed+=("$name")
        fi
    done
    printf '\n13 Sentinels: Aegis Rim - Linux tests: %d run, %d failed\n' "${#linux_tests[@]}" "${#failed[@]}"
    ((${#failed[@]} == 0)) || { printf '  failed: %s\n' "${failed[@]}"; exit 1; }
fi

version=CUSA19610-1.00
view=$root/game/prepared/$version image=$root/build/image/$version
if [[ -f $view/.tsr_view.json ]]; then
    if [[ ! -f $image/boot.bin || $view/.tsr_view.json -nt $image/boot.bin ||
          $root/launcher/boot_image.cpp -nt $image/boot.bin || $root/launcher/game_profile.h -nt $image/boot.bin ]]; then
        step 'Updating the boot image'
        "$loader" --prepare-image "$view" "$image"
    fi
else
    echo 'The game is not installed yet: start the loader and use Install game... on the launcher'\''s Play page.'
fi
printf '\nBuilt %s\n' "$loader"
((run)) || exit 0
cd -- "$(dirname -- "$loader")"
exec "$loader" "${loader_args[@]}"
