#!/usr/bin/env bash
# Copyright (c) 2026 KhasMek
# SPDX-License-Identifier: MIT
#
# Assembles the Port for KNULLI on the TrimUI Brick (task M3-A, docs/briefs/M3.md).
#
#   scripts/package_port.sh [--build] [--from <device-build-dir>] [--out <dir>]
#
# 1. Packages the EXISTING device build (<device-build-dir>/port/, made and tested
#    by scripts/build_device.sh) without rebuilding, so the packaged binaries are the
#    ones that were tested. It refuses if their SHA-256 differs from what
#    build_device.sh recorded (<device-build-dir>/SHA256SUMS), checks the copies in
#    the package again, and prints the hashes. --build runs scripts/build_device.sh
#    first. (--skip-build is accepted for compatibility; it is the default.)
#    The documented flow is scripts/make_port.sh.
# 2. Lays out <out>/AudreyBrick/ (default <out> is dist/ in the repository, which
#    is git-ignored):
#        AudreyBrick.sh                              packaging/AudreyBrick.sh
#        audreybrick/audreybrick                     the executable
#        audreybrick/libs.aarch64/libaudrey_lgpl.so  ReverbSc and Tone (LGPL-2.1)
#        audreybrick/licenses/                       every file of licenses/, plus
#                                                    Audrey II's CREDITS.md
#        audreybrick/THIRD_PARTY.md, LICENSE, README.txt (packaging/README.txt)
#    and NO data/ folder (PRESET-C): the launcher keeps the user's data in
#    /userdata/saves/audreybrick (or creates audreybrick/data where that folder is
#    missing), so replacing the audreybrick folder on an update deletes nothing of
#    the user's. The script fails if a data/ folder ends up in the package. Then
#    <out>/AudreyBrick-<version>.zip of that folder, plus
#    <out>/AudreyBrick-<version>.sha256 (the two binaries and the zip). Prints the
#    listing and the hashes.
# Nothing else goes in: no tests, tools, reference audio, docs, nothing from the
# device image.
#
# --from: the build_device.sh output directory (default $HOME/.cache/audreybrick/device-build).
# --out:  dist/ in the repository (the default), or a directory outside the
#         repository (scripts/test_on_knulli_rootfs.sh uses one). Only this script's
#         own names in it are replaced: AudreyBrick/, AudreyBrick-*.zip and
#         AudreyBrick-*.sha256; a
#         directory outside the repository must be new, empty or marked by an
#         earlier run (.audreybrick-package-out).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
AB_SCRIPT="package_port"
# shellcheck source=scripts/_paths.sh
. "$REPO_ROOT/scripts/_paths.sh"

MARKER=".audreybrick-package-out"
FROM="${HOME}/.cache/audreybrick/device-build"
OUT="$REPO_ROOT/dist"
BUILD=0

usage() { echo "usage: scripts/package_port.sh [--build] [--from <device-build-dir>] [--out <dir>]" >&2; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --build)      BUILD=1 ;;
        --skip-build) BUILD=0 ;;
        --from)       [ $# -ge 2 ] || usage; FROM="$2"; shift ;;
        --out)        [ $# -ge 2 ] || usage; OUT="$2"; shift ;;
        *) usage ;;
    esac
    shift
done

req="$OUT"
OUT="$(ab_resolve_path "$req")" || { echo "$AB_SCRIPT: cannot resolve --out '$req'; refused" >&2; exit 2; }
if [ "$OUT" != "$REPO_ROOT/dist" ]; then
    ab_check_outside_repo "--out '$req'" "$OUT"
fi
ab_check_reusable "--out '$req'" "$OUT" "$MARKER"
FROM="$(ab_resolve_path "$FROM")" || { echo "$AB_SCRIPT: cannot resolve --from; refused" >&2; exit 2; }

if [ "$BUILD" -eq 1 ]; then
    "$REPO_ROOT/scripts/build_device.sh" --out "$FROM"
fi
for f in "$FROM/port/audreybrick" "$FROM/port/libs.aarch64/libaudrey_lgpl.so" "$FROM/VERSION" "$FROM/SHA256SUMS"; do
    [ -f "$f" ] || { echo "$AB_SCRIPT: $f is missing; run scripts/build_device.sh first" >&2; exit 1; }
