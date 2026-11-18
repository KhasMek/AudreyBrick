#!/usr/bin/env bash
# Re-fetch the pinned upstream commits into a temporary directory and compare
# every vendored file byte for byte. Also fails if anything under third_party/
# or licenses/ (files, symlinks, anything that is not a directory) is neither in
# the manifest nor one of our own notes listed in OWN_NOTES.
#
# Exit 0 only if every file is identical and nothing unexpected is present.
#
# Usage: scripts/verify_vendor.sh
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

AUDREY_URL="https://github.com/Synthux-Academy/Audrey-II"
AUDREY_SHA="e5ab79910eb7ccbac5bbc865fae5459d65f5b5e3"
DAISYSP_URL="https://github.com/electro-smith/DaisySP"
DAISYSP_SHA="7f67768eb22103e5f7743e709e090ac435a11fae"
ARCHIVE_URL="https://github.com/Synthux-Academy/simple-designer-instruments"
ARCHIVE_SHA="b16423ea875afc7bb3d8c4282e730127f3808d3f"
LGPL_URL="https://github.com/electro-smith/DaisySP-LGPL"
LGPL_SHA="c89d380c62620d6fee9105c5035e081c9b7ea843"
# Upstream-of-upstream licence notices (licences/ only; no code is vendored from these)
EURORACK_URL="https://github.com/pichenettes/eurorack"
EURORACK_SHA="f4cfa7697095a28d292bff758beb51deb49b5a86"
STMLIB_URL="https://github.com/pichenettes/stmlib"
STMLIB_SHA="d18def816c51d1da0c108236928b2bbd25c17481"
SOUNDPIPE_URL="https://github.com/PaulBatchelor/Soundpipe"
SOUNDPIPE_SHA="3efb43bdabd0ed23b17c694292b5a79f1692a3ea"
# dr_wav (tools and tests only), v0.14.6
DRLIBS_URL="https://github.com/mackron/dr_libs"
DRLIBS_SHA="35b5c392cc15456736c440a68dbe073178bbffe0"
# font8x8 (the app's bitmap font, public domain)
FONT8X8_URL="https://github.com/dhepper/font8x8"
FONT8X8_SHA="8e279d2d864e79128e96188a6b9526cfa3fbfef9"

# Manifest: "<repo-key> <path in upstream> <path in this repo>".
# "<path>#L<a>-<b>" compares only lines a to b of the upstream file (used for a
# licence notice that exists only as a file header).
MANIFEST="
audrey Source/BiquadFilters.cpp        third_party/audrey-ii/Source/BiquadFilters.cpp
audrey Source/BiquadFilters.h          third_party/audrey-ii/Source/BiquadFilters.h
audrey Source/DSPUtils.h               third_party/audrey-ii/Source/DSPUtils.h
audrey Source/EchoDelay.h              third_party/audrey-ii/Source/EchoDelay.h
audrey Source/FeedbackSynthEngine.cpp  third_party/audrey-ii/Source/FeedbackSynthEngine.cpp
audrey Source/FeedbackSynthEngine.h    third_party/audrey-ii/Source/FeedbackSynthEngine.h
audrey Source/KarplusString.cpp        third_party/audrey-ii/Source/KarplusString.cpp
audrey Source/KarplusString.h          third_party/audrey-ii/Source/KarplusString.h
audrey Source/ParameterRegistry.h      third_party/audrey-ii/Source/ParameterRegistry.h
audrey Source/SmoothedValue.h          third_party/audrey-ii/Source/SmoothedValue.h
audrey LICENSE                         third_party/audrey-ii/LICENSE
audrey LICENSE                         licenses/audrey-ii.LICENSE
audrey CREDITS.md                      third_party/audrey-ii/CREDITS.md
archive official/audrey-ii/LICENSE     third_party/audrey-ii/LICENSE.archive-2022
archive official/audrey-ii/LICENSE     licenses/audrey-ii.LICENSE.archive-2022
daisysp Source/Utility/dsp.h           third_party/daisysp/Source/Utility/dsp.h
daisysp Source/Utility/delayline.h     third_party/daisysp/Source/Utility/delayline.h
daisysp Source/Utility/dcblock.h       third_party/daisysp/Source/Utility/dcblock.h
daisysp Source/Utility/dcblock.cpp     third_party/daisysp/Source/Utility/dcblock.cpp
daisysp Source/Dynamics/crossfade.h    third_party/daisysp/Source/Dynamics/crossfade.h
daisysp Source/Dynamics/crossfade.cpp  third_party/daisysp/Source/Dynamics/crossfade.cpp
daisysp Source/Dynamics/limiter.h      third_party/daisysp/Source/Dynamics/limiter.h
daisysp Source/Dynamics/limiter.cpp    third_party/daisysp/Source/Dynamics/limiter.cpp
daisysp Source/Effects/overdrive.h     third_party/daisysp/Source/Effects/overdrive.h
daisysp Source/Effects/overdrive.cpp   third_party/daisysp/Source/Effects/overdrive.cpp
daisysp Source/Noise/whitenoise.h      third_party/daisysp/Source/Noise/whitenoise.h
daisysp LICENSE                        third_party/daisysp/LICENSE
daisysp LICENSE                        licenses/daisysp.LICENSE
daisysp Source/Effects/reverbsc.h      third_party/daisysp-lgpl/Source/Effects/reverbsc.h
daisysp Source/Effects/reverbsc.cpp    third_party/daisysp-lgpl/Source/Effects/reverbsc.cpp
daisysp Source/Filters/tone.h          third_party/daisysp-lgpl/Source/Filters/tone.h
daisysp Source/Filters/tone.cpp        third_party/daisysp-lgpl/Source/Filters/tone.cpp
daisysp LICENSE                        third_party/daisysp-lgpl/LICENSE.daisysp-mit-7f67768
lgpl    LICENSE                        third_party/daisysp-lgpl/LICENSE
lgpl    LICENSE                        licenses/daisysp-lgpl.LICENSE
stmlib  LICENSE                        licenses/stmlib.LICENSE
eurorack plaits/dsp/fx/overdrive.h#L1-23 licenses/plaits.NOTICE
soundpipe LICENSE                      licenses/soundpipe.LICENSE
drlibs  dr_wav.h                       third_party/dr_libs/dr_wav.h
drlibs  dr_wav.h#L8658-8704            licenses/dr_wav.LICENSE
font8x8 font8x8_basic.h                third_party/font8x8/font8x8_basic.h
font8x8 README                         third_party/font8x8/README
font8x8 README                         licenses/font8x8.README
"

