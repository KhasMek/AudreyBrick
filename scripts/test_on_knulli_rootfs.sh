#!/usr/bin/env bash
# Copyright (c) 2026 KhasMek
# SPDX-License-Identifier: MIT
#
# Runs the device build against the device's own system libraries (task M3-A, part B).
#
#   scripts/test_on_knulli_rootfs.sh <device.sqfs | extracted-root-dir>
#            [--rootfs <dir>] [--from <device-build-dir>] [--work <dir>]
#
# The device's system image (KNULLI's squashfs) is never copied into the repository
# or the package. This script:
#   1. Extracts the part of it needed to run the app into a directory outside the
#      repository (default: next to the image, rootfs-<image name>/root), unless an
#      earlier extraction of the same image is there (it is reused). Extracted:
#        /etc, /bin, /lib64 and /usr/lib64 (symlinks), /lib without firmware/ and
#        modules/, /usr/lib without libretro/, python3.12/, locale/, samba/ and
#        xtables/ (emulator cores, Python, locales, Samba: about 1.7 GB not needed;
#        xtables has file names that differ only in case, which a case-insensitive
#        macOS disk cannot hold), and from /usr/bin only coreutils, ldd and the
#        coreutils symlinks. About 400 MB. unsquashfs runs in the device build
#        container (Debian's squashfs-tools); any other case-only name clash fails
#        the extraction.
#      With a directory argument, that directory is used as the root as it is.
#   2. Imports that tree as a Docker image (audreybrick-knulli-rootfs:<id>, reused if
#      present), so a container runs with the device's /lib/ld-linux-aarch64.so.1,
#      glibc 2.39, libstdc++ 6.0.32 and libSDL2 2.32.8. Docker adds /proc, /dev
#      and /sys; /tmp is a tmpfs.
#   3. Assembles the package with scripts/package_port.sh (no rebuild) into a work
#      directory outside the repository, then runs docker/knulli_rootfs_tests.sh in
#      that root with the device's own bash (no network, as the calling user):
#      the device's ldd on the binary; the self-test with --no-video and the dummy
#      audio driver (the device's SDL has no dummy video driver, only mali),
#      checking the SDL version the app logs; that without --no-video the video
#      failure is fatal and reported;
#      $ORIGIN resolution through the dynamic loader; and the packaged
#      AudreyBrick.sh with the executable bit removed, from another working
#      directory, with a space in the Ports path, plus log rotation, AUDREY_BUFFER
#      handling and exit-status propagation; and (M3-B) the CPU governor handling
#      against a fake sysfs tree (AUDREY_SYSFS_CPU): set and restored on a normal
#      exit, on SIGTERM and on SIGKILL to the launcher, AUDREY_GOVERNOR=keep, a
#      governor not offered, invalid names, read-only files; and AUDREY_SWAP_AB.
#   Nothing from the image is executed except the dynamic loader, the libraries
#   and standard tools (bash, ldd, coreutils, grep, sed, procps).
#
# NOT exercised here: the display path (the Mali video driver, EGL, the renderer),
# ALSA/PipeWire output, the buttons and the device's CPU. Those can only be tested
# on the device (the first-run checklist).
#
# --from: build_device.sh output (default $HOME/.cache/audreybrick/device-build).
# --package: an assembled package folder (e.g. dist/AudreyBrick, holding
#         AudreyBrick.sh and audreybrick/) to test exactly as packaged: the ldd and
#         direct self-test checks then use its binary, and the launcher checks its
#         launcher. Without it, a package is assembled from --from into the work
#         directory.
# --work: work directory for the package copy and the logs (default: a new
#         temporary directory). Must be outside the repository; reused only if
#         marked by an earlier run. Kept at the end for inspection.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
AB_SCRIPT="test_on_knulli_rootfs"
# shellcheck source=scripts/_paths.sh
. "$REPO_ROOT/scripts/_paths.sh"

BUILD_IMAGE="audreybrick-arm64-device:local"
FROM="${HOME}/.cache/audreybrick/device-build"
ROOTFS=""
WORK=""
PACKAGE=""
SRC=""
ROOT_MARKER=".audreybrick-rootfs"
WORK_MARKER=".audreybrick-rootfs-test-out"

