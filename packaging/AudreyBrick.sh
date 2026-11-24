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
#   - chooses the DATA FOLDER, which holds everything the user owns (logs, run
#     counter, input/, recordings/, presets/; PRESET-C):
#       1. /userdata/saves/audreybrick (see AUDREY_SAVES_DIR), if it already is a
#          writable folder, or if /userdata/saves is a writable folder and
#          "audreybrick" can be created in it with mkdir. KNULLI creates
#          /userdata/saves, and replacing the app folder on an update leaves it
#          alone. /userdata/saves may be a link (it is followed); saves/audreybrick
#          itself is never used if it is a link (a PROBLEM line);
#       2. otherwise audreybrick/data inside the app folder (other firmwares, tests);
#       3. for the logs only, if that cannot be written either: /tmp/audreybrick-data.
#     stdout.log says which one and why ("launcher: data folder: ..."). When (2) is
#     used although /userdata/saves/audreybrick exists, that is a PROBLEM line (and
#     on stderr): what the run saves is then in audreybrick/data (it is not moved
#     automatically; move it by hand);
#   - when the data folder is not audreybrick/data, and audreybrick/data is a
#     folder (not a link) holding anything (an older version kept its files
#     there): writes one notice line to stdout.log and one to stderr, saying that
#     its files are left untouched and can be moved by hand into the data folder
#     (PRESET-D). This is only a look (one glob in this shell): the launcher never
#     renames, moves, deletes or writes anything in audreybrick/data unless it is
#     the data folder in use;
#   - writes the logs of the current run to audreybrick.log (the app's diagnostic
#     log) and stdout.log (this launcher's notes, the device state at start and
#     everything the app prints) in the data folder;
#   - keeps the last 8 runs: at start, the previous run's pair is renamed to
#     audreybrick-NNNNNN-YYYYMMDD-HHMMSS.log and stdout-NNNNNN-YYYYMMDD-HHMMSS.log
#     (NNNNNN = run number from run-counter, so the names stay unique and in order
#     even when the clock is not set) and the oldest numbered pairs beyond 7 are
#     deleted. The rename never goes over an existing file: if that name is taken
#     (for example by a log copied in by hand), the first free one of
#     <name>-2.log, -3, ... is used. If the old audreybrick.log or stdout.log cannot
#     be renamed at all, it is left alone and this run's log of that kind is written
#     to a free audreybrick-<this run's id>.log (stdout-...) instead, with a PROBLEM
#     line, so the app never empties an old log. Only the two current logs are
#     renamed by this, and only files matching the patterns below deleted;
#   - creates input/ in the data folder (the INPUT tab's FILE source lists the WAV
#     files in it; M2b-A) with a plain mkdir -p, notes in stdout.log how many WAV
#     files it holds, and passes its path to the app (--input-dir). A folder that
#     cannot be created is not fatal: the app then shows no files;
#   - creates recordings/ in the data folder (Start + Y records the output there as
#     rec-0001.wav, rec-0002.wav, ...; REC) the same way, notes in stdout.log how
#     many recordings it holds, and passes its path to the app (--record-dir). A
#     folder that cannot be created is not fatal: the app tries again when a
#     recording starts and shows REC: CANNOT CREATE FILE if that fails too;
#   - creates presets/ in the data folder the same way, notes how many *.json
#     files it holds, and passes its path to the app (--preset-dir; PRESET-B and
#     PRESET-C; see PASS_PRESET_DIR below);
#   - keeps the device awake while the app runs (see AUDREY_KEEP_AWAKE);
#   - makes the A, B, X and Y buttons match their labels (see AUDREY_SWAP_AB and
#     AUDREY_SWAP_XY);
#   - optionally sets a CPU governor while the app runs (see AUDREY_GOVERNOR; off
#     by default);
#   - writes one line to stdout.log with the app's version and these settings as
#     resolved ("launcher: settings: ...");
#   - notes in stdout.log whether KNULLI's input helpers (evmapy, hotkeygen,
#     triggerhappy) are running for this launch (see "System hotkeys" below);
#   - runs the app in the background, forwards TERM, INT and HUP to it, waits
#     until it has really exited, and returns its exit status. The app is also
#     started with --exit-with-parent, so it quits even if this script is killed
#     with SIGKILL (KNULLI's launcher does that on SIGINT). In that case the app
#     itself removes the keep-awake file and restores the governor
#     (--remove-on-exit, --restore-on-exit); otherwise this script does it at exit
#     as well (the second restore finds nothing to do).
#
# System hotkeys (M3-C): the app quits on Menu + Start itself (it reads both
# buttons from the pad). KNULLI's soft reset (Menu + Start pressed twice quickly,
# triggerhappy -> knulli-softreset) ends the port with SIGTERM, which the app also
# handles. KNULLI's screenshot (Start pressed while Select is held, triggerhappy ->
# knulli-screenshot -> /userdata/screenshots) needs nothing from the port. No
# pad-to-keyboard (.keys) file is shipped, so KNULLI's evmapy does not run.
#
# The only files this script ever deletes, empties or overwrites: the keep-awake
# file it created (deleted at exit); only with AUDREY_GOVERNOR set, the
# scaling_governor files of the CPU frequency policies whose governor it changed
# (written back only while they still hold the governor this script set); and its
# own files in the data folder: run-counter and latest-run (rewritten at every
# start, only as regular files, never through a link) and the numbered logs
# (deleted only by the pruning below). The two current logs are only renamed as
# above, never over an existing file; stdout.log is only ever appended to, and the
# app's audreybrick.log is opened anew only under a name that did not exist. It
# also creates the data folder and its input/, recordings/ and presets/ if they are
# missing, and never touches what is in those three. Nothing in an old
# audreybrick/data that is not the data folder in use is ever renamed, moved,
# deleted or written.
# Log rotation deletes only files in the data folder itself whose names match
# audreybrick-NNNNNN-*.log, stdout-NNNNNN-*.log, audreybrick-previous-NNNNNN.log or
# stdout-previous-NNNNNN.log (N a digit). So a numbered log renamed by the rotation
# to a -2, -3 name (audreybrick-NNNNNN-...-2.log), or one that the one-time move of
# PRESET-C (since removed) brought in as audreybrick-NNNNNN-...-from-app-folder.log,
# counts among the numbered logs and is pruned with them, oldest first; one named
# ...-previous-NNNNNN-2.log or ...-previous-NNNNNN-from-app-folder.log is never
# pruned (delete it by hand).
#
# Signals: before the app has started, TERM, INT and HUP only set a flag; the
# script checks it at safe points and then stops through its normal exit (which
# undoes whatever it had changed). The script's own shell never runs a command
# substitution (see main() below: the device's bash 5.2.21 aborts if a trap runs
# while it parses one). Nothing slow runs between the first change to the system
# (the keep-awake file, a governor) and the app's start, so that window, after
# which the app shares the duty to undo the changes, is a few milliseconds.
#
# Settings (environment variables, or edit the defaults below):
#   AUDREY_BUFFER      audio buffer in frames, 16 to 32768 (default 256). Anything
#                      else: 256 is used and stdout.log says so.
#   AUDREY_LIVE_TARGET the live input's buffer, in audio buffers, 1 to 32 (default
#                      2; passed as --live-target-buffers). More: steadier with a
#                      device that delivers in large bursts, but a longer delay.
#                      Anything else: 2 is used and stdout.log says so.
#   AUDREY_KEEP_AWAKE  1 (default): while the app runs, a file
#                      /var/run/battery-saver/audreybrick.pause exists; KNULLI's
#                      idle watcher skips dimming, muting and suspend while any
#                      *.pause file is there. 0: not done (the system may dim and
#                      mute after its idle time).
#   AUDREY_SWAP_AB     1 (default): the app is started with --swap-ab, so the
#                      buttons labelled A and B do what the button table says.
#                      KNULLI's default controller mapping for ports names buttons
#                      by position, Xbox style, while the Brick is labelled
#                      Nintendo style, so without this A and B arrive the other way
#                      round (seen on the first device run). 0: SDL's own naming.
#   AUDREY_SWAP_XY     1 (default): likewise for X and Y (--swap-xy). Expected from
#                      the firmware's mapping (labelled X arrives as SDL 'y', Y as
#                      'x'); the next run's log confirms it. 0: SDL's own naming.
#                      For both: anything other than 0 or 1 counts as 1, with a
#                      note in stdout.log.
#                      KNULLI has its own per-port switch for this, in the port's
#                      game options: "SWITCH A/B and X/Y". When it is on, KNULLI
#                      creates /var/run/ports_xbox_layout.flag for the launch and
#                      the buttons already arrive with their labels' names; this
#                      script then turns BOTH of its swaps off for that run (so the
#                      two can never swap twice) and says so in stdout.log. Only a
#                      value set in the environment (AUDREY_SWAP_AB=... or
#                      AUDREY_SWAP_XY=...) overrides that automatic choice.
#   AUDREY_GOVERNOR    keep (default): the system's CPU governor is left alone.
#                      To run the CPU at full speed while the app plays, the
#                      recommended way is KNULLI's own option for the port: in its
#                      game options, POWER MODE: High Performance (KNULLI sets and
#                      restores the governor itself). The alternative is
#                      AUDREY_GOVERNOR=performance (or another governor the device
#                      offers): every CPU frequency policy
#                      (/sys/devices/system/cpu/cpufreq/policy*) that offers it is
#                      switched to it while the app runs, and the governor found
#                      there is written back when the app ends, unless something
#                      else has changed it meanwhile. A full-speed CPU costs
#                      battery. Neither is required: the first device run had no
#                      audio overruns at the default governor. A name a policy
#                      does not offer is skipped with a note; a value that is not a
#                      plain governor name counts as keep.
#   AUDREY_ARGS        a testing aid: extra arguments appended to the app's command
#                      line, split on spaces (e.g. "--selftest <script> --seconds 5
#                      --no-video"). Normally unset.
#   AUDREY_SYSFS_CPU   a testing aid only: the directory used in place of
#                      /sys/devices/system/cpu, by this script and by the app (which
#                      inherits it), so the governor handling can be tested against
#                      a fake tree. Normally unset.
#   AUDREY_SAVES_DIR   a testing aid only: the folder used in place of
#                      /userdata/saves (an absolute path, not inside the app
#                      folder; anything else is ignored with a note). Normally
#                      unset.
#
# Executable bit: FAT/exFAT cards, or copying through some tools, can lose it. If
# audreybrick/audreybrick is executable it is started directly; otherwise through
# the system's dynamic loader (/lib/ld-linux-aarch64.so.1), which only needs to read
# the file. Both ways find libs.aarch64 (rpath $ORIGIN/libs.aarch64 and LD_LIBRARY_PATH).
set -u

