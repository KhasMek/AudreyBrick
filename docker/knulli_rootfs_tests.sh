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
# M2b-A: the launcher's data/input folder and --input-dir, a FILE
# self-test through the launcher with a WAV file written here with bash's printf
# (the device root has no tool of ours), and a data/input that cannot be created.
# PRESET-C: /userdata is an empty tmpfs; /userdata/saves does not exist until the
# section "PRESET-C" creates it (so every section before it tests the app-folder
# branch, data/ inside the package copy). The package itself ships no data folder.
# PRESET-D: an older version's data/ next to a data folder in /userdata/saves is
# left untouched (compared entry by entry, with times) and gives one notice.
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

# --- PRESET-C as root (AB_ROOT_SECTION=1; a second container started by
# scripts/test_on_knulli_rootfs.sh without --user) ----------------------------------------
# On the device the launcher runs as root, for which permission bits never make a
# folder unwritable; only a read-only file system does. Mounts of that container:
#   /ro-empty               an empty folder, read-only bind mount
#   /ro-saves               holds audreybrick/, read-only bind mount
#   /userdata, /tmp, /var/run  tmpfs as in the main container
if [ "${AB_ROOT_SECTION:-0}" = 1 ]; then
    section "PRESET-C as root: read-only file systems (uid $(id -u))"
    [ "$(id -u)" -eq 0 ] && pass "running as root, as the launcher does on the device" || fail "not root (uid $(id -u))"
    QUICK="--selftest /ab/rise.txt --seconds 0.3 --no-video"
    RP2="/tmp/root ports"
    rpkg() { rm -rf "$RP2"; mkdir -p "$RP2" && cp -r /ab/pkg/. "$RP2/" && mkdir -p "$RP2/audreybrick/data/recordings" && printf 'mine\n' > "$RP2/audreybrick/data/recordings/rec-0001.wav"; }
    rlaunch() { local pf="$1"; shift; ( cd / && env SDL_AUDIODRIVER=dummy AUDREY_KEEP_AWAKE=0 "$@" /bin/bash "$pf/AudreyBrick.sh" ); }
    # R1: the saves folder is on a read-only file system (empty).
    rpkg
    rlaunch "$RP2" AUDREY_SAVES_DIR=/ro-empty AUDREY_ARGS="$QUICK"
    rc=$?
    grep -E '^launcher: (PROBLEM: )?(data folder|old data folder)' "$RP2/audreybrick/data/stdout.log" | sed 's/^/  /'
    [ "$rc" -eq 0 ] && grep -qF "launcher: data folder: $RP2/audreybrick/data (the app folder's data/: /ro-empty is not writable (AUDREY_SAVES_DIR, testing aid))" "$RP2/audreybrick/data/stdout.log" \
        && [ "$(cat "$RP2/audreybrick/data/recordings/rec-0001.wav")" = mine ] && [ -z "$(ls -A /ro-empty)" ] \
        && ! grep -q 'old data folder' "$RP2/audreybrick/data/stdout.log" \
        && pass "root, saves on a read-only file system: the app folder's data/ used (no notice), nothing written there" || fail "root R1 (rc $rc)"
    # R2: saves/audreybrick exists on a read-only file system.
    rpkg
    rlaunch "$RP2" AUDREY_SAVES_DIR=/ro-saves AUDREY_ARGS="$QUICK" 2>/dev/null
    rc=$?
    [ "$rc" -eq 0 ] && grep -qF "launcher: PROBLEM: data folder: /ro-saves/audreybrick exists but is not a writable folder; using $RP2/audreybrick/data instead" "$RP2/audreybrick/data/stdout.log" \
        && [ "$(cat "$RP2/audreybrick/data/recordings/rec-0001.wav")" = mine ] \
        && pass "root, saves/audreybrick on a read-only file system: PROBLEM line, the app folder's data/ used" || fail "root R2 (rc $rc)"
    rm -rf "$RP2"
    # R3: permission bits do not stop root (as on the device): mode 000 is still used;
    # the app folder's old data/ is left untouched, with the notice.
    mkdir -p /userdata/saves/audreybrick && chmod 000 /userdata/saves/audreybrick
    rpkg
    rlaunch "$RP2" AUDREY_ARGS="$QUICK" 2>/dev/null
    grep -qF 'launcher: data folder: /userdata/saves/audreybrick (it exists and is writable)' /userdata/saves/audreybrick/stdout.log \
        && [ "$(cat "$RP2/audreybrick/data/recordings/rec-0001.wav")" = mine ] && [ ! -e /userdata/saves/audreybrick/recordings/rec-0001.wav ] \
        && grep -qF "launcher: old data folder: $RP2/audreybrick/data holds files from an older version; they are left untouched" /userdata/saves/audreybrick/stdout.log \
        && pass "root: a saves/audreybrick with mode 000 is used (permission bits do not apply to root); the old data/ untouched, with the notice" || fail "root R3"
    rm -rf /userdata/saves "$RP2"
    echo
    if [ "$fails" -eq 0 ]; then echo "rootfs tests (root section): OK"; exit 0; fi
    echo "rootfs tests (root section): $fails FAILED"
    exit 1
