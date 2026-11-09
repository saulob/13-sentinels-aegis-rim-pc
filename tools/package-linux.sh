#!/usr/bin/env bash
# Packages the tested Linux build as dist/13SentinelsAegisRimPC-v<version>-x86_64.AppImage (and its .sha256). Local
# only: nothing is published, tagged or uploaded. The Linux counterpart of tools\package.ps1.
#
#   tools/package-linux.sh                build with tools/build.sh, then package
#   tools/package-linux.sh --skip-build   package the existing build/cmake/linux/bin/13_sentinels_aegis_rim
#   tools/package-linux.sh --out-dir DIR  write the AppImage, .sha256 and symbols to DIR instead of dist/
#   tools/package-linux.sh --verify DIR   check an existing AppDir
#   tools/package-linux.sh --self-test    run the release checks on fixtures that must pass and fixtures that must fail
#
# Needs patchelf, binutils, curl, git and dpkg (licence texts come from the packages of the bundled libraries). Build
# on Ubuntu 20.04 (packaging/linux/ci-deps.sh): the glibc of the build host is the oldest one the AppImage runs on. GPU
# drivers, the Vulkan loader, X11/Wayland, the audio servers and glibc always come from the player's system. The
# patched AppImage runtime is built by tools/build-appimage-runtime.sh, which needs root or sudo (docs/LINUX.md).
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
skip_build=0 verify_dir= out_dir= self_test=0
while (($#)); do
    case $1 in
        --skip-build) skip_build=1 ;;
        --out-dir) out_dir=${2:?--out-dir needs a folder}; shift ;;
        --verify) verify_dir=$(cd -- "${2:?--verify needs a folder}" && pwd) || exit 2; shift ;;
        --self-test) self_test=1 ;;
        -h|--help) sed -n '2,14p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Unknown option: $1" >&2; exit 2 ;;
    esac
    shift
done
if [[ -n $out_dir ]]; then
    mkdir -p -- "$out_dir" && out_dir=$(cd -- "$out_dir" && pwd) || exit 2
fi
cd "$root"

step() { printf '\n==> %s\n' "$*"; }
die() { echo "ERROR: $*" >&2; exit 1; }

# The release baseline: Ubuntu 20.04. No shipped ELF may need a newer glibc.
max_glibc=2.31
binary=13_sentinels_aegis_rim
app_id=13-sentinels-aegis-rim-pc
desktop_file=$app_id.desktop
doc_name=13SentinelsAegisRimPC
appimagetool_version=1.9.1
appimagetool_sha256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
deps_dir=$root/build/deps/linux

# The host libraries the AppImage may need without carrying them: glibc, the Vulkan loader and drivers, OpenGL/EGL, the
# display servers, the audio servers and the system services SDL talks to.
host_libs='^(ld-linux.*|libc|libm|libdl|librt|libpthread|libresolv|libutil|libnsl|libanl|libcrypt|libmvec|libvulkan|libGL|libGLX|libGLdispatch|libEGL|libOpenGL|libgbm|libdrm.*|libX11|libX11-xcb|libxcb.*|libXau|libXdmcp|libXext|libXfixes|libXrender|libXi|libXcursor|libXrandr|libXss|libXtst|libXinerama|libxkbcommon.*|libwayland-.*|libdecor-.*|libasound|libudev|libpulse.*|libpipewire.*|libjack|libsndio|libdbus-1|libibus-1\.0|libfontconfig|libOpenCL|libva.*|libvdpau|libnvidia-.*|libcuda|libz|libselinux|libpcre2-.*|libsystemd|libcap|libusb-1\.0)\.so'
# Fallback copies, used only when the system's are older than the build's.
compat_libs='^(libstdc\+\+|libgcc_s)\.so'

# Matched without case: game data, user data and build output never ship, whatever their spelling.
forbidden_names='^(game|user|logs|cache|build|prepared|extracted|image|work|trophy|savedata|sce_sys|sce_module|eboot\.bin|param\.sfo|tsr\.ini|boot\.bin|content\.bin|image\.json|imports\.json|bug_reports\.json|symbols|renderdoc.*|librenderdoc.*|vklayer_.*|implicit_layer\.d|explicit_layer\.d|icd\.d)$'
forbidden_patterns='\.(pkg|pdb|debug|log|dmp|ini|bin|dat|self|sprx|prx|elf|cpk|sfo|json|trp|spv|pipeline_cache|o|a|tmp|bak|sav|undo|py|ps1|c|cpp|h|env|pem|key)$'
glibc_parts='^(ld-linux.*|libc|libm|libdl|libpthread|librt|libresolv|libutil|libmvec|libanl|libnsl|libBrokenLocale|libthread_db|libnss_.*)\.so'
# Text that must not appear in any shipped file: local paths, private tooling and secrets.
fixed_needles=(VisualStudio DEBUG_SAULO gow3 'God of War' referencias-ps4 build/cmake /mnt/c/Users /mnt/s/ 'BEGIN PRIVATE KEY'
               'BEGIN RSA PRIVATE KEY' 'BEGIN OPENSSH PRIVATE KEY' github_pat_ TSR_BUG_REPORT_ADMIN_TOKEN)