# Our own notes, allowed under third_party/ and licenses/ without an upstream.
OWN_NOTES="
third_party/audrey-ii/UPSTREAM.md
third_party/daisysp/UPSTREAM.md
third_party/daisysp-lgpl/PROVENANCE.md
third_party/dr_libs/UPSTREAM.md
third_party/font8x8/UPSTREAM.md
"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/audreybrick-verify.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT

fetch() {
    local key="$1" url="$2" sha="$3"
    local dir="$WORK/$key"
    git init -q "$dir"
    git -C "$dir" remote add origin "$url"
    if ! git -C "$dir" fetch -q --depth 1 origin "$sha" 2>/dev/null; then
        # Fallback for servers that refuse fetching a commit by SHA.
        git -C "$dir" fetch -q origin
    fi
    git -C "$dir" -c advice.detachedHead=false checkout -q "$sha"
    local got
    got="$(git -C "$dir" rev-parse HEAD)"
    if [ "$got" != "$sha" ]; then
        echo "ERROR: $url checked out $got, expected $sha" >&2
        exit 2
    fi
    echo "fetched $url @ $sha"
}

fetch audrey  "$AUDREY_URL"  "$AUDREY_SHA"
fetch daisysp "$DAISYSP_URL" "$DAISYSP_SHA"
fetch archive "$ARCHIVE_URL" "$ARCHIVE_SHA"
fetch lgpl    "$LGPL_URL"    "$LGPL_SHA"
fetch eurorack  "$EURORACK_URL"  "$EURORACK_SHA"
fetch stmlib    "$STMLIB_URL"    "$STMLIB_SHA"
fetch soundpipe "$SOUNDPIPE_URL" "$SOUNDPIPE_SHA"
fetch drlibs    "$DRLIBS_URL"    "$DRLIBS_SHA"
fetch font8x8   "$FONT8X8_URL"   "$FONT8X8_SHA"

status=0
checked=0
expected_list="$WORK/expected.txt"
: > "$expected_list"

while read -r key upstream local_path; do
    [ -z "${key:-}" ] && continue
    checked=$((checked + 1))
    echo "$local_path" >> "$expected_list"
    upstream_file="${upstream%%#*}"
    range=""
    if [ "$upstream_file" != "$upstream" ]; then
        range="${upstream#*#L}"            # e.g. 1-23
    fi
    src="$WORK/$key/$upstream_file"
    dst="$REPO_ROOT/$local_path"
    if [ -L "$dst" ] || [ ! -f "$dst" ]; then
        echo "MISSING   $local_path  (absent, or not a regular file)"
        status=1
    elif [ ! -f "$src" ]; then
        echo "NO-UPSTREAM $key:$upstream"
        status=1
    else
        if [ -n "$range" ]; then
            sed -n "${range%-*},${range#*-}p" "$src" > "$WORK/excerpt"
            src="$WORK/excerpt"
        fi
        if cmp -s "$src" "$dst"; then
            echo "OK        $local_path"
        else
            echo "DIFFERS   $local_path  (upstream $key:$upstream)"
            status=1
        fi
    fi
done <<< "$MANIFEST"

while read -r note; do
    [ -n "$note" ] && echo "$note" >> "$expected_list"
done <<< "$OWN_NOTES"

# Nothing unexpected under third_party/ or licenses/: every entry that is not a
# directory (regular files, symlinks, anything else) must be listed above.
while IFS= read -r f; do
    rel="${f#"$REPO_ROOT"/}"
    if ! grep -qxF "$rel" "$expected_list"; then
        echo "UNEXPECTED $rel"
        status=1
    fi
done < <(find "$REPO_ROOT/third_party" "$REPO_ROOT/licenses" ! -type d | sort)

if [ "$status" -eq 0 ]; then
    echo "verify_vendor: all $checked vendored files identical to pinned upstream"
else
    echo "verify_vendor: FAILED"
fi
exit "$status"
