AudreyBrick
===========

A port of the Audrey II "horrorscape" feedback synthesiser to the TrimUI Brick
running KNULLI. A droning feedback instrument: a plucked-string model fed with
inaudible noise, distortion, filters, reverb and a long body delay fed back into
the string, and a final delay whose repeats can run away and saturate. Nothing
sounds until you raise the feedback (R2).

Version: see the first line of audreybrick.log in the data folder after a run
(see "Your files" below).


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
Source repository: https://github.com/KhasMek/AudreyBrick


Installing
----------
Copy AudreyBrick.sh and the folder audreybrick into the Ports folder of the SD card
(roms/ports, which is /userdata/roms/ports on the device). In KNULLI, update the
game lists, then start AudreyBrick from Ports.

Mind your ears: the feedback can run away. Start with the volume low (OUT tab).

Updating: replace AudreyBrick.sh and the audreybrick folder with the new ones.
  On KNULLI this is safe: your files are not in the audreybrick folder but in
  saves/audreybrick (see "Your files"), which an update does not touch.
  Coming from a version that kept them in audreybrick/data: look in that
  folder first and move out what you want to keep, because replacing the
  audreybrick folder deletes it (see "An old audreybrick/data folder").
  On a firmware without a writable /userdata/saves folder your files are in
  audreybrick/data instead: there, copy the new files INTO the existing
  audreybrick folder (replacing files of the same name, not the folder), so
  audreybrick/data stays.


Your files
----------
Everything that is yours lives in one data folder:

  On KNULLI:  /userdata/saves/audreybrick  (on the SD card: saves/audreybrick),
              outside the app's folder
  Elsewhere:  audreybrick/data inside the app folder, if the firmware has no
              writable /userdata/saves folder (see "Updating" above)

It holds:
  input/        WAV files for the INPUT tab's FILE source (you put them there)
  recordings/   what START + Y records: rec-0001.wav, rec-0002.wav, ...
  presets/      presets (*.json)
  audreybrick.log, stdout.log and the numbered logs of earlier runs (the 8 most
                recent runs are kept; older numbered logs are deleted). A log is
                never overwritten: if a numbered name is taken, the next one
                gets -2, -3, ... (audreybrick-000041-...-2.log)
  run-counter, latest-run (the launcher's run number; leave them alone)
stdout.log names the folder used in its line "launcher: data folder: ...". If
saves/audreybrick exists but cannot be used for a start (for example the card
was read-only), that line starts with "launcher: PROBLEM:", and what that run
saves goes to audreybrick/data. It is not moved later by itself: move it to
saves/audreybrick by hand if you want it there.

An old audreybrick/data folder. Versions before this one kept your files in
audreybrick/data inside the app folder. While saves/audreybrick is in use,
AudreyBrick never moves, renames, changes or deletes anything there. (Only if
saves/audreybrick cannot be used for a start does audreybrick/data become the
folder in use again: that run then keeps its logs there and tidies old numbered
logs as usual. Recordings, input files and presets are never changed.) If that folder is still on the card and holds
anything, every start writes one line to stdout.log ("launcher: old data folder:
...") saying so. To use those files, move them by hand (on a computer, or in a
file manager) into the same folders in saves/audreybrick (input/, recordings/).
Leave the old logs, run-counter and latest-run where they are. Where a file of the same name is already there, rename one of the
two first, so neither is overwritten. Once audreybrick/data
is empty or gone, the line no longer appears. Replacing the audreybrick folder
deletes audreybrick/data with it, so move what you want to keep first.


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
                   INPUT: choosing the file or the capture device; MOD:
                   left/right select the slot; see below)
  Y + d-pad        reset that parameter to its default
  START            nothing by itself
  START + Y        start / stop recording the output (see Recording below)
  START + A        save a new preset (see Presets below)
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
           release X to open it (the gain goes to -inf dB first; a device
           opened before in this run then gets its own gain back)
  MOD      the four modulation slots (see Modulation below)
  PRESET   the presets (see Presets below). The tab bar shows the current tab
           and its neighbours; small arrows at its ends mean more tabs that
           way. L1 from LOOP goes to PRESET, the last tab.


Audio input (INPUT tab)
-----------------------
The original instrument has an audio input: whatever comes in is added to the
string, inside the feedback loop, so it sounds even with the feedback at its
minimum and rings the string and the loop as you raise it. AudreyBrick can feed
it from a WAV file (FILE) or from a capture device (LIVE): the Brick's built-in
microphone or a USB audio interface. LIVE is described further down.

  1. Put WAV files into the input folder of the data folder (see "Your files"):
     on KNULLI saves/audreybrick/input on the SD card (on the device
     /userdata/saves/audreybrick/input). The launcher creates the folder if it
     is missing. Files directly in that folder are used; folders
     inside it, links and files whose names start with "." are ignored (so are
     the "._" files a Mac leaves next to each file it copies).
  2. In AudreyBrick go to the INPUT tab (L1 twice from LOOP, or R1 past OUT).
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
playing keeps playing. The log (audreybrick.log) has a line for every file
loaded and every failure.

