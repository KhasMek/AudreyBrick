#!/usr/bin/env bash
# Copyright (c) 2026 KhasMek
# SPDX-License-Identifier: MIT
#
# AudreyBrick: steps that run inside the device build container (task M3-A).
# Called by scripts/build_device.sh; not meant to be run by hand.
#
#   device_in_container.sh build   configure (Release, gcc 12, AUDREY_PORT_INSTALL=ON,
#                                  the version and date from AB_VERSION/AB_BUILD_DATE),
#                                  build, ctest (every test; the SDL tests set the
#                                  dummy drivers themselves), install into a staging
#                                  directory and copy the binaries to /out/port/
#   device_in_container.sh check   check /out/port/audreybrick and
#                                  /out/port/libs.aarch64/libaudrey_lgpl.so: highest
#                                  needed GLIBC_, GLIBCXX_, CXXABI_ versions against
#                                  AB_MAX_GLIBC / AB_MAX_GLIBCXX / AB_MAX_CXXABI, the
#                                  NEEDED list against the libraries the device has,
#                                  the executable's RUNPATH
#
# Mounts: /src the repository, READ-ONLY; /out the output directory (outside the
# repository). Builds happen in /tmp/work, which vanishes with the container.
set -euo pipefail

SRC=/src
OUT=/out
WORK=/tmp/work
MODE="${1:-}"

section() { printf '\n==== %s ====\n' "$*"; }

# Libraries the binaries may need: all present in the device image
# (docs/PLATFORM_NOTES.md section 10), plus our own LGPL library.
ALLOWED_NEEDED="libSDL2-2.0.so.0 libaudrey_lgpl.so libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6 ld-linux-aarch64.so.1"

# 0 if version $1 <= version $2 (dotted numbers).
version_le() {
    [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -n 1)" = "$2" ]
}

case "$MODE" in
build)
    mkdir -p "$WORK" "$OUT/logs" "$OUT/port"
    section "Container environment"
    echo "uname -m: $(uname -m)"
    [ "$(uname -m)" = "aarch64" ] || { echo "error: expected aarch64" >&2; exit 1; }
    echo "Debian:   $(cat /etc/debian_version)"
    echo "g++:      $(g++ --version | head -n1)"
    echo "glibc:    $(ldd --version | head -n1)"
    echo "cmake:    $(cmake --version | head -n1)"
    echo "ninja:    $(ninja --version)"
    echo "SDL2:     $(dpkg-query -W -f='${Version}' libsdl2-dev) (libsdl2-dev; headers only matter, SDL is not shipped)"
    echo "binutils: $(readelf --version | head -n1)"

    section "Configure (Release; version ${AB_VERSION:-unknown})"
    rc=0
    cmake -S "$SRC" -B "$WORK/device" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        "-DAUDREY_VERSION=${AB_VERSION:-unknown}" "-DAUDREY_BUILD_DATE=${AB_BUILD_DATE:-}" \
        -DAUDREY_PORT_INSTALL=ON > "$OUT/logs/configure.log" 2>&1 || rc=$?
    grep -E "SDL2 found|AUDREY" "$OUT/logs/configure.log" || true
    tail -n 3 "$OUT/logs/configure.log"
    [ "$rc" -eq 0 ] || { cat "$OUT/logs/configure.log"; echo "error: configure failed" >&2; exit 1; }

    section "Build (full log: logs/build.log)"
    cmake --build "$WORK/device" -j "$(nproc)" > "$OUT/logs/build.log" 2>&1 || rc=$?
    tail -n 1 "$OUT/logs/build.log"
    warnings="$(grep -E '(warning|error):' "$OUT/logs/build.log" || true)"
    if [ -n "$warnings" ]; then echo "Compiler diagnostics:"; printf '%s\n' "$warnings"; else echo "Compiler warnings: none"; fi
    [ "$rc" -eq 0 ] || { tail -n 60 "$OUT/logs/build.log"; echo "error: build failed" >&2; exit 1; }

    section "ctest (full output: logs/ctest.log)"
    ctest --test-dir "$WORK/device" -V > "$OUT/logs/ctest.log" 2>&1 || rc=$?
    grep -E '^ *[0-9]+/[0-9]+ Test +#|tests passed|tests failed|Total Test time|^The following tests FAILED|^[[:space:]]+[0-9]+ - ' \
        "$OUT/logs/ctest.log" || true
    if [ "$rc" -ne 0 ]; then tail -n 80 "$OUT/logs/ctest.log"; echo "error: tests failed" >&2; exit 1; fi

    section "Install (staging) and copy out"
    cmake --install "$WORK/device" --prefix "$WORK/stage" > "$OUT/logs/install.log" 2>&1 \
        || { cat "$OUT/logs/install.log"; echo "error: install failed" >&2; exit 1; }
    cat "$OUT/logs/install.log"
    rm -rf "$OUT/port"
    mkdir -p "$OUT/port/libs.aarch64"
    cp "$WORK/stage/audreybrick" "$OUT/port/audreybrick"
    cp "$WORK/stage/libs.aarch64/libaudrey_lgpl.so" "$OUT/port/libs.aarch64/libaudrey_lgpl.so"
    chmod 755 "$OUT/port/audreybrick" "$OUT/port/libs.aarch64/libaudrey_lgpl.so"
    ls -l "$OUT/port" "$OUT/port/libs.aarch64"
    echo "build: OK"
    ;;
