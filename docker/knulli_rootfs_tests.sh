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
# M3-B: the CPU governor handling, the A/B swap setting and the settings line are
# tested against a fake sysfs tree in /tmp/fakesys (AUDREY_SYSFS_CPU, a testing aid).
# M2b-A: the package's data/input folder, the launcher's --input-dir, a FILE
# self-test through the launcher with a WAV file written here with bash's printf
# (the device root has no tool of ours), and a data/input that cannot be created.
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

section "M2b-A: the input folder, --input-dir, and FILE through the launcher"
[ -d /ab/pkg/audreybrick/data/input ] && [ -z "$(ls -A /ab/pkg/audreybrick/data/input)" ] \
    && pass "the package ships an empty audreybrick/data/input/" || fail "no empty data/input in the package"
grep -qF "launcher: input folder: $PORTS/audreybrick/data/input (0 WAV file(s);" "$DATA/stdout.log" \
    && pass "the launcher notes the input folder (0 WAV files)" || fail "no input folder note in stdout.log"
grep -qF "input: source OFF at start; folder $PORTS/audreybrick/data/input" "$DATA/audreybrick.log" \
    && pass "the app got --input-dir (the path has a space) and starts with the source OFF" || fail "app input folder line"
# A 2 s, 1 kHz square wave at amplitude 0.25 (0x2000), 48 kHz mono 16-bit PCM.
hi=""; lo=""
for i in $(seq 24); do hi+='\x00\x20'; lo+='\x00\xe0'; done
SQ="$DATA/input/square.wav"
{ printf 'RIFF\x24\xee\x02\x00WAVEfmt \x10\x00\x00\x00\x01\x00\x01\x00\x80\xbb\x00\x00\x00\x77\x01\x00\x02\x00\x10\x00data\x00\xee\x02\x00'
  for i in $(seq 2000); do printf "$hi$lo"; done; } > "$SQ"
[ "$(wc -c < "$SQ")" -eq 192044 ] && pass "wrote square.wav (192044 bytes) into data/input" || fail "square.wav size $(wc -c < "$SQ")"
# Entries the app ignores must not be counted by the launcher either: a link to
# the file, an AppleDouble "._" file, a hidden file and a directory named .wav.
ln -s square.wav "$DATA/input/link.wav"
cp "$SQ" "$DATA/input/._square.wav"
cp "$SQ" "$DATA/input/.hidden.wav"
mkdir -p "$DATA/input/folder.wav"
# M2b-A3: FILE (no automatic load), then hold X, d-pad right, release X: the file loads.
printf '0.10 L1 down\n0.15 L1 up\n0.30 DpadRight down\n0.35 DpadRight up\n0.40 X down\n0.45 DpadRight down\n0.50 DpadRight up\n0.55 X up\n' > /tmp/input_file.txt
launch AUDREY_ARGS="--selftest /tmp/input_file.txt --seconds 3 --no-video --expect-knob FeedbackGain 0 0 --expect-input-peak-min 0.2 --expect-peak-min 0.02 --expect-peak-max 1.0"
rc=$?
grep -E "^launcher: input folder|^input:|^selftest: (ok|FAIL|PASSED|FAILED)|^output peak" "$DATA/stdout.log" | sed 's/^/  /'
grep -E "input: " "$DATA/audreybrick.log" | sed 's/^\[[ 0-9.]*\] /  log: /'
[ "$rc" -eq 0 ] && grep -q 'self-test: PASSED' "$DATA/audreybrick.log" \
    && pass "FILE through the launcher: the self-test passed (input above 0.2, output above 0.02 at minimum feedback)" \
    || fail "FILE self-test through the launcher (rc $rc)"
grep -qF "launcher: input folder: $PORTS/audreybrick/data/input (1 WAV file(s);" "$DATA/stdout.log" \
    && grep -q "input: loaded 'square.wav': 1 channel(s), 48000 Hz, PCM 16-bit, 96000 frames" "$DATA/audreybrick.log" \
    && pass "the launcher counted 1 WAV file; the app logged the load" || fail "input load lines"
grep -q "input: scan of $PORTS/audreybrick/data/input: 5 entries, 1 WAV file(s), 4 other entries ignored" "$DATA/audreybrick.log" \
    && pass "the launcher and the app agree: 1 WAV file (the link, ._, hidden file and folder are ignored by both)" \
    || fail "launcher/app file counts differ"
cp "$DATA/audreybrick.log" /ab/out/launcher-input-audreybrick.log
rm -rf "$SQ" "$DATA/input/link.wav" "$DATA/input/._square.wav" "$DATA/input/.hidden.wav" "$DATA/input/folder.wav"