fi

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
# PRESET-C: the package ships no data folder (it was an empty data/input before);
# with no /userdata/saves here the launcher made data/ and data/input itself.
[ ! -e /ab/pkg/audreybrick/data ] \
    && pass "the package ships no audreybrick/data folder (PRESET-C; replacing the app folder deletes no user data)" \
    || fail "the package holds audreybrick/data"
[ -d "$DATA/input" ] && [ -d "$DATA/recordings" ] && [ -d "$DATA/presets" ] \
    && grep -qF "launcher: data folder: $PORTS/audreybrick/data (the app folder's data/: /userdata/saves does not exist)" "$DATA/stdout.log" \
    && grep -qF "launcher: presets folder: $PORTS/audreybrick/data/presets (0 preset file(s))" "$DATA/stdout.log" \
    && pass "no /userdata/saves: the launcher uses and creates the app folder's data/ (input, recordings, presets) and says why" \
    || fail "data folder in the app folder (no /userdata/saves)"
grep -qF "preset: folder $PORTS/audreybrick/data/presets: 0 preset(s) listed" "$DATA/audreybrick.log" \
    && pass "PRESET-B: the app got --preset-dir (the app folder's data/presets) and lists it" \
    || fail "PRESET-B: no --preset-dir in the app-folder branch (no 'preset: folder' line)"
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
# PRESET-B: eight tabs; L1 three times from LOOP: PRESET, MOD, INPUT.
printf '0.02 L1 down\n0.04 L1 up\n0.10 L1 down\n0.15 L1 up\n0.20 L1 down\n0.25 L1 up\n0.30 DpadRight down\n0.35 DpadRight up\n0.40 X down\n0.45 DpadRight down\n0.50 DpadRight up\n0.55 X up\n' > /tmp/input_file.txt
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
    '0.02 L1 down' '0.04 L1 up' '0.10 L1 down' '0.15 L1 up' '0.20 L1 down' '0.25 L1 up' \
    '0.30 DpadRight down' '0.35 DpadRight up' '0.40 DpadRight down' '0.45 DpadRight up' \
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
    && grep -q "input: capture device opened: 'usb' (asked for 'usb'; asked 48000 Hz f32 2 ch 256 frames; obtained 48000 Hz f32 2 ch 256 frames; route by name (simulated))" "$DATA/audreybrick.log" \
    && grep -q "input: DEVICE LOST: 'usb'" "$DATA/audreybrick.log" \
    && pass "the log has the failed open, the gain reset, the open (asked and obtained) and the loss" \
    || fail "LIVE log lines"
# M2b-B2: the capture rule is logged at start. The self-test uses the simulated
# capture system, which offers SYSTEM DEFAULT; the ALSA rule (no SYSTEM DEFAULT,
# the direct route, the mic's mixer) needs a sound card and amixer, which this
# container has neither of: it is tested with fakes in input_capture_route_test.
grep -q "input: capture rule: SYSTEM DEFAULT is offered first in the device list" "$DATA/audreybrick.log" \
    && pass "the capture rule is logged at start (simulated capture: SYSTEM DEFAULT offered)" \
    || fail "capture rule line"
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