usage() { echo "usage: scripts/test_on_knulli_rootfs.sh <device.sqfs | extracted-root-dir> [--rootfs <dir>] [--from <dir>] [--package <dir>] [--work <dir>]" >&2; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --rootfs) [ $# -ge 2 ] || usage; ROOTFS="$2"; shift ;;
        --from)   [ $# -ge 2 ] || usage; FROM="$2"; shift ;;
        --work)   [ $# -ge 2 ] || usage; WORK="$2"; shift ;;
        --package) [ $# -ge 2 ] || usage; PACKAGE="$2"; shift ;;
        -*) usage ;;
        *) [ -z "$SRC" ] || usage; SRC="$1" ;;
    esac
    shift
done
[ -n "$SRC" ] || usage
command -v docker >/dev/null 2>&1 || { echo "$AB_SCRIPT: docker is not installed" >&2; exit 1; }
case "$REPO_ROOT" in *,*) echo "$AB_SCRIPT: the repository path contains a comma" >&2; exit 2 ;; esac

SRC="$(ab_resolve_path "$SRC")" && [ -e "$SRC" ] || { echo "$AB_SCRIPT: $SRC does not exist" >&2; exit 2; }
ab_check_outside_repo "the device image" "$SRC"
# Every path argument is validated here, before any extraction or image import.
if [ -n "$PACKAGE" ]; then
    PACKAGE="$(ab_resolve_path "$PACKAGE")" && [ -d "$PACKAGE" ] \
        || { echo "$AB_SCRIPT: --package: no such directory" >&2; exit 2; }
    [ -f "$PACKAGE/AudreyBrick.sh" ] && [ -f "$PACKAGE/audreybrick/audreybrick" ] \
        && [ -f "$PACKAGE/audreybrick/libs.aarch64/libaudrey_lgpl.so" ] \
        || { echo "$AB_SCRIPT: --package $PACKAGE does not hold AudreyBrick.sh and audreybrick/ (with the binary and libs.aarch64)" >&2; exit 2; }
    case "$PACKAGE" in *,*) echo "$AB_SCRIPT: --package path contains a comma" >&2; exit 2 ;; esac
    PORT_UNDER_TEST="$PACKAGE/audreybrick"
else
    FROM="$(ab_resolve_path "$FROM")" || { echo "$AB_SCRIPT: cannot resolve --from" >&2; exit 2; }
    [ -f "$FROM/port/audreybrick" ] || { echo "$AB_SCRIPT: no device build in $FROM; run scripts/build_device.sh first" >&2; exit 1; }
    case "$FROM" in *,*) echo "$AB_SCRIPT: --from path contains a comma" >&2; exit 2 ;; esac
    PORT_UNDER_TEST="$FROM/port"
fi
if [ -z "$WORK" ]; then
    tmpbase="$(ab_resolve_path "${TMPDIR:-/tmp}")" || exit 2
    ab_check_outside_repo "the temporary directory" "$tmpbase"
    WORK_NEW=1
else
    WORK="$(ab_resolve_path "$WORK")" || { echo "$AB_SCRIPT: cannot resolve --work" >&2; exit 2; }
    ab_check_outside_repo "--work" "$WORK"
    ab_check_reusable "--work" "$WORK" "$WORK_MARKER"
    WORK_NEW=0
fi
if [ ! -d "$SRC" ]; then
    name="$(basename "$SRC")"; name="${name%.sqfs}"; name="${name%.squashfs}"
    [ -n "$ROOTFS" ] || ROOTFS="$(dirname "$SRC")/rootfs-$name"
    ROOTFS="$(ab_resolve_path "$ROOTFS")" || { echo "$AB_SCRIPT: cannot resolve --rootfs" >&2; exit 2; }
    ab_check_outside_repo "--rootfs" "$ROOTFS"
    ab_check_reusable "--rootfs" "$ROOTFS" "$ROOT_MARKER"
fi

echo "==== Build image $BUILD_IMAGE (for unsquashfs and tar) ===="
docker build -q --platform linux/arm64 -t "$BUILD_IMAGE" -f "$REPO_ROOT/docker/Dockerfile.arm64-device" "$REPO_ROOT/docker" >/dev/null