section "M2b-B: LIVE with a simulated capture device through the launcher; the capture lines of the device state"
# The self-test's simulated capture (no device is opened): LIVE, a failed open,
# then usb (the gain forced to -inf dB), Y + up: unity; the tone reaches the
# input meter and the output at minimum feedback.
printf '%s\n' 'capture device usb 2 220 0.25 48000' 'capture device busy fail' \
    '0.10 L1 down' '0.15 L1 up' '0.30 DpadRight down' '0.35 DpadRight up' '0.40 DpadRight down' '0.45 DpadRight up' \
    '0.60 X down' '0.65 DpadRight down' '0.70 DpadRight up' '0.75 DpadRight down' '0.80 DpadRight up' '0.85 X up' \
    '1.00 X down' '1.05 DpadLeft down' '1.10 DpadLeft up' '1.15 X up' \
    '1.50 Y down' '1.55 DpadUp down' '1.60 DpadUp up' '1.65 Y up' '2.50 capture unplug usb' > /tmp/input_live.txt
launch AUDREY_LIVE_TARGET=4 AUDREY_ARGS="--selftest /tmp/input_live.txt --seconds 3.5 --no-video --expect-knob FeedbackGain 0 0 --expect-input-peak-min 0.4 --expect-peak-min 0.02 --expect-peak-max 1.0 --expect-live-status lost"
rc=$?
grep -E "^live:|^selftest: (ok|FAIL|PASSED|FAILED)" "$DATA/stdout.log" | sed 's/^/  /'
grep -E "input: (capture|gain|DEVICE|live)|stats:.*live:" "$DATA/audreybrick.log" | sed 's/^\[[ 0-9.]*\] /  log: /' | cut -c1-260
[ "$rc" -eq 0 ] && grep -q 'self-test: PASSED' "$DATA/audreybrick.log" \
    && pass "LIVE through the launcher: the self-test passed (input above 0.4, output above 0.02, DEVICE LOST at the end)" \
    || fail "LIVE self-test through the launcher (rc $rc)"
grep -q "input: capture device 'busy' could not be opened (asked for 'busy'): SIMULATED: DEVICE BUSY" "$DATA/audreybrick.log" \
    && grep -q "input: gain 0.500 -> 0 (-inf dB) before opening a capture device" "$DATA/audreybrick.log" \
    && grep -q "input: capture device opened: 'usb' (asked for 'usb'; asked 48000 Hz f32 2 ch 256 frames; obtained 48000 Hz f32 2 ch 256 frames)" "$DATA/audreybrick.log" \
    && grep -q "input: DEVICE LOST: 'usb'" "$DATA/audreybrick.log" \
    && pass "the log has the failed open, the gain reset, the open (asked and obtained) and the loss" \
    || fail "LIVE log lines"
grep -qE '^launcher: settings: .* AUDREY_LIVE_TARGET=4 ' "$DATA/stdout.log" \
    && grep -q "input: live target 1024 frames (4 output buffers, --live-target-buffers)" "$DATA/audreybrick.log" \
    && pass "AUDREY_LIVE_TARGET=4 reaches the app as --live-target-buffers 4 (a 1024-frame target)" \
    || fail "AUDREY_LIVE_TARGET did not reach the app"
launch AUDREY_LIVE_TARGET=99 AUDREY_ARGS="$QUICK"
grep -q "AUDREY_LIVE_TARGET '99' is not a whole number from 1 to 32; using 2" "$DATA/stdout.log" \
    && grep -q "input: live target 512 frames (2 output buffers" "$DATA/audreybrick.log" \
    && pass "AUDREY_LIVE_TARGET=99: noted, 2 used" || fail "AUDREY_LIVE_TARGET=99 not handled"
grep -qE '^launcher: (pactl list short sources|pactl not found|capture: pactl)' "$DATA/stdout.log" \
    && grep -qE '^launcher: (arecord -L|arecord not found|arecord -L skipped)' "$DATA/stdout.log" \
    && pass "the device state lists PipeWire's sources (pactl) and ALSA's capture PCMs (arecord -L)" \
    || fail "no pactl / arecord -L lines in the device state"
grep -E '^launcher: (pactl list|arecord -L|capture)' "$DATA/stdout.log" | head -n 8 | sed 's/^/  /'
cp "$DATA/audreybrick.log" /ab/out/launcher-live-audreybrick.log

section "M3-C: the launcher reports KNULLI's input helpers; no .keys file is shipped"
[ ! -e "$PORTS/AudreyBrick.sh.keys" ] && pass "no AudreyBrick.sh.keys in the package (the app handles Menu + Start itself)" \
    || fail "the package holds an AudreyBrick.sh.keys"
grep -qE '^launcher: input helper evmapy: (running|not running)' "$DATA/stdout.log" \
    && grep -qE '^launcher: input helper hotkeygen: (running|not running)' "$DATA/stdout.log" \
    && grep -qE '^launcher: input helper triggerhappy \(thd\): (running|not running)' "$DATA/stdout.log" \
    && grep -qE '^launcher: (no /var/run/evmapy/\*\.json|evmapy config )' "$DATA/stdout.log" \
    && pass "stdout.log notes the input helpers (nothing runs in this container)" || fail "no input-helper notes"
