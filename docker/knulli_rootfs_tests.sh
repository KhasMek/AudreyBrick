#!/bin/bash
# Copyright (c) 2026 KhasMek
# SPDX-License-Identifier: MIT
#
# AudreyBrick: tests run INSIDE a container whose root filesystem is part of the
# device's own system image (task M3-A, part B), with the device's bash.
# Called by scripts/test_on_knulli_rootfs.sh; not meant to be run by hand.
#
# Mounts:
#   /ab/port                          the device build (audreybrick, libs.aarch64/), read-only
#   /ab/rise.txt                      tests/platform/rise.txt, read-only
#   /ab/pkg                           the assembled package (AudreyBrick.sh, audreybrick/), read-only
#   /ab/out                           logs for the caller
#   /tmp                              a tmpfs mounted exec (Docker's default tmpfs is noexec)
# The package is copied to "/tmp/ports with space" (a space in the path, a file
# system where chmod works; on the Mac's shared folder it does not).
#
# Only the device's loader, libraries and standard tools (bash, ldd, coreutils,
# grep, sed, procps) run here, plus our app.
# The device's SDL has only the Mali video driver (no dummy or offscreen video), so
# every self-test here runs with --no-video and SDL_AUDIODRIVER=dummy. NOT exercised
# here: the display path (Mali driver, EGL, the renderer), ALSA/PipeWire output,
# the buttons and the CPU load; they can only be tested on the device.
set -u

PORTS="/tmp/ports with space"
mkdir -p "$PORTS" && cp -r /ab/pkg/. "$PORTS/" || { echo "[FAIL] cannot copy the package"; exit 1; }
PKG_BIN="$PORTS/audreybrick/audreybrick"
LOADER=/lib/ld-linux-aarch64.so.1
EXPECT=(--expect-knob FeedbackGain 0.913611 0.933611 --expect-knob OutputVolume 0.339294 0.359294
        --expect-peak-min 0.01 --expect-peak-max 1.0)
fails=0
pass() { echo "[PASS] $*"; }
fail() { echo "[FAIL] $*"; fails=$((fails + 1)); }
section() { printf '\n==== %s ====\n' "$*"; }

section "The device root"
echo "uname -m:  $(uname -m)"
echo "bash:      $BASH_VERSION (the device's /bin/bash)"
echo "glibc:     $("$LOADER" --version 2>&1 | head -n 1)"
echo "SDL2:      $(ls /usr/lib/libSDL2-2.0.so.0.* 2>/dev/null)"
echo "libstdc++: $(ls /usr/lib/libstdc++.so.6.* 2>/dev/null | grep -v gdb)"
echo "NOTE: NOT exercised in this container: the display path (Mali video driver, EGL, the renderer),"
echo "      ALSA/PipeWire audio output, the buttons and the CPU load. They can only be tested on the device."
echo "      The device's SDL has no dummy video driver, so the self-tests below run with --no-video and"
echo "      the dummy audio driver: they exercise the loader, glibc, libstdc++, SDL 2.32.8 (audio, events,"
echo "      game controller subsystem), the app's control path and its log."

section "ldd (the device's own) on the build"
ldd_out="$(ldd /ab/port/audreybrick 2>&1)"
printf '%s\n' "$ldd_out"
if printf '%s\n' "$ldd_out" | grep -q 'not found'; then fail "ldd: a library is not found"; else pass "ldd: every library found in the device root"; fi
printf '%s\n' "$ldd_out" | grep -qE 'libSDL2-2.0.so.0 => /usr/lib(64)?/libSDL2-2.0.so.0' && pass "ldd: SDL2 is the device's /usr/lib/libSDL2-2.0.so.0 (/usr/lib64 is a symlink to it)" || fail "ldd: SDL2 not from /usr/lib"
printf '%s\n' "$ldd_out" | grep -q 'libaudrey_lgpl.so => /ab/port/libs.aarch64/libaudrey_lgpl.so' && pass "ldd: libaudrey_lgpl.so through \$ORIGIN/libs.aarch64" || fail "ldd: libaudrey_lgpl.so not found through the rpath"
echo "-- libaudrey_lgpl.so:"
ldd /ab/port/libs.aarch64/libaudrey_lgpl.so

