#!/usr/bin/env bash
# Copyright (c) 2026 KhasMek
# SPDX-License-Identifier: MIT
#
# One command for the Port (task M3-A): build, test against the device's own
# libraries, package exactly the tested binaries, and test the package itself.
#
#   scripts/make_port.sh <device.sqfs | extracted-root-dir> [--from <device-build-dir>]
#
#   1. scripts/build_device.sh                     build, all tests, symbol check, hashes
#   2. scripts/test_on_knulli_rootfs.sh <image>    the build in the device root
#   3. scripts/package_port.sh                     dist/AudreyBrick and the zip, from
#                                                  those exact binaries (hash-checked)
#   4. scripts/test_on_knulli_rootfs.sh <image> --package dist/AudreyBrick
#                                                  the package as it will be copied
# Then prints the SHA-256 of the packaged binaries and the zip, and the zip's path.
# Stops at the first failing step.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
FROM="${HOME}/.cache/audreybrick/device-build"
SRC=""
usage() { echo "usage: scripts/make_port.sh <device.sqfs | extracted-root-dir> [--from <device-build-dir>]" >&2; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --from) [ $# -ge 2 ] || usage; FROM="$2"; shift ;;
        -*) usage ;;
        *) [ -z "$SRC" ] || usage; SRC="$1" ;;
    esac
    shift
done
[ -n "$SRC" ] || usage
[ -e "$SRC" ] || { echo "make_port: $SRC does not exist" >&2; exit 2; }

step() { printf '\n######## make_port: %s ########\n' "$*"; }
step "1/4 device build"
"$REPO_ROOT/scripts/build_device.sh" --out "$FROM"
step "2/4 the build in the device root"
"$REPO_ROOT/scripts/test_on_knulli_rootfs.sh" "$SRC" --from "$FROM"
step "3/4 package"
"$REPO_ROOT/scripts/package_port.sh" --from "$FROM"
step "4/4 the package in the device root"
"$REPO_ROOT/scripts/test_on_knulli_rootfs.sh" "$SRC" --package "$REPO_ROOT/dist/AudreyBrick"

step "result"
sums="$(ls "$REPO_ROOT"/dist/AudreyBrick-*.sha256)"
zip="$(ls "$REPO_ROOT"/dist/AudreyBrick-*.zip)"
echo "SHA-256 of the packaged binaries and the zip (from $sums):"
sed 's/^/  /' "$sums"
echo "device build hashes ($FROM/SHA256SUMS):"
sed 's/^/  /' "$FROM/SHA256SUMS"
echo "zip: $zip ($(wc -c < "$zip" | tr -d ' ') bytes)"
echo "The packaged files above are the ones tested in step 4 (and built and tested in steps 1 and 2)."
echo "make_port: OK"