grep -E '^launcher: (input helper|evmapy config|no /var/run/evmapy)' "$DATA/stdout.log" | sed 's/^/  /'

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
    if [ -z "$left" ] && tail -n 1 "$DATA/audreybrick.log" | grep -q 'shut down cleanly' && printf '%s' "$reason" | grep -q 'a stop signal'; then
        pass "SIG$sig to the launcher: the app quit cleanly (its stop-signal path), no process left"
    else
        fail "SIG$sig to the launcher: processes left '$left', reason '$reason'"
    fi
    [ "$sig" = KILL ] && { printf '%s' "$reason" | grep -q 'parent process) is gone' && pass "SIGKILL: the app says its launcher is gone" || fail "SIGKILL: reason does not mention the parent"; }
    [ ! -e /var/run/battery-saver/audreybrick.pause ] && pass "SIG$sig: no keep-awake file left behind" || fail "SIG$sig: audreybrick.pause left behind"
    [ "$sig" != KILL ] && { grep -q "received SIG$sig; passing it to the app" "$DATA/stdout.log" && pass "SIG$sig: the launcher forwarded it" || fail "SIG$sig not forwarded"; }
    cp "$DATA/audreybrick.log" "/ab/out/signal-$sig-audreybrick.log"; cp "$DATA/stdout.log" "/ab/out/signal-$sig-stdout.log"
done

set +m

# --- M3-B: CPU governor (fake sysfs tree), A/B swap, settings line -------------------------
# The container's real /sys is read-only (or has no cpufreq), so the governor handling
# runs against a fake tree through AUDREY_SYSFS_CPU (a testing aid for launcher and
# app). policy0 offers performance; policy4 does not (it must be skipped with a note).
FAKE=/tmp/fakesys
mkfake() {
    rm -rf "$FAKE"
    mkdir -p "$FAKE/cpufreq/policy0" "$FAKE/cpufreq/policy4"
    printf 'schedutil\n' > "$FAKE/cpufreq/policy0/scaling_governor"
    printf 'ondemand userspace powersave performance schedutil \n' > "$FAKE/cpufreq/policy0/scaling_available_governors"
    printf '408000\n' > "$FAKE/cpufreq/policy0/scaling_cur_freq"
    printf '408000\n' > "$FAKE/cpufreq/policy0/scaling_min_freq"
    printf '1416000\n' > "$FAKE/cpufreq/policy0/scaling_max_freq"
    printf '0 1 2 3\n' > "$FAKE/cpufreq/policy0/affected_cpus"
    printf 'schedutil\n' > "$FAKE/cpufreq/policy4/scaling_governor"
    printf 'powersave schedutil \n' > "$FAKE/cpufreq/policy4/scaling_available_governors"
    printf '1200000\n' > "$FAKE/cpufreq/policy4/scaling_cur_freq"
}
gov() { cat "$FAKE/cpufreq/$1/scaling_governor" 2>/dev/null; }

section "Governor: performance while the app runs, restored on a normal exit (fake sysfs)"
mkfake
launch AUDREY_SYSFS_CPU="$FAKE" AUDREY_GOVERNOR=performance AUDREY_ARGS="--selftest /ab/rise.txt --seconds 3 --no-video" &
lpid=$!
sleep 1.5
during0="$(gov policy0)"; during4="$(gov policy4)"
wait "$lpid"
rc=$?
sed 's/^/  stdout.log: /' "$DATA/stdout.log" | grep -E 'governor|settings|restore|policy'
grep -E 'cpu (frequency|governor)|restore-on-exit|stats:|input: A/B' "$DATA/audreybrick.log" | sed 's/^/  app log: /'
[ "$rc" -eq 0 ] && pass "launcher returned 0" || fail "launcher returned $rc"
[ "$during0" = performance ] && pass "policy0 was 'performance' while the app ran" || fail "policy0 during the run: '$during0'"
[ "$during4" = schedutil ] && grep -q "governor: policy4: 'performance' is not in scaling_available_governors ('powersave schedutil'); skipped, left at 'schedutil'" "$DATA/stdout.log" \
    && pass "policy4 (no performance governor offered) skipped with a note, left at schedutil" || fail "policy4: '$during4'"
[ "$(gov policy0)" = schedutil ] && [ "$(gov policy4)" = schedutil ] && pass "both policies are 'schedutil' after the exit" || fail "after exit: '$(gov policy0)' '$(gov policy4)'"
grep -q "governor: policy0: 'schedutil' -> 'performance' (restored when the app ends)" "$DATA/stdout.log" && pass "stdout.log: the change, before -> after" || fail "change not logged"
grep -q "restore-on-exit requested: 'schedutil' -> $FAKE/cpufreq/policy0/scaling_governor if it still holds 'performance'" "$DATA/audreybrick.log" \
    && ! grep -q "restore-on-exit requested: .*policy4" "$DATA/audreybrick.log" \
    && pass "the app was given --restore-on-exit for policy0 only" || fail "restore-on-exit arguments"
