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
font8x8 by Daniel Hepper (public domain). WAV files are read with dr_wav by David
Reid (MIT No Attribution). Full notices: THIRD_PARTY.md and the licenses/ folder.

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
  X (hold)         the d-pad moves the tab's other parameter (DELAY: repeats;
                   INPUT: choosing the file or the capture device, see below)
  Y + d-pad        reset that parameter to its default
  START            nothing by itself
  MENU + START     quit

Tabs (d-pad left/right, d-pad up/down):
  LOOP     frequency (one semitone per tap)   body
  FILTER   lowpass                            highpass
  REVERB   dry/wet                            decay
  DELAY    time                               mix;  X + up/down: repeats
  OUT      -                                  volume
  INPUT    source (OFF, FILE, LIVE)           input gain;  Y + up/down: gain to 0 dB
           with FILE: hold X to choose a file (X + left/right: one by one;
           X + up/down: the list), release X to load it; X + Y: LOOP/ONCE;
           X + SELECT: play the file again from the start
           with LIVE: hold X to choose a capture device the same way,
           release X to open it (the gain goes to -inf dB first)


Audio input (INPUT tab)
-----------------------
The original instrument has an audio input: whatever comes in is added to the
string, inside the feedback loop, so it sounds even with the feedback at its
minimum and rings the string and the loop as you raise it. AudreyBrick can feed
it from a WAV file (FILE) or from a capture device (LIVE): the Brick's built-in
microphone or a USB audio interface. LIVE is described further down.

  1. Put WAV files into the folder audreybrick/data/input on the SD card
     (roms/ports/audreybrick/data/input; on the device
     /userdata/roms/ports/audreybrick/data/input). The launcher creates the
     folder if it is missing. Files directly in that folder are used; folders
     inside it, links and files whose names start with "." are ignored (so are
     the "._" files a Mac leaves next to each file it copies).
  2. In AudreyBrick go to the INPUT tab (L1 from LOOP, or R1 past OUT).
  3. D-pad right selects FILE. Nothing loads by itself. To choose a file,
     hold X:
       X + left/right  the previous / next file (by name, wrapping round),
                       shown in the FILE row; nothing loads yet
       X + up/down     opens a large list of the files; up/down move through
                       it (hold to scroll); left/right still step. Opening
                       the list and releasing X without moving only looks
                       (with nothing playing yet, it loads the highlighted
                       file)
       release X       loads the file you went to (if you pressed a direction
                       and it is not the file already playing). While it
                       loads, the playing file goes on; then the two crossfade
                       (100 ms). Releasing X without pressing a direction
                       does nothing; choosing the playing file again while
                       another loads cancels that load.
       X + Y           play mode: LOOP (round and round, the default) or ONCE
                       (to the end, then silence; ENDED on the screen)
       X + SELECT      play the file again from the start (also after ENDED;
                       or choose the ended file again and release X)
     L1/R1 while X is held cancels the choice. The FILE row shows the file,
     its progress, LOOP or ONCE and ENDED; the list marks the playing file
     PLAY. D-pad up/down sets the input gain (-inf to +12 dB, 0 dB in the
     middle; Y + up/down sets it back to 0 dB).
  4. The INPUT tab shows the input level after the gain (large meter, with a
     CLIP mark from 0 dBFS). Whenever the source is not OFF, a small IN bar at the
     bottom of the meter panel shows the input level on every tab.

The source is OFF at every start, the play mode LOOP; nothing about the input
is remembered (not the file, not the capture device).

Files: mono or stereo (stereo is mixed as left + right), 16-bit, 24-bit, 32-bit
or float, and the other formats dr_wav reads, at 8 to 384 kHz. Rates other than
48 kHz are converted when the file loads. At most the first 10 minutes of a file
are used (the screen then says CUT TO 10 MIN), and at most 256 files are listed.
A 10-minute file takes about 115 MB of memory while it plays. At most two such
files are in memory at once (the playing one and the one loading, or during
the 100 ms crossfade the old one): a file asked for during a crossfade starts
loading when the crossfade is over.

If you replace a file on the card while AudreyBrick runs, it notices when the
list is read again (on entering the INPUT tab or selecting FILE): choosing
that name then loads the new file instead of keeping the old one.
A file that cannot be used shows CANNOT LOAD and the reason; whatever was
playing keeps playing. The log (data/audreybrick.log) has a line for every file
loaded and every failure.

Keep your own copy of the files elsewhere: replacing the audreybrick folder with
a new version deletes data/, and data/input with it. No audio files are shipped.

Mind your ears: a loud file through the distortion and feedback can be very loud.
Start with the gain at 0 dB or below and the volume low.