section "Self-test with the device's libraries (--no-video, dummy audio driver)"
mkdir -p /ab/out/direct
SDL_AUDIODRIVER=dummy /ab/port/audreybrick --selftest /ab/rise.txt --seconds 5 --no-video "${EXPECT[@]}" \
    --fullscreen --log /ab/out/direct/audreybrick.log > /ab/out/direct/stdout.log 2>&1
rc=$?
grep -E '^selftest: (ok|FAIL|PASSED|FAILED)|^output peak|^video:|^audio:' /ab/out/direct/stdout.log
[ "$rc" -eq 0 ] && pass "self-test passed (exit 0)" || fail "self-test exit $rc"
sdl_line="$(grep -m1 'sdl: compiled against' /ab/out/direct/audreybrick.log)"
echo "log: ${sdl_line#*] }"
printf '%s' "$sdl_line" | grep -q 'linked 2\.32\.8' && pass "the app logs SDL linked 2.32.8 (the device's)" || fail "SDL linked version is not 2.32.8"
grep -E 'video environment|video: none|audio: driver|exit:' /ab/out/direct/audreybrick.log | sed 's/^/log: /'
grep -q 'video environment: video drivers compiled into the linked SDL: mali$' /ab/out/direct/audreybrick.log \
    && pass "the log lists the device SDL's video drivers: mali only" || fail "video driver list"
section "Without --no-video the device's SDL must fail visibly here (no display in a container)"
SDL_AUDIODRIVER=dummy /ab/port/audreybrick --selftest /ab/rise.txt --seconds 1 --log /ab/out/direct/video-failure.log \
    > /ab/out/direct/video-failure-stdout.log 2>&1
rc=$?
grep -E 'VIDEO FAILURE|video environment' /ab/out/direct/video-failure-stdout.log | sed 's/^/  stdout: /'
[ "$rc" -eq 1 ] && grep -q 'VIDEO FAILURE: SDL_Init(SDL_INIT_VIDEO) failed' /ab/out/direct/video-failure.log \
    && pass "video failure is fatal (exit 1), with SDL's error in the log and on stderr" || fail "video failure not reported (exit $rc)"

section "Through the dynamic loader: \$ORIGIN, with LD_LIBRARY_PATH unset"
chmod a-x "$PKG_BIN"
[ -x "$PKG_BIN" ] && fail "could not remove the executable bit" || pass "executable bit removed from the packaged binary ($(ls -l "$PKG_BIN" | cut -c1-10))"
trace="$(env -u LD_LIBRARY_PATH LD_TRACE_LOADED_OBJECTS=1 "$LOADER" "$PKG_BIN" 2>&1)"
printf '%s\n' "$trace" | sed 's/^/  /'
printf '%s\n' "$trace" | grep -qF "libaudrey_lgpl.so => $PORTS/audreybrick/libs.aarch64/libaudrey_lgpl.so" \
    && pass "via the loader, \$ORIGIN/libs.aarch64 resolves to the binary's own folder (path with a space)" \
    || fail "via the loader, libaudrey_lgpl.so did not resolve through \$ORIGIN"

DATA="$PORTS/audreybrick/data"
# Runs the packaged launcher from / with the dummy audio driver; extra env as arguments.
launch() { ( cd / && env SDL_AUDIODRIVER=dummy "$@" /bin/bash "$PORTS/AudreyBrick.sh" ); }
QUICK="--selftest /ab/rise.txt --seconds 0.3 --no-video"