On KNULLI, replacing the audreybrick folder with a new version does not touch the
input folder (it is in saves/audreybrick; see "Updating" for the one exception:
a firmware without a writable /userdata/saves folder).
Keeping your own copy elsewhere is still wise.
No audio files are shipped.

Mind your ears: a loud file through the distortion and feedback can be very loud.
Start with the gain at 0 dB or below and the volume low.


Live input: microphone and USB audio (INPUT tab, LIVE)
------------------------------------------------------
WARNING: the Brick's speaker and its microphone sit close together, and Audrey II
is a feedback instrument. With the speaker on, the microphone hears the speaker,
the instrument feeds that back into itself and it can howl, loudly. Use
headphones, or keep the input gain low. AudreyBrick cannot tell whether
headphones are plugged in, so it does not mute anything by itself; it sets the
gain to -inf dB (silence) the first time a device opens in a run and leaves
raising it to you. After that it remembers that device's gain until you quit
(see step 4). So a gain you set with headphones on comes back when you return
to that device, also after you have unplugged the headphones: lower the gain
before you leave LIVE if you will come back on the speaker.
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
  3. When a device opens for the first time in a run, the gain is set to
     -inf dB. The DEVICE row then shows
     what the device delivers (for example "48000 Hz 1 CH 256 FR"; DIRECT next
     to the row's name when it was opened directly on its sound card, as the
     built-in microphone always is) and a PRE bar:
     the level coming from the device before the gain, so you can see that the
     microphone works while the gain is still at zero. Raise the gain with
     d-pad up (or Y + up for 0 dB) and watch the large input meter.
  4. Leaving LIVE (d-pad left) closes the device, so the microphone is never
     open without you seeing it. Selecting LIVE again opens the same device
     again. The gain is remembered for each source until you quit: FILE has
     its own gain, and each device has its own (for up to 8 devices). A device
     you have opened before comes back at the gain you left it at: it opens
     at -inf dB, and once it is open its gain fades in over a tenth of a
     second. If it howls, bring the gain down; leaving LIVE while it howls
     stores that gain for the device. Changes you make while no device is open (or with OFF) are not
     remembered. Nothing is saved: the next launch starts at -inf dB again.
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
adds delay. The log (audreybrick.log) has a "live:" part in every
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


Modulation (MOD tab)
--------------------
Not part of the original instrument: four modulation slots that move any
setting by themselves. Each slot has a shape, a rate, a depth, a polarity and
one target.
With every slot off (the start, and whenever a slot's target is NONE or its
depth 0) AudreyBrick sounds exactly as without it.

  Shapes   SINE, TRIANGLE, SQUARE, RAMP UP (rises steadily, then jumps back
           down), RAMP DOWN (the reverse), EXP UP (rises slowly, then faster
           and faster, then jumps back), EXP DOWN (jumps up, falls fast, then
           ever more slowly), S&H (sample and hold: a new random value each
           cycle) and ENVELOPE (follows the level of the audio input before
           the INPUT tab's gain, so it still works with the gain at -inf dB,
           where you do not hear the input; it is still while the input is
           OFF).
  Rate     0.010 HZ (one cycle in 100 s) to 2.00 KHZ. With ENVELOPE the same
           row is RELEASE: how fast the envelope falls back, 5.00 S to 5.00 MS
           (it rises in 2 ms).
  Depth    -100 % to +100 % of the target's travel (a knob from end to end).
           Several slots on one target add up; the result stops at the knob's
           ends.
  Polarity BIPOLAR (the start): the target swings both ways round the knob.
           UNIPOLAR: it only moves to one side: above the knob with a positive
           depth, below it with a negative one. ENVELOPE is always unipolar.
           On the slot cards: +/- or +.
  Targets  NONE, every knob in the order of the value strip (FEEDBACK,
           FREQUENCY, BODY, LOWPASS, HIGHPASS, DRY/WET, DECAY, TIME, MIX,
           REPEATS, VOLUME), INPUT GAIN and DELAY SWITCH (on while the
           modulation is above zero, in addition to B and SELECT + B).

