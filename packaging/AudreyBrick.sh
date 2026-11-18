#!/bin/bash
# Copyright (c) 2026 KhasMek
# SPDX-License-Identifier: MIT
#
# AudreyBrick launcher for KNULLI (TrimUI Brick). Copy this file and the folder
# "audreybrick" next to it into the Ports folder of the SD card (roms/ports).
# KNULLI starts it as: /bin/bash /userdata/roms/ports/AudreyBrick.sh
#
# Self-contained: no PortMaster, no control.txt, no gptokeyb. It
#   - finds its own folder (works from any working directory, with spaces in paths);
#   - keeps SDL_GAMECONTROLLERCONFIG as KNULLI set it, and sets no SDL variable;
#   - points LD_LIBRARY_PATH at audreybrick/libs.aarch64 (libaudrey_lgpl.so);
#   - writes the logs of the current run to audreybrick/data/audreybrick.log (the
#     app's diagnostic log) and audreybrick/data/stdout.log (this launcher's notes,
#     the device state at start and everything the app prints);
#   - keeps the last 8 runs: at start, the previous run's pair is renamed to
#     audreybrick-NNNNNN-YYYYMMDD-HHMMSS.log and stdout-NNNNNN-YYYYMMDD-HHMMSS.log
#     (NNNNNN = run number from data/run-counter, so the names stay unique and in
#     order even when the clock is not set) and the oldest numbered pairs beyond 7
#     are deleted. Only files with exactly those name patterns in its own data/
#     folder are ever renamed or deleted;
#   - keeps the device awake while the app runs (see AUDREY_KEEP_AWAKE);
#   - runs the app in the background, forwards TERM, INT and HUP to it, waits
#     until it has really exited, and returns its exit status. The app is also
#     started with --exit-with-parent, so it quits even if this script is killed
#     with SIGKILL (KNULLI's launcher does that on SIGINT).
#
# Settings (environment variables, or edit the defaults below):
#   AUDREY_BUFFER      audio buffer in frames, 16 to 32768 (default 256). Anything
#                      else: 256 is used and stdout.log says so.
#   AUDREY_KEEP_AWAKE  1 (default): while the app runs, a file
#                      /var/run/battery-saver/audreybrick.pause exists; KNULLI's
#                      idle watcher skips dimming, muting and suspend while any
#                      *.pause file is there. 0: not done (the system may dim and
#                      mute after its idle time).
#   AUDREY_ARGS        a testing aid: extra arguments appended to the app's command
#                      line, split on spaces (e.g. "--selftest <script> --seconds 5
#                      --no-video"). Normally unset.
#
# Executable bit: FAT/exFAT cards, or copying through some tools, can lose it. If
# audreybrick/audreybrick is executable it is started directly; otherwise through
# the system's dynamic loader (/lib/ld-linux-aarch64.so.1), which only needs to read
# the file. Both ways find libs.aarch64 (rpath $ORIGIN/libs.aarch64 and LD_LIBRARY_PATH).
set -u

AUDREY_BUFFER="${AUDREY_BUFFER:-256}"
AUDREY_KEEP_AWAKE="${AUDREY_KEEP_AWAKE:-1}"
KEEP_RUNS=8
PAUSE_DIR="/var/run/battery-saver"
PAUSE_FILE="$PAUSE_DIR/audreybrick.pause"
LOADER="/lib/ld-linux-aarch64.so.1"

OUT=""          # stdout.log of this run, once known
PAUSE_MADE=0    # 1 if this run created PAUSE_FILE
child=""        # the app's process id while it runs

# A note for stdout.log (and stderr for problems).
note() { [ -n "$OUT" ] && echo "launcher: $*" >> "$OUT" 2>/dev/null; }
problem() {
    echo "AudreyBrick: $*" >&2
    [ -n "$OUT" ] && echo "launcher: PROBLEM: $*" >> "$OUT" 2>/dev/null
}
have() { command -v "$1" > /dev/null 2>&1; }

cleanup() {
    if [ "$PAUSE_MADE" -eq 1 ]; then
        if [ -e "$PAUSE_FILE" ]; then
            rm -f "$PAUSE_FILE" 2>/dev/null && note "keep-awake: removed $PAUSE_FILE"
        else
            note "keep-awake: $PAUSE_FILE already removed by the app"
        fi
        PAUSE_MADE=0
    fi
}
forward() { # <signal> <exit status if the app has not started yet>
    if [ -z "$child" ]; then
        note "received SIG$1 before the app started; stopping"
        exit "$2" # the EXIT trap removes the pause file
    fi
    note "received SIG$1; passing it to the app"
    kill "-$1" "$child" 2>/dev/null
}
trap cleanup EXIT
trap 'forward TERM 143' TERM
trap 'forward INT 130' INT
trap 'forward HUP 129' HUP