section "Packaged launcher, executable bit removed, started from / as KNULLI does (/bin/bash <path>.sh)"
launch AUDREY_ARGS="--selftest /ab/rise.txt --seconds 5 --no-video --expect-peak-min 0.01 --expect-peak-max 1.0"
rc=$?
sed 's/^/  stdout.log: /' "$DATA/stdout.log"
[ "$rc" -eq 0 ] && pass "launcher returned 0" || fail "launcher returned $rc"
grep -q 'starting the app through /lib/ld-linux-aarch64.so.1' "$DATA/stdout.log" && pass "launcher used the dynamic loader" || fail "launcher did not use the loader"
grep -q 'linked 2\.32\.8' "$DATA/audreybrick.log" && pass "data/audreybrick.log written, SDL 2.32.8" || fail "no log or wrong SDL"
grep -q 'options: buffer 256 frames requested, fullscreen yes' "$DATA/audreybrick.log" && pass "app got --fullscreen and --buffer 256" || fail "app options wrong"
grep -q 'exit-with-parent: SIGTERM is delivered' "$DATA/audreybrick.log" && pass "app got --exit-with-parent" || fail "no --exit-with-parent"
grep -q 'self-test: PASSED' "$DATA/audreybrick.log" && pass "self-test passed through the launcher" || fail "self-test through the launcher"
grep -q 'launcher: --- end of device state ---' "$DATA/stdout.log" && pass "device-state block written" || fail "no device-state block"
ms="$(sed -n 's/^launcher: device state took \([0-9]*\) ms$/\1/p' "$DATA/stdout.log")"
[ -n "$ms" ] && [ "$ms" -lt 1500 ] && pass "device-state block took $ms ms" || fail "device-state block time: '$ms'"
cp "$DATA/audreybrick.log" /ab/out/launcher-audreybrick.log
cp "$DATA/stdout.log" /ab/out/launcher-stdout.log

section "Executable bit restored, AUDREY_BUFFER 512, then out-of-range values"
chmod a+x "$PKG_BIN"
launch AUDREY_BUFFER=512 AUDREY_ARGS="$QUICK"
rc=$?
[ "$rc" -eq 0 ] && grep -q 'starting the app directly, buffer 512' "$DATA/stdout.log" && grep -q 'buffer 512 frames requested' "$DATA/audreybrick.log" \
    && pass "started directly with buffer 512" || fail "direct start with 512 (rc $rc)"
for b in 8 32769 99999999 abc ""; do
    launch AUDREY_BUFFER="$b" AUDREY_ARGS="$QUICK"
    rc=$?
    if [ -z "$b" ]; then
        # An empty variable means "use the default".
        [ "$rc" -eq 0 ] && grep -q 'buffer 256 frames requested' "$DATA/audreybrick.log" && pass "AUDREY_BUFFER empty: default 256" || fail "empty AUDREY_BUFFER (rc $rc)"
    else
        [ "$rc" -eq 0 ] && grep -q "AUDREY_BUFFER '$b' is not a whole number from 16 to 32768; using 256" "$DATA/stdout.log" \
            && grep -q 'buffer 256 frames requested' "$DATA/audreybrick.log" \
            && pass "AUDREY_BUFFER '$b': falls back to 256, the app runs" || fail "AUDREY_BUFFER '$b' (rc $rc)"
    fi
done
launch AUDREY_ARGS="--bogus-option"
rc=$?
[ "$rc" -eq 2 ] && grep -q 'unknown option' "$DATA/stdout.log" && pass "the app's exit status (2) and error come through" || fail "exit status $rc"

