#!/usr/bin/env bash
# Build a fully static, stripped x86_64 musl vidwizard via the Alpine chroot at
# /mnt/alpine. Host default `make` stays debug/dynamic.
#
# Usage (from repo root or anywhere):
#   ./scripts/build-static-alpine.sh
#
# Requires: sudo -S password in /tmp/password.txt, Alpine chroot at /mnt/alpine.
set -euo pipefail

PASS="${PASSWORD_FILE:-/tmp/password.txt}"
ALPINE="${ALPINE_CHROOT:-/mnt/alpine}"
PREFIX="${VIDWIZARD_STATIC_PREFIX:-/opt/vidwizard-static}"
SRC_DIR="${VIDWIZARD_STATIC_SRC:-/src/vidwizard-static}"
FFMPEG_VER="${FFMPEG_VER:-8.1.2}"
OPUS_VER="${OPUS_VER:-1.6.1}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"

host_root() {
    cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd
}

die() {
    echo "build-static-alpine: $*" >&2
    exit 1
}

if [[ ! -f /etc/alpine-release ]]; then
    ROOT="$(host_root)"
    [[ -f "$PASS" ]] || die "missing sudo password file $PASS"
    [[ -d "$ALPINE" ]] || die "Alpine chroot not found: $ALPINE"
    [[ -x "$ALPINE/sbin/apk" ]] || die "not an Alpine chroot: $ALPINE"

    sudo -S mkdir -p "$ALPINE/mnt/vidwizard" "$ALPINE$SRC_DIR" "$ALPINE$PREFIX" <"$PASS"
    if ! mountpoint -q "$ALPINE/mnt/vidwizard"; then
        sudo -S mount --bind "$ROOT" "$ALPINE/mnt/vidwizard" <"$PASS"
    fi
    if ! mountpoint -q "$ALPINE/proc"; then
        sudo -S mount -t proc proc "$ALPINE/proc" <"$PASS"
    fi
    if ! mountpoint -q "$ALPINE/dev"; then
        sudo -S mount --bind /dev "$ALPINE/dev" <"$PASS"
    fi

    echo "build-static-alpine: entering $ALPINE (jobs=$JOBS)" >&2
    exec sudo -S chroot "$ALPINE" /bin/bash /mnt/vidwizard/scripts/build-static-alpine.sh <"$PASS"
fi

# --- inside Alpine chroot ---

export PATH="/usr/bin:/bin:/usr/sbin:/sbin"
export PREFIX
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:/usr/lib/pkgconfig"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig:/usr/lib/pkgconfig"
umask 022
cd /mnt/vidwizard

apk add --no-cache \
    build-base g++ make pkgconf nasm yasm \
    curl wget xz tar file linux-headers bash \
    x264-dev \
    freetype-dev freetype-static \
    lame-dev \
    opus-dev \
    libpng-dev libpng-static \
    zlib-dev zlib-static \
    bzip2-dev bzip2-static \
    brotli-dev brotli-static \
    autoconf automake libtool >/dev/null

mkdir -p "$SRC_DIR" "$PREFIX" /mnt/vidwizard/dist /mnt/vidwizard/bin/static

need_libav_a() {
    [[ -f "$PREFIX/lib/libavcodec.a" && -f "$PREFIX/lib/libavformat.a" &&
        -f "$PREFIX/lib/libavfilter.a" && -f "$PREFIX/lib/libavutil.a" &&
        -f "$PREFIX/lib/libswscale.a" && -f "$PREFIX/lib/libswresample.a" ]]
}

if [[ ! -f "$PREFIX/lib/libopus.a" ]]; then
    echo "build-static-alpine: building opus $OPUS_VER static" >&2
    cd "$SRC_DIR"
    if [[ ! -f "opus-${OPUS_VER}.tar.gz" ]]; then
        curl -fL --retry 3 -o "opus-${OPUS_VER}.tar.gz" \
            "https://ftp.osuosl.org/pub/xiph/releases/opus/opus-${OPUS_VER}.tar.gz"
    fi
    rm -rf "opus-${OPUS_VER}"
    tar xzf "opus-${OPUS_VER}.tar.gz"
    cd "opus-${OPUS_VER}"
    [[ -x ./configure ]] || die "opus tarball has no ./configure"
    ./configure --prefix="$PREFIX" --enable-static --disable-shared \
        --disable-doc --disable-extra-programs
    make -j"$JOBS"
    make install
    [[ -f "$PREFIX/lib/libopus.a" ]] || die "opus static install failed"
fi