# --- PRESET-C/D: the data folder in /userdata/saves; an old data/ left untouched --------
SAVESD=/userdata/saves
N="$SAVESD/audreybrick"
P="/tmp/ports preset c"
newpkg() { rm -rf "$P"; mkdir -p "$P" && cp -r /ab/pkg/. "$P/"; }
OLD="$P/audreybrick/data"
launch_p() { ( cd / && env SDL_AUDIODRIVER=dummy "$@" /bin/bash "$P/AudreyBrick.sh" ); }
# "md5 relative-path" of every regular file below a folder, sorted.
snap() { # <dir> <out file>
    ( shopt -s globstar dotglob nullglob
      cd "$1" 2>/dev/null || exit 0
      for f in **; do [ -f "$f" ] && [ ! -L "$f" ] && printf '%s %s\n' "$(md5sum < "$f" | cut -c1-32)" "$f"; done ) | sort > "$2"
}
md5of() { md5sum < "$1" 2>/dev/null | cut -c1-32; }
# Every content (md5) of the first snapshot is somewhere in the second, except
# run-counter and latest-run (the launcher's own state, rewritten every start).
nothing_lost() { # <before> <after>
    [ -z "$(comm -23 <(grep -v -E ' (run-counter|latest-run)$' "$1" | cut -c1-32 | sort) <(cut -c1-32 "$2" | sort))" ]
}
rnd() { head -c "$2" /dev/urandom > "$1"; } # <file> <bytes>
# An older version's data/ as it looks on the card after a few runs.
make_old() {
    mkdir -p "$OLD/input/sub dir" "$OLD/recordings" "$OLD/empty dir"
    printf '41\n' > "$OLD/run-counter"
    printf '000041-19700616-070532\n' > "$OLD/latest-run"
    printf 'old app log, run 41\n' > "$OLD/audreybrick.log"
    printf 'old stdout, run 41\n' > "$OLD/stdout.log"
    printf 'old app log, run 40\n' > "$OLD/audreybrick-000040-19700616-060000.log"
    printf 'old stdout, run 40\n' > "$OLD/stdout-000040-19700616-060000.log"
    rnd "$OLD/input/a.wav" 20000; rnd "$OLD/input/sub dir/b.wav" 3000
    rnd "$OLD/input/._a.wav" 100; rnd "$OLD/input/.hidden" 50
    for k in 1 2 3 4 5 6; do rnd "$OLD/recordings/rec-000$k.wav" $((k * 1000)); done
    printf 'my notes\n' > "$OLD/my notes.txt"
}

section "PRESET-C: /userdata/saves exists, fresh install (no old data/)"
mkdir -p "$SAVESD"
newpkg
launch_p AUDREY_ARGS="$QUICK"
rc=$?
grep -E '^launcher: (data folder|old data folder|input folder|recordings folder|presets folder)' "$N/stdout.log" | sed 's/^/  /'
[ "$rc" -eq 0 ] && [ -d "$N/input" ] && [ -d "$N/recordings" ] && [ -d "$N/presets" ] && [ -f "$N/audreybrick.log" ] && [ -f "$N/stdout.log" ] \
    && pass "the launcher made $N with input/, recordings/, presets/ and the logs" || fail "fresh install into $N (rc $rc)"
grep -qF "launcher: data folder: $N (created in $SAVESD)" "$N/stdout.log" && ! grep -q 'old data folder' "$N/stdout.log" \
    && pass "stdout.log: data folder $N (created in $SAVESD); no notice (no old data/)" || fail "data folder line (fresh install)"
[ ! -e "$OLD" ] && pass "nothing is created in the app folder (no audreybrick/data)" || fail "an audreybrick/data was created in the app folder"
grep -qF "input: source OFF at start; folder $N/input" "$N/audreybrick.log" \
    && grep -qF "launcher: recordings folder: $N/recordings (0 recording(s)" "$N/stdout.log" \
    && grep -qF "launcher: presets folder: $N/presets (0 preset file(s))" "$N/stdout.log" \
    && pass "the app got --input-dir and --record-dir in $N; presets/ noted" || fail "folders passed to the app"
grep -qF "preset: folder $N/presets: 0 preset(s) listed; next new preset preset-0001.json" "$N/audreybrick.log" \
    && pass "PRESET-B: --preset-dir is passed (PASS_PRESET_DIR=1): the app lists $N/presets" \
    || fail "PRESET-B: --preset-dir not passed (no 'preset: folder $N/presets' line in the app's log)"
launch_p AUDREY_ARGS="$QUICK"
grep -qF "launcher: data folder: $N (it exists and is writable)" "$N/stdout.log" && [ "$(cat "$N/run-counter")" = 2 ] \
    && pass "second start: same folder (it exists and is writable), run counter 2" || fail "second start in $N"
rm -rf "$N"

section "PRESET-B: Start + A saves, the PRESET tab loads, through the packaged launcher (/userdata/saves)"
# A button script: right twice on LOOP (Frequency 24/56 -> 26/56), Start + A
# (preset-0001.json), right three times more (29/56), L1 to PRESET (the last
# tab; the cursor is on the preset just saved), right: load it (back to 26/56).
newpkg
printf '%s\n' '0.05 DpadRight down' '0.07 DpadRight up' '0.10 DpadRight down' '0.12 DpadRight up' \
    '0.30 Start down' '0.32 A down' '0.34 A up' '0.36 Start up' \
    '0.50 DpadRight down' '0.52 DpadRight up' '0.55 DpadRight down' '0.57 DpadRight up' '0.60 DpadRight down' '0.62 DpadRight up' \
    '0.70 L1 down' '0.72 L1 up' '0.90 DpadRight down' '0.92 DpadRight up' > /tmp/preset_save_load.txt