# --- Defaults (edit here) --------------------------------------------------------------
DEFAULT_SWAP_AB=1
DEFAULT_SWAP_XY=1
AUDREY_BUFFER="${AUDREY_BUFFER:-256}"
AUDREY_LIVE_TARGET="${AUDREY_LIVE_TARGET:-2}"
AUDREY_KEEP_AWAKE="${AUDREY_KEEP_AWAKE:-1}"
AUDREY_GOVERNOR="${AUDREY_GOVERNOR:-keep}"

# PRESET-B/C: 1 passes the presets folder to the app as --preset-dir (the app
# accepts it since PRESET-B); 0 would only create and count the folder.
PASS_PRESET_DIR=1

SYSFS_CPU="${AUDREY_SYSFS_CPU:-/sys/devices/system/cpu}"
SAVES_DEFAULT="/userdata/saves" # KNULLI's saves folder (made by its data initialisation)
XBOX_LAYOUT_FLAG="/var/run/ports_xbox_layout.flag" # KNULLI's ports_hooks.sh
KEEP_RUNS=8
PAUSE_DIR="/var/run/battery-saver"
PAUSE_FILE="$PAUSE_DIR/audreybrick.pause"
LOADER="/lib/ld-linux-aarch64.so.1"

OUT=""          # stdout.log of this run, once known
PAUSE_MADE=0    # 1 if this run created PAUSE_FILE
child=""        # the app's process id while it runs
STOP=""         # TERM, INT or HUP if one arrived before the app started
GOV_FILES=()    # scaling_governor files whose governor this run changed
GOV_PREV=()     # the governor each of them had before
GOV_SET=()      # the governor this run set there (as read back)