if ! need_libav_a; then
    echo "build-static-alpine: building ffmpeg $FFMPEG_VER static (no Alpine libav .a)" >&2
    cd "$SRC_DIR"
    if [[ ! -f "ffmpeg-${FFMPEG_VER}.tar.xz" ]]; then
        curl -fL --retry 3 -o "ffmpeg-${FFMPEG_VER}.tar.xz" \
            "https://ffmpeg.org/releases/ffmpeg-${FFMPEG_VER}.tar.xz"
    fi
    if [[ ! -f "ffmpeg-${FFMPEG_VER}/configure" ]]; then
        tar xf "ffmpeg-${FFMPEG_VER}.tar.xz"
    fi
    cd "ffmpeg-${FFMPEG_VER}"
    if [[ ! -f ffbuild/config.mak ]]; then
        ./configure \
            --prefix="$PREFIX" \
            --pkg-config-flags="--static" \
            --extra-cflags="-O3 -ffunction-sections -fdata-sections -I${PREFIX}/include" \
            --extra-ldflags="-L${PREFIX}/lib" \
            --extra-libs="-lm -lpthread" \
            --enable-static \
            --disable-shared \
            --disable-autodetect \
            --disable-doc \
            --disable-htmlpages \
            --disable-manpages \
            --disable-podpages \
            --disable-txtpages \
            --disable-programs \
            --disable-debug \
            --disable-network \
            --disable-avdevice \
            --enable-gpl \
            --enable-zlib \
            --enable-bzlib \
            --enable-libx264 \
            --enable-libfreetype \
            --enable-libmp3lame \
            --enable-libopus
    fi
    make -j"$JOBS"
    make install
    need_libav_a || die "ffmpeg static install missing libav*.a"
fi

cd /mnt/vidwizard
echo "build-static-alpine: pkg-config --static --libs:" >&2
pkg-config --static --libs libavfilter libavformat libavcodec libavutil libswscale libswresample >&2

echo "build-static-alpine: compiling vidwizard (BUILD=static)" >&2
rm -rf build/static bin/static
make BUILD=static -j"$JOBS" V=0 all

BIN="bin/static/vidwizard"
[[ -f "$BIN" ]] || die "missing $BIN"

cp -a "$BIN" "${BIN}.unstripped"
strip --strip-all -o "$BIN.stripped" "${BIN}.unstripped"
if readelf -n "$BIN.stripped" 2>/dev/null | grep -qi build-id; then
    strip -R .note.gnu.build-id -o "$BIN.stripped2" "$BIN.stripped" || true
    if [[ -f "$BIN.stripped2" ]]; then
        mv "$BIN.stripped2" "$BIN.stripped"
    fi
fi
mv "$BIN.stripped" "$BIN"
chmod 755 "$BIN"

install -m 755 "$BIN" /mnt/vidwizard/dist/vidwizard-x86_64-static
install -m 755 "$BIN" /mnt/vidwizard/dist/vidwizard

echo "build-static-alpine: verifying" >&2
file "$BIN"
if file "$BIN" | grep -qi 'dynamic'; then
    die "file(1) reports a dynamic binary"
fi
if ! file "$BIN" | grep -qi 'static'; then
    die "file(1) does not report statically linked"
fi
if readelf -l "$BIN" | grep -q INTERP; then
    die "PT_INTERP present — not a fully static ET_EXEC"
fi
if readelf -d "$BIN" 2>/dev/null | grep -q NEEDED; then
    die "DT_NEEDED present — not fully static"
fi
ldd "$BIN" >/tmp/vidwizard-ldd.txt 2>&1 || true
cat /tmp/vidwizard-ldd.txt
if grep -qiE '=> /|linux-vdso|ld-linux|ld-musl' /tmp/vidwizard-ldd.txt; then
    # musl ldd is ld-musl itself and may print its path even for static-pie;
    # INTERP/NEEDED checks above are authoritative.
    if readelf -l "$BIN" | grep -q INTERP; then
        die "binary is not fully static (INTERP + ldd shared objects)"
    fi
fi

readelf -n "$BIN" | grep -i build || echo "build-id: none"
"$BIN" -v
"$BIN" -h | head -n 5

if [[ -f testdata/clip.mp4 ]]; then
    mkdir -p testdata/out
    echo "build-static-alpine: smoke --grayscale testdata/clip.mp4" >&2
    "$BIN" --grayscale testdata/clip.mp4 -o testdata/out/static_gray.mp4
    ls -lh testdata/out/static_gray.mp4
fi

if [[ -n "${SUDO_UID:-}" ]]; then
    chown "${SUDO_UID}:${SUDO_GID:-$SUDO_UID}" \
        /mnt/vidwizard/dist/vidwizard \
        /mnt/vidwizard/dist/vidwizard-x86_64-static \
        "$BIN" "${BIN}.unstripped" 2>/dev/null || true
fi

ls -lh /mnt/vidwizard/dist/vidwizard-x86_64-static
echo "build-static-alpine: done -> dist/vidwizard-x86_64-static" >&2