launch_p AUDREY_SWAP_AB=0 AUDREY_SWAP_XY=0 AUDREY_ARGS="--selftest /tmp/preset_save_load.txt --seconds 1.5 --no-video --expect-preset saved 1 --expect-preset loaded 1 --expect-preset failed 0 --expect-preset files 1 --expect-knob Frequency 0.4637 0.4649"
rc=$?
grep -E 'preset: ' "$N/audreybrick.log" | sed 's/^\[[ 0-9.]*\] /  log: /'
[ "$rc" -eq 0 ] && grep -q 'self-test: PASSED' "$N/audreybrick.log" \
    && pass "the self-test passed: one preset saved and loaded, none failed, Frequency back at 26/56 after the load" \
    || fail "preset save/load self-test through the launcher (rc $rc)"
[ -f "$N/presets/preset-0001.json" ] && grep -qF '"format": "audreybrick-preset"' "$N/presets/preset-0001.json" \
    && [ -z "$(ls -A "$N/presets" | grep -v '^preset-0001\.json$')" ] \
    && pass "$N/presets holds preset-0001.json (a version 1 preset) and nothing else (no temporary file)" \
    || fail "presets folder after the save: $(ls -A "$N/presets" 2>&1 | tr '\n' ' ')"
grep -qF 'preset: saved preset-0001.json (Start + A); 1 preset(s) in the folder' "$N/audreybrick.log" \
    && grep -qF 'preset: loaded preset-0001.json' "$N/audreybrick.log" \
    && pass "the app's log has the save (Start + A) and the load" || fail "preset save/load log lines"
launch_p AUDREY_ARGS="$QUICK"
grep -qF "launcher: presets folder: $N/presets (1 preset file(s))" "$N/stdout.log" \
    && grep -qF "preset: folder $N/presets: 1 preset(s) listed; next new preset preset-0002.json" "$N/audreybrick.log" \
    && pass "next start: the launcher counts 1 preset file and the app lists it (next new preset-0002.json)" \
    || fail "the saved preset after a restart"
cp "$N/audreybrick.log" /ab/out/presetb-audreybrick.log
rm -rf "$N"

section "PRESET-D: an old data/ is left untouched, with one notice per start (data folder /userdata/saves/audreybrick)"
# Every entry below a folder and the folder itself: type, name, content (md5 or link
# target), size, permissions, and modification and change times with nanoseconds
# (a rename, a new or deleted entry or a rewrite in a folder changes its times).
tree_state() { # <dir>
    ( shopt -s globstar dotglob nullglob
      cd "$1" 2>/dev/null || { echo "MISSING $1"; exit 0; }
      printf 'top %s\n' "$(stat -c '%F|%a|%y|%z' .)"
      for f in **; do
          if [ -L "$f" ]; then printf 'L %q -> %s |%s\n' "$f" "$(readlink "$f")" "$(stat -c '%y|%z' "$f")"
          elif [ -f "$f" ]; then printf 'F %q %s |%s\n' "$f" "$(md5sum < "$f" | cut -c1-32)" "$(stat -c '%s|%a|%y|%z' "$f")"
          elif [ -d "$f" ]; then printf 'D %q |%s\n' "$f" "$(stat -c '%a|%y|%z' "$f")"
          else printf 'O %q\n' "$f"; fi
      done ) | sort
}
# The device root has no cmp or diff: md5sum and comm.
same_state() { [ "$(md5sum < /tmp/old-before.txt)" = "$(md5sum < /tmp/old-after.txt)" ]; }
NOTICE="launcher: old data folder: $OLD holds files from an older version; they are left untouched (nothing there is moved or changed automatically). Look there before replacing the app folder, which deletes it. To keep recordings/ or input/ files, move them by hand into the same folders in $N, without overwriting files of the same name"
NOTICE_ERR="AudreyBrick: files found in the old folder $OLD; left untouched; move them by hand into $N"
newpkg; make_old; ln -s rec-0001.wav "$OLD/recordings/link.wav"
tree_state "$OLD" > /tmp/old-before.txt
echo "  old data/: $(grep -c '^[FLD] ' /tmp/old-before.txt) entries (files, a link, hidden names, an empty folder, folders with spaces)"
for start in 1 2; do
    launch_p AUDREY_ARGS="$QUICK" 2> /tmp/presetd-stderr.txt
    rc=$?
    tree_state "$OLD" > /tmp/old-after.txt
    [ "$start" -eq 1 ] && grep -E '^launcher: (PROBLEM: )?(data folder|old data folder)' "$N/stdout.log" | sed 's/^/  /' && sed 's/^/  stderr: /' /tmp/presetd-stderr.txt
    [ "$rc" -eq 0 ] && grep -q 'self-test: PASSED' "$N/audreybrick.log" && same_state \
        && pass "start $start: the old data/ is byte-for-byte and name-for-name unchanged (contents, names, links, folders, sizes, permissions, modification and change times); the app ran" \
        || { fail "start $start: the old data/ changed (rc $rc)"; comm -3 /tmp/old-before.txt /tmp/old-after.txt | head -n 8 | sed 's/^/    before|after: /'; }
    [ "$(grep -c '^launcher: old data folder: ' "$N/stdout.log")" -eq 1 ] && grep -qxF "$NOTICE" "$N/stdout.log" \
        && [ "$(wc -l < /tmp/presetd-stderr.txt)" -eq 1 ] && grep -qxF "$NOTICE_ERR" /tmp/presetd-stderr.txt \
        && pass "start $start: exactly one notice line in stdout.log and one line on stderr" \
        || fail "start $start: notice ($(grep -c '^launcher: old data folder: ' "$N/stdout.log") in stdout.log, $(wc -l < /tmp/presetd-stderr.txt) stderr line(s))"