# A note for stdout.log (and stderr for problems).
note() { [ -n "$OUT" ] && echo "launcher: $*" >> "$OUT" 2>/dev/null; }
problem() {
    echo "AudreyBrick: $*" >&2
    [ -n "$OUT" ] && echo "launcher: PROBLEM: $*" >> "$OUT" 2>/dev/null
}
have() { command -v "$1" > /dev/null 2>&1; }

# Writes back the governor remembered for each policy this run changed, only
# while the policy still holds the governor this run set. The app may have done
# it already (--restore-on-exit). Never creates a file.
restore_governors() {
    local i f prev set cur now
    i=0
    while [ "$i" -lt "${#GOV_FILES[@]}" ]; do
        f="${GOV_FILES[$i]}"
        prev="${GOV_PREV[$i]}"
        set="${GOV_SET[$i]}"
        i=$((i + 1))
        cur=""
        if [ ! -e "$f" ]; then
            note "governor: $f no longer exists; nothing restored"
            continue
        fi
        read -r cur 2>/dev/null < "$f"
        if [ "$cur" = "$prev" ]; then
            note "governor: ${f%/scaling_governor}: already back to '$prev' (the app restores it too); nothing to do"
        elif [ "$cur" != "$set" ]; then
            note "governor: ${f%/scaling_governor}: changed by someone else to '$cur'; not restoring '$prev'"
        elif echo "$prev" 2>/dev/null > "$f"; then
            now=""
            read -r now 2>/dev/null < "$f"
            note "governor: ${f%/scaling_governor}: restored '$cur' -> '$now'"
        else
            note "governor: ${f%/scaling_governor}: could not write back '$prev' (still '$cur'; not fatal)"
        fi
    done
    GOV_FILES=()
    GOV_PREV=()
    GOV_SET=()
}

cleanup() {
    restore_governors
    if [ "$PAUSE_MADE" -eq 1 ]; then
        if [ -e "$PAUSE_FILE" ]; then
            rm -f "$PAUSE_FILE" 2>/dev/null && note "keep-awake: removed $PAUSE_FILE"
        else
            note "keep-awake: $PAUSE_FILE already removed by the app"
        fi
        PAUSE_MADE=0
    fi
}
# The signal traps never exit: before the app has started they only remember the
# signal (stop_point acts on it); afterwards they pass it to the app.
forward() { # <signal>
    if [ -z "$child" ]; then
        [ -n "$STOP" ] || STOP="$1"
        return
    fi
    note "received SIG$1; passing it to the app"
    kill "-$1" "$child" 2>/dev/null
}
# Called at safe points before the app starts: if a signal arrived, stop through
# the normal exit (the EXIT trap undoes whatever this run changed).
stop_point() { # <where>
    [ -n "$STOP" ] || return 0
    note "received SIG$STOP before the app started ($1); stopping"
    case "$STOP" in INT) exit 130 ;; HUP) exit 129 ;; *) exit 143 ;; esac
}

# --- Helpers for the log rotation ------------------------------------------------------
FREE_NAME=""
# The previous run's id in latest-run: run number, date, time (see printf -v id below).
ID_PAT='[0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9]'
# A file the launcher rewrites as its own (run-counter, latest-run): only a regular
# file or nothing; never through a link, never into anything else.
plain_or_none() { [ ! -L "$1" ] && { [ -f "$1" ] || [ ! -e "$1" ]; }; }

# free_name <folder> <name>: sets FREE_NAME to the first of <name>, <stem>-2<.ext>,
# <stem>-3<.ext>, ... -999 that does not exist in <folder> (not even as a dangling
# link) and is at most 255 bytes long. Fails if none. No command substitution.
free_name() {
    local dir="$1" name="$2" stem ext cand n=1 LC_ALL=C
    case "$name" in
        ?*.*) stem="${name%.*}"; ext=".${name##*.}" ;;
        *) stem="$name"; ext="" ;;
    esac
    while [ "$n" -le 999 ]; do
        cand="$stem$ext"
        [ "$n" -gt 1 ] && cand="$stem-$n$ext"
        n=$((n + 1))
        [ "${#cand}" -le 255 ] || continue # LC_ALL=C: bytes
        if [ ! -e "$dir/$cand" ] && [ ! -L "$dir/$cand" ]; then FREE_NAME="$cand"; return 0; fi
    done
    FREE_NAME=""
    return 1
}