grep -q "restore-on-exit: wrote 'schedutil' to $FAKE/cpufreq/policy0/scaling_governor" "$DATA/audreybrick.log" \
    && grep -q "governor: $FAKE/cpufreq/policy0: already back to 'schedutil' (the app restores it too); nothing to do" "$DATA/stdout.log" \
    && pass "the app restored first; the launcher's second restore found nothing to do (harmless)" || fail "restore sequence"
grep -q 'cpu governor at start: policy0 performance; policy4 schedutil' "$DATA/audreybrick.log" \
    && grep -q 'cpu governor after restore-on-exit: policy0 schedutil; policy4 schedutil' "$DATA/audreybrick.log" \
    && pass "the app logs the governors at start and after its restore" || fail "governor lines in the app log"
grep -qE 'stats: .*callback load .*p50 .*, cpu freq policy0 408 MHz \(window 408\.\.408 MHz, [0-9]+ samples\); policy4 1200 MHz' "$DATA/audreybrick.log" \
    && pass "the stats line carries the CPU frequency of both policies" || fail "no cpu freq on the stats line"
grep -qE "^launcher: settings: AudreyBrick [^ ]+ \(built .*\); AUDREY_BUFFER=256 AUDREY_LIVE_TARGET=2 AUDREY_KEEP_AWAKE=1 AUDREY_SWAP_AB=1 AUDREY_SWAP_XY=1 AUDREY_GOVERNOR=performance AUDREY_SYSFS_CPU=$FAKE \(testing aid\)$" "$DATA/stdout.log" \
    && pass "one settings line with the app's version and the resolved settings" || fail "settings line"
grep -q 'launcher: policy0 governor schedutil, cur 408000 kHz, min 408000 kHz, max 1416000 kHz, cpus 0 1 2 3' "$DATA/stdout.log" \
    && pass "device state lists each policy before the change" || fail "device state policy line"
cp "$DATA/audreybrick.log" /ab/out/governor-normal-audreybrick.log; cp "$DATA/stdout.log" /ab/out/governor-normal-stdout.log

section "Governor: the launcher restores by itself when the app did not (app rejects its options, exit 2)"
mkfake
launch AUDREY_SYSFS_CPU="$FAKE" AUDREY_GOVERNOR=performance AUDREY_ARGS="--bogus-option"
rc=$?
grep -E 'governor' "$DATA/stdout.log" | sed 's/^/  stdout.log: /'
[ "$rc" -eq 2 ] && [ "$(gov policy0)" = schedutil ] \
    && grep -q "governor: $FAKE/cpufreq/policy0: restored 'performance' -> 'schedutil'" "$DATA/stdout.log" \
    && pass "app exit 2 before its own restore: the launcher restored schedutil and logged it" || fail "launcher-only restore (rc $rc, '$(gov policy0)')"

section "Governor: SIGTERM and SIGKILL to the launcher (fake sysfs)"
set -m
for sig in TERM KILL; do
    mkfake
    launch AUDREY_SYSFS_CPU="$FAKE" AUDREY_GOVERNOR=performance AUDREY_ARGS="--selftest /ab/rise.txt --seconds 60 --no-video" &
    lpid=$!
    sleep 2
    during="$(gov policy0)"
    bpid="$(pgrep -f -n "/bin/bash $PORTS/AudreyBrick.sh")"
    apid="$(pgrep -f -n "audreybrick/audreybrick --fullscreen")"
    kill "-$sig" "$bpid"
    for i in 1 2 3 4 5 6 7 8 9 10; do kill -0 "$apid" 2>/dev/null || break; sleep 0.5; done
    wait "$lpid" 2>/dev/null
    after="$(gov policy0)"
    echo "  SIG$sig: policy0 during '$during', after '$after'"
    grep -E 'governor|restore' "$DATA/stdout.log" | sed 's/^/  stdout.log: /'
    grep -E 'restore-on-exit|cpu governor|exit:' "$DATA/audreybrick.log" | sed 's/^/  app log: /'
    [ "$during" = performance ] && [ "$after" = schedutil ] && pass "SIG$sig to the launcher: performance during the run, schedutil restored" \
        || fail "SIG$sig: during '$during', after '$after'"
    grep -q "restore-on-exit: wrote 'schedutil'" "$DATA/audreybrick.log" && pass "SIG$sig: the app restored it (logged)" || fail "SIG$sig: no app restore"
    if [ "$sig" = TERM ]; then
        grep -q "already back to 'schedutil' (the app restores it too)" "$DATA/stdout.log" && pass "SIGTERM: the launcher's restore after the app's was harmless" || fail "SIGTERM: launcher restore line"
    else
        ! grep -q "already back to\|governor: .*restored '" "$DATA/stdout.log" && pass "SIGKILL: the launcher could not restore (killed); the app did" || fail "SIGKILL: unexpected launcher restore"
    fi
    pgrep -f "audreybrick/audreybrick" > /dev/null && fail "SIG$sig: an app process is left" || pass "SIG$sig: no app process left"
    cp "$DATA/audreybrick.log" "/ab/out/governor-$sig-audreybrick.log"; cp "$DATA/stdout.log" "/ab/out/governor-$sig-stdout.log"