The MOD tab (L1 twice from LOOP, or R1 past INPUT) shows the selected slot's
settings in five rows and all four slots underneath as cards, each with a
small bar that moves with what the slot does right now. The selected card has
a bright frame and its number in a bright box. The cards are dimmed while you
edit the rows; hold X and they light up (the rows dim) so you can pick a slot.
A slot with no target or no depth has a darker card.

  d-pad up/down        the row: TARGET, SHAPE, RATE (RELEASE), DEPTH, POLARITY
  d-pad left/right     the row's value for the selected slot: TARGET
                       (previous / next, at once, round from DELAY SWITCH to
                       NONE and back; held, it keeps stepping, 8 per second),
                       SHAPE, RATE (a tap is 1/100 of the range, about 13 %
                       faster or slower; held, it ramps), DEPTH (2 % per tap;
                       held, it ramps through 0). SELECT held: fine steps
                       (RATE 1/1000, DEPTH 0.2 %). For RELEASE, right makes
                       the release longer. POLARITY: right UNIPOLAR, left
                       BIPOLAR (with ENVELOPE it stays UNIPOLAR).
  X + left/right       select the slot, 1 to 4 (round from 4 to 1 and back),
                       one per press; the rows keep their place, so on DEPTH
                       you land on the next slot's DEPTH
  Y + left/right       the row back to its default: TARGET NONE, SHAPE sine,
                       RATE 1 Hz, DEPTH 0, POLARITY bipolar (nothing with
                       ENVELOPE)
  Y + X + left/right   the whole selected slot back to its start (all five
                       rows)

Everything else (L2/R2, A, B, SELECT + B, L1/R1, START + Y, MENU + START) works
on MOD as on every tab. On the other tabs a modulated knob carries a small
violet tag with the slot numbers (M1, M13, ...; in the value strip, where
there is no room for them, M and the number of slots, or just M) and a thin
violet line on its bar showing where the modulation has moved it (a short stub
when that is the bottom of the range); the number printed is still the knob's
own setting. On the switch box, ON and MOD in green on a dark box mean
that only the modulation holds the switch on. With a slot on INPUT GAIN, the
INPUT tab's meter shows the level after the modulation.