# Everything below runs inside main(), called on the last line. The device's bash
# (5.2.21) aborts ("reader_loop: bad jump ... Aborting") if a trap runs while it
# is parsing a command substitution, and it parses a $(...) in double quotes again
# each time it expands it. So this shell itself never runs a command substitution:
# values are read with `cmd | read -r var` (lastpipe) or printf -v, and the
# device-state block, which needs many, runs in a subshell (the traps are not
# inherited there, and this shell runs a trap only after the subshell has ended).
main() {
    shopt -s lastpipe # the last command of a pipeline runs in this shell (read ... results stay)
    trap cleanup EXIT
    trap 'forward TERM' TERM
    trap 'forward INT' INT
    trap 'forward HUP' HUP

    # --- Folders -----------------------------------------------------------------------
    script="${BASH_SOURCE[0]}"
    case "$script" in
        /*) ;;
        *) script="$PWD/$script" ;;
    esac
    PORT_NAME="${script##*/}" # the name KNULLI's per-game settings use, e.g. AudreyBrick.sh
    cd -P "${script%/*}" 2>/dev/null || { problem "cannot find the launcher's folder"; exit 1; }
    PORT_DIR="$PWD"
    APP_DIR="$PORT_DIR/audreybrick"
    BIN="$APP_DIR/audreybrick"
    LIBS="$APP_DIR/libs.aarch64"
    cd -P "$APP_DIR" 2>/dev/null || { problem "cannot enter $APP_DIR (copy the whole audreybrick folder next to AudreyBrick.sh)"; exit 1; }
    APP_REAL="$PWD" # the app folder with links resolved

    # --- The data folder (PRESET-C): /userdata/saves/audreybrick, else data/ ------------
    SAVES="$SAVES_DEFAULT"
    saves_note=""
    if [ -n "${AUDREY_SAVES_DIR:-}" ]; then
        case "$AUDREY_SAVES_DIR" in
            /*)
                # Never inside the app folder (replacing the app folder would delete it).
                saves_real="$AUDREY_SAVES_DIR"
                if cd -P -- "$AUDREY_SAVES_DIR" 2>/dev/null; then saves_real="$PWD"; fi
                cd -P -- "$APP_DIR" 2>/dev/null || { problem "cannot enter $APP_DIR again"; exit 1; }
                # ("$saves_real/audreybrick" is the folder used: it must not be
                # the app folder itself either.)
                case "$saves_real/audreybrick/" in
                    "$APP_REAL"/* | "$APP_DIR"/*)
                        saves_note=" (AUDREY_SAVES_DIR '$AUDREY_SAVES_DIR' is inside the app folder; ignored)" ;;
                    *) SAVES="$AUDREY_SAVES_DIR"; saves_note=" (AUDREY_SAVES_DIR, testing aid)" ;;
                esac ;;
            *) saves_note=" (AUDREY_SAVES_DIR '$AUDREY_SAVES_DIR' is not an absolute path; ignored)" ;;
        esac
    fi
    SAVES_HOME="$SAVES/audreybrick"
    OLD_DATA="$APP_DIR/data"
    DATA_HOME=""
    data_rule=""
    data_problem=""
    if [ -L "$SAVES_HOME" ]; then
        data_problem="$SAVES_HOME is a link (a link is never used as the data folder); using $OLD_DATA instead$saves_note"
    elif [ -d "$SAVES_HOME" ] && [ -w "$SAVES_HOME" ]; then
        DATA_HOME="$SAVES_HOME"
        data_rule="it exists and is writable$saves_note"
    elif [ -e "$SAVES_HOME" ]; then
        data_problem="$SAVES_HOME exists but is not a writable folder; using $OLD_DATA instead$saves_note"
    elif [ ! -d "$SAVES" ]; then
        if [ -e "$SAVES" ] || [ -L "$SAVES" ]; then
            data_rule="$SAVES is not a folder$saves_note"
        else
            data_rule="$SAVES does not exist$saves_note"
        fi
    elif [ ! -w "$SAVES" ]; then
        data_rule="$SAVES is not writable$saves_note"
    elif mkdir -- "$SAVES_HOME" 2>/dev/null && [ -d "$SAVES_HOME" ] && [ -w "$SAVES_HOME" ]; then
        DATA_HOME="$SAVES_HOME"
        data_rule="created in $SAVES$saves_note"
    else
        data_problem="could not create $SAVES_HOME; using $OLD_DATA instead$saves_note"
    fi
    if [ -z "$DATA_HOME" ]; then
        DATA_HOME="$OLD_DATA"
        [ -n "$data_rule" ] && data_rule="the app folder's data/: $data_rule"
        # The choice flipped for this start although KNULLI's folder is there: say so loudly.
        if [ -z "$data_problem" ] && { [ -e "$SAVES_DEFAULT/audreybrick" ] || [ -L "$SAVES_DEFAULT/audreybrick" ]; }; then
            data_problem="$OLD_DATA is used for this start ($data_rule), although $SAVES_DEFAULT/audreybrick exists: what this run saves is in $OLD_DATA and is not moved automatically (move it to $SAVES_DEFAULT/audreybrick by hand)"
        fi
    fi
    # PRESET-D: an older version's data/ in the app folder is never moved, renamed,
    # deleted or written. If it is not the data folder in use and holds anything
    # (hidden names too), one notice says so after the logs are set up. Only a glob
    # in this shell (no process); a link is not followed (no notice).
    old_data_notice=0
    if [ "$DATA_HOME" != "$OLD_DATA" ] && [ -d "$OLD_DATA" ] && [ ! -L "$OLD_DATA" ]; then
        shopt -s nullglob dotglob
        for e in "$OLD_DATA"/*; do old_data_notice=1; break; done
        shopt -u nullglob dotglob
    fi

    # --- Logs: in the data folder (or /tmp/audreybrick-data if it cannot be written) -----
    DATA="$DATA_HOME"
    fallback_reason=""
    if ! mkdir -p "$DATA" 2>/dev/null || [ ! -w "$DATA" ]; then
        fallback_reason="$DATA_HOME is not writable"
        DATA="/tmp/audreybrick-data"
        mkdir -p "$DATA" 2>/dev/null || { problem "cannot write logs to $DATA_HOME or $DATA"; exit 1; }
    fi

    # Run number (6 digits) and time stamp of this run. run-counter and latest-run are
    # this script's own files: rewritten only as a regular file (or created), never
    # through a link.
    run=0
    if [ -f "$DATA/run-counter" ] && [ -r "$DATA/run-counter" ]; then read -r run < "$DATA/run-counter" 2>/dev/null || run=0; fi
    case "$run" in '' | *[!0-9]* | ??????????*) run=0 ;; esac
    run=$(( 10#$run + 1 ))
    plain_or_none "$DATA/run-counter" && echo "$run" > "$DATA/run-counter" 2>/dev/null
    stamp=""
    date '+%Y%m%d-%H%M%S' 2>/dev/null | read -r stamp # lastpipe: read runs in this shell
    case "$stamp" in [0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]-[0-9][0-9][0-9][0-9][0-9][0-9]) ;; *) stamp="00000000-000000" ;; esac
    printf -v id '%06d-%s' "$run" "$stamp"

    # Archive the previous run's pair under its own id (from latest-run), then prune.
    # Never over an existing file: if <kind>-<id>.log is taken (for example by a log
    # copied in by hand), the first free <kind>-<id>-2.log, -3, ... is used (mv -n
    # as well, in case a file of that name appears in between).
    rot_notes=()
    prev=""
    if [ -f "$DATA/latest-run" ] && [ -r "$DATA/latest-run" ]; then read -r prev < "$DATA/latest-run" 2>/dev/null || prev=""; fi
    case "$prev" in $ID_PAT) ;; *) printf -v prev 'previous-%06d' $((run - 1)) ;; esac
    for kind in audreybrick stdout; do
        f="$DATA/$kind.log"
        { [ -e "$f" ] || [ -L "$f" ]; } || continue
        if free_name "$DATA" "$kind-$prev.log" && mv -n -T -- "$f" "$DATA/$FREE_NAME" 2>/dev/null \
            && [ ! -e "$f" ] && [ ! -L "$f" ]; then
            [ "$FREE_NAME" = "$kind-$prev.log" ] \
                || rot_notes+=("log rotation: $kind-$prev.log exists already; the previous run's $kind.log is kept as $FREE_NAME")
        else
            rot_notes+=("PROBLEM: log rotation: $f could not be renamed to $kind-$prev.log or a free name like it; it is left as it is, and this run's $kind log gets another name")
        fi
    done
    # This run's two logs: their usual names, or, if the old ones are still there (the
    # app would empty audreybrick.log when it opens it), free names of this run's id.
    LOG="$DATA/audreybrick.log"
    if [ -e "$LOG" ] || [ -L "$LOG" ]; then
        if free_name "$DATA" "audreybrick-$id.log"; then LOG="$DATA/$FREE_NAME"; else LOG="/tmp/audreybrick-$id-$$.log"; fi
        rot_notes+=("PROBLEM: log rotation: this run's app log is $LOG")
    fi
    OUT="$DATA/stdout.log"
    if [ -e "$OUT" ] || [ -L "$OUT" ]; then
        if free_name "$DATA" "stdout-$id.log"; then OUT="$DATA/$FREE_NAME"; else OUT="/tmp/stdout-$id-$$.log"; fi
        rot_notes+=("PROBLEM: log rotation: this run's stdout log is $OUT")
    fi
    plain_or_none "$DATA/latest-run" && echo "$id" > "$DATA/latest-run" 2>/dev/null
    # Pruning: only files in the data folder whose names match these patterns.
    for kind in audreybrick stdout; do
        files=( "$DATA/$kind"-[0-9][0-9][0-9][0-9][0-9][0-9]-*.log "$DATA/$kind"-previous-[0-9][0-9][0-9][0-9][0-9][0-9].log )
        kept=()
        for f in "${files[@]}"; do [ -f "$f" ] && kept+=( "$f" ); done
        # Glob order is by run number (zero-padded); keep the newest KEEP_RUNS - 1.
        excess=$(( ${#kept[@]} - (KEEP_RUNS - 1) ))
        i=0
        while [ "$i" -lt "$excess" ]; do rm -f -- "${kept[$i]}"; i=$((i + 1)); done
    done

    # Appended to, never emptied (it does not exist at this point, see above).
    : >> "$OUT" 2>/dev/null || OUT=""
    when="date unknown"
    date '+%Y-%m-%d %H:%M:%S %z' 2>/dev/null | read -r when
    note "AudreyBrick run $run, ${when:-date unknown}"
    note "folder $APP_DIR"
    if [ -n "$data_problem" ]; then
        problem "data folder: $data_problem"
    else
        note "data folder: $DATA_HOME${data_rule:+ ($data_rule)}"
    fi
    if [ "$old_data_notice" -eq 1 ]; then
        note "old data folder: $OLD_DATA holds files from an older version; they are left untouched (nothing there is moved or changed automatically). Look there before replacing the app folder, which deletes it. To keep recordings/ or input/ files, move them by hand into the same folders in $DATA_HOME, without overwriting files of the same name"
        echo "AudreyBrick: files found in the old folder $OLD_DATA; left untouched; move them by hand into $DATA_HOME" >&2
    fi
    for line in ${rot_notes[@]+"${rot_notes[@]}"}; do
        case "$line" in "PROBLEM: "*) problem "${line#PROBLEM: }" ;; *) note "$line" ;; esac
    done
    [ -n "$fallback_reason" ] && problem "$fallback_reason; logs for this run are in $DATA (lost on reboot)"
    stop_point "after setting up the logs"

    # --- Input folder (M2b-A): input/ in the data folder ----------------------------------------
    # Always $DATA_HOME/input (also when the logs had to go to /tmp). No command
    # substitution: the WAV files are counted with a glob. Not fatal if it fails.
    INPUT_DIR="$DATA_HOME/input"
    if mkdir -p "$INPUT_DIR" 2>/dev/null && [ -d "$INPUT_DIR" ]; then
        n_wav=0
        # Counted as the app lists them: regular files only, no links; the glob
        # already skips hidden names (those starting with ".", also "._x.wav").
        for f in "$INPUT_DIR"/*.[wW][aA][vV]; do [ -f "$f" ] && [ ! -L "$f" ] && n_wav=$((n_wav + 1)); done
        note "input folder: $INPUT_DIR ($n_wav WAV file(s); the INPUT tab's FILE source lists them)"
    else
        note "input folder: could not create $INPUT_DIR; the INPUT tab will show no files (not fatal)"
    fi

    # --- Recordings folder (REC): recordings/ in the data folder ---------------------------------
    # Always $DATA_HOME/recordings. Counted with a glob (no command substitution).
    RECORD_DIR="$DATA_HOME/recordings"
    if mkdir -p "$RECORD_DIR" 2>/dev/null && [ -d "$RECORD_DIR" ]; then
        n_rec=0
        for f in "$RECORD_DIR"/rec-*.[wW][aA][vV]; do [ -f "$f" ] && n_rec=$((n_rec + 1)); done
        note "recordings folder: $RECORD_DIR ($n_rec recording(s); Start + Y records into it)"
    else
        note "recordings folder: could not create $RECORD_DIR; the app tries again when a recording starts (not fatal)"
    fi

    # --- Presets folder (PRESET-C): presets/ in the data folder ---------------------------------
    # Always $DATA_HOME/presets. Counted as the preset list shows them: regular
    # *.json files (any case), not hidden. Not fatal if it fails.
    PRESET_DIR="$DATA_HOME/presets"
    if mkdir -p "$PRESET_DIR" 2>/dev/null && [ -d "$PRESET_DIR" ]; then
        n_pre=0
        for f in "$PRESET_DIR"/*.[jJ][sS][oO][nN]; do [ -f "$f" ] && n_pre=$((n_pre + 1)); done
        note "presets folder: $PRESET_DIR ($n_pre preset file(s))"
    else
        note "presets folder: could not create $PRESET_DIR (not fatal)"
    fi

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

    live_target="$AUDREY_LIVE_TARGET"
    case "$live_target" in
        '' | *[!0-9]* | ???*) live_ok=0 ;;
        *) if [ $((10#$live_target)) -ge 1 ] && [ $((10#$live_target)) -le 32 ]; then live_ok=1; else live_ok=0; fi ;;
    esac
    if [ "$live_ok" -eq 1 ]; then
        live_target=$((10#$live_target))
    else
        note "AUDREY_LIVE_TARGET '$AUDREY_LIVE_TARGET' is not a whole number from 1 to 32; using 2"
        live_target=2
    fi

    case "$AUDREY_KEEP_AWAKE" in 0) keep_awake=0 ;; *) keep_awake=1 ;; esac

    # The swaps: an explicit environment value wins; otherwise KNULLI's own layout
    # switch (the flag file) turns both off; otherwise the defaults above.
    swap_value() { # <variable to set> <name> <value>: sets it to 0 or 1
        case "$3" in
            0 | 1) printf -v "$1" '%s' "$3" ;;
            *) note "$2 '$3' is not 0 or 1; using 1"; printf -v "$1" '%s' 1 ;;
        esac
    }
    xbox_flag=0
    [ -e "$XBOX_LAYOUT_FLAG" ] && xbox_flag=1
    if [ -n "${AUDREY_SWAP_AB:-}" ]; then
        swap_value swap_ab AUDREY_SWAP_AB "$AUDREY_SWAP_AB"; swap_ab_rule="set in the environment"
    elif [ "$xbox_flag" -eq 1 ]; then
        swap_ab=0; swap_ab_rule="off: KNULLI's SWITCH A/B and X/Y is on for this port ($XBOX_LAYOUT_FLAG exists)"
    else
        swap_value swap_ab DEFAULT_SWAP_AB "$DEFAULT_SWAP_AB"; swap_ab_rule="default"
    fi
    if [ -n "${AUDREY_SWAP_XY:-}" ]; then
        swap_value swap_xy AUDREY_SWAP_XY "$AUDREY_SWAP_XY"; swap_xy_rule="set in the environment"
    elif [ "$xbox_flag" -eq 1 ]; then
        swap_xy=0; swap_xy_rule="off: KNULLI's SWITCH A/B and X/Y is on for this port ($XBOX_LAYOUT_FLAG exists)"
    else
        swap_value swap_xy DEFAULT_SWAP_XY "$DEFAULT_SWAP_XY"; swap_xy_rule="default"
    fi

    governor="$AUDREY_GOVERNOR"
    case "$governor" in
        keep) ;;
        *[!a-z0-9_]* | ?????????????????????????????????*)
            note "AUDREY_GOVERNOR '$AUDREY_GOVERNOR' is not a governor name; using keep (the system's governor is left alone)"
            governor=keep ;;
    esac

    export LD_LIBRARY_PATH="$LIBS${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    extra=()
    if [ -n "${AUDREY_ARGS:-}" ]; then
        read -r -a extra <<< "$AUDREY_ARGS"
    fi

    TO=""
    have timeout && TO="timeout"
    appver=""
    ${TO:+$TO 3} "${launch[@]}" --version 2>/dev/null | head -n 1 | read -r appver # lastpipe
    [ -n "$appver" ] || appver="AudreyBrick (version unknown: --version printed nothing)"

    # --- Device state (each item guarded; about a second at most) ----------------------------
    # In a subshell: its command substitutions run in a child process, and a signal
    # to this script is handled only after the child has finished.
    (
        t0="$(date +%s%N 2>/dev/null)"
        echo "launcher: --- device state ---"
        echo "launcher: kernel $(uname -r 2>/dev/null || echo '(uname missing)')"
        nice_now="$(nice 2>/dev/null)" || nice_now="(nice missing)"
        echo "launcher: nice value $nice_now"
        [ -z "$TO" ] && echo "launcher: 'timeout' not found: commands that could block are skipped"
        if have knulli-settings-get; then
            for k in system.batterysaver.mode system.batterysaver.mute system.batterysaver.aggressive \
                     system.batterysaver.extendedmode system.idlewatcher.timer system.idlewatcher.extendedtimer \
                     "ports[\"$PORT_NAME\"].xbox_layout" ports.xbox_layout; do
                v="$(${TO:+$TO 0.3} knulli-settings-get "$k" 2>/dev/null)" || v="(not set)"
                echo "launcher: setting $k=${v:-(empty)}"
            done
        else
            echo "launcher: knulli-settings-get not found"
        fi
        # The keys KNULLI's powermode_launch_hooks.sh and knulli-power-mode read.
        if have knulli-settings-get-master; then
            for k in global.powermode global.batterymode system.cpu.governor \
                     "ports[\"$PORT_NAME\"].powermode" ports.powermode; do
                v="$(${TO:+$TO 0.3} knulli-settings-get-master "$k" 2>/dev/null)" || v="(not set)"
                echo "launcher: setting (master) $k=${v:-(empty)}"
            done
        else
            echo "launcher: knulli-settings-get-master not found"
        fi
        if [ -e "$XBOX_LAYOUT_FLAG" ]; then
            echo "launcher: $XBOX_LAYOUT_FLAG exists (KNULLI's SWITCH A/B and X/Y is on for this launch)"
        else
            echo "launcher: $XBOX_LAYOUT_FLAG does not exist (KNULLI's SWITCH A/B and X/Y is off)"
        fi
        # KNULLI's input helpers (M3-C): evmapy (pad to keyboard, started by KNULLI
        # only if it finds a <rom>.keys file), hotkeygen and triggerhappy (thd).
        if have pgrep; then
            for helper in evmapy hotkeygen; do
                pids="$(pgrep -f "bin/$helper" 2>/dev/null | tr '\n' ' ')"
                if [ -n "$pids" ]; then echo "launcher: input helper $helper: running (pid ${pids% })"
                else echo "launcher: input helper $helper: not running"; fi
            done
            pids="$(pgrep -x thd 2>/dev/null | tr '\n' ' ')"
            if [ -n "$pids" ]; then echo "launcher: input helper triggerhappy (thd): running (pid ${pids% })"
            else echo "launcher: input helper triggerhappy (thd): not running"; fi
        else
            echo "launcher: pgrep not found: input helpers not checked"
        fi
        if compgen -G "/var/run/evmapy/*.json" > /dev/null 2>&1; then
            for cfg in /var/run/evmapy/*.json; do
                echo "launcher: evmapy config ${cfg##*/}: $(grep -c '"trigger"' "$cfg" 2>/dev/null) action(s), keys: $(grep -o 'KEY_[A-Z0-9_]*' "$cfg" 2>/dev/null | sort -u | tr '\n' ' ')"
            done
        else
            echo "launcher: no /var/run/evmapy/*.json (evmapy has no pad configuration for this launch)"
        fi
        if compgen -G "$PAUSE_DIR/*.pause" > /dev/null 2>&1; then
            echo "launcher: pause files present before start: $(cd "$PAUSE_DIR" && echo *.pause)"
        else
            echo "launcher: no $PAUSE_DIR/*.pause before start"
        fi
        ps_found=0
        for ps in /sys/class/power_supply/*; do
            [ -d "$ps" ] || continue
            ps_found=1
            echo "launcher: power supply ${ps##*/}: type $(cat "$ps/type" 2>/dev/null), status $(cat "$ps/status" 2>/dev/null), capacity $(cat "$ps/capacity" 2>/dev/null), online $(cat "$ps/online" 2>/dev/null)"
        done
        [ "$ps_found" -eq 1 ] || echo "launcher: no /sys/class/power_supply entries"
        n_pol=0
        for cf in "$SYSFS_CPU"/cpufreq/policy*; do [ -d "$cf" ] && n_pol=$((n_pol + 1)); done
        n_cpu=0
        for cf in "$SYSFS_CPU"/cpu[0-9]*/cpufreq; do [ -d "$cf" ] && n_cpu=$((n_cpu + 1)); done
        echo "launcher: cpufreq layout: $n_pol policy director(ies) under $SYSFS_CPU/cpufreq, $n_cpu cpu*/cpufreq director(ies)"
        for cf in "$SYSFS_CPU"/cpufreq/policy*; do
            [ -d "$cf" ] || continue
            echo "launcher: ${cf##*/} governor $(cat "$cf/scaling_governor" 2>/dev/null), cur $(cat "$cf/scaling_cur_freq" 2>/dev/null) kHz, min $(cat "$cf/scaling_min_freq" 2>/dev/null) kHz, max $(cat "$cf/scaling_max_freq" 2>/dev/null) kHz, cpus $(cat "$cf/affected_cpus" 2>/dev/null), available governors: $(cat "$cf/scaling_available_governors" 2>/dev/null)"
        done
        if have df; then echo "launcher: free space on the log folder: $(df -h "$DATA" 2>/dev/null | tail -n 1)"; else echo "launcher: df not found"; fi
        # REC: the data folder (recordings need 200 MiB free to start; they stop below 100 MiB).
        if have df; then echo "launcher: free space on the data folder ($DATA_HOME): $(df -h "$DATA_HOME" 2>/dev/null | tail -n 1)"; fi
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
        # M2b-B: the capture side's names, so a USB interface's PipeWire and ALSA
        # names are in the log next to what SDL lists.
        if [ -z "$TO" ]; then echo "launcher: pactl list short sources skipped (no timeout)"
        elif have pactl; then
            echo "launcher: pactl list short sources (PipeWire's capture sources, at most 12 lines):"
            $TO 0.4 pactl list short sources 2>&1 | head -n 12 | sed 's/^/launcher: pactl list short sources: /'
        else echo "launcher: pactl not found (no PipeWire source list)"; fi
        if [ -z "$TO" ]; then echo "launcher: arecord -L skipped (no timeout)"
        elif have arecord; then
            echo "launcher: arecord -L (ALSA's capture PCMs, at most 30 lines):"
            $TO 0.3 arecord -L 2>&1 | head -n 30 | sed 's/^/launcher: arecord -L: /'
        else echo "launcher: arecord not found (no ALSA capture list)"; fi
        t1="$(date +%s%N 2>/dev/null)"
        case "$t0$t1" in *[!0-9]*) ;; ?*) echo "launcher: device state took $(( (t1 - t0) / 1000000 )) ms" ;; esac
        echo "launcher: --- end of device state ---"
    ) >> "${OUT:-/dev/null}" 2>&1

    if [ -n "${SDL_GAMECONTROLLERCONFIG:-}" ]; then
        note "SDL_GAMECONTROLLERCONFIG is set (${#SDL_GAMECONTROLLERCONFIG} characters)"
    else
        note "SDL_GAMECONTROLLERCONFIG is not set"
    fi
    [ "${#extra[@]}" -gt 0 ] && note "extra arguments: ${extra[*]}"
    note "buttons: A/B swap $swap_ab ($swap_ab_rule); X/Y swap $swap_xy ($swap_xy_rule)"
    note "settings: $appver; AUDREY_BUFFER=$buffer AUDREY_LIVE_TARGET=$live_target AUDREY_KEEP_AWAKE=$keep_awake AUDREY_SWAP_AB=$swap_ab AUDREY_SWAP_XY=$swap_xy AUDREY_GOVERNOR=$governor${AUDREY_SYSFS_CPU:+ AUDREY_SYSFS_CPU=$AUDREY_SYSFS_CPU (testing aid)}"
    stop_point "after the device-state block"

    # =====================================================================================
    # From here until the app has started: no command substitution, no slow command.
    # =====================================================================================
    stop_point "before changing the system"

    # --- Keep awake ---------------------------------------------------------------------------
    if [ "$keep_awake" -eq 0 ]; then
        note "keep-awake: off (AUDREY_KEEP_AWAKE=0); the system may dim and mute after its idle time"
    elif mkdir -p "$PAUSE_DIR" 2>/dev/null && : > "$PAUSE_FILE" 2>/dev/null; then
        PAUSE_MADE=1
        note "keep-awake: created $PAUSE_FILE (the idle watcher skips dim, mute and suspend while it exists)"
    else
        note "keep-awake: could not create $PAUSE_FILE; the system may dim and mute after its idle time"
    fi

    # --- CPU governor (only with AUDREY_GOVERNOR set) -----------------------------------------
    # As KNULLI's own scripts do (/usr/bin/knulli-power-mode, /etc/init.d/S18governor):
    # every policy*/scaling_governor, only to a governor listed in
    # scaling_available_governors. Each file is remembered BEFORE it is written, so a
    # signal arriving in between still restores it.
    if [ "$governor" = keep ]; then
        note "governor: left alone (AUDREY_GOVERNOR=keep)"
    else
        gov_found=0
        for p in "$SYSFS_CPU"/cpufreq/policy*; do
            [ -e "$p/scaling_governor" ] || continue
            gov_found=1
            name="${p##*/}"
            cur=""
            read -r cur 2>/dev/null < "$p/scaling_governor"
            avail=""
            read -r avail 2>/dev/null < "$p/scaling_available_governors"
            case "$cur" in
                '' | *[!a-z0-9_]* | ?????????????????????????????????*)
                    note "governor: $name: current governor '$cur' cannot be read as a governor name; left alone (it could not be restored)"
                    continue ;;
            esac
            case " $avail " in
                *" $governor "*) ;;
                *) note "governor: $name: '$governor' is not in scaling_available_governors ('$avail'); skipped, left at '$cur'"
                   continue ;;
            esac
            if [ "$cur" = "$governor" ]; then
                note "governor: $name: already '$governor'; nothing changed (nothing to restore)"
                continue
            fi
            GOV_FILES+=("$p/scaling_governor")
            GOV_PREV+=("$cur")
            GOV_SET+=("$governor")
            last=$(( ${#GOV_FILES[@]} - 1 ))
            if echo "$governor" 2>/dev/null > "$p/scaling_governor"; then
                now=""
                read -r now 2>/dev/null < "$p/scaling_governor"
                [ -n "$now" ] && GOV_SET[$last]="$now"
                note "governor: $name: '$cur' -> '$now' (restored when the app ends)"
            else
                note "governor: $name: could not write '$governor' (no permission or read-only); left at '$cur'"
                unset "GOV_FILES[$last]" "GOV_PREV[$last]" "GOV_SET[$last]"
            fi
        done
        [ "$gov_found" -eq 1 ] || note "governor: no cpufreq policy under $SYSFS_CPU/cpufreq; nothing changed"
    fi

    stop_point "before starting the app"

    # --- Run ------------------------------------------------------------------------------------
    # If this script is killed with SIGKILL its EXIT trap does not run; the app then
    # removes the pause file itself (--remove-on-exit) and restores the governors
    # (--restore-on-exit <file> <found> <set>, one per policy).
    args=(--fullscreen --exit-with-parent --buffer "$buffer" --live-target-buffers "$live_target" --log "$LOG" --input-dir "$INPUT_DIR" --record-dir "$RECORD_DIR")
    # PRESET-B/C: the presets folder (PASS_PRESET_DIR near the top).
    [ "$PASS_PRESET_DIR" -eq 1 ] && args+=(--preset-dir "$PRESET_DIR")
    [ "$swap_ab" -eq 1 ] && args+=(--swap-ab)
    [ "$swap_xy" -eq 1 ] && args+=(--swap-xy)
    [ "$PAUSE_MADE" -eq 1 ] && args+=(--remove-on-exit "$PAUSE_FILE")
    i=0
    while [ "$i" -lt "${#GOV_FILES[@]}" ]; do
        args+=(--restore-on-exit "${GOV_FILES[$i]}" "${GOV_PREV[$i]}" "${GOV_SET[$i]}")
        i=$((i + 1))
    done
    note "starting the app $how, buffer $buffer frames"
    "${launch[@]}" "${args[@]}" ${extra[@]+"${extra[@]}"} >> "${OUT:-/dev/null}" 2>&1 &
    child=$!
    # A signal that arrived while the app was being started: pass it on now.
    if [ -n "$STOP" ]; then
        note "received SIG$STOP while starting the app; passing it to the app"
        kill "-$STOP" "$child" 2>/dev/null
    fi
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
}

main "$@"