done
[ "$(cat "$N/run-counter")" = 2 ] && [ ! -e "$N/recordings/rec-0001.wav" ] && [ ! -e "$N/input/a.wav" ] && [ ! -e "$N/data-move.log" ] \
    && [ -z "$(cd "$N" && ls -A | grep -v -E '^(input|recordings|presets|run-counter|latest-run|audreybrick\.log|stdout\.log|(audreybrick|stdout)-00000[0-9]-.*\.log)$')" ] \
    && pass "the data folder holds only what the launcher and the app made (run counter 2, no copied file, no data-move.log)" \
    || fail "data folder after two starts with an old data/: $(ls -A "$N" | tr '\n' ' ')"
cp "$N/stdout.log" /ab/out/presetd-notice-stdout.log
rm -rf "$N"
# Only a hidden file in the old data/: it counts (hidden names are looked at too).
newpkg; mkdir -p "$OLD"; printf 'x\n' > "$OLD/.hidden"
tree_state "$OLD" > /tmp/old-before.txt
launch_p AUDREY_ARGS="$QUICK" 2> /tmp/presetd-stderr.txt
tree_state "$OLD" > /tmp/old-after.txt
grep -qxF "$NOTICE" "$N/stdout.log" && same_state \
    && pass "an old data/ holding only a hidden file: notice, the file untouched" || fail "old data/ with only a hidden file"
rm -rf "$N"
# An empty old data/: no notice, and the empty folder stays.
newpkg; mkdir -p "$OLD"
tree_state "$OLD" > /tmp/old-before.txt
launch_p AUDREY_ARGS="$QUICK" 2> /tmp/presetd-stderr.txt
rc=$?
tree_state "$OLD" > /tmp/old-after.txt
[ "$rc" -eq 0 ] && ! grep -q 'old data folder' "$N/stdout.log" && [ ! -s /tmp/presetd-stderr.txt ] && [ -d "$OLD" ] && same_state \
    && pass "an empty old data/: no notice (stdout.log and stderr), the empty folder left as it was" || fail "empty old data/ (rc $rc)"
rm -rf "$N"
# No old data/: no notice, nothing created in the app folder.
newpkg
launch_p AUDREY_ARGS="$QUICK" 2> /tmp/presetd-stderr.txt
[ ! -e "$OLD" ] && ! grep -q 'old data folder' "$N/stdout.log" && [ ! -s /tmp/presetd-stderr.txt ] \
    && pass "no old data/: no notice, no audreybrick/data created" || fail "absent old data/"
rm -rf "$N"
# An old data/ that is a link: not followed, no notice, the link and its target untouched.
newpkg
mkdir -p /tmp/elsewhere && printf 'keep\n' > /tmp/elsewhere/keep.txt
ln -s /tmp/elsewhere "$OLD"
launch_p AUDREY_ARGS="$QUICK" 2> /tmp/presetd-stderr.txt
[ -L "$OLD" ] && [ "$(readlink "$OLD")" = /tmp/elsewhere ] && [ "$(ls -A /tmp/elsewhere)" = keep.txt ] && [ "$(cat /tmp/elsewhere/keep.txt)" = keep ] \
    && ! grep -q 'old data folder' "$N/stdout.log" && [ ! -e "$N/keep.txt" ] \
    && pass "an old data/ that is a link: no notice, the link and its target untouched" || fail "old data/ as a link"
