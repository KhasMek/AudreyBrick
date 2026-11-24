#!/usr/bin/env bash
# Copyright (c) 2026 KhasMek
# SPDX-License-Identifier: MIT
#
# The device build for the TrimUI Brick on KNULLI Scarab (task M3-A, docs/briefs/M3.md).
#
#   scripts/build_device.sh [--out <dir>] [--check-only]
#
# Steps:
#   1. Build the image audreybrick-arm64-device:local from docker/Dockerfile.arm64-device
#      (Debian 12 arm64, pinned by digest; never pushed).
#   2. In a container (repository mounted READ-ONLY, no network, as the calling user):
#      configure Release with the version (`git describe --always --dirty`, computed
#      here on the host) and the build date, build, run every test (the SDL tests with
#      the dummy video and audio drivers, as their ctest entries set), then
#      `cmake --install` into a staging directory: audreybrick with an rpath of
#      $ORIGIN/libs.aarch64, and libs.aarch64/libaudrey_lgpl.so. SDL2 is linked by
#      soname (libSDL2-2.0.so.0) and not bundled; libstdc++ and libgcc are dynamic.
#   3. Copy the two binaries out to <out>/port/ and check them (docker/device_in_container.sh
#      check): every symbol version they need must be at most GLIBC_2.39,
#      GLIBCXX_3.4.32 and CXXABI_1.3.14 (what the device's glibc and GCC 13 libstdc++
#      provide, docs/PLATFORM_NOTES.md section 10), the NEEDED list must hold only
#      libraries the device has, and the executable's RUNPATH must be
#      $ORIGIN/libs.aarch64. Prints the highest versions, the per-library table and the
#      NEEDED lists; any violation fails the script.
#
# --check-only: skip the build and re-run step 3 on <out>/port/ (fast).
# The limits can be made stricter (to show that the check fails) through the
# environment: AB_MAX_GLIBC, AB_MAX_GLIBCXX, AB_MAX_CXXABI, e.g.
#   AB_MAX_GLIBC=2.16 scripts/build_device.sh --check-only     # must exit non-zero
#
# The documented flow (scripts/make_port.sh runs it in one command):
#   scripts/build_device.sh                       build, test, symbol check
#   scripts/test_on_knulli_rootfs.sh <image>      the build against the device's libraries
#   scripts/package_port.sh                       package exactly those binaries
#   scripts/test_on_knulli_rootfs.sh <image> --package dist/AudreyBrick
#
# Output directory (--out, default $HOME/.cache/audreybrick/device-build): must be
# outside the repository, and new, empty or marked by an earlier run
# (.audreybrick-device-build-out). Only this script's own names in it are replaced:
# port/, logs/, image-build.log, VERSION, SHA256SUMS. scripts/package_port.sh reads
# <out>/port/ and refuses files whose SHA-256 differs from <out>/SHA256SUMS.
#
# Exit status: 0 only if the image builds, configure, build and every test pass,
# and the symbol check passes.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
AB_SCRIPT="build_device"
# shellcheck source=scripts/_paths.sh
. "$REPO_ROOT/scripts/_paths.sh"

IMAGE="audreybrick-arm64-device:local"
MARKER=".audreybrick-device-build-out"
OUT="${HOME}/.cache/audreybrick/device-build"
CHECK_ONLY=0

usage() { echo "usage: scripts/build_device.sh [--out <dir>] [--check-only]" >&2; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --out)        [ $# -ge 2 ] || usage; OUT="$2"; shift ;;
        --check-only) CHECK_ONLY=1 ;;
        *) usage ;;
    esac
    shift
done

command -v docker >/dev/null 2>&1 || { echo "$AB_SCRIPT: docker is not installed" >&2; exit 1; }
case "$REPO_ROOT" in *,*) echo "$AB_SCRIPT: the repository path contains a comma, which docker --mount cannot take" >&2; exit 2 ;; esac

req="$OUT"
OUT="$(ab_resolve_path "$req")" \
    || { echo "$AB_SCRIPT: cannot resolve --out '$req'; refused" >&2; exit 2; }
