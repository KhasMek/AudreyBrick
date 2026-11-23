#!/usr/bin/env bash
# Regenerate the reference set from every parameter script in scripts/.
#
#   scripts/make_reference.sh [--set mod] [--check] [--out <dir>] [--build <dir>]
#
#   (no options)    render into reference/, run wavcheck, rewrite reference/SHA256SUMS
#   --check         render and run wavcheck, then compare the hashes with
#                   reference/SHA256SUMS instead of writing any SHA256SUMS
#   --out <dir>     render into <dir> instead of reference/ (created if missing).
#                   Without --check, the hashes go to <dir>/SHA256SUMS. reference/
#                   is never written: with --check, reference/SHA256SUMS is only read.
#                   This is how a read-only checkout (M1-D, Linux) renders the set.
#   --set mod       MOD-A: the modulation set instead of the reference set: the
#                   scripts in scripts/mod/, rendered into reference/mod/ (or --out),
#                   hashes in reference/mod/SHA256SUMS. The reference set (the 25
#                   files of reference/SHA256SUMS) never includes reference/mod/.
#   --build <dir>   build directory with audrey-render, wavcheck and audrey-gen-input
#                   (default $BUILD_DIR, else build/host). It must already be
#                   configured; the three tools are (re)built in it.
#
# Steps:
#   1. Build the tools.
#   2. audrey-gen-input writes the input WAVs to <out>/inputs/.
#   3. Each scripts/<name>.txt is rendered to <out>/<name>.wav by audrey-render, which
#      takes the duration and the input file from the script's own header
#      ("# @duration", "# @input", resolved in <out>/inputs; src/tools/ScriptMeta.h).
#   4. wavcheck --script runs on every render; it takes the thresholds (@jump, @click,
#      @click-near) and the event times from the script. This script parses nothing.
#   5. SHA-256 of every WAV (inputs and renders), sorted, paths relative to <out>.
#      Uses shasum -a 256, or sha256sum where shasum is not installed (same format).
#
# Exit 0 only if every render succeeded and passed wavcheck (and, with --check,
# every hash matches). The WAVs are not committed (reference/.gitignore); see
# reference/README.md.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
REF="$REPO_ROOT/reference"
SCRIPTS="$REPO_ROOT/scripts"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build/host}"
OUT=""
MODE="write"
SET="reference"

usage() { echo "usage: scripts/make_reference.sh [--set mod] [--check] [--out <dir>] [--build <dir>]" >&2; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --check) MODE="check" ;;
        --set)   [ $# -ge 2 ] && [ "$2" = "mod" ] || usage; SET="mod"; shift ;;
        --out)   [ $# -ge 2 ] || usage; OUT="$2"; shift ;;
        --build) [ $# -ge 2 ] || usage; BUILD_DIR="$2"; shift ;;
        *) usage ;;
    esac
    shift
done
if [ "$SET" = "mod" ]; then
    REF="$REPO_ROOT/reference/mod"
    SCRIPTS="$REPO_ROOT/scripts/mod"
fi
[ -n "$OUT" ] || OUT="$REF"

if command -v shasum >/dev/null 2>&1; then
    sha256() { shasum -a 256 "$@"; }
elif command -v sha256sum >/dev/null 2>&1; then
    sha256() { sha256sum "$@"; }
else
    echo "make_reference: neither shasum nor sha256sum is installed" >&2
    exit 1
fi

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "make_reference: $BUILD_DIR is not configured. Run, for example:" >&2
    echo "  cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release" >&2
    exit 1
fi
cmake --build "$BUILD_DIR" -j --target audrey-render wavcheck audrey-gen-input >/dev/null
BUILD_DIR="$(cd "$BUILD_DIR" && pwd -P)"
RENDER="$BUILD_DIR/audrey-render"
WAVCHECK="$BUILD_DIR/wavcheck"

mkdir -p "$OUT/inputs"
OUT="$(cd "$OUT" && pwd -P)"
IN_REPO_REF=0
[ "$OUT" = "$REF" ] && IN_REPO_REF=1

echo "make_reference: output $OUT"
echo "make_reference: build $BUILD_DIR ($(grep '^CMAKE_CXX_COMPILER:' "$BUILD_DIR/CMakeCache.txt" | cut -d= -f2-), $(grep '^CMAKE_BUILD_TYPE:' "$BUILD_DIR/CMakeCache.txt" | cut -d= -f2-))"

"$BUILD_DIR/audrey-gen-input" "$OUT/inputs"

status=0
for script in "$SCRIPTS"/*.txt; do
    name="$(basename "$script" .txt)"
    if ! "$RENDER" --inputs-dir "$OUT/inputs" "$script" "$OUT/$name.wav"; then
        status=1
        continue
    fi
    if ! (cd "$OUT" && "$WAVCHECK" "$name.wav" --script "$script"); then
        status=1
    fi
done

# The reference set leaves out reference/mod/ (the modulation set, MOD-A).
sums="$(cd "$OUT" && find . -path ./mod -prune -o -name '*.wav' -print | sed 's|^\./||' | LC_ALL=C sort | while IFS= read -r f; do sha256 "$f"; done)"
if [ "$MODE" = "write" ]; then
    target="$OUT/SHA256SUMS" # reference/SHA256SUMS only when OUT is reference/
    if [ -f "$target" ] && [ "$(cat "$target")" = "$sums" ]; then
        echo "make_reference: $target unchanged"
    else
        echo "make_reference: $target written (changed or new)"
    fi
    printf '%s\n' "$sums" > "$target"
else
    if [ "$IN_REPO_REF" -eq 0 ]; then
        echo "make_reference: comparing $OUT with $REF/SHA256SUMS (read only)"
    fi
    if (cd "$OUT" && sha256 -c "$REF/SHA256SUMS"); then
        echo "make_reference: every hash matches $REF/SHA256SUMS"
    else
        status=1
    fi
fi

total="$(cd "$OUT" && find . -path ./mod -prune -o -name '*.wav' -print0 | xargs -0 cat | wc -c | tr -d ' ')"
echo "make_reference: $(cd "$OUT" && find . -path ./mod -prune -o -name '*.wav' -print | wc -l | tr -d ' ') WAV files, $total bytes in total ($SET set)"
if [ "$status" -eq 0 ]; then echo "make_reference: OK"; else echo "make_reference: FAILED"; fi
exit "$status"