Good to know:
  - Fast, deep modulation of LOWPASS and HIGHPASS gets shallower as the rate
    rises: the filter cutoffs may only move so fast (that keeps the feedback
    loop from blowing up). At 200 Hz about half an octave either way is left,
    at 1 kHz about a tenth.
  - BODY and TIME respond slowly by the instrument's nature: the original
    smooths both inside its sound engine (0.2 s and 0.5 s; the slow sweep of
    TIME is the delay's pitch-bending character). Modulating them faster than a
    few hertz mostly averages out. The same goes for DELAY SWITCH.
  - Modulation cannot open an input whose gain is at -inf dB: a closed input
    stays silent (the safeguard against howling stays in your hands).
  - Square, S&H and the ends of the ramps jump; on VOLUME or the filters that
    is heard as clicks, by design. Depth, target and polarity changes
    themselves fade in over about 10 ms.
  - ENVELOPE listens before the gain: turn the INPUT gain down to -inf dB and
    a microphone, USB input or file can still drive a filter or the feedback
    without being heard. The input must still be on (FILE or LIVE).
  - Careful with ENVELOPE on INPUT GAIN with a positive depth: because the
    envelope no longer shrinks when you turn the gain down, it can boost a
    turned-down input a lot (gain knob at -12 dB, an envelope of 0.3, depth
    +100 %: about +13.7 dB). With the speaker and the microphone that is a
    loop that feeds on its own level and can howl: use headphones, or a
    negative depth (which ducks the input instead). A gain at exactly -inf dB
    still stays silent whatever the modulation.
  - EXP UP and EXP DOWN are not symmetric round the knob's setting: EXP DOWN
    spends most of each cycle near its low end, and so does EXP UP (its mirror
    in time) before its fast rise. At very high rates
    the ramps get coarse: at 2 kHz a cycle has only 6 steps.
  - The settings last until you quit; nothing is saved yet. The log
    (audreybrick.log) has a line for a slot once its settings have stayed
    unchanged for half a second, for example
    "mod: slot 2 triangle -> FeedbackLPFCutoff 3.20 Hz depth +34.0%".


Quitting
--------
  - MENU + START: hold MENU and press START (or the other way round), once. The
    app quits at once. (KNULLI's soft reset, MENU + START pressed twice quickly,
    also ends AudreyBrick.)
  - On a USB keyboard: Escape, or Alt+F4.


Recording
---------
Hold START and press Y (or hold Y and press START), on any tab, to start
recording what AudreyBrick plays. Do it again to stop. While it records, a thin
red frame runs round the whole screen and REC 1:23 (minutes and seconds; hours
from the first hour, 1:02:03) counts up in red under the feedback bar. After a
stop it shows the file's name and length for 2 seconds: SAVED REC-0003.WAV 1:23.

What is recorded: exactly what goes to the speaker or headphones, after
everything (the processed audio input included; the input is not recorded on its
own). 16-bit, stereo, 48 kHz WAV, without any processing of its own. Recording
carries on when you change tabs or the input source.

Where: the recordings folder of the data folder (see "Your files"; on KNULLI
saves/audreybrick/recordings on the SD card), as rec-0001.wav, rec-0002.wav, ...
(the Brick's clock is not set, so the names are numbers, not dates). A new
recording takes the number after the highest one in the folder; nothing is ever
overwritten. To copy them off: switch the Brick off, put the SD card in a
computer and copy saves/audreybrick/recordings. On KNULLI, replacing the
audreybrick folder with a new version does not touch them (see "Updating"). On a FAT or exFAT card macOS adds hidden
._ files; they do no harm.

Limits:
  - There is no time limit of your own to set. A file stops by itself just
    before 4 GB (about 5 hours 47 minutes): REC STOPPED: FILE LIMIT. It is saved
    up to there; press START + Y again for a new file.
  - Free space on the card: a recording does not start with less than 200 MB
    free (REC: CARD FULL), and stops by itself when less than 100 MB is left
    (REC STOPPED: CARD FULL). Both keep what was recorded.
  - REC: CANNOT CREATE FILE: the folder or the file could not be made (a
    read-only or full card). REC: WRITE ERROR: the card refused a write; the file
    is kept up to that point.
  - REC BUSY: you pressed START + Y while the previous file was still being
    finished (usually a fraction of a second); press it again.
  - If the card is too slow for a moment, AudreyBrick skips whole pieces of
    sound in the file rather than stall the instrument; the readout then shows
    DROP and the number of skipped sample frames, and the log says so.

Quitting while recording (MENU + START, KNULLI's soft reset) finishes the file
properly. If the Brick loses power or the app is killed, the file is still
playable up to the last 2 seconds before that.


Presets
-------
A preset is the sound of the moment, saved as one small file you can keep,
copy and rename:
  - the eleven settings (FEEDBACK as it is set with L2/R2, not the A button's
    MAX; FREQUENCY, BODY, LOWPASS, HIGHPASS, DRY/WET, DECAY, TIME, MIX,
    REPEATS, VOLUME), the delay switch's latch (SELECT + B), and the four
    modulation slots (shape, target, rate, depth, polarity).
  - NOT the audio input: loading a preset never changes the source, the
    device, the file, LOOP/ONCE or the input gain. A and B held are not saved
    and a load does not change them (with A held, MAX stays until you let go).
  - Nothing is loaded when AudreyBrick starts: it always starts as it always
    has.

Saving: hold START and press A (or hold A and press START), on any tab, also
while recording. The new preset is saved as preset-0001.json, preset-0002.json,
... (one more than the highest number in the folder) and the screen says
SAVED PRESET-0008 under the feedback bar. If A came first, MAX sounds until
START is pressed; it does not change what is saved.

The PRESET tab (the last one):
  up / down        move through the list (hold to scroll; up from the top
                   goes to the bottom, the last entry in the list)
  right            on + SAVE NEW (the top row): save a new preset
                   on a preset: load it, at once
  X + right (hold about 1 second)  overwrite the preset with the current sound
  Y + right (hold about 1 second)  delete the preset
  Press X or Y first, then hold right; a bar fills while you hold. Let go early
  and nothing happens. The preset you loaded or saved last is marked LAST.

Loading is audible by design: every setting jumps to the preset's value and
moves there as quickly as the original's knobs do. Loading a loud preset while
the instrument is silent starts it at once: keep the volume down on headphones.

Where: the presets folder of the data folder (see "Your files"; on KNULLI
saves/audreybrick/presets on the SD card). The list shows every .json file
there by its name without .json, sorted by name (numbers in order: preset-2
before preset-10). To give a preset a name, rename the file on a computer
("Night drone.json" shows as Night drone). The next new preset is numbered one
more than the highest preset-NNNN number left in the folder: gaps lower down
are not filled again (renaming or deleting the highest one frees its number).
You can also copy presets to another card, or delete them on the computer. A
file that is not a preset, or is damaged, is listed but does not load: LOAD
FAILED: and the reason (the log has the details); nothing changes.
The format is described in docs/PRESET_FORMAT.md in the source.


Screenshots
-----------
AudreyBrick adds no screenshot function of its own. Use KNULLI's: hold SELECT and
press START. KNULLI saves the screen as a PNG in the screenshots folder of the SD
card (/userdata/screenshots on the device). AudreyBrick keeps running and the
picture shows its normal screen.


Logs
----
Each run writes, in the data folder (see "Your files"):
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
and the app at a fake copy of /sys/devices/system/cpu. AUDREY_SAVES_DIR, also for
testing only, is the folder used in place of /userdata/saves.


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
AudreyBrick source repository, https://github.com/KhasMek/AudreyBrick