ab_check_outside_repo "--out '$req'" "$OUT"
ab_check_reusable "--out '$req'" "$OUT" "$MARKER"

if [ "$CHECK_ONLY" -eq 1 ]; then
    [ -f "$OUT/$MARKER" ] && [ -f "$OUT/port/audreybrick" ] \
        || { echo "$AB_SCRIPT: --check-only: no earlier build in $OUT (run without --check-only first)" >&2; exit 1; }
fi

mkdir -p "$OUT"
printf 'Output directory of scripts/build_device.sh (AudreyBrick M3-A).\n' > "$OUT/$MARKER"
echo "$AB_SCRIPT: repository $REPO_ROOT (mounted read-only)"
echo "$AB_SCRIPT: output     $OUT"

echo "==== Image $IMAGE ===="
docker build --platform linux/arm64 -t "$IMAGE" -f "$REPO_ROOT/docker/Dockerfile.arm64-device" \
    "$REPO_ROOT/docker" >"$OUT/image-build.log" 2>&1 \
    || { cat "$OUT/image-build.log" >&2; echo "$AB_SCRIPT: docker build failed" >&2; exit 1; }
echo "base image: $(grep -m1 '^FROM' "$REPO_ROOT/docker/Dockerfile.arm64-device" | cut -d' ' -f2)"
echo "image id:   $(docker image inspect --format '{{.Id}}' "$IMAGE")"

run_container() { # <mode>
    docker run --rm --platform linux/arm64 --network none \
        --user "$(id -u):$(id -g)" -e HOME=/tmp \
        -e AB_VERSION="${AB_VERSION:-}" -e AB_BUILD_DATE="${AB_BUILD_DATE:-}" \
        -e AB_MAX_GLIBC="${AB_MAX_GLIBC:-2.39}" -e AB_MAX_GLIBCXX="${AB_MAX_GLIBCXX:-3.4.32}" \
        -e AB_MAX_CXXABI="${AB_MAX_CXXABI:-1.3.14}" \
        --mount "type=bind,src=$REPO_ROOT,dst=/src,readonly" \
        --mount "type=bind,src=$OUT,dst=/out" \
        "$IMAGE" bash /src/docker/device_in_container.sh "$1"
}

if [ "$CHECK_ONLY" -eq 0 ]; then
    # The version: git describe on the host (read-only; the container has no git).
    # GIT_OPTIONAL_LOCKS=0: git describe must not refresh (write) the index.
    # A release build names its version itself (AUDREY_RELEASE_VERSION, the tag).
    AB_VERSION="${AUDREY_RELEASE_VERSION:-$(GIT_OPTIONAL_LOCKS=0 git -C "$REPO_ROOT" describe --always --dirty 2>/dev/null || echo unknown)}"
    AB_VERSION="$(printf '%s' "$AB_VERSION" | tr -c 'A-Za-z0-9._+-' '_')"
    AB_BUILD_DATE="$(date -u '+%Y-%m-%d %H:%M UTC')"
    export AB_VERSION AB_BUILD_DATE
    echo "version:    $AB_VERSION"
    echo "build date: $AB_BUILD_DATE"
    rm -rf "${OUT:?}/port" "${OUT:?}/logs" "${OUT:?}/VERSION" "${OUT:?}/SHA256SUMS"
    run_container build
    printf '%s\n' "$AB_VERSION" > "$OUT/VERSION"
    # The hashes of exactly what was built and tested; package_port.sh refuses to
    # package files that differ from them.
    (cd "$OUT/port" && shasum -a 256 audreybrick libs.aarch64/libaudrey_lgpl.so) > "$OUT/SHA256SUMS"
fi
run_container check
echo
echo "$AB_SCRIPT: binaries in $OUT/port (audreybrick, libs.aarch64/libaudrey_lgpl.so), SHA-256:"
sed 's/^/  /' "$OUT/SHA256SUMS"
echo "$AB_SCRIPT: OK"