Live input: microphone and USB audio (INPUT tab, LIVE)
------------------------------------------------------
WARNING: the Brick's speaker and its microphone sit close together, and Audrey II
is a feedback instrument. With the speaker on, the microphone hears the speaker,
the instrument feeds that back into itself and it can howl, loudly. Use
headphones, or keep the input gain low. AudreyBrick cannot tell whether
headphones are plugged in, so it does not mute anything by itself; it sets the
gain to -inf dB (silence) every time a device opens and leaves raising it to you.
The INPUT tab shows SPEAKER + MIC CAN HOWL / USE HEADPHONES OR KEEP GAIN LOW
whenever LIVE is selected.

  1. On the INPUT tab press d-pad right until SOURCE shows LIVE. Nothing opens
     by itself: the DEVICE row says NO DEVICE CHOSEN.
  2. Hold X to choose a device, as with files:
       X + left/right  the previous / next device; nothing opens yet
       X + up/down     opens the list of devices (with nothing open, X, down,
                       release opens the highlighted one)
       release X       opens the device you went to
     The list shows every device the system reports, by the names it gives
     them: on the Brick the built-in microphone is called "audiocodec, " and a
     USB interface something like "JIANT FM, USB Audio". A USB interface
     appears when it is plugged in, also while AudreyBrick runs. On the Brick
     there is no SYSTEM DEFAULT entry (see "The built-in microphone" below); on
     other systems (a computer) the list starts with SYSTEM DEFAULT. X + Y and
     X + SELECT do nothing with LIVE.
  3. When a device opens, the gain is set to -inf dB. The DEVICE row then shows
     what the device delivers (for example "48000 Hz 1 CH 256 FR"; DIRECT next
     to the row's name when it was opened directly on its sound card, as the
     built-in microphone always is) and a PRE bar:
     the level coming from the device before the gain, so you can see that the
     microphone works while the gain is still at zero. Raise the gain with
     d-pad up (or Y + up for 0 dB) and watch the large input meter.
  4. Leaving LIVE (d-pad left) closes the device, so the microphone is never
     open without you seeing it. Selecting LIVE again opens the same device
     again, with the gain back at -inf dB.
  5. If a device cannot be opened, the DEVICE row says CANNOT OPEN and the
     reason. If the open device is unplugged, the sound may stop abruptly at
     that moment (the system can hand over silence before it reports the
     loss); where AudreyBrick sees the loss first, the input fades out over
     10 ms. Then the device is closed and the row says DEVICE LOST: choose a
     device again. A device that sends nothing for 3 seconds is treated the
     same way.

Stereo devices are mixed to one channel as left + right (as files are); devices
with more channels use their first two. The sound is passed on with a small
buffer (two audio buffers by default, about 11 ms at the default buffer size),
so there is a short delay between what the microphone hears and what you hear.
If the device's clock runs a little fast or slow against the Brick's output,
the buffer is corrected now and then with a short crossfade: a few milliseconds
are skipped (fast) or repeated (slow). If the device delivers its sound in
large bursts and the buffer runs dry (an "underrun": a short fade out, a gap,
a fade in), AudreyBrick doubles the buffer after two underruns within a
second, or after a single one when the buffer had no room left for its slow-
clock corrections, up to 16 audio buffers; it goes back to the setting when you choose a
device again. To start with a larger buffer, set AUDREY_LIVE_TARGET (1 to 32
audio buffers, default 2) near the top of AudreyBrick.sh; more is steadier but
adds delay. The log (data/audreybrick.log) has a "live:" part in every
statistics line while LIVE is selected (buffer fill and target, underruns,
skips, repeats, frames captured and played, levels), and a line for every
device listed, opened, closed or lost and every time the buffer was doubled.

The built-in microphone and why there is no SYSTEM DEFAULT on the Brick:
  - The Brick's system default capture device goes through its audio server
    (PipeWire). With the built-in microphone it delivers nothing, and it leaves
    the microphone blocked for every program until the next reboot. So on the
    Brick AudreyBrick never offers or opens the system default for recording;
    choose a device by its name.
  - The built-in microphone ("audiocodec, ") cannot be opened the usual way
    (its chip rejects the format the system picks), so AudreyBrick opens it
    directly on its sound card (hw:0,0), as one channel (mono: it is one
    microphone). The DEVICE row then shows DIRECT next to its name and
    "48000 Hz 1 CH 256 FR" below. A USB interface opens by its name, asking for
    two channels (mixed to one as left + right); if opening it by name ever
    fails, AudreyBrick tries it directly on its card too. A listed name that is
    not one of the Brick's sound cards is never opened (CANNOT OPEN: NOT AN
    ALSA CARD).
  - The built-in microphone delivers nothing at all until its input mixer is
    set. Before opening it, AudreyBrick reads three mixer settings of the
    Brick's sound card and changes them with the system's amixer tool: "ADCL
    Input MIC1 Boost Switch" on, "MIC1 gain volume" 31, "ADC volume" 160,160.
    When the microphone is closed again (you leave LIVE, choose another device,
    it is lost, or you quit AudreyBrick normally) it writes back the values it
    read (if amixer fails to write them back, it tries again at the next close
    and when AudreyBrick quits). If AudreyBrick is killed without its normal
    shutdown, the values stay
    as set until the next reboot; nothing breaks (the microphone input simply
    stays switched on). If amixer is missing or fails, the log says so and the
    microphone stays silent (DEVICE LOST after 3 seconds).
  - If you ever chose SYSTEM DEFAULT in an earlier AudreyBrick version (or
    recorded through the audio server some other way) since the Brick last
    started, the microphone may be blocked ("Device or resource busy"): reboot
    the Brick.


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
                    sound settings at start, and the sound system's capture
                    devices) and everything the app printed
These two are always the latest run. Earlier runs are kept as
audreybrick-NNNNNN-YYYYMMDD-HHMMSS.log and stdout-NNNNNN-YYYYMMDD-HHMMSS.log
(NNNNNN is the run number, so they sort in order even if the clock was not set);
the last 8 runs are kept in total, the oldest are deleted. If the data folder
cannot be written, the logs go to /tmp/audreybrick-data instead (lost on reboot).
Replacing the audreybrick folder with a new version deletes data/: copy the logs
off first.

To change the audio buffer size, edit the number on the AUDREY_BUFFER line near the
top of AudreyBrick.sh (default 256 frames; 16 to 32768). The live input's buffer,
in audio buffers: AUDREY_LIVE_TARGET (default 2; 1 to 32).

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