section "Keep-awake (/var/run is a tmpfs here; on the device it exists)"
if [ -d /var/run ] && [ -w /var/run ]; then
    touch /var/run/battery-saver/other.pause 2>/dev/null || { mkdir -p /var/run/battery-saver && touch /var/run/battery-saver/other.pause; }
    launch AUDREY_ARGS="--selftest /ab/rise.txt --seconds 3 --no-video" &
    lpid=$!
    sleep 1.5
    [ -f /var/run/battery-saver/audreybrick.pause ] && pass "audreybrick.pause exists while the app runs" || fail "no pause file while running"
    wait "$lpid"
    [ ! -e /var/run/battery-saver/audreybrick.pause ] && pass "audreybrick.pause removed at exit" || fail "pause file left behind"
    [ -f /var/run/battery-saver/other.pause ] && pass "another program's pause file is untouched" || fail "other.pause removed"
    grep -q 'pause files present before start: other.pause' "$DATA/stdout.log" && pass "pre-existing pause files are logged" || fail "pre-existing pause not logged"
    grep -q 'keep-awake: created /var/run/battery-saver/audreybrick.pause' "$DATA/stdout.log" && grep -qE 'keep-awake: (removed|.*already removed by the app)' "$DATA/stdout.log" \
        && pass "keep-awake logged in stdout.log" || fail "keep-awake not logged"
    launch AUDREY_KEEP_AWAKE=0 AUDREY_ARGS="$QUICK"
    grep -q 'keep-awake: off (AUDREY_KEEP_AWAKE=0)' "$DATA/stdout.log" && [ ! -e /var/run/battery-saver/audreybrick.pause ] \
        && pass "AUDREY_KEEP_AWAKE=0: no pause file" || fail "AUDREY_KEEP_AWAKE=0"
    rm -f /var/run/battery-saver/other.pause
else
    fail "/var/run is not writable in this container (the test script mounts a tmpfs there)"
fi

section "Signals: SIGTERM to the launcher ends the app cleanly; SIGKILL to the launcher too (--exit-with-parent)"
# Job control on: otherwise this non-interactive shell would start the launcher with
# SIGINT ignored (as KNULLI does not), and the INT case would test nothing.
set -m
for sig in TERM INT HUP KILL; do
    launch AUDREY_ARGS="--selftest /ab/rise.txt --seconds 60 --no-video" &
    lpid=$!
    # The launcher's bash is the child of the subshell started by launch().
    sleep 2
    bpid="$(pgrep -f -n "/bin/bash $PORTS/AudreyBrick.sh")"
    apid="$(pgrep -f -n "audreybrick/audreybrick --fullscreen")"
    kill "-$sig" "$bpid"
    for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$apid" 2>/dev/null || break; sleep 0.5; done
    wait "$lpid" 2>/dev/null
    rc=$?
    left="$(pgrep -f "audreybrick/audreybrick" | tr '\n' ' ')"
    reason="$(grep -m1 'exit:' "$DATA/audreybrick.log" | sed 's/^\[[ 0-9.]*\] //')"
    echo "  SIG$sig: launcher exit $rc; app log: $reason; last line: $(tail -n 1 "$DATA/audreybrick.log" | sed 's/^\[[ 0-9.]*\] //')"
    if [ -z "$left" ] && tail -n 1 "$DATA/audreybrick.log" | grep -q 'shut down cleanly' && printf '%s' "$reason" | grep -q 'SDL_QUIT'; then
        pass "SIG$sig to the launcher: the app quit cleanly (SDL_QUIT), no process left"
    else
        fail "SIG$sig to the launcher: processes left '$left', reason '$reason'"
    fi
    [ "$sig" = KILL ] && { printf '%s' "$reason" | grep -q 'parent process) is gone' && pass "SIGKILL: the app says its launcher is gone" || fail "SIGKILL: reason does not mention the parent"; }
    [ ! -e /var/run/battery-saver/audreybrick.pause ] && pass "SIG$sig: no keep-awake file left behind" || fail "SIG$sig: audreybrick.pause left behind"
    [ "$sig" != KILL ] && { grep -q "received SIG$sig; passing it to the app" "$DATA/stdout.log" && pass "SIG$sig: the launcher forwarded it" || fail "SIG$sig not forwarded"; }
    cp "$DATA/audreybrick.log" "/ab/out/signal-$sig-audreybrick.log"; cp "$DATA/stdout.log" "/ab/out/signal-$sig-stdout.log"
done

set +m

section "Launcher-level failures"
F="/tmp/fail test"
mkdir -p "$F" && cp -r /ab/pkg/. "$F/"
rm "$F/audreybrick/audreybrick"
( cd / && /bin/bash "$F/AudreyBrick.sh" ) 2> /tmp/fail-stderr.txt
rc=$?
[ "$rc" -eq 1 ] && grep -q 'PROBLEM: .*audreybrick is missing' "$F/audreybrick/data/stdout.log" && grep -q 'is missing' /tmp/fail-stderr.txt \
    && pass "missing binary: exit 1, in data/stdout.log and on stderr" || fail "missing binary (rc $rc)"
