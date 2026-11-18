AudreyBrick (working title)
===========================

A port of the Audrey II "horrorscape" feedback synthesiser to the TrimUI Brick
running KNULLI. A droning feedback instrument: a plucked-string model fed with
inaudible noise, distortion, filters, reverb and a long body delay fed back into
the string, and a final delay whose repeats can run away and saturate. Nothing
sounds until you raise the feedback (R2).

Version: see the first line of audreybrick/data/audreybrick.log after a run.


Credits
-------
Audrey II by Synthux Academy.
  Concept and firmware: Nick Donaldson (Infrasonic Audio).
  Visual and hardware design: Roey Tsemah.
  Original repository: https://github.com/Synthux-Academy/Audrey-II

AudreyBrick is an unofficial port and is not made or endorsed by Synthux Academy
or Infrasonic Audio. The DSP is the original firmware's code (MIT licence). DaisySP
by Electrosmith (MIT, and LGPL-2.1 for ReverbSc and Tone, see below). The font is
font8x8 by Daniel Hepper (public domain). Full notices: THIRD_PARTY.md and the
licenses/ folder.

AudreyBrick's own code: MIT licence, see LICENSE.
Source repository: to be published.


Installing
----------
Copy AudreyBrick.sh and the folder audreybrick into the Ports folder of the SD card
(roms/ports, which is /userdata/roms/ports on the device). In KNULLI, update the
game lists, then start AudreyBrick from Ports.

Mind your ears: the feedback can run away. Start with the volume low (OUT tab).


Buttons
-------
Always:
  L1 / R1          previous / next tab
  R2 (hold)        feedback up (ramps; a tap is a small step)
  L2 (hold)        feedback down
  A (hold)         feedback to maximum while held
  B (hold)         delay switch on while held (the pitch-warping "!!" switch)
  SELECT (hold)    fine: the d-pad and L2/R2 move in small steps (SELECT + START
                   has no function in AudreyBrick; it is KNULLI's screenshot, see below)
  SELECT + B       latch the delay switch on or off
  X (hold)         the d-pad moves the tab's other parameter (DELAY: repeats)
  Y + d-pad        reset that parameter to its default
  START            nothing by itself
  MENU + START     quit

Tabs (d-pad left/right, d-pad up/down):
  LOOP     frequency (one semitone per tap)   body
  FILTER   lowpass                            highpass
  REVERB   dry/wet                            decay
  DELAY    time                               mix;  X + up/down: repeats
  OUT      -                                  volume


Quitting
--------
  - MENU + START: hold MENU and press START (or the other way round), once. The
    app quits at once. (KNULLI's soft reset, MENU + START pressed twice quickly,
    also ends AudreyBrick.)
  - On a USB keyboard: Escape, or Alt+F4.


Screenshots
-----------
AudreyBrick adds no screenshot function of its own. Use KNULLI's: hold SELECT and
press START. KNULLI saves the screen as a PNG in the screenshots folder of the SD
card (/userdata/screenshots on the device). AudreyBrick keeps running and the
picture shows its normal screen.


Logs
----
Each run writes, in the audreybrick/data folder:
  audreybrick.log   what the app found (screen, sound, buttons, controller) and
                    statistics every 10 seconds
  stdout.log        the launcher's notes (including the device's power, CPU and
                    sound settings at start) and everything the app printed
These two are always the latest run. Earlier runs are kept as
audreybrick-NNNNNN-YYYYMMDD-HHMMSS.log and stdout-NNNNNN-YYYYMMDD-HHMMSS.log
(NNNNNN is the run number, so they sort in order even if the clock was not set);
the last 8 runs are kept in total, the oldest are deleted. If the data folder
cannot be written, the logs go to /tmp/audreybrick-data instead (lost on reboot).
Replacing the audreybrick folder with a new version deletes data/: copy the logs
off first.

To change the audio buffer size, edit the number on the AUDREY_BUFFER line near the
top of AudreyBrick.sh (default 256 frames; 16 to 32768).

stdout.log has one line starting "launcher: settings:" with the app's version and
the settings below as they were used for that run.