rm -rf "$N" /tmp/elsewhere
# The app folder's data/ is itself the data folder (no /userdata/saves): no notice.
rmdir "$SAVESD"
newpkg; make_old
launch_p AUDREY_ARGS="$QUICK" 2> /tmp/presetd-stderr.txt
rc=$?
[ "$rc" -eq 0 ] && grep -qF "launcher: data folder: $OLD (the app folder's data/: $SAVESD does not exist)" "$OLD/stdout.log" \
    && ! grep -q 'old data folder' "$OLD/stdout.log" && [ ! -s /tmp/presetd-stderr.txt ] && [ "$(cat "$OLD/run-counter")" = 42 ] \
    && pass "the app folder's data/ in use as the data folder (holding files): no notice, run 42 continues there" || fail "data/ as the data folder (rc $rc)"
mkdir -p "$SAVESD"

section "PRESET-C: AUDREY_SAVES_DIR (testing aid)"
SAME=/tmp/saves-same
mkdir -p "$SAME"
newpkg
launch_p AUDREY_SAVES_DIR="$SAME" AUDREY_ARGS="$QUICK"
rc=$?
[ "$rc" -eq 0 ] && grep -qF "launcher: data folder: $SAME/audreybrick (created in $SAME (AUDREY_SAVES_DIR, testing aid))" "$SAME/audreybrick/stdout.log" \
    && pass "AUDREY_SAVES_DIR replaces /userdata/saves (testing aid)" || fail "AUDREY_SAVES_DIR (rc $rc)"
rm -rf "$SAME"
rmdir "$SAVESD"
launch_p AUDREY_SAVES_DIR="saves" AUDREY_ARGS="$QUICK"
grep -qF "launcher: data folder: $OLD (the app folder's data/: /userdata/saves does not exist (AUDREY_SAVES_DIR 'saves' is not an absolute path; ignored))" "$OLD/stdout.log" \
    && pass "a relative AUDREY_SAVES_DIR is ignored with a note" || fail "relative AUDREY_SAVES_DIR"
mkdir -p "$SAVESD"

section "Log rotation never overwrites a log (L3 to L5)"
# The launcher's stub app here (only the launcher is under test; keeps the runs short).
STUB=/tmp/stub-app
printf '#!/bin/bash\ncase "${1:-}" in --version) echo "stub 1.0"; exit 0 ;; esac\nexit 0\n' > "$STUB"
PS="/userdata/ports same"
stubpkg() { # <ports folder>
    rm -rf "$1"; mkdir -p "$1/audreybrick" && cp /ab/pkg/AudreyBrick.sh "$1/" && cp "$STUB" "$1/audreybrick/audreybrick" && chmod +x "$1/audreybrick/audreybrick"
}
launch_s() { # <ports folder> [env ...]
    local pf="$1"; shift
    ( cd / && env AUDREY_KEEP_AWAKE=0 "$@" /bin/bash "$pf/AudreyBrick.sh" ) > /dev/null 2>&1
}
# L3: the archive names are taken.
stubpkg "$PS"; rm -rf "$N"; mkdir -p "$N"
printf '41\n' > "$N/run-counter"; printf '000041-19700616-070532\n' > "$N/latest-run"
printf 'CURRENT app log\n' > "$N/audreybrick.log"; printf 'CURRENT stdout\n' > "$N/stdout.log"
printf 'ARCHIVED app log\n' > "$N/audreybrick-000041-19700616-070532.log"; printf 'ARCHIVED stdout\n' > "$N/stdout-000041-19700616-070532.log"
launch_s "$PS"
[ "$(cat "$N/audreybrick-000041-19700616-070532.log")" = 'ARCHIVED app log' ] && [ "$(cat "$N/audreybrick-000041-19700616-070532-2.log")" = 'CURRENT app log' ] \
    && [ "$(cat "$N/stdout-000041-19700616-070532.log")" = 'ARCHIVED stdout' ] && [ "$(cat "$N/stdout-000041-19700616-070532-2.log")" = 'CURRENT stdout' ] \
    && grep -qF "launcher: log rotation: audreybrick-000041-19700616-070532.log exists already; the previous run's audreybrick.log is kept as audreybrick-000041-19700616-070532-2.log" "$N/stdout.log" \
    && pass "L3: log rotation with both archive names taken: nothing overwritten (the previous pair as -2), noted in stdout.log" \
    || fail "L3: $(ls -A "$N" | tr '\n' ' ')"