# --- 1. The root filesystem -----------------------------------------------------------
if [ -d "$SRC" ]; then
    ROOT="$SRC"
    ID="dir-$(printf '%s' "$ROOT" | shasum -a 256 | cut -c1-12)"
    echo "==== Root: the given directory $ROOT ===="
    [ -x "$ROOT/lib/ld-linux-aarch64.so.1" ] || [ -f "$ROOT/lib/ld-linux-aarch64.so.1" ] \
        || { echo "$AB_SCRIPT: $ROOT has no lib/ld-linux-aarch64.so.1" >&2; exit 2; }
else
    ROOT="$ROOTFS/root"
    stamp="$(stat -f '%z %m' "$SRC" 2>/dev/null || stat -c '%s %Y' "$SRC")"
    if [ -f "$ROOTFS/$ROOT_MARKER" ] && [ -d "$ROOT" ] && [ "$(sed -n 1p "$ROOTFS/$ROOT_MARKER")" = "$stamp" ]; then
        ID="$(sed -n 2p "$ROOTFS/$ROOT_MARKER")"
        echo "==== Root: reusing the extraction in $ROOT (image $ID) ===="
    else
        echo "==== Root: extracting part of $SRC into $ROOT ===="
        ID="$(shasum -a 256 "$SRC" | cut -c1-12)"
        mkdir -p "$ROOTFS"
        rm -rf "${ROOTFS:?}/root" "${ROOTFS:?}/root.partial" "${ROOTFS:?}/$ROOT_MARKER" "${ROOTFS:?}/extract-list.txt"
        docker run --rm --platform linux/arm64 --network none --user "$(id -u):$(id -g)" -e HOME=/tmp \
            --mount "type=bind,src=$SRC,dst=/img/image.sqfs,readonly" \
            --mount "type=bind,src=$ROOTFS,dst=/dest" \
            "$BUILD_IMAGE" bash -euo pipefail -c '
                unsquashfs -lls -d R /img/image.sqfs > /tmp/listing.txt
                # Paths (without the R prefix) of everything that is extracted.
                awk '"'"'
                    { p = $6; if (p !~ /^R\//) next; p = substr(p, 2) }
                    p == "/etc" || p == "/bin" || p == "/lib64" || p == "/usr/lib64" { print p; next }
                    p ~ /^\/lib\/[^\/]+$/ && p !~ /^\/lib\/(firmware|modules)$/ { print p; next }
                    p ~ /^\/usr\/lib\/[^\/]+$/ && p !~ /^\/usr\/lib\/(libretro|python3\.12|locale|samba|xtables)$/ { print p; next }
                    p == "/usr/bin/coreutils" || p == "/usr/bin/ldd" { print p; next }
                    p ~ /^\/usr\/bin\/[^\/]+$/ && $7 == "->" && $8 == "coreutils" { print p; next }
                '"'"' /tmp/listing.txt > /dest/extract-list.txt
                # Case-only name clashes within what is extracted (macOS disks are case-insensitive).
                awk '"'"'
                    NR == FNR { sel[$0] = 1; next }
                    { p = $6; if (p !~ /^R\//) next; p = substr(p, 2); n = split(p, a, "/")
                      k = (a[2] == "usr" && n >= 4) ? "/" a[2] "/" a[3] "/" a[4] : (n >= 3 && a[2] != "etc" && a[2] != "bin") ? "/" a[2] "/" a[3] : "/" a[2]
                      if (k in sel) print tolower(p) }
                '"'"' /dest/extract-list.txt /tmp/listing.txt | sort | uniq -d > /tmp/clash.txt
                if [ -s /tmp/clash.txt ]; then echo "case-only name clashes:"; cat /tmp/clash.txt; exit 1; fi
                echo "unsquashfs: $(unsquashfs -version | head -n1)"
                echo "entries to extract: $(wc -l < /dest/extract-list.txt) top-level paths"
                unsquashfs -no-progress -q -d /dest/root.partial -ef /dest/extract-list.txt /img/image.sqfs > /tmp/unsq.log 2>&1 \
                    || { tail -n 30 /tmp/unsq.log; exit 1; }
                tail -n 2 /tmp/unsq.log
                mv /dest/root.partial /dest/root
                du -sh /dest/root | cut -f1 | sed "s/^/extracted size: /"'
        printf '%s\n%s\nExtraction of %s by scripts/test_on_knulli_rootfs.sh (AudreyBrick M3-A). Not part of the repository.\n' \
            "$stamp" "$ID" "$SRC" > "$ROOTFS/$ROOT_MARKER"
    fi
fi

# --- 2. The Docker image of that root -------------------------------------------------
# One image per extraction (tag = the image file's SHA-256 prefix, or a hash of the
# root directory's path): reused on every later run, never re-imported.
ROOT_IMAGE="audreybrick-knulli-rootfs:$ID"
if docker image inspect "$ROOT_IMAGE" >/dev/null 2>&1; then
    echo "==== Root image $ROOT_IMAGE (reused) ===="
else
    echo "==== Root image $ROOT_IMAGE (importing) ===="
    docker run --rm --platform linux/arm64 --network none \
        --mount "type=bind,src=$ROOT,dst=/rootfs,readonly" "$BUILD_IMAGE" \
        tar -C /rootfs --numeric-owner --owner=0 --group=0 -cf - . \
        | docker import --platform linux/arm64 -c 'ENV PATH=/usr/bin:/bin:/usr/sbin:/sbin' - "$ROOT_IMAGE" >/dev/null
fi
echo "root image id: $(docker image inspect --format '{{.Id}}' "$ROOT_IMAGE")"

# --- 3. The package and the tests -----------------------------------------------------
if [ "$WORK_NEW" -eq 1 ]; then
    WORK="$(mktemp -d "$tmpbase/audreybrick-rootfs-test.XXXXXX")"
else
    mkdir -p "$WORK"
fi
printf 'Work directory of scripts/test_on_knulli_rootfs.sh (AudreyBrick M3-A).\n' > "$WORK/$WORK_MARKER"
rm -rf "${WORK:?}/pkg" "${WORK:?}/out"
mkdir -p "$WORK/out"
if [ -n "$PACKAGE" ]; then
    PKG_UNDER_TEST="$PACKAGE"
    echo "==== Testing the given package $PACKAGE (read-only) ===="
else
    echo "==== Package (scripts/package_port.sh) of the build in $FROM into $WORK/pkg ===="
    "$REPO_ROOT/scripts/package_port.sh" --from "$FROM" --out "$WORK/pkg" | tail -n 4
    PKG_UNDER_TEST="$WORK/pkg/AudreyBrick"
fi
echo "files under test (SHA-256):"
(cd "$PORT_UNDER_TEST" && shasum -a 256 audreybrick libs.aarch64/libaudrey_lgpl.so) | sed 's/^/  /'
(cd "$PKG_UNDER_TEST/audreybrick" && shasum -a 256 audreybrick libs.aarch64/libaudrey_lgpl.so) | sed 's/^/  package: /'

echo "==== Tests in the device root ===="
rc=0
docker run --rm --platform linux/arm64 --network none --user "$(id -u):$(id -g)" \
    --tmpfs /tmp:exec,size=256m --tmpfs /var/run:mode=1777,size=1m -e HOME=/tmp -w / \
    --mount "type=bind,src=$PORT_UNDER_TEST,dst=/ab/port,readonly" \
    --mount "type=bind,src=$REPO_ROOT/tests/platform/rise.txt,dst=/ab/rise.txt,readonly" \
    --mount "type=bind,src=$REPO_ROOT/docker/knulli_rootfs_tests.sh,dst=/ab/tests.sh,readonly" \
    --mount "type=bind,src=$PKG_UNDER_TEST,dst=/ab/pkg,readonly" \
    --mount "type=bind,src=$WORK/out,dst=/ab/out" \
    "$ROOT_IMAGE" /bin/bash /ab/tests.sh || rc=$?
echo
echo "$AB_SCRIPT: logs and the tested package copy are in $WORK/out"
echo "$AB_SCRIPT: NOT exercised: the display path (Mali video driver, EGL, renderer) and ALSA output; test those on the device"
if [ "$rc" -eq 0 ]; then echo "$AB_SCRIPT: OK"; else echo "$AB_SCRIPT: FAILED"; fi
exit "$rc"