done
set +m

section "Governor: keep, a governor not offered, an invalid name, a read-only file (fake sysfs)"
mkfake
mtime0="$(stat -c %.9Y "$FAKE/cpufreq/policy0/scaling_governor")"
launch AUDREY_SYSFS_CPU="$FAKE" AUDREY_ARGS="--selftest /ab/rise.txt --seconds 2 --no-video" &
lpid=$!
sleep 1
during="$(gov policy0)"
wait "$lpid"
[ "$during" = schedutil ] && [ "$(gov policy0)" = schedutil ] && grep -q 'governor: left alone (AUDREY_GOVERNOR=keep)' "$DATA/stdout.log" \
    && grep -q 'AUDREY_GOVERNOR=keep' "$DATA/stdout.log" && ! grep -q 'restore-on-exit requested' "$DATA/audreybrick.log" \
    && pass "AUDREY_GOVERNOR unset (default keep): nothing changed, nothing to restore" || fail "default keep (during '$during')"
[ "$(stat -c %.9Y "$FAKE/cpufreq/policy0/scaling_governor")" = "$mtime0" ] \
    && pass "keep: scaling_governor was not written (modification time unchanged, $mtime0)" || fail "keep: scaling_governor was written"
mkfake
launch AUDREY_SYSFS_CPU="$FAKE" AUDREY_GOVERNOR=conservative AUDREY_ARGS="$QUICK"
grep -q "governor: policy0: 'conservative' is not in scaling_available_governors" "$DATA/stdout.log" \
    && grep -q "governor: policy4: 'conservative' is not in scaling_available_governors" "$DATA/stdout.log" && [ "$(gov policy0)" = schedutil ] \
    && pass "a governor no policy offers: skipped with a note, nothing changed" || fail "unavailable governor"
for bad in 'perf ormance' 'performance;reboot' '../x' 'PERFORMANCE'; do
    mkfake
    launch AUDREY_SYSFS_CPU="$FAKE" AUDREY_GOVERNOR="$bad" AUDREY_ARGS="$QUICK"
    grep -qF "AUDREY_GOVERNOR '$bad' is not a governor name; using keep" "$DATA/stdout.log" && [ "$(gov policy0)" = schedutil ] \
        && pass "AUDREY_GOVERNOR '$bad': treated as keep" || fail "AUDREY_GOVERNOR '$bad'"
done
mkfake
chmod a-w "$FAKE/cpufreq/policy0/scaling_governor"
launch AUDREY_SYSFS_CPU="$FAKE" AUDREY_GOVERNOR=performance AUDREY_ARGS="$QUICK" 2> /tmp/ro-stderr.txt
rc=$?
[ ! -s /tmp/ro-stderr.txt ] && pass "a failed governor write prints nothing on the launcher's stderr" || { fail "launcher stderr:"; cat /tmp/ro-stderr.txt; }
chmod u+w "$FAKE/cpufreq/policy0/scaling_governor"
[ "$rc" -eq 0 ] && grep -q "governor: policy0: could not write 'performance' (no permission or read-only); left at 'schedutil'" "$DATA/stdout.log" \
    && ! grep -q 'restore-on-exit requested' "$DATA/audreybrick.log" && [ "$(gov policy0)" = schedutil ] \
    && pass "a scaling_governor that cannot be written: noted, not fatal, nothing to restore" || fail "read-only governor (rc $rc)"
# The app's own restore against a file that cannot be written: logged, not fatal.
mkfake
chmod a-w "$FAKE/cpufreq/policy0/scaling_governor"
SDL_AUDIODRIVER=dummy /ab/port/audreybrick --selftest /ab/rise.txt --seconds 0.3 --no-video --log /ab/out/restore-ro.log \
    --restore-on-exit "$FAKE/cpufreq/policy0/scaling_governor" ondemand schedutil > /dev/null 2>&1
rc=$?
chmod u+w "$FAKE/cpufreq/policy0/scaling_governor"
[ "$rc" -eq 0 ] && grep -q 'restore-on-exit: writing .ondemand. to .* failed\|restore-on-exit: .*Permission denied (not written; not fatal)' /ab/out/restore-ro.log \
    && [ "$(gov policy0)" = schedutil ] && pass "app restore into a read-only file: logged, exit 0" || fail "app restore read-only (rc $rc)"
