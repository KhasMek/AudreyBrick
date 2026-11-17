#!/usr/bin/env bash
# AudreyBrick: steps that run inside the Linux aarch64 test container (task M1-D).
# Called by scripts/build_linux_arm64.sh; not meant to be run by hand.
#
#   in_container.sh release    configure + build (Release, gcc), ctest, render every
#                              script with scripts/make_reference.sh, check the hashes
#                              against reference/SHA256SUMS (report only), and compare
#                              every render with the macOS render of the same name
#                              With AB_LIBM_PROBE=1, also record the libm arguments
#                              of every render (docker/libm_log.c) and evaluate them
#                              with glibc (docker/libm_probe.cpp) into /out/libm/
#   in_container.sh sanitize   configure + build (Debug, ASan + UBSan, LeakSanitizer is
#                              part of ASan on Linux), ctest
#
# Mounts (set up by the caller):
#   /src   the repository, READ-ONLY. Nothing is ever written there.
#   /out   a host directory outside the repository: logs/, renders/, diff_profiles/
# All builds happen in /tmp/work, which lives only as long as the container (--rm).
#
# Exit status: non-zero if configure, build or ctest fails, if a render fails or
# fails wavcheck --script, or if a render holds NaN/inf. A difference between the
# Linux and the macOS renders is reported and is not a failure.
set -euo pipefail

SRC=/src
OUT=/out
WORK=/tmp/work
MODE="${1:-}"
mkdir -p "$WORK" "$OUT/logs"

section() { printf '\n==== %s ====\n' "$*"; }

section "Container environment ($MODE)"
echo "uname -m: $(uname -m)"
if [ "$(uname -m)" != "aarch64" ]; then
    echo "error: uname -m is $(uname -m), expected aarch64" >&2
    exit 1
fi
echo "Debian:   $(cat /etc/debian_version)"
echo "gcc:      $(gcc --version | head -n1)"
echo "g++:      $(g++ --version | head -n1)"
echo "glibc:    $(ldd --version | head -n1)"
echo "cmake:    $(cmake --version | head -n1)"
echo "ninja:    $(ninja --version)"
echo "nproc:    $(nproc)"
echo "shasum:   $(command -v shasum || echo 'not installed')"
echo "sha256sum: $(command -v sha256sum || echo 'not installed')"
echo "locales:  $(locale -a | tr '\n' ' ')"

# Configure and build one tree; print every compiler warning from the build log.
configure_and_build() { # <name> <cmake args...>
    local name="$1"; shift
    local dir="$WORK/$name"
    section "Configure $name"
    local rc=0
    cmake -S "$SRC" -B "$dir" -G Ninja "$@" > "$OUT/logs/configure-$name.log" 2>&1 || rc=$?
    tail -n 4 "$OUT/logs/configure-$name.log"
    if [ "$rc" -ne 0 ]; then
        cat "$OUT/logs/configure-$name.log"
        echo "error: configure $name failed (exit $rc)" >&2
        exit 1
    fi
    section "Build $name (full log: logs/build-$name.log)"
    cmake --build "$dir" -j "$(nproc)" > "$OUT/logs/build-$name.log" 2>&1 || rc=$?
    grep -E '^\[[0-9]+/[0-9]+\] Linking' "$OUT/logs/build-$name.log" || true
    tail -n 1 "$OUT/logs/build-$name.log"
    local warnings
    warnings="$(grep -E '(warning|error):' "$OUT/logs/build-$name.log" || true)"
    if [ -n "$warnings" ]; then
        echo "Compiler diagnostics in the $name build:"
        printf '%s\n' "$warnings"
    else
        echo "Compiler warnings in the $name build: none"
    fi
    if [ "$rc" -ne 0 ]; then
        tail -n 60 "$OUT/logs/build-$name.log"
        echo "error: build $name failed (exit $rc)" >&2
        exit 1
    fi
}

run_ctest() { # <name>
    local name="$1"
    section "ctest $name (full test output: logs/ctest-$name.log)"
    local rc=0
    # -V keeps every test's own output in the log (passing tests included).
    ctest --test-dir "$WORK/$name" -V > "$OUT/logs/ctest-$name.log" 2>&1 || rc=$?
    grep -E '^ *[0-9]+/[0-9]+ Test +#|tests passed|tests failed|Total Test time|^The following tests FAILED|^[[:space:]]+[0-9]+ - ' \
        "$OUT/logs/ctest-$name.log" || true
    echo "-- selected lines from the test output (locale test, allocation test, sanitizer reports):"
    grep -E '\[SKIP\]|LC_NUMERIC|operator new|allocat|LeakSanitizer|AddressSanitizer|runtime error|ERROR:' \
        "$OUT/logs/ctest-$name.log" | sed 's/^/   /' || true
    if [ "$rc" -ne 0 ]; then
        echo "-- ctest $name failed (exit $rc); last 80 lines of the log:"
        tail -n 80 "$OUT/logs/ctest-$name.log"
    fi
    return "$rc"
}

