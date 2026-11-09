#!/usr/bin/env bash
# Builds the FFmpeg pinned in packaging/linux/ffmpeg.env into DEPS/ffmpeg: shared libavcodec, libavutil and libswscale
# with only the H.264 and HEVC decoders the game's videos need, LGPL (no GPL parts). The pinned nasm assembles FFmpeg's
# x86 code and is not shipped. tools/build.sh runs this; it rebuilds only when the pins or this script change.
# Nothing is installed outside DEPS.
#
#   tools/build-ffmpeg-linux.sh DEPS
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/.." && pwd)
# shellcheck source=../packaging/linux/ffmpeg.env
. "$root/packaging/linux/ffmpeg.env"
deps=$(realpath -m -- "${1:?usage: tools/build-ffmpeg-linux.sh DEPS}")
die() { echo "ERROR: $*" >&2; exit 1; }

cc=${CC:-clang}
for tool in "$cc" make curl tar xz sha256sum pkg-config; do
    command -v "$tool" >/dev/null || die "Missing $tool"
done

# Written only into the pkg-config files (rewritten below) and FFmpeg's configuration string: no path of this machine.
prefix=/opt/13-sentinels-ffmpeg
configure_flags=(--prefix="$prefix" --cc="$cc" --enable-shared --disable-static --enable-pic --disable-debug
    --disable-programs --disable-doc --disable-autodetect --disable-everything --enable-pthreads
    --disable-avformat --disable-avdevice --disable-avfilter --disable-swresample --disable-network --enable-swscale
    --enable-decoder=h264 --enable-decoder=hevc --enable-parser=h264 --enable-parser=hevc)
out=$deps/ffmpeg
stamp=$out/.tsr_ffmpeg
fingerprint=$({ cat "$root/packaging/linux/ffmpeg.env" "${BASH_SOURCE[0]}"; "$cc" --version | head -1
                printf '%s\n' "${configure_flags[@]}"; } | sha256sum | cut -d' ' -f1)
if [[ -f $stamp && $(<"$stamp") == "$fingerprint" && -f $out/lib/pkgconfig/libavcodec.pc ]]; then
    echo "FFmpeg $FFMPEG_VERSION: current"
    exit 0
fi

fetch() { # url sha256 file
    if [[ ! -f $3 ]] || ! echo "$2  $3" | sha256sum -c --status -; then
        rm -f -- "$3"
        curl -fsSL --retry 3 -o "$3.part" "$1"
        mv -f -- "$3.part" "$3"
    fi
    echo "$2  $3" | sha256sum -c --status - || die "$1 does not match its pinned SHA-256"
}

work=$deps/work
downloads=$deps/downloads
rm -rf -- "$work" "$out" "$out.stage"
mkdir -p -- "$work" "$downloads"
jobs=$(nproc)

echo "== nasm $NASM_VERSION (build tool only)"
fetch "https://www.nasm.us/pub/nasm/releasebuilds/$NASM_VERSION/nasm-$NASM_VERSION.tar.xz" "$NASM_SHA256" \
    "$downloads/nasm-$NASM_VERSION.tar.xz"
tar -xJf "$downloads/nasm-$NASM_VERSION.tar.xz" -C "$work"
(cd "$work/nasm-$NASM_VERSION" && ./configure CC="$cc" >"$work/nasm-configure.log" 2>&1 && make -s -j"$jobs" nasm \
    >"$work/nasm-make.log" 2>&1) || { tail -30 "$work"/nasm-*.log >&2; die 'nasm did not build'; }
# On PATH under its plain name: a path given to configure would be recorded in the libraries.
export PATH="$work/nasm-$NASM_VERSION:$PATH"
[[ $(nasm -v) == *"$NASM_VERSION"* ]] || die "nasm on PATH is not $NASM_VERSION"

echo "== FFmpeg $FFMPEG_VERSION (H.264 and HEVC decoders, LGPL)"
fetch "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz" "$FFMPEG_SHA256" "$downloads/ffmpeg-$FFMPEG_VERSION.tar.xz"
tar -xJf "$downloads/ffmpeg-$FFMPEG_VERSION.tar.xz" -C "$work"
src=$work/ffmpeg-$FFMPEG_VERSION
# Built in its own folder, so the source paths compiled into the libraries stay relative.
(cd "$src" && ./configure "${configure_flags[@]}" >"$work/ffmpeg-configure.log" 2>&1) ||
    { tail -30 "$work/ffmpeg-configure.log" >&2; die 'FFmpeg configure failed'; }
grep -q '^#define CONFIG_GPL 0' "$src/config.h" || die 'FFmpeg was configured with GPL parts'
grep -q '^#define HAVE_X86ASM 1' "$src/config.h" || die 'FFmpeg was configured without its x86 assembly'
make -s -C "$src" -j"$jobs" >"$work/ffmpeg-make.log" 2>&1 || { tail -40 "$work/ffmpeg-make.log" >&2; die 'FFmpeg did not build'; }
make -s -C "$src" install DESTDIR="$out.stage" >"$work/ffmpeg-install.log" 2>&1 ||
    { tail -20 "$work/ffmpeg-install.log" >&2; die 'FFmpeg install failed'; }
mv -- "$out.stage$prefix" "$out"
rm -rf -- "$out.stage"
sed -i "s|$prefix|$out|g" "$out"/lib/pkgconfig/*.pc

licences=$out/share/licenses/ffmpeg
install -Dm644 -t "$licences" "$src/COPYING.LGPLv2.1" "$src/LICENSE.md"
{
    echo "FFmpeg $FFMPEG_VERSION, LGPL-2.1-or-later: libavcodec, libavutil and libswscale, built unmodified from"
    echo "  https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
    echo "  SHA-256 $FFMPEG_SHA256"
    echo "configured with: ${configure_flags[*]}"
} > "$licences/SOURCE.txt"
for library in libavcodec libavutil libswscale; do
    PKG_CONFIG_PATH="$out/lib/pkgconfig" pkg-config --exists "$library" || die "$library.pc is not usable"
done
rm -rf -- "$work"
printf '%s' "$fingerprint" > "$stamp"
echo "FFmpeg $FFMPEG_VERSION: built into $out"