echo "  real /sys in this container (no AUDREY_SYSFS_CPU), from the first launcher run:"
grep -E 'governor|cpufreq' /ab/out/launcher-stdout.log | sed 's/^/    /'
grep -E 'cpu frequency|cpu governor' /ab/out/launcher-audreybrick.log | sed 's/^/    /'

section "AUDREY_SWAP_AB / AUDREY_SWAP_XY: values reach the app; KNULLI's layout flag turns both off"
for v in 1 0 unset yes 2 ""; do
    if [ "$v" = unset ]; then launch AUDREY_ARGS="$QUICK"; else launch AUDREY_SWAP_AB="$v" AUDREY_SWAP_XY="$v" AUDREY_ARGS="$QUICK"; fi
    rc=$?
    case "$v" in
        0) wa='input: A/B swap off'; wx='input: X/Y swap off'; set_want='AUDREY_SWAP_AB=0 AUDREY_SWAP_XY=0' ;;
        *) wa='input: A/B swap ON (--swap-ab)'; wx='input: X/Y swap ON (--swap-xy)'; set_want='AUDREY_SWAP_AB=1 AUDREY_SWAP_XY=1' ;;
    esac
    ok=1
    [ "$rc" -eq 0 ] || ok=0
    grep -qF "$wa" "$DATA/audreybrick.log" || ok=0
    grep -qF "$wx" "$DATA/audreybrick.log" || ok=0
    grep -qF "$set_want" "$DATA/stdout.log" || ok=0
    case "$v" in
        yes|2) grep -qF "AUDREY_SWAP_AB '$v' is not 0 or 1; using 1" "$DATA/stdout.log" && grep -qF "AUDREY_SWAP_XY '$v' is not 0 or 1; using 1" "$DATA/stdout.log" || ok=0 ;;
        unset|"") grep -qF "launcher: buttons: A/B swap 1 (default); X/Y swap 1 (default)" "$DATA/stdout.log" || ok=0 ;;
        *) grep -qF "A/B swap $([ "$v" = 0 ] && echo 0 || echo 1) (set in the environment)" "$DATA/stdout.log" || ok=0 ;;
    esac
    [ "$ok" -eq 1 ] && pass "AUDREY_SWAP_AB/XY '$v': app log says '$wa' and '$wx', settings line $set_want" || fail "AUDREY_SWAP_AB/XY '$v' (rc $rc)"
done
# KNULLI's own SWITCH A/B and X/Y for this launch: the flag file (ports_hooks.sh).
touch /var/run/ports_xbox_layout.flag
launch AUDREY_ARGS="$QUICK"
grep -qF "launcher: buttons: A/B swap 0 (off: KNULLI's SWITCH A/B and X/Y is on for this port (/var/run/ports_xbox_layout.flag exists)); X/Y swap 0 (off: KNULLI's" "$DATA/stdout.log" \
    && grep -qF 'input: A/B swap off' "$DATA/audreybrick.log" && grep -qF 'input: X/Y swap off' "$DATA/audreybrick.log" \
    && grep -qF '/var/run/ports_xbox_layout.flag exists' "$DATA/stdout.log" \
    && pass "KNULLI's layout flag present: both swaps off, and stdout.log says why" || fail "layout flag: swaps not turned off"
launch AUDREY_SWAP_AB=1 AUDREY_ARGS="$QUICK"
grep -qF "launcher: buttons: A/B swap 1 (set in the environment); X/Y swap 0 (off: KNULLI's" "$DATA/stdout.log" \
    && grep -qF 'input: A/B swap ON' "$DATA/audreybrick.log" && grep -qF 'input: X/Y swap off' "$DATA/audreybrick.log" \
    && pass "layout flag present, AUDREY_SWAP_AB=1 in the environment: the environment wins for A/B only" || fail "layout flag with env override"
rm -f /var/run/ports_xbox_layout.flag
launch AUDREY_ARGS="$QUICK"
grep -qF '/var/run/ports_xbox_layout.flag does not exist' "$DATA/stdout.log" && grep -qF 'input: X/Y swap ON' "$DATA/audreybrick.log" \
    && pass "flag removed: defaults (both swaps on) again" || fail "after removing the flag"

section "Launcher logging additions (before any state change)"
for want in 'launcher: knulli-settings-get not found' 'launcher: knulli-settings-get-master not found' \
            'launcher: /var/run/ports_xbox_layout.flag does not exist' 'launcher: cpufreq layout: ' 'power_supply'; do
    grep -qF "$want" "$DATA/stdout.log" && pass "stdout.log has: $want" || fail "stdout.log lacks: $want"