case "$MODE" in
release)
    configure_and_build release -DCMAKE_BUILD_TYPE=Release
    status=0
    run_ctest release || status=1   # keep going: the renders are still informative

    section "Render every script on Linux (scripts/make_reference.sh)"
    start="$(date +%s.%N)"
    "$SRC/scripts/make_reference.sh" --build "$WORK/release" --out "$OUT/renders" > "$OUT/logs/make_reference.log" 2>&1 \
        || { echo "error: make_reference.sh failed (wavcheck or render failure)" >&2; status=1; }
    cat "$OUT/logs/make_reference.log"
    end="$(date +%s.%N)"
    echo "wall time: $(awk -v a="$start" -v b="$end" 'BEGIN{printf "%.1f", b-a}') s (renders and wavcheck, one core each, sequential)"
    printf '%s\n' "$start $end" > "$OUT/logs/render_time.txt"

    section "Render speed (audrey-render alone, no wavcheck, one core)"
    for n in feedback_rise all_max sweep_reverb_decay; do
        t0="$(date +%s.%N)"
        "$WORK/release/audrey-render" --inputs-dir "$OUT/renders/inputs" "$SRC/scripts/$n.txt" "$WORK/speed.wav" > /dev/null
        t1="$(date +%s.%N)"
        secs="$(grep -m1 '^# @duration' "$SRC/scripts/$n.txt" | awk '{print $3}')"
        awk -v n="$n" -v d="$secs" -v a="$t0" -v b="$t1" 'BEGIN{printf "   %-20s %5.1f s of audio in %.3f s: %.0fx real time\n", n, d, b-a, d/(b-a)}'
    done

    section "Hashes against reference/SHA256SUMS (macOS; expected to differ, report only)"
    (cd "$OUT/renders" && sha256sum -c "$SRC/reference/SHA256SUMS" 2>/dev/null) > "$OUT/logs/sha256-check.log" || true
    echo "matching: $(grep -c ': OK$' "$OUT/logs/sha256-check.log" || true) of $(wc -l < "$SRC/reference/SHA256SUMS")"
    grep ': OK$' "$OUT/logs/sha256-check.log" | sed 's/^/   same bits: /' || true

    section "Linux render versus macOS render, per file"
    g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -o "$WORK/wavdiff" "$SRC/docker/wavdiff.cpp"
    mkdir -p "$OUT/diff_profiles"
    "$WORK/wavdiff" --header | tee "$OUT/diff_table.txt"
    shopt -s nullglob
    # Every Linux render needs a macOS reference of the same name and vice versa.
    for ref in "$SRC"/reference/inputs/*.wav "$SRC"/reference/*.wav; do
        rel="${ref#"$SRC"/reference/}"
        if [ ! -f "$OUT/renders/$rel" ]; then
            echo "error: macOS reference $rel has no Linux render" >&2
            status=1
        fi
    done
    for wav in "$OUT"/renders/inputs/*.wav "$OUT"/renders/*.wav; do
        rel="${wav#"$OUT"/renders/}"
        name="${rel%.wav}"
        ref="$SRC/reference/$rel"
        if [ ! -f "$ref" ]; then
            echo "error: Linux render $rel has no macOS reference" >&2
            status=1
            continue
        fi
        prof="$OUT/diff_profiles/${name//\//_}.txt"
        "$WORK/wavdiff" "$name" "$wav" "$ref" --profile "$prof" | tee -a "$OUT/diff_table.txt" || status=1
    done
    total_frames="$(awk 'NR>1 && $1 !~ /^inputs\// {s+=$2} END{print s}' "$OUT/diff_table.txt")"
    echo "rendered audio: $(awk -v f="$total_frames" 'BEGIN{printf "%.0f", f/48000}') s;" \
         "speed: $(awk -v f="$total_frames" -v a="$start" -v b="$end" 'BEGIN{printf "%.0f", f/48000/(b-a)}')x real time" \
         "(includes wavcheck time)"
    if [ "${AB_LIBM_PROBE:-0}" = "1" ]; then
        section "libm probe: record the arguments of every libm call, evaluate them with glibc"
        echo "libm functions imported by the Linux DSP build (nm -D -u):"
        for f in "$WORK/release/audrey-render" "$WORK/release/libaudrey_lgpl.so"; do
            printf '   %-18s %s\n' "$(basename "$f"):" "$(nm -D -u "$f" | awk '{print $NF}' | sed 's/@.*//' \
                | grep -xE '(a?(sin|cos|tan)h?|exp|exp2|exp10|expm1|log|log10|log2|pow|sqrt|sincos)f?' | tr '\n' ' ')"
        done
        gcc -O2 -shared -fPIC -o "$WORK/libm_log.so" "$SRC/docker/libm_log.c" -ldl
        g++ -std=c++17 -O2 -fno-builtin -o "$WORK/libm_probe" "$SRC/docker/libm_probe.cpp"
        mkdir -p "$OUT/libm"
        for script in "$SRC"/scripts/*.txt; do
            name="$(basename "$script" .txt)"
            LD_PRELOAD="$WORK/libm_log.so" LIBM_LOG="$OUT/libm/args_$name.txt" LIBM_LOG_STATS="$OUT/libm/stats_$name.txt" \
                "$WORK/release/audrey-render" --inputs-dir "$OUT/renders/inputs" "$script" "$WORK/probe.wav" > /dev/null
            # The shim must not change the render.
            cmp -s "$WORK/probe.wav" "$OUT/renders/$name.wav" || { echo "error: libm shim changed $name" >&2; status=1; }
            sort -o "$OUT/libm/args_$name.txt" "$OUT/libm/args_$name.txt"
        done
        LC_ALL=C sort -u "$OUT"/libm/args_*.txt > "$OUT/libm/all_args.txt"
        "$WORK/libm_probe" < "$OUT/libm/all_args.txt" > "$OUT/libm/results_linux.txt"
        echo "distinct (function, argument) pairs over all renders, by function:"
        awk '{print $1}' "$OUT/libm/all_args.txt" | sort | uniq -c | sed 's/^/   /'
        echo "calls per function, feedback_rise (for scale):"
        sed 's/^/   /' "$OUT/libm/stats_feedback_rise.txt"
    fi

    [ "$status" -eq 0 ] && echo "release: OK" || echo "release: FAILED"
    exit "$status"
    ;;
sanitize)
    configure_and_build asan -DCMAKE_BUILD_TYPE=Debug \
        "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize=signed-integer-overflow"
    echo "$(grep '^CMAKE_CXX_FLAGS:' "$WORK/asan/CMakeCache.txt")"

    # Canary: prove that ASan, LeakSanitizer and UBSan really report in this container
    # (LeakSanitizer stops the world with ptrace, which some container setups forbid).
    section "Sanitizer canaries (each must be detected)"
    cat > "$WORK/canary.cpp" <<'CANARY'
#include <cstdio>
#include <cstdlib>
#include <cstring>
int main(int argc, char** argv)
{
    if(argc < 2) return 2;
    if(std::strcmp(argv[1], "leak") == 0) { void* volatile p = std::malloc(77); p = nullptr; (void)p; return 0; }
    if(std::strcmp(argv[1], "overflow") == 0) { char* volatile p = static_cast<char*>(std::malloc(8)); p[8] = 1; std::free(p); return 0; }
    if(std::strcmp(argv[1], "ub") == 0) { volatile int s = 40; return (1 << (s + argc)) == 0; }
    return 2;
}
CANARY
    g++ -std=c++17 -g -O0 -fsanitize=address,undefined -o "$WORK/canary" "$WORK/canary.cpp"
    canary_ok=1
    for kind in leak:LeakSanitizer overflow:AddressSanitizer ub:"runtime error"; do
        arg="${kind%%:*}"; want="${kind#*:}"
        out="$(UBSAN_OPTIONS=halt_on_error=1 "$WORK/canary" "$arg" 2>&1)" && crc=0 || crc=$?
        if [ "$crc" -ne 0 ] && printf '%s' "$out" | grep -q "$want"; then
            echo "   $arg: detected ($want, exit $crc)"
        else
            echo "   $arg: NOT detected (exit $crc):"; printf '%s\n' "$out" | sed 's/^/      /'
            canary_ok=0
        fi
    done
    [ "$canary_ok" -eq 1 ] || { echo "sanitize: FAILED (sanitizers do not report in this container)"; exit 1; }
    run_ctest asan && echo "sanitize: OK" || { echo "sanitize: FAILED"; exit 1; }
    ;;
*)
    echo "usage: in_container.sh release|sanitize" >&2
    exit 2
    ;;
esac