done
# The binaries must be the ones build_device.sh built and tested.
(cd "$FROM/port" && shasum -a 256 -c "$FROM/SHA256SUMS" > /dev/null 2>&1) || {
    echo "$AB_SCRIPT: the binaries in $FROM/port differ from the hashes build_device.sh recorded" >&2
    echo "  ($FROM/SHA256SUMS); refused. Run scripts/build_device.sh again." >&2
    exit 1
}
VERSION="$(head -n 1 "$FROM/VERSION" | tr -c 'A-Za-z0-9._+-\n' '_')"
[ -n "$VERSION" ] || VERSION=unknown

mkdir -p "$OUT"
printf 'Output directory of scripts/package_port.sh (AudreyBrick M3-A).\n' > "$OUT/$MARKER"
rm -rf "${OUT:?}/AudreyBrick"
rm -f "${OUT:?}"/AudreyBrick-*.zip "${OUT:?}"/AudreyBrick-*.sha256

PKG="$OUT/AudreyBrick"
APP="$PKG/audreybrick"
mkdir -p "$APP/libs.aarch64" "$APP/licenses"
cp "$REPO_ROOT/packaging/AudreyBrick.sh" "$PKG/AudreyBrick.sh"
cp "$FROM/port/audreybrick" "$APP/audreybrick"
cp "$FROM/port/libs.aarch64/libaudrey_lgpl.so" "$APP/libs.aarch64/libaudrey_lgpl.so"
cp "$REPO_ROOT"/licenses/* "$APP/licenses/"
cp "$REPO_ROOT/third_party/audrey-ii/CREDITS.md" "$APP/licenses/audrey-ii.CREDITS.md"
cp "$REPO_ROOT/THIRD_PARTY.md" "$REPO_ROOT/LICENSE" "$APP/"
cp "$REPO_ROOT/packaging/README.txt" "$APP/README.txt"
chmod 755 "$PKG/AudreyBrick.sh" "$APP/audreybrick" "$APP/libs.aarch64/libaudrey_lgpl.so"
chmod 644 "$APP"/licenses/* "$APP/THIRD_PARTY.md" "$APP/LICENSE" "$APP/README.txt"

# Every licence text of the repository is in the package.
missing=0
for f in "$REPO_ROOT"/licenses/*; do
    cmp -s "$f" "$APP/licenses/$(basename "$f")" || { echo "$AB_SCRIPT: licence $(basename "$f") missing or different" >&2; missing=1; }
done
[ "$missing" -eq 0 ] || exit 1

# PRESET-C: no data folder in the package (the user's data lives outside it).
[ ! -e "$APP/data" ] || { echo "$AB_SCRIPT: the package holds audreybrick/data; refused" >&2; exit 1; }

# The packaged copies must still match the recorded hashes.
(cd "$APP" && shasum -a 256 -c "$FROM/SHA256SUMS" > /dev/null 2>&1) \
    || { echo "$AB_SCRIPT: the packaged binaries differ from the recorded hashes; refused" >&2; exit 1; }

ZIP="$OUT/AudreyBrick-$VERSION.zip"
(cd "$OUT" && zip -q -r -X "$(basename "$ZIP")" AudreyBrick)
SUMS="$OUT/AudreyBrick-$VERSION.sha256"
{
    (cd "$OUT" && shasum -a 256 AudreyBrick/audreybrick/audreybrick AudreyBrick/audreybrick/libs.aarch64/libaudrey_lgpl.so)
    (cd "$OUT" && shasum -a 256 "$(basename "$ZIP")")
} > "$SUMS"

echo "$AB_SCRIPT: version $VERSION"
echo "$AB_SCRIPT: package $PKG"
(cd "$OUT" && find AudreyBrick | LC_ALL=C sort | while IFS= read -r p; do
    if [ -d "$p" ]; then printf '  %-60s dir\n' "$p/"; else printf '  %-60s %8s bytes  %s\n' "$p" "$(wc -c < "$p" | tr -d ' ')" "$(ls -l "$p" | cut -c1-10)"; fi
done)
echo "$AB_SCRIPT: zip $ZIP ($(wc -c < "$ZIP" | tr -d ' ') bytes)"
echo "$AB_SCRIPT: SHA-256 (also in $SUMS); the binaries match the device build's recorded hashes:"
sed 's/^/  /' "$SUMS"
echo "$AB_SCRIPT: every licence text in licenses/ is in the package"
echo "$AB_SCRIPT: OK"