check)
    MAX_GLIBC="${AB_MAX_GLIBC:-2.39}"
    MAX_GLIBCXX="${AB_MAX_GLIBCXX:-3.4.32}"
    MAX_CXXABI="${AB_MAX_CXXABI:-1.3.14}"
    section "Symbol-version check (limits: GLIBC_$MAX_GLIBC, GLIBCXX_$MAX_GLIBCXX, CXXABI_$MAX_CXXABI)"
    status=0
    for f in "$OUT/port/audreybrick" "$OUT/port/libs.aarch64/libaudrey_lgpl.so"; do
        name="${f#"$OUT"/port/}"
        echo
        echo "-- $name: $(file -b "$f" | cut -d, -f1-3)"
        [ -f "$f" ] || { echo "   MISSING"; status=1; continue; }
        # Version needs, per library ("File: libc.so.6" headers, then "Name: GLIBC_2.17").
        echo "   version needs (readelf -V), per library:"
        readelf -V --wide "$f" | awk '
            /Version needs section/ { s = 1; next }
            s && /File:/ { if (lib != "") print "     " lib ":" vers; lib = $0; sub(/.*File: */, "", lib); sub(/ .*/, "", lib); vers = ""; next }
            s && /Name:/ { v = $0; sub(/.*Name: */, "", v); sub(/ .*/, "", v); vers = vers " " v; next }
            s && /^$/ { if (lib != "") print "     " lib ":" vers; lib = ""; s = 0 }
            END { if (lib != "") print "     " lib ":" vers }'
        needs="$(readelf -V --wide "$f" | awk '/Version needs section/{s=1;next} s && /Name:/ {v=$0; sub(/.*Name: */,"",v); sub(/ .*/,"",v); print v}')"
        # Imported symbols per version (objdump -T, undefined symbols only).
        echo "   imported symbols per version (objdump -T):"
        objdump -T "$f" | awk '/\*UND\*/' | grep -oE '(GLIBC|GLIBCXX|CXXABI|GCC)_[0-9][0-9.]*' \
            | sort -V | uniq -c | awk '{printf "     %-18s %5d\n", $2, $1}'
        if printf '%s\n' "$needs" | grep -q 'PRIVATE'; then echo "   FAIL: needs a PRIVATE version"; status=1; fi
        for fam in GLIBC GLIBCXX CXXABI; do
            case "$fam" in GLIBC) lim="$MAX_GLIBC" ;; GLIBCXX) lim="$MAX_GLIBCXX" ;; CXXABI) lim="$MAX_CXXABI" ;; esac
            hi="$(printf '%s\n' "$needs" | grep -E "^${fam}_[0-9]" | sed "s/^${fam}_//" | sort -V | tail -n 1 || true)"
            if [ -z "$hi" ]; then
                printf '   highest %-8s (none needed)\n' "$fam"
            elif version_le "$hi" "$lim"; then
                printf '   highest %-8s %-10s <= %s  ok\n' "$fam" "$hi" "$lim"
            else
                printf '   highest %-8s %-10s >  %s  FAIL: the device does not have it\n' "$fam" "$hi" "$lim"
                status=1
            fi
        done
        gcc_hi="$(printf '%s\n' "$needs" | grep -E '^GCC_[0-9]' | sed 's/^GCC_//' | sort -V | tail -n 1 || true)"
        [ -n "$gcc_hi" ] && printf '   highest %-8s %-10s (libgcc_s; the device has GCC 13)\n' "GCC" "$gcc_hi"
        echo "   NEEDED:"
        needed="$(readelf -d "$f" | awk '/\(NEEDED\)/ {gsub(/[\[\]]/, "", $NF); print $NF}')"
        for lib in $needed; do
            if printf ' %s ' "$ALLOWED_NEEDED" | grep -qF " $lib "; then
                echo "     $lib"
            else
                echo "     $lib   FAIL: not expected on the device"
                status=1
            fi
        done
        rpath="$(readelf -d "$f" | awk '/\((RUNPATH|RPATH)\)/ {print $1 " " $2 " " $NF}' || true)"
        echo "   RUNPATH/RPATH: ${rpath:-(none)}"
        if [ "$name" = "audreybrick" ]; then
            case "$rpath" in
                *'[$ORIGIN/libs.aarch64]'*) ;;
                *) echo "   FAIL: the executable's run path must be \$ORIGIN/libs.aarch64"; status=1 ;;
            esac
            printf '%s\n' "$needed" | grep -qx 'libSDL2-2.0.so.0' \
                || { echo "   FAIL: SDL2 must be linked by soname libSDL2-2.0.so.0"; status=1; }
        fi
        if readelf -d "$f" | grep -q '(SONAME)'; then
            echo "   SONAME: $(readelf -d "$f" | awk '/\(SONAME\)/ {gsub(/[\[\]]/, "", $NF); print $NF}')"
        fi
    done
    echo
    if [ "$status" -eq 0 ]; then echo "check: OK"; else echo "check: FAILED"; fi
    exit "$status"
    ;;
*)
    echo "usage: device_in_container.sh build|check" >&2
    exit 2
    ;;
esac