A, B, X and Y buttons
---------------------
KNULLI's default controller mapping for ports names the buttons by position,
Xbox style, while the Brick's buttons are labelled Nintendo style. So the button
labelled A reaches apps as "B" and the other way round, and (expected from the
firmware's mapping; the next run's log confirms it) the same for X and Y. The
launcher therefore starts the app with A/B and X/Y swapped back, so every button
does what the button table above says for its label. Only these four buttons are
affected. stdout.log says which swaps were used and why ("launcher: buttons: ...").

To turn a swap off, set AUDREY_SWAP_AB=0 or AUDREY_SWAP_XY=0 (or change
DEFAULT_SWAP_AB / DEFAULT_SWAP_XY near the top of AudreyBrick.sh to 0).

KNULLI has its own switch for this, per port: in AudreyBrick's game options,
"SWITCH A/B and X/Y". When it is on, the buttons already arrive with their
labels' names, and the launcher sees this (KNULLI creates the file
/var/run/ports_xbox_layout.flag for that launch) and turns both of its own swaps
off, so the buttons are never swapped twice. You can use either; you do not need
both. A value set in the environment (AUDREY_SWAP_AB=... or AUDREY_SWAP_XY=...)
overrides this automatic choice.


CPU speed (governor)
--------------------
By default the launcher leaves the CPU speed to KNULLI. KNULLI normally lets the
CPU slow down when it has little to do ("schedutil"). If you want the CPU at full
speed while AudreyBrick plays (to give the sound processing more headroom), the
recommended way is KNULLI's own option for the port: in AudreyBrick's game options,
POWER MODE: High Performance. KNULLI sets it when the port starts and puts the
normal mode back when it ends.

The alternative is the launcher's AUDREY_GOVERNOR setting: change
AUDREY_GOVERNOR="${AUDREY_GOVERNOR:-keep}" near the top of AudreyBrick.sh to
AUDREY_GOVERNOR="${AUDREY_GOVERNOR:-performance}". The launcher then switches the
CPU to the "performance" governor while the app runs and writes back the governor
it found when the app ends (unless something else changed it meanwhile).
stdout.log lists each change ("governor: ...") and the restore.

Neither is required: the first run on the Brick had no audio overruns at KNULLI's
normal setting. Both cost battery while the app runs. The app's log shows the CPU
frequency in every statistics line, whichever you choose.

If the app or the launcher is stopped abruptly, whichever of the two is left puts
the governor back. If both are killed at once, KNULLI's own end-of-game step
(which re-applies the system's power mode when the port ends) normally puts the
governor back anyway. In that rare case the keep-awake file
/var/run/battery-saver/audreybrick.pause would remain until the next reboot, and
KNULLI would not dim the screen or mute when idle; to remove it by hand, reboot,
or over SSH run: rm /var/run/battery-saver/audreybrick.pause


Keeping the device awake
------------------------
While AudreyBrick runs, it stops KNULLI from dimming the screen, muting the sound
and suspending when no button has been pressed for a while (an instrument often
plays untouched). It does this by creating the file
/var/run/battery-saver/audreybrick.pause, which KNULLI's idle watcher respects, and
removes it when the app ends. To turn this off, change AUDREY_KEEP_AWAKE="${AUDREY_KEEP_AWAKE:-1}"
near the top of AudreyBrick.sh to AUDREY_KEEP_AWAKE="${AUDREY_KEEP_AWAKE:-0}".


Testing aid
-----------
If the environment variable AUDREY_ARGS is set, its words are added to the app's
command line (for example "--selftest <script> --seconds 5 --no-video"). Leave it
unset for normal use. AUDREY_SYSFS_CPU, also for testing only, points the launcher
and the app at a fake copy of /sys/devices/system/cpu.


LGPL notice: libaudrey_lgpl.so
------------------------------
audreybrick/libs.aarch64/libaudrey_lgpl.so contains DaisySP's ReverbSc and Tone,
which are licensed under the GNU Lesser General Public License, version 2.1 (text in
licenses/daisysp-lgpl.LICENSE). The app loads it as a separate shared library so
that you can replace it: build your own version from the published source (the
files third_party/daisysp-lgpl/Source/Effects/reverbsc.cpp and
Source/Filters/tone.cpp in the AudreyBrick source, or DaisySP-LGPL), as an aarch64
Linux shared library exporting the same symbols (daisysp::ReverbSc and
daisysp::Tone with the same class layout), name it libaudrey_lgpl.so and put it in
audreybrick/libs.aarch64/ in place of this one. The source of this build is in the
AudreyBrick source repository (to be published).