done
mkfake
launch AUDREY_SYSFS_CPU="$FAKE" AUDREY_ARGS="$QUICK"
grep -qF "launcher: cpufreq layout: 2 policy director(ies) under $FAKE/cpufreq, 0 cpu*/cpufreq director(ies)" "$DATA/stdout.log" \
    && pass "cpufreq layout counted (2 policies, no cpu*/cpufreq)" || fail "cpufreq layout line"
dev_line="$(grep -n 'end of device state' "$DATA/stdout.log" | cut -d: -f1)"
first_change="$(grep -n 'keep-awake: created' "$DATA/stdout.log" | cut -d: -f1)"
[ -n "$dev_line" ] && [ -n "$first_change" ] && [ "$dev_line" -lt "$first_change" ] \
    && pass "every device-state item is logged before the first change to the system" || fail "order of device state and keep-awake"

section "Governor: changed by someone else while the app runs -> not restored (fake sysfs)"
mkfake
launch AUDREY_SYSFS_CPU="$FAKE" AUDREY_GOVERNOR=performance AUDREY_ARGS="--selftest /ab/rise.txt --seconds 3 --no-video" &
lpid=$!
sleep 1.5
during="$(gov policy0)"
printf 'powersave\n' > "$FAKE/cpufreq/policy0/scaling_governor" # as KNULLI's gameStop hook might
wait "$lpid"
grep -E 'restore|governor' "$DATA/audreybrick.log" "$DATA/stdout.log" | grep -i 'someone' | sed 's/^/  /'
[ "$during" = performance ] && [ "$(gov policy0)" = powersave ] \
    && grep -q "restore-on-exit: $FAKE/cpufreq/policy0/scaling_governor: changed by someone else to 'powersave' (expected 'performance'); not restoring 'schedutil'" "$DATA/audreybrick.log" \
    && grep -q "governor: $FAKE/cpufreq/policy0: changed by someone else to 'powersave'; not restoring 'schedutil'" "$DATA/stdout.log" \
    && pass "a governor changed by someone else is left alone by the app and the launcher, both logged" || fail "changed-by-someone-else (now '$(gov policy0)')"

section "Signal sweep: SIGTERM to the launcher at 0 to 400 ms (fake sysfs, AUDREY_GOVERNOR=performance)"
# Each run: a fresh fake tree, the launcher started directly (its pid is $!),
# SIGTERM after a delay, then the final state must be clean: every policy back to
# schedutil, no pause file, no app process; and bash must never abort.
mkfake_many() { # <n policies>
    rm -rf "$FAKE"
    local k
    for k in $(seq 0 $(( $1 - 1 ))); do
        mkdir -p "$FAKE/cpufreq/policy$k"
        printf 'schedutil\n' > "$FAKE/cpufreq/policy$k/scaling_governor"
        printf 'ondemand powersave performance schedutil \n' > "$FAKE/cpufreq/policy$k/scaling_available_governors"
        printf '408000\n' > "$FAKE/cpufreq/policy$k/scaling_cur_freq"
    done
}
sweep() { # <label> <runs> <max ms> <policies>
    local label="$1" runs="$2" maxms="$3" npol="$4" r d pid rc aborts=0 dirty=0 changed_runs=0 app_started=0 stopped_early=0
    for r in $(seq 0 $(( runs - 1 ))); do
        mkfake_many "$npol"
        rm -f /var/run/battery-saver/audreybrick.pause "$DATA/stdout.log"
        d=$(( r * maxms / runs ))
        ( cd / && exec env SDL_AUDIODRIVER=dummy AUDREY_SYSFS_CPU="$FAKE" AUDREY_GOVERNOR=performance \
              AUDREY_ARGS="--selftest /ab/rise.txt --seconds 5 --no-video" /bin/bash "$PORTS/AudreyBrick.sh" 2> /tmp/sweep-err.txt ) &
        pid=$!
        sleep "$(printf '%d.%03d' $((d / 1000)) $((d % 1000)))"
        kill -TERM "$pid" 2>/dev/null
        wait "$pid" 2>/dev/null
        rc=$?
        for i in $(seq 1 50); do pgrep -f "audreybrick/audreybrick --fullscreen" > /dev/null || break; sleep 0.1; done
        if [ "$rc" -eq 134 ] || grep -q 'bad jump\|Aborting' /tmp/sweep-err.txt 2>/dev/null; then
            aborts=$((aborts + 1))
            echo "  ABORT at $d ms (rc $rc): $(tr '\n' ' ' < /tmp/sweep-err.txt)"
            grep -q 'keep-awake: created' "$DATA/stdout.log" && echo "    (after the system had been changed)"
        fi
        grep -q 'keep-awake: created' "$DATA/stdout.log" && changed_runs=$((changed_runs + 1))
        grep -q 'starting the app' "$DATA/stdout.log" && app_started=$((app_started + 1))
        grep -q 'before the app started' "$DATA/stdout.log" && stopped_early=$((stopped_early + 1))
        bad=""
        for g in "$FAKE"/cpufreq/policy*/scaling_governor; do
            [ "$(cat "$g")" = schedutil ] || bad="$bad ${g#$FAKE/cpufreq/}=$(cat "$g")"
        done
        [ -e /var/run/battery-saver/audreybrick.pause ] && bad="$bad pause-file-left"
        pgrep -f "audreybrick/audreybrick --fullscreen" > /dev/null && { bad="$bad app-left"; kill -KILL $(pgrep -f "audreybrick/audreybrick --fullscreen") 2>/dev/null; }
        if [ -n "$bad" ]; then
            dirty=$((dirty + 1))
            echo "  DIRTY at $d ms (rc $rc):$bad"
            sed 's/^/    /' "$DATA/stdout.log" | grep -E 'governor|keep-awake|SIG|starting|exited' | head -n 12
        fi
    done
    echo "  $label: $runs runs, delays 0..$maxms ms: launcher stopped before starting the app $stopped_early, app started $app_started, system changed $changed_runs; bash aborts $aborts; unclean final states $dirty"
    [ "$aborts" -eq 0 ] && pass "$label: no bash abort in $runs runs" || fail "$label: $aborts bash abort(s)"
    [ "$dirty" -eq 0 ] && pass "$label: every run ended clean (governors restored, no pause file, no app left)" || fail "$label: $dirty unclean run(s)"
}
sweep "one policy" 200 400 1
sweep "64 policies" 60 400 64