# L4: the previous audreybrick.log cannot be renamed at all (every free name taken):
# it is left alone, and the app writes this run's log under another name.
newpkg; rm -rf "$N"; mkdir -p "$N"
printf '41\n' > "$N/run-counter"; printf 'CURRENT app log, never emptied\n' > "$N/audreybrick.log"
: > "$N/audreybrick-previous-000041.log"; for i in $(seq 2 999); do : > "$N/audreybrick-previous-000041-$i.log"; done
launch_p AUDREY_ARGS="$QUICK" 2>/dev/null
rc=$?
applog="$(ls "$N" | grep '^audreybrick-000042-.*\.log$')"
[ "$rc" -eq 0 ] && [ "$(cat "$N/audreybrick.log")" = 'CURRENT app log, never emptied' ] && [ -n "$applog" ] && grep -q 'self-test: PASSED' "$N/$applog" \
    && grep -qF "launcher: PROBLEM: log rotation: this run's app log is $N/$applog" "$N/stdout.log" \
    && pass "L4: an audreybrick.log that cannot be archived is left as it is; the app logs to $applog (PROBLEM line)" \
    || fail "L4 (rc $rc, app log '$applog')"
# L5: a file appears under the chosen archive name between the launcher's choice and
# its mv (an mv on PATH creates it first): mv -n must not overwrite it.
RW=/tmp/racewrap
mkdir -p "$RW"
printf '#!/bin/bash\nfor t; do :; done\ncase "${t##*/}" in audreybrick-0*.log) printf "RACE: made in between\\n" > "$t" ;; esac\nexec /bin/mv "$@"\n' > "$RW/mv"
chmod +x "$RW/mv"
stubpkg "$PS"; rm -rf "$N"; mkdir -p "$N"
printf '41\n' > "$N/run-counter"; printf '000041-19700616-070532\n' > "$N/latest-run"
printf 'CURRENT app log\n' > "$N/audreybrick.log"; printf 'CURRENT stdout\n' > "$N/stdout.log"
launch_s "$PS" PATH="$RW:$PATH"
[ "$(cat "$N/audreybrick-000041-19700616-070532.log")" = 'RACE: made in between' ] && [ "$(cat "$N/audreybrick.log")" = 'CURRENT app log' ] \
    && [ "$(cat "$N/stdout-000041-19700616-070532.log")" = 'CURRENT stdout' ] \
    && grep -qF "launcher: PROBLEM: log rotation: $N/audreybrick.log could not be renamed to audreybrick-000041-19700616-070532.log or a free name like it" "$N/stdout.log" \
    && grep -qF "launcher: PROBLEM: log rotation: this run's app log is $N/audreybrick-000042-" "$N/stdout.log" \
    && pass "L5: a file made under the archive name just before the mv is not overwritten (mv -n); the old audreybrick.log is left as it is, PROBLEM lines" \
    || fail "L5: $(for f in "$N"/audreybrick*.log; do printf '%s=[%s] ' "${f##*/}" "$(cat "$f")"; done)"
rm -rf "$RW" "$PS" "$N"

section "PRESET-C: data folders that cannot be used: the app folder's data/, nothing lost"
# /userdata/saves not writable (and no audreybrick in it): the app folder's data/.
newpkg; make_old
snap "$OLD" /tmp/before.txt
chmod a-w "$SAVESD"
launch_p AUDREY_ARGS="$QUICK"
rc=$?
chmod u+w "$SAVESD"
grep -E '^launcher: (data folder|old data folder)' "$OLD/stdout.log" | sed 's/^/  /'
[ "$rc" -eq 0 ] && grep -qF "launcher: data folder: $OLD (the app folder's data/: $SAVESD is not writable)" "$OLD/stdout.log" \
    && ! grep -q 'old data folder' "$OLD/stdout.log" && [ ! -e "$N" ] \
    && pass "$SAVESD not writable: the app folder's data/ is used (no notice)" || fail "unwritable $SAVESD (rc $rc)"
snap "$OLD" /tmp/after.txt
nothing_lost /tmp/before.txt /tmp/after.txt && [ "$(cat "$OLD/run-counter")" = 42 ] && grep -qF "input: source OFF at start; folder $OLD/input" "$OLD/audreybrick.log" \
    && pass "the old data/ is used as before (run 42, the app's folders there)" || fail "old data/ after the fallback"
# /userdata/saves/audreybrick exists as a file, then as a folder that is not writable.
for kind in file folder; do
    newpkg; make_old
    snap "$OLD" /tmp/before.txt
    if [ "$kind" = file ]; then printf 'not a folder\n' > "$N"; else mkdir -p "$N"; chmod a-w "$N"; fi
    launch_p AUDREY_ARGS="$QUICK" 2> /tmp/presetc-stderr.txt
    rc=$?
    [ "$kind" = folder ] && chmod u+w "$N"
    snap "$OLD" /tmp/after.txt
    [ "$rc" -eq 0 ] && grep -qF "launcher: PROBLEM: data folder: $N exists but is not a writable folder; using $OLD instead" "$OLD/stdout.log" \
        && grep -qF "exists but is not a writable folder" /tmp/presetc-stderr.txt && nothing_lost /tmp/before.txt /tmp/after.txt \
        && pass "$N is a $( [ "$kind" = file ] && echo file || echo 'folder without write permission'): PROBLEM in stdout.log and on stderr, the app folder's data/ used" \
        || fail "$N as a $kind (rc $rc)"
    if [ "$kind" = file ]; then [ "$(cat "$N")" = 'not a folder' ] && pass "the file $N is untouched" || fail "the file $N was changed"; fi
    rm -rf "$N"
done

section "PRESET-C: how the data folder choice is reported"
# /userdata/saves/audreybrick is a link: refused, with a PROBLEM line.
mkdir -p /tmp/real-saves-target
newpkg; mkdir -p "$OLD"; printf 'x\n' > "$OLD/x.txt"
ln -s /tmp/real-saves-target "$N"
launch_p AUDREY_ARGS="$QUICK" 2> /tmp/presetc-stderr.txt
grep -qF "launcher: PROBLEM: data folder: $N is a link (a link is never used as the data folder); using $OLD instead" "$OLD/stdout.log" \
    && [ -z "$(ls -A /tmp/real-saves-target)" ] && [ "$(cat "$OLD/x.txt")" = x ] \
    && pass "saves/audreybrick as a link: refused (PROBLEM), nothing written through it, the app folder's data/ used" || fail "saves/audreybrick as a link"
rm -f "$N"; rm -rf /tmp/real-saves-target
# The choice flips for one start (here: AUDREY_SAVES_DIR points elsewhere) while
# /userdata/saves/audreybrick exists: a PROBLEM line, not just a note.
mkdir -p "$N"
newpkg
launch_p AUDREY_SAVES_DIR=/tmp/no-such-saves AUDREY_ARGS="$QUICK" 2> /tmp/presetc-stderr.txt
grep -qF "launcher: PROBLEM: data folder: $OLD is used for this start (the app folder's data/: /tmp/no-such-saves does not exist (AUDREY_SAVES_DIR, testing aid)), although $SAVESD/audreybrick exists: what this run saves is in $OLD and is not moved automatically (move it to $SAVESD/audreybrick by hand)" "$OLD/stdout.log" \
    && grep -q 'although /userdata/saves/audreybrick exists' /tmp/presetc-stderr.txt \
    && pass "the app folder's data/ used although $N exists: a PROBLEM line in stdout.log and on stderr (not moved automatically)" || fail "flipped data folder not reported as a PROBLEM"
rm -rf "$N"
# AUDREY_SAVES_DIR inside the app folder: ignored (the normal folder is used); the
# old data/ there is then left untouched, with the notice.
newpkg; mkdir -p "$OLD/recordings"; printf 'r1\n' > "$OLD/recordings/rec-0001.wav"
launch_p AUDREY_SAVES_DIR="$OLD" AUDREY_ARGS="$QUICK" 2>/dev/null
grep -qF "launcher: data folder: $N (created in $SAVESD (AUDREY_SAVES_DIR '$OLD' is inside the app folder; ignored))" "$N/stdout.log" \
    && [ "$(cat "$OLD/recordings/rec-0001.wav")" = r1 ] && [ ! -e "$N/recordings/rec-0001.wav" ] && grep -qxF "$NOTICE" "$N/stdout.log" \
    && pass "AUDREY_SAVES_DIR inside the app folder: ignored with a note; the old data/ left untouched, with the notice" || fail "AUDREY_SAVES_DIR inside the app folder"
rm -rf "$N" "$P"
rmdir "$SAVESD"
# /userdata/saves is a file.
printf 'not a folder\n' > "$SAVESD"
newpkg
launch_p AUDREY_ARGS="$QUICK"
grep -qF "launcher: data folder: $OLD (the app folder's data/: $SAVESD is not a folder)" "$OLD/stdout.log" && [ "$(cat "$SAVESD")" = 'not a folder' ] \
    && pass "/userdata/saves as a file: reported as 'is not a folder' (not 'does not exist'); the file untouched" || fail "/userdata/saves as a file"
rm -f "$SAVESD"; rm -rf "$P"

section "The launcher touched only its own data/ files"
ls -lR "$PORTS" | sed 's/^/  /'
cp -r "$PORTS" /ab/out/package-after-tests
pgrep -f audreybrick > /dev/null && fail "an audreybrick process is still running" || pass "no audreybrick process left"

echo
echo "REMINDER: the display path (Mali, EGL, renderer) and ALSA output were NOT exercised here."
if [ "$fails" -eq 0 ]; then echo "rootfs tests: OK"; exit 0; fi
echo "rootfs tests: $fails FAILED"
exit 1