cp -r /ab/pkg/audreybrick/audreybrick "$F/audreybrick/audreybrick"
rm -rf /tmp/audreybrick-data
chmod a-w "$F/audreybrick/data"
( cd / && SDL_AUDIODRIVER=dummy AUDREY_ARGS="$QUICK" /bin/bash "$F/AudreyBrick.sh" ) 2> /tmp/fail-stderr.txt
rc=$?
chmod u+w "$F/audreybrick/data"
[ "$rc" -eq 0 ] && grep -q 'data is not writable; logs for this run are in /tmp/audreybrick-data' /tmp/audreybrick-data/stdout.log \
    && grep -q 'not writable' /tmp/fail-stderr.txt && [ -f /tmp/audreybrick-data/audreybrick.log ] \
    && pass "data/ not writable: logs in /tmp/audreybrick-data, stated there and on stderr" || fail "read-only data/ (rc $rc)"

section "Log rotation over 10 runs (2 of them with no usable clock)"
RT="/tmp/rotation test"
mkdir -p "$RT" /tmp/fakebin && cp -r /ab/pkg/. "$RT/"
printf '#!/bin/bash\nexit 1\n' > /tmp/fakebin/date && chmod +x /tmp/fakebin/date
for i in 1 2 3 4 5 6 7 8 9 10; do
    if [ "$i" -eq 5 ] || [ "$i" -eq 6 ]; then p="/tmp/fakebin:$PATH"; else p="$PATH"; fi
    ( cd / && PATH="$p" SDL_AUDIODRIVER=dummy AUDREY_ARGS="$QUICK" /bin/bash "$RT/AudreyBrick.sh" ) || fail "rotation run $i failed"
done
ls "$RT/audreybrick/data" | sed 's/^/  /'
na="$(ls "$RT/audreybrick/data" | grep -c '^audreybrick-[0-9]\{6\}-.*\.log$')"
ns="$(ls "$RT/audreybrick/data" | grep -c '^stdout-[0-9]\{6\}-.*\.log$')"
[ "$na" -eq 7 ] && [ "$ns" -eq 7 ] && [ -f "$RT/audreybrick/data/audreybrick.log" ] && [ -f "$RT/audreybrick/data/stdout.log" ] \
    && pass "8 runs kept: the latest pair plus 7 numbered pairs" || fail "kept $na + $ns numbered logs"
first="$(ls "$RT/audreybrick/data" | grep '^audreybrick-[0-9]' | head -n 1)"
[ "${first:12:6}" = "000003" ] && pass "the oldest kept is run 3 (runs 1 and 2 pruned, oldest first)" || fail "oldest kept is '$first'"
ls "$RT/audreybrick/data" | grep -q '^audreybrick-000005-00000000-000000\.log$' && ls "$RT/audreybrick/data" | grep -q '^audreybrick-000006-00000000-000000\.log$' \
    && pass "runs without a clock still get unique, ordered names (run number first)" || fail "no-clock names"
[ "$(cat "$RT/audreybrick/data/run-counter")" = 10 ] && grep -q '^000010-' "$RT/audreybrick/data/latest-run" \
    && pass "run counter at 10, latest-run names run 10" || fail "run counter"

section "The launcher touched only its own data/ files"
ls -lR "$PORTS" | sed 's/^/  /'
cp -r "$PORTS" /ab/out/package-after-tests
pgrep -f audreybrick > /dev/null && fail "an audreybrick process is still running" || pass "no audreybrick process left"

echo
echo "REMINDER: the display path (Mali, EGL, renderer) and ALSA output were NOT exercised here."
if [ "$fails" -eq 0 ]; then echo "rootfs tests: OK"; exit 0; fi
echo "rootfs tests: $fails FAILED"
exit 1