# --- Folders -----------------------------------------------------------------------
script="${BASH_SOURCE[0]}"
case "$script" in
    /*) ;;
    *) script="$PWD/$script" ;;
esac
PORT_DIR="$(cd "${script%/*}" && pwd -P)" || { problem "cannot find the launcher's folder"; exit 1; }
APP_DIR="$PORT_DIR/audreybrick"
BIN="$APP_DIR/audreybrick"
LIBS="$APP_DIR/libs.aarch64"
cd "$APP_DIR" 2>/dev/null || { problem "cannot enter $APP_DIR (copy the whole audreybrick folder next to AudreyBrick.sh)"; exit 1; }

# --- Logs: data/ (or /tmp/audreybrick-data if data/ cannot be written) ---------------
DATA="data"
fallback_reason=""
if ! mkdir -p "$DATA" 2>/dev/null || [ ! -w "$DATA" ]; then
    fallback_reason="$APP_DIR/data is not writable"
    DATA="/tmp/audreybrick-data"
    mkdir -p "$DATA" 2>/dev/null || { problem "cannot write logs to $APP_DIR/data or $DATA"; exit 1; }
fi
LOG="$DATA/audreybrick.log"

# Run number (6 digits) and time stamp of this run.
run=0
if [ -r "$DATA/run-counter" ]; then read -r run < "$DATA/run-counter" 2>/dev/null || run=0; fi
case "$run" in '' | *[!0-9]*) run=0 ;; esac
run=$(( 10#$run + 1 ))
echo "$run" > "$DATA/run-counter" 2>/dev/null
stamp="$(date '+%Y%m%d-%H%M%S' 2>/dev/null)" || stamp=""
[ -n "$stamp" ] || stamp="00000000-000000"
id="$(printf '%06d-%s' "$run" "$stamp")"

# Archive the previous run's pair under its own id (from data/latest-run), then prune.
prev=""
if [ -r "$DATA/latest-run" ]; then read -r prev < "$DATA/latest-run" 2>/dev/null || prev=""; fi
case "$prev" in [0-9][0-9][0-9][0-9][0-9][0-9]-[0-9]*) ;; *) prev="previous-$(printf '%06d' $((run - 1)))" ;; esac
[ -f "$LOG" ] && mv -f "$LOG" "$DATA/audreybrick-$prev.log"
[ -f "$DATA/stdout.log" ] && mv -f "$DATA/stdout.log" "$DATA/stdout-$prev.log"
echo "$id" > "$DATA/latest-run" 2>/dev/null
for kind in audreybrick stdout; do
    files=( "$DATA/$kind"-[0-9][0-9][0-9][0-9][0-9][0-9]-*.log "$DATA/$kind"-previous-[0-9][0-9][0-9][0-9][0-9][0-9].log )
    kept=()
    for f in "${files[@]}"; do [ -f "$f" ] && kept+=( "$f" ); done
    # Glob order is by run number (zero-padded); keep the newest KEEP_RUNS - 1.
    excess=$(( ${#kept[@]} - (KEEP_RUNS - 1) ))
    i=0
    while [ "$i" -lt "$excess" ]; do rm -f "${kept[$i]}"; i=$((i + 1)); done
done

OUT="$DATA/stdout.log"
: > "$OUT" 2>/dev/null || OUT=""
note "AudreyBrick run $run, $(date '+%Y-%m-%d %H:%M:%S %z' 2>/dev/null || echo 'date unknown')"
note "folder $APP_DIR"
[ -n "$fallback_reason" ] && problem "$fallback_reason; logs for this run are in $DATA (lost on reboot)"

# --- Checks ---------------------------------------------------------------------------
if [ ! -f "$BIN" ]; then
    problem "$BIN is missing; copy the whole audreybrick folder next to AudreyBrick.sh"
    exit 1
fi
if [ -x "$BIN" ]; then
    launch=("$BIN")
    how="directly"
elif [ -r "$LOADER" ]; then
    launch=("$LOADER" "$BIN")
    how="through $LOADER (the executable bit is missing)"
else
    problem "$BIN is not executable and $LOADER was not found"
    exit 1
fi

buffer="$AUDREY_BUFFER"
case "$buffer" in
    '' | *[!0-9]* | ??????*) buffer_ok=0 ;;
    *) if [ $((10#$buffer)) -ge 16 ] && [ $((10#$buffer)) -le 32768 ]; then buffer_ok=1; else buffer_ok=0; fi ;;
esac
if [ "$buffer_ok" -eq 1 ]; then
    buffer=$((10#$buffer))
else
    note "AUDREY_BUFFER '$AUDREY_BUFFER' is not a whole number from 16 to 32768; using 256"
    buffer=256
fi

export LD_LIBRARY_PATH="$LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
extra=()
if [ -n "${AUDREY_ARGS:-}" ]; then
    read -r -a extra <<< "$AUDREY_ARGS"
fi

# --- Device state (each item guarded; about a second at most) ----------------------------
TO=""
have timeout && TO="timeout"
t0="$(date +%s%N 2>/dev/null)"
{
    echo "launcher: --- device state ---"
    echo "launcher: kernel $(uname -r 2>/dev/null || echo '(uname missing)')"
    nice_now="$(nice 2>/dev/null)" || nice_now="(nice missing)"
    echo "launcher: nice value $nice_now"
    [ -z "$TO" ] && echo "launcher: 'timeout' not found: commands that could block are skipped"
    if have knulli-settings-get; then
        for k in system.batterysaver.mode system.batterysaver.mute system.batterysaver.aggressive \
                 system.batterysaver.extendedmode system.idlewatcher.timer system.idlewatcher.extendedtimer; do
            v="$(${TO:+$TO 0.3} knulli-settings-get "$k" 2>/dev/null)" || v="(not set)"
            echo "launcher: setting $k=$v"
        done
    else
        echo "launcher: knulli-settings-get not found"
    fi
    if compgen -G "$PAUSE_DIR/*.pause" > /dev/null 2>&1; then
        echo "launcher: pause files present before start: $(cd "$PAUSE_DIR" && echo *.pause)"
    else
        echo "launcher: no $PAUSE_DIR/*.pause before start"
    fi
    cf=/sys/devices/system/cpu/cpu0/cpufreq
    if [ -r "$cf/scaling_governor" ]; then
        echo "launcher: cpu0 governor $(cat "$cf/scaling_governor" 2>/dev/null), cur $(cat "$cf/scaling_cur_freq" 2>/dev/null) kHz, min $(cat "$cf/scaling_min_freq" 2>/dev/null) kHz, max $(cat "$cf/scaling_max_freq" 2>/dev/null) kHz"
    else
        echo "launcher: $cf not readable"
    fi
    if have df; then echo "launcher: free space on the log folder: $(df -h "$DATA" 2>/dev/null | tail -n 1)"; else echo "launcher: df not found"; fi
    if [ -z "$TO" ]; then
        echo "launcher: audio server state skipped (no timeout)"
    elif have pactl; then
        $TO 0.4 pactl info 2>&1 | grep -E 'Server Name|Default Sink|Default Source|Default Sample|Connection failure' | head -n 5 | sed 's/^/launcher: pactl: /'
    elif have wpctl; then
        $TO 0.4 wpctl status 2>&1 | grep -E '\*|Error|error' | head -n 6 | sed 's/^/launcher: wpctl: /'
    else
        echo "launcher: pactl and wpctl not found"
    fi
    for tool in aplay arecord; do
        if [ -z "$TO" ]; then echo "launcher: $tool skipped (no timeout)"
        elif have "$tool"; then $TO 0.3 "$tool" -l 2>&1 | head -n 12 | sed "s/^/launcher: $tool -l: /"
        else echo "launcher: $tool not found"; fi
    done
    t1="$(date +%s%N 2>/dev/null)"
    case "$t0$t1" in *[!0-9]*) ;; ?*) echo "launcher: device state took $(( (t1 - t0) / 1000000 )) ms" ;; esac
    echo "launcher: --- end of device state ---"
} >> "${OUT:-/dev/null}" 2>&1

# --- Keep awake ---------------------------------------------------------------------------
if [ "$AUDREY_KEEP_AWAKE" = "0" ]; then
    note "keep-awake: off (AUDREY_KEEP_AWAKE=0); the system may dim and mute after its idle time"
elif mkdir -p "$PAUSE_DIR" 2>/dev/null && : > "$PAUSE_FILE" 2>/dev/null; then
    PAUSE_MADE=1
    note "keep-awake: created $PAUSE_FILE (the idle watcher skips dim, mute and suspend while it exists)"
else
    note "keep-awake: could not create $PAUSE_FILE; the system may dim and mute after its idle time"
fi

# --- Run ------------------------------------------------------------------------------------
if [ -n "${SDL_GAMECONTROLLERCONFIG:-}" ]; then
    note "SDL_GAMECONTROLLERCONFIG is set (${#SDL_GAMECONTROLLERCONFIG} characters)"
else
    note "SDL_GAMECONTROLLERCONFIG is not set"
fi
[ "${#extra[@]}" -gt 0 ] && note "extra arguments: ${extra[*]}"
note "starting the app $how, buffer $buffer frames"

# If this script is killed with SIGKILL its EXIT trap does not run; the app then
# removes the pause file itself (--remove-on-exit).
remove=()
[ "$PAUSE_MADE" -eq 1 ] && remove=(--remove-on-exit "$PAUSE_FILE")
"${launch[@]}" --fullscreen --exit-with-parent --buffer "$buffer" --log "$LOG" ${remove[@]+"${remove[@]}"} ${extra[@]+"${extra[@]}"} \
    >> "${OUT:-/dev/null}" 2>&1 &
child=$!
# wait returns early (status > 128) when a trapped signal arrives; then, if the app
# is still there (kill -0 also succeeds for an exited, not yet reaped app), wait
# again until it has really exited; the last wait returns its status.
while :; do
    wait "$child"
    status=$?
    if [ "$status" -gt 128 ] && kill -0 "$child" 2>/dev/null; then
        continue
    fi
    break
done
child=""
note "the app exited with status $status"
cleanup
sync 2>/dev/null
exit "$status"