newer_than() { [[ $1 != "$2" && $(printf '%s\n' "$1" "$2" | sort -V | tail -1) == "$1" ]]; }
magic_of() { head -c 4 -- "$1" | od -An -tx1 | tr -d ' \n'; }
is_elf() { [[ $(magic_of "$1") == 7f454c46 ]]; }
# The newest GLIBC_ version in `readelf --version-info` text: the dynamic loader refuses to start an ELF without it.
newest_glibc_in() {
    awk '/^Version needs section/ {needs = 1; next} /^Version / {needs = 0} needs' |
        { grep -oE 'Name: GLIBC_[0-9]+(\.[0-9]+)*' || true; } | sed 's/^Name: GLIBC_//' | sort -V | tail -1
}
# `readelf -d` text: one "KIND value" line per DT_NEEDED, DT_RPATH and DT_RUNPATH entry.
dynamic_entries_in() {
    sed -nE 's/.*\((NEEDED|RPATH|RUNPATH)\)[^[]*\[([^]]*)\].*/\1 \2/p'
}

# The rules for one ELF of the AppDir, given its readelf output: glibc baseline, RPATH/RUNPATH and the libraries it needs.
check_elf_text() { # label relative-path version-info-text dynamic-text bundled-names
    local label=$1 rel=$2 versions=$3 dynamic=$4 bundled=$5 newest kind value expected_rpath
    newest=$(newest_glibc_in <<<"$versions")
    printf '  %-48s glibc %s\n' "$rel" "${newest:-(none)}"
    if [[ -n $newest ]]; then
        if [[ -z $glibc_newest ]] || newer_than "$newest" "$glibc_newest"; then glibc_newest=$newest glibc_newest_file=$rel; fi
        newer_than "$newest" "$max_glibc" &&
            problem "$label $rel needs glibc $newest, newer than the $max_glibc baseline (build on Ubuntu 20.04)"
    fi
    case $rel in
        usr/bin/$binary) expected_rpath='RPATH $ORIGIN/../lib' ;;
        usr/lib/*) expected_rpath='$ORIGIN' ;;
        *) expected_rpath= ;;
    esac
    local saw_rpath=0
    while read -r kind value; do
        [[ -n ${kind:-} ]] || continue
        case $kind in
            NEEDED)
                if ! grep -qxF -- "$value" <<<"$bundled" && ! [[ $value =~ $host_libs ]]; then
                    problem "$label $rel needs $value, which is neither bundled nor a permitted host library"
                fi ;;
            RPATH|RUNPATH)
                saw_rpath=1
                if [[ $rel == usr/bin/$binary ]]; then
                    [[ "$kind $value" == "$expected_rpath" ]] || problem "$label $rel has $kind '$value', expected $expected_rpath"
                elif [[ $value != '$ORIGIN' ]]; then
                    problem "$label $rel has $kind '$value': only \$ORIGIN is allowed"
                fi ;;
        esac
    done < <(dynamic_entries_in <<<"$dynamic")
    [[ $rel != usr/bin/$binary || $saw_rpath == 1 ]] || problem "$label $rel has no RPATH: it would not find its libraries"
}

expected_entry() { # relative path
    case $1 in
        AppRun|"$desktop_file"|"$app_id.png"|.DirIcon|usr|usr/bin|usr/bin/$binary|usr/bin/assets|usr/bin/assets/launcher|\
        usr/bin/assets/launcher/header-art.png|usr/bin/assets/launcher/sponsor-heart.png|usr/lib|usr/lib/compat|\
        usr/lib/*.so*|usr/lib/compat/*.so*|usr/share|usr/share/applications|usr/share/applications/"$desktop_file"|\
        usr/share/icons|usr/share/icons/hicolor|usr/share/icons/hicolor/256x256|usr/share/icons/hicolor/256x256/apps|\
        usr/share/icons/hicolor/256x256/apps/"$app_id.png"|usr/share/licenses|usr/share/licenses/$doc_name|\
        usr/share/licenses/$doc_name/*|usr/share/doc|usr/share/doc/$doc_name|usr/share/doc/$doc_name/README.txt) return 0 ;;
    esac
    return 1
}

check_appdir() { # dir [the AppImage file, whose runtime is an ELF too]
    local dir=$1 problems=0 item rel name lower needle magic bundled files=() needles=()
    glibc_newest= glibc_newest_file=
    problem() { echo "  $*" >&2; problems=$((problems + 1)); }
    needles=("$root" "${fixed_needles[@]}")
    [[ -n ${HOME:-} && $HOME != / ]] && needles+=("$HOME/")
    [[ -n ${TSR_SOURCE_CHECKOUT:-} ]] && needles+=("$TSR_SOURCE_CHECKOUT")
    while IFS= read -r -d '' item; do
        rel=${item#"$dir"/}
        name=$(basename -- "$item")
        lower=${name,,}
        expected_entry "$rel" || problem "unexpected item: $rel"
        [[ $lower =~ $forbidden_names ]] && problem "forbidden name: $rel"
        [[ $name =~ $glibc_parts ]] && problem "glibc must come from the player's system: $rel"
        [[ $rel == usr/lib/* && $name =~ $host_libs ]] && problem "a host library must come from the player's system: $rel"
        if [[ $rel != usr/share/licenses/* && $rel != usr/share/doc/* && -f $item && $lower =~ $forbidden_patterns ]]; then
            problem "forbidden file type: $rel"
        fi
        if [[ -f $item && ! -L $item ]]; then
            files+=("$item")
            magic=$(magic_of "$item")
            case $magic in
                7f434e54) problem "PS4 package: $rel" ;;
                4f153d1d) problem "PS4 SELF: $rel" ;;
                43504b20) problem "CPK archive: $rel" ;;
                00505346) problem "PS4 param.sfo: $rel" ;;
            esac
            for needle in "${needles[@]}"; do
                grep -a -q -F -- "$needle" "$item" && { problem "local path, secret or stale name '$needle' in $rel"; break; }
            done
        fi
    done < <(find "$dir" -mindepth 1 -print0)
    for leaf in AppRun "$desktop_file" "$app_id.png" .DirIcon usr/bin/$binary \
                usr/bin/assets/launcher/header-art.png usr/bin/assets/launcher/sponsor-heart.png \
                usr/share/licenses/$doc_name/LICENSE.txt usr/share/licenses/$doc_name/THIRD-PARTY-LICENSES.txt \
                usr/share/licenses/$doc_name/SYSTEM-PACKAGES.txt usr/share/licenses/$doc_name/LibAtrac9/LICENSE \
                usr/share/licenses/$doc_name/NotoSansTC/LICENSE-NotoSansTC.txt \
                usr/share/licenses/$doc_name/system/ffmpeg/COPYING.LGPLv2.1 usr/share/licenses/$doc_name/system/ffmpeg/SOURCE.txt \
                usr/share/doc/$doc_name/README.txt; do
        [[ -e $dir/$leaf || -L $dir/$leaf ]] || problem "missing: $leaf"
    done
    for library in libavcodec.so libavutil.so libswscale.so libSDL3.so libssl.so libcrypto.so; do
        compgen -G "$dir/usr/lib/$library.*" >/dev/null || problem "missing bundled library: usr/lib/$library.*"
    done
    bundled=$(cd "$dir/usr/lib" 2>/dev/null && ls -1 -- *.so* compat/*.so* 2>/dev/null | sed 's|.*/||' | sort -u || true)
    echo "Each ELF: newest glibc import (baseline $max_glibc), RPATH/RUNPATH, needed libraries:"
    local file
    for file in "${files[@]}"; do
        is_elf "$file" || continue
        check_elf_text 'the AppDir:' "${file#"$dir"/}" "$(readelf --version-info -W -- "$file" 2>/dev/null)" \
            "$(readelf -d -W -- "$file" 2>/dev/null)" "$bundled"
    done
    if [[ -x $dir/usr/bin/$binary ]]; then
        local missing
        missing=$(env -u LD_LIBRARY_PATH ldd "$dir/usr/bin/$binary" 2>&1 | grep 'not found' || true)
        [[ -z $missing ]] || problem "unresolved libraries: $missing"
    fi
    if (($# >= 2)); then
        check_elf_text 'the AppImage runtime:' "$(basename -- "$2")" "$(readelf --version-info -W -- "$2" 2>/dev/null)" \
            "$(readelf -d -W -- "$2" 2>/dev/null)" ""
    fi
    echo "Newest glibc import: ${glibc_newest:-(none)}${glibc_newest_file:+ ($glibc_newest_file)}; baseline $max_glibc"
    ((problems == 0)) || die "release check failed ($problems problem(s))"
    echo 'AppDir is clean'
}

# The runtime at the front of the AppImage is the reviewed one: its bytes up to the file system equal the runtime file,
# apart from the sections appimagetool fills (digest, update information, signature).
verify_embedded_runtime() { # appimage runtime
    local appimage=$1 runtime=$2 size offset work section start length masked=0
    size=$(stat -c %s -- "$runtime")
    offset=$("$appimage" --appimage-offset)
    [[ $offset == "$size" ]] || die "the AppImage's file system starts at $offset, the pinned runtime is $size bytes"
    work=$(mktemp -d)
    head -c "$size" -- "$appimage" > "$work/embedded"
    cp -- "$runtime" "$work/pinned"
    # readelf -S -W: name, type, address, then the file offset and size in hexadecimal.
    while read -r section start length; do
        for copy in embedded pinned; do
            dd if=/dev/zero of="$work/$copy" bs=1 seek=$((16#$start)) count=$((16#$length)) conv=notrunc status=none
        done
        masked=$((masked + 1))
    done < <(readelf -S -W -- "$work/pinned" | awk '{for (i = 1; i < NF - 3; i++) if ($i == ".digest_md5" || $i == ".upd_info" ||
        $i == ".sha256_sig" || $i == ".sig_key") print $i, $(i + 3), $(i + 4)}')
    ((masked > 0)) || { rm -rf -- "$work"; die 'the pinned runtime has none of the sections appimagetool fills'; }
    cmp -s -- "$work/embedded" "$work/pinned" || { rm -rf -- "$work"; die 'the runtime embedded in the AppImage is not the pinned runtime'; }
    rm -rf -- "$work"
    echo "Embedded runtime: the pinned runtime ($(sha256sum -- "$runtime" | cut -d' ' -f1)), $size bytes"
}

# The checks above against fixtures, with canned readelf output where a real ELF of that shape cannot be built here.
run_self_test() {
    local work fixture failures=0 output
    work=$(mktemp -d)
    trap 'rm -rf -- "$work"' RETURN
    make_fixture() {
        rm -rf -- "$work/app"
        mkdir -p "$work/app/usr/bin/assets/launcher" "$work/app/usr/lib/compat" "$work/app/usr/share/applications" \
                 "$work/app/usr/share/icons/hicolor/256x256/apps" "$work/app/usr/share/licenses/$doc_name/LibAtrac9" \
                 "$work/app/usr/share/licenses/$doc_name/NotoSansTC" "$work/app/usr/share/licenses/$doc_name/system/ffmpeg" \
                 "$work/app/usr/share/doc/$doc_name"
        printf '#!/bin/sh\n' > "$work/app/AppRun"
        for file in "$desktop_file" "$app_id.png" usr/bin/assets/launcher/header-art.png usr/bin/assets/launcher/sponsor-heart.png \
                    usr/share/applications/$desktop_file usr/share/icons/hicolor/256x256/apps/$app_id.png \
                    usr/share/licenses/$doc_name/LICENSE.txt usr/share/licenses/$doc_name/THIRD-PARTY-LICENSES.txt \
                    usr/share/licenses/$doc_name/SYSTEM-PACKAGES.txt usr/share/licenses/$doc_name/LibAtrac9/LICENSE \
                    usr/share/licenses/$doc_name/NotoSansTC/LICENSE-NotoSansTC.txt \
                    usr/share/licenses/$doc_name/system/ffmpeg/COPYING.LGPLv2.1 usr/share/licenses/$doc_name/system/ffmpeg/SOURCE.txt \
                    usr/share/doc/$doc_name/README.txt usr/bin/$binary; do
            echo fixture > "$work/app/$file"
        done
        ln -s "$app_id.png" "$work/app/.DirIcon"
        for library in libavcodec.so.62 libavutil.so.60 libswscale.so.9 libSDL3.so.0 libssl.so.3 libcrypto.so.3; do
            echo fixture > "$work/app/usr/lib/$library"
        done
    }
    expect() { # pass|fail description command...
        local want=$1 what=$2 status=0
        shift 2
        output=$( ("$@") 2>&1 ) || status=$?
        if [[ $want == pass && $status != 0 ]] || [[ $want == fail && $status == 0 ]]; then
            echo "SELF-TEST FAIL: $what (expected $want)"; printf '%s\n' "$output" | sed 's/^/    /'
            failures=$((failures + 1))
        else
            echo "ok: $what"
        fi
    }
    make_fixture
    expect pass 'a clean AppDir' check_appdir "$work/app"
    make_fixture; mkdir "$work/app/usr/bin/game"
    expect fail 'a game folder' check_appdir "$work/app"
    make_fixture; echo x > "$work/app/usr/bin/ROBO_US.CPK"
    expect fail 'an upper-case .CPK archive' check_appdir "$work/app"
    make_fixture; echo x > "$work/app/usr/bin/EBOOT.BIN"
    expect fail 'an eboot.bin in any case' check_appdir "$work/app"
    make_fixture; printf '\x7fCNT' > "$work/app/usr/bin/assets/launcher/header-art.png"
    expect fail 'a PS4 package by content' check_appdir "$work/app"
    make_fixture; printf 'CPK ' > "$work/app/usr/bin/assets/launcher/sponsor-heart.png"
    expect fail 'a CPK archive by content' check_appdir "$work/app"
    make_fixture; echo "$root/build/cmake/linux" > "$work/app/usr/share/doc/$doc_name/README.txt"
    expect fail 'a local build path' check_appdir "$work/app"
    make_fixture; echo 'DEBUG_SAULO probe' > "$work/app/AppRun"
    expect fail 'a DEBUG_SAULO marker' check_appdir "$work/app"
    make_fixture; echo 'token=github_pat_x' > "$work/app/AppRun"
    expect fail 'a secret' check_appdir "$work/app"
    make_fixture; echo x > "$work/app/usr/lib/libvulkan.so.1"
    expect fail 'a bundled Vulkan loader' check_appdir "$work/app"
    make_fixture; echo x > "$work/app/usr/lib/libc.so.6"
    expect fail 'a bundled glibc' check_appdir "$work/app"
    make_fixture; mkdir -p "$work/app/usr/share/vulkan/implicit_layer.d"
    expect fail 'a Vulkan layer manifest folder' check_appdir "$work/app"
    make_fixture; echo x > "$work/app/usr/lib/librenderdoc.so"
    expect fail 'RenderDoc' check_appdir "$work/app"
    make_fixture; echo x > "$work/app/tsr.ini"
    expect fail 'user settings' check_appdir "$work/app"
    make_fixture; rm "$work/app/usr/share/licenses/$doc_name/system/ffmpeg/COPYING.LGPLv2.1"
    expect fail 'a missing FFmpeg licence' check_appdir "$work/app"
    make_fixture; rm "$work/app/usr/lib/libavcodec.so.62"
    expect fail 'a missing FFmpeg library' check_appdir "$work/app"

    local needs_230='Version needs section '"'"'.gnu.version_r'"'"' contains 1 entry:
  000000: Version: 1  File: libc.so.6  Cnt: 2
  0x0010:   Name: GLIBC_2.30  Flags: none  Version: 3
  0x0020:   Name: GLIBC_2.2.5  Flags: none  Version: 2'
    local needs_234=${needs_230//GLIBC_2.30/GLIBC_2.34}
    local good_dynamic=' 0x0000000000000001 (NEEDED)             Shared library: [libSDL3.so.0]
 0x0000000000000001 (NEEDED)             Shared library: [libvulkan.so.1]
 0x0000000000000001 (NEEDED)             Shared library: [libc.so.6]
 0x000000000000000f (RPATH)              Library rpath: [$ORIGIN/../lib]'
    local bundled=$'libSDL3.so.0\nlibavcodec.so.62'
    elf_case() { # versions dynamic relative-path
        glibc_newest= glibc_newest_file=
        local problems=0
        problem() { echo "  $*" >&2; problems=$((problems + 1)); }
        check_elf_text 'fixture:' "${3:-usr/bin/$binary}" "$1" "$2" "$bundled"
        ((problems == 0))
    }
    expect pass 'glibc 2.30, bundled and host libraries, $ORIGIN RPATH' elf_case "$needs_230" "$good_dynamic"
    expect fail 'an ELF that needs glibc 2.34' elf_case "$needs_234" "$good_dynamic"
    expect fail 'an absolute RPATH' elf_case "$needs_230" "${good_dynamic//\$ORIGIN\/..\/lib//root/.cache/x/build/deps/linux/ffmpeg/lib}"
    expect fail 'a RUNPATH instead of the RPATH' elf_case "$needs_230" "${good_dynamic//(RPATH)              Library rpath/(RUNPATH)            Library runpath}"
    expect fail 'no RPATH on the executable' elf_case "$needs_230" "$(grep -v RPATH <<<"$good_dynamic")"
    expect fail 'a library that is neither bundled nor permitted' elf_case "$needs_230" "$good_dynamic
 0x0000000000000001 (NEEDED)             Shared library: [libx264.so.155]"
    expect pass 'a bundled library with $ORIGIN' elf_case "$needs_230" ' 0x1 (NEEDED) Shared library: [libc.so.6]
 0x1d (RUNPATH) Library runpath: [$ORIGIN]' usr/lib/libavcodec.so.62
    expect fail 'a bundled library with a build path' elf_case "$needs_230" ' 0x1d (RUNPATH) Library runpath: [/usr/local/lib]' \
        usr/lib/libSDL3.so.0
    ((failures == 0)) || die "package-linux.sh self-test: $failures case(s) failed"
    echo 'package-linux.sh self-test: every case passed'
}

if ((self_test)); then
    run_self_test
    exit 0
fi
if [[ -n $verify_dir ]]; then
    check_appdir "$verify_dir"
    exit 0
fi

[[ $(uname -s) == Linux && $(uname -m) == x86_64 ]] || die 'Packaging needs Linux x86_64 (Windows: tools\package.ps1).'
for tool in patchelf objdump objcopy readelf strip curl dpkg dpkg-query ldd sha256sum cmp; do
    command -v "$tool" >/dev/null || die "Missing $tool"
done
host_glibc=$(getconf GNU_LIBC_VERSION | cut -d' ' -f2)
newer_than "$host_glibc" "$max_glibc" &&
    die "This system has glibc $host_glibc. Release AppImages are built on Ubuntu 20.04 (glibc $max_glibc): see packaging/linux/ci-deps.sh."

version=$(sed -nE 's/^project\(thirteen_sentinels_aegis_rim_pc VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/p' CMakeLists.txt)
[[ -n $version ]] || die 'project VERSION not found in CMakeLists.txt'
name=13SentinelsAegisRimPC-v$version-x86_64
loader=$root/build/cmake/linux/bin/$binary
dist=${out_dir:-$root/dist}
work=$root/build/appimage
appdir=$work/AppDir
symbols=$dist/$name-symbols

step "13 Sentinels: Aegis Rim PC Port $version - Linux AppImage (local only)"
((skip_build)) || "$root/tools/build.sh"
[[ -x $loader ]] || die "Missing $loader (run without --skip-build)"
grep -a -q -F "13-Sentinels-Aegis-Rim-PC/$version bug-report" "$loader" ||
    die "$loader was not built from version $version (run without --skip-build)"

rm -rf "$appdir"
mkdir -p "$dist" "$symbols" "$appdir/usr/bin" "$appdir/usr/lib/compat" "$appdir/usr/share/licenses/$doc_name" \
         "$appdir/usr/share/doc/$doc_name" "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/256x256/apps"
rm -f "$dist/$name.AppImage" "$dist/$name.AppImage.sha256"

step "Staging $binary without debug information"
objcopy --only-keep-debug "$loader" "$symbols/$binary.debug"
objcopy --strip-debug --add-gnu-debuglink="$symbols/$binary.debug" "$loader" "$appdir/usr/bin/$binary"
chmod 755 "$appdir/usr/bin/$binary"

step 'Staging the launcher images'
install -d "$appdir/usr/bin/assets/launcher"
for image in header-art.png sponsor-heart.png; do
    install -m644 "assets/launcher/$image" "$appdir/usr/bin/assets/launcher/$image"
done

step 'Staging the libraries it loads'
declare -A seen=()
declare -A owners=()
bundle() {
    local elf=$1 line lib file
    while read -r line; do
        lib=$(awk '/=> \//{print $3}' <<<"$line")
        [[ -n $lib ]] || continue
        file=$(basename -- "$lib")
        [[ -n ${seen[$file]:-} ]] && continue
        seen[$file]=1
        [[ $file =~ $host_libs ]] && continue
        if [[ $file =~ $compat_libs ]]; then
            install -m755 "$(readlink -f -- "$lib")" "$appdir/usr/lib/compat/$file"
        else
            install -m755 "$(readlink -f -- "$lib")" "$appdir/usr/lib/$file"
            bundle "$lib"
        fi
        owners[$file]=$lib
    done < <(env -u LD_LIBRARY_PATH ldd "$elf")
}
bundle "$loader"
for lib in "$appdir"/usr/lib/*.so*; do
    strip --strip-unneeded "$lib"
    patchelf --set-rpath '$ORIGIN' "$lib"
done
for lib in "$appdir"/usr/lib/compat/*.so*; do
    [[ -f $lib ]] || continue
    strip --strip-unneeded "$lib"
    patchelf --remove-rpath "$lib"
done
# DT_RPATH (not RUNPATH) is inherited by the libraries' own dependencies and wins over LD_LIBRARY_PATH. The build's
# RUNPATH (to build/deps) goes first: patchelf would otherwise rewrite it in place and keep the RUNPATH tag.
patchelf --remove-rpath "$appdir/usr/bin/$binary"
patchelf --force-rpath --set-rpath '$ORIGIN/../lib' "$appdir/usr/bin/$binary"
echo "Bundled: $(cd "$appdir/usr/lib" && ls -- *.so* | tr '\n' ' ')"
echo "Fallback: $(cd "$appdir/usr/lib/compat" && ls -- *.so* 2>/dev/null | tr '\n' ' ')"

step 'Staging AppRun, desktop entry, icon, licences and README'
glibcxx_need=$(objdump -T "$appdir/usr/bin/$binary" "$appdir"/usr/lib/*.so* | grep -o 'GLIBCXX_3\.4\.[0-9]*' |
               sed 's/.*\.//' | sort -n | tail -1)
gcc_need=$(objdump -T "$appdir/usr/bin/$binary" "$appdir"/usr/lib/*.so* | grep -o 'GCC_[0-9][0-9.]*' |
           sed 's/^GCC_//; s/\..*//' | sort -n | tail -1)
sed "s/@GLIBCXX_NEED@/${glibcxx_need:-0}/g; s/@GCC_NEED@/${gcc_need:-0}/g" packaging/linux/AppRun > "$appdir/AppRun"
chmod 755 "$appdir/AppRun"
install -m644 "packaging/linux/$desktop_file" "$appdir/$desktop_file"
install -m644 "packaging/linux/$desktop_file" "$appdir/usr/share/applications/$desktop_file"
install -m644 packaging/icons/app-icon-256.png "$appdir/$app_id.png"
install -m644 packaging/icons/app-icon-256.png "$appdir/usr/share/icons/hicolor/256x256/apps/$app_id.png"
ln -s "$app_id.png" "$appdir/.DirIcon"
if command -v desktop-file-validate >/dev/null; then
    desktop-file-validate "$appdir/$desktop_file" || die "$desktop_file is not a valid desktop entry"
fi
sed "s/@VERSION@/$version/" packaging/README-linux.txt > "$appdir/usr/share/doc/$doc_name/README.txt"

licenses=$appdir/usr/share/licenses/$doc_name
install -m644 LICENSE "$licenses/LICENSE.txt"
install -m644 packaging/licenses/THIRD-PARTY-LICENSES.txt "$licenses/"
# The code compiled into the program from this repository and its fetched sources (the list tools\package.ps1 stages).
declare -A built_in=(
    [shadPS4]=gpu/shadps4/LICENSE [sirit]=gpu/third_party/sirit/LICENSE.txt
    [SPIRV-Headers]=gpu/third_party/sirit/externals/SPIRV-Headers/LICENSE [half]=gpu/third_party/half/LICENSE.txt
    [SMAA]=gpu/shadps4/video_core/host_shaders/smaa/LICENSE.txt [DejaVu]=gpu/third_party/fonts/LICENSE-DejaVu.txt
    [NotoSans]=third_party/fonts/LICENSE-NotoSans.txt [NotoSansTC]=third_party/fonts/LICENSE-NotoSansTC.txt
    [imgui]=gpu/third_party/imgui/LICENSE.txt [magic_enum]=gpu/third_party/magic_enum/LICENSE
    [miniz]=gpu/third_party/miniz/LICENSE [nanosvg]=launcher/third_party/nanosvg/LICENSE.txt
    [xbyak]=gpu/third_party/xbyak/COPYRIGHT [LibAtrac9]=third_party/LibAtrac9/LICENSE)
for component in "${!built_in[@]}"; do
    [[ -f ${built_in[$component]} ]] || die "Missing licence file: ${built_in[$component]}"
    mkdir -p "$licenses/$component"
    install -m644 "${built_in[$component]}" "$licenses/$component/"
done
# The licence texts of the Ubuntu packages the bundled libraries come from, of the sources packaging/linux/ci-deps.sh
# built (it installs their texts in /usr/local/share/licenses/<component>), or of the pinned FFmpeg.
declare -A source_built=([libSDL3]=SDL3 [libfmt]=fmt [libZydis]=zydis [libZycore]=zydis [libxxhash]=xxhash
                         [libcrypto]=openssl [libssl]=openssl)
manifest=$licenses/SYSTEM-PACKAGES.txt
{
    echo "Libraries bundled in the AppImage, with the Ubuntu packages or sources they come from and their licence texts."
    echo "Libraries and drivers the player's system provides (glibc, Vulkan, X11/Wayland, audio) are not bundled."
} > "$manifest"
for lib in $(cd "$appdir/usr/lib" && ls -- *.so* compat/*.so* 2>/dev/null | sed 's|.*/||' | sort -u); do
    path=${owners[$lib]:-}
    [[ -n $path ]] || die "No source recorded for $lib"
    real=$(readlink -f -- "$path")
    package=$(dpkg-query -S "$real" "$path" 2>/dev/null | head -1 | cut -d: -f1 || true)
    if [[ $real == "$deps_dir"/ffmpeg/lib/* ]]; then
        mkdir -p "$licenses/system/ffmpeg"
        install -m644 "$deps_dir"/ffmpeg/share/licenses/ffmpeg/* "$licenses/system/ffmpeg/"
        printf '\n%s (FFmpeg, built from the pinned source by tools/build-ffmpeg-linux.sh)\n  Licence text: licenses/system/ffmpeg/ (LGPL-2.1-or-later; source: SOURCE.txt)\n' \
            "$lib" >> "$manifest"
    elif [[ -n $package ]]; then
        version_of=$(dpkg-query -W -f='${Version}' "$package")
        mkdir -p "$licenses/system/$package"
        install -m644 "/usr/share/doc/$package/copyright" "$licenses/system/$package/copyright"
        printf '\n%s (%s %s)\n  Licence text: licenses/system/%s/copyright\n' "$lib" "$package" "$version_of" "$package" >> "$manifest"
    elif component=${source_built[${lib%%.so*}]:-} && [[ -n $component && $real == /usr/local/lib/* ]]; then
        texts=(/usr/local/share/licenses/"$component"/*)
        [[ -f ${texts[0]} ]] || die "No licence text in /usr/local/share/licenses/$component (run packaging/linux/ci-deps.sh)"
        mkdir -p "$licenses/system/$component"
        install -m644 "${texts[@]}" "$licenses/system/$component/"
        printf '\n%s (%s, built from source by packaging/linux/ci-deps.sh)\n  Licence text: licenses/system/%s/\n' \
            "$lib" "$component" "$component" >> "$manifest"
    else
        die "No package owns $path: add its licence handling to tools/package-linux.sh"
    fi
done

step 'Checking the AppDir'
check_appdir "$appdir"

step 'Writing the AppImage'
tool=$work/appimagetool-$appimagetool_version-x86_64.AppImage
if [[ ! -x $tool ]]; then
    curl -fL --retry 3 -o "$tool.part" \
        "https://github.com/AppImage/appimagetool/releases/download/$appimagetool_version/appimagetool-x86_64.AppImage"
    echo "$appimagetool_sha256  $tool.part" | sha256sum -c - || die 'appimagetool does not match the pinned checksum'
    chmod +x "$tool.part"
    mv "$tool.part" "$tool"
fi
echo "$appimagetool_sha256  $tool" | sha256sum -c --status - || die 'appimagetool does not match the pinned checksum'
# appimagetool would otherwise download the unpinned "continuous" runtime.
runtime=$work/runtime-x86_64
bash tools/build-appimage-runtime.sh "$runtime" "$work/appimage-runtime"
ARCH=x86_64 "$tool" --appimage-extract-and-run --runtime-file "$runtime" "$appdir" "$dist/$name.AppImage.new" \
    >"$work/appimagetool.log" 2>&1 || { cat "$work/appimagetool.log" >&2; die 'appimagetool failed'; }
chmod 755 "$dist/$name.AppImage.new"
bash tools/build-appimage-runtime.sh --check "$dist/$name.AppImage.new"
verify_embedded_runtime "$dist/$name.AppImage.new" "$runtime"

step 'Checking the contents of the written AppImage'
rm -rf "$work/written"
mkdir -p "$work/written"
(cd "$work/written" && "$dist/$name.AppImage.new" --appimage-extract >/dev/null)
check_appdir "$work/written/squashfs-root" "$dist/$name.AppImage.new"
diff -r --no-dereference "$appdir" "$work/written/squashfs-root" >/dev/null ||
    die 'the written AppImage does not hold exactly the checked AppDir'
rm -rf "$work/written"
mv -f "$dist/$name.AppImage.new" "$dist/$name.AppImage"
(cd "$dist" && sha256sum "$name.AppImage" > "$name.AppImage.sha256")

printf '\nAPPIMAGE READY (nothing was published)\n'
printf '  AppImage: %s (%s)\n' "$dist/$name.AppImage" "$(du -h "$dist/$name.AppImage" | cut -f1)"
printf '  Linux AppImage SHA-256: %s\n' "$(cut -d' ' -f1 "$dist/$name.AppImage.sha256")"
printf '  Symbols:  %s (not in the package)\n' "$symbols/$binary.debug"
printf '  VirusTotal: scan the AppImage on its own (docs/RELEASE_SECURITY.md); uploads are manual\n'