section "Signal sweep: SIGTERM to the app itself at 0 to 300 ms after exec"
# The app run directly with --remove-on-exit and --restore-on-exit; its exit
# actions must have run, unless the signal arrived before its handlers existed
# (before main: the process then dies by SIGTERM, status 143), which is counted.
aok=0; apre=0; abad=0
for r in $(seq 0 199); do
    d=$(( r * 300 / 200 ))
    mkfake
    printf 'performance\n' > "$FAKE/cpufreq/policy0/scaling_governor"
    : > /tmp/app-sweep.pause
    rm -f /tmp/app-sweep.log
    ( exec env SDL_AUDIODRIVER=dummy /ab/port/audreybrick --selftest /ab/rise.txt --seconds 5 --no-video --log /tmp/app-sweep.log \
          --remove-on-exit /tmp/app-sweep.pause --restore-on-exit "$FAKE/cpufreq/policy0/scaling_governor" schedutil performance \
          > /tmp/app-sweep.out 2>&1 ) &
    pid=$!
    sleep "$(printf '0.%03d' "$d")"
    kill -TERM "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null
    rc=$?
    if [ "$(gov policy0)" = schedutil ] && [ ! -e /tmp/app-sweep.pause ]; then
        aok=$((aok + 1))
    elif [ "$rc" -eq 143 ] && ! grep -q 'AudreyBrick' /tmp/app-sweep.log 2>/dev/null; then
        apre=$((apre + 1))
    else
        abad=$((abad + 1))
        echo "  BAD at $d ms (rc $rc): governor '$(gov policy0)', pause $( [ -e /tmp/app-sweep.pause ] && echo left || echo removed)"
        tail -n 5 /tmp/app-sweep.log 2>/dev/null | sed 's/^/    /'
    fi
done
echo "  app sweep: 200 runs, delays 0..300 ms: exit actions ran $aok; killed before the app's handlers existed (status 143, no log) $apre; other failures $abad"
[ "$abad" -eq 0 ] && pass "app sweep: whenever the app had started, a SIGTERM ended in its exit actions" || fail "app sweep: $abad failure(s)"
rm -f /tmp/app-sweep.pause
rm -rf "$FAKE"

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
rmdir "$F/audreybrick/data/input" # M2b-A: so the launcher cannot create it either
chmod a-w "$F/audreybrick/data"
( cd / && SDL_AUDIODRIVER=dummy AUDREY_ARGS="$QUICK" /bin/bash "$F/AudreyBrick.sh" ) 2> /tmp/fail-stderr.txt
rc=$?
chmod u+w "$F/audreybrick/data"
[ "$rc" -eq 0 ] && grep -q 'data is not writable; logs for this run are in /tmp/audreybrick-data' /tmp/audreybrick-data/stdout.log \
    && grep -q 'not writable' /tmp/fail-stderr.txt && [ -f /tmp/audreybrick-data/audreybrick.log ] \
    && pass "data/ not writable: logs in /tmp/audreybrick-data, stated there and on stderr" || fail "read-only data/ (rc $rc)"
grep -qF "launcher: input folder: could not create $F/audreybrick/data/input; the INPUT tab will show no files (not fatal)" /tmp/audreybrick-data/stdout.log \
    && grep -qF "input: scan of $F/audreybrick/data/input: NO FOLDER (" /tmp/audreybrick-data/audreybrick.log \
    && pass "data/input cannot be created: noted, the app runs and logs NO FOLDER (not fatal)" || fail "uncreatable data/input"

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
