# Third-party components

AudreyBrick is a port of **Audrey II** by Synthux Academy: concept and firmware by Nick Donaldson (Infrasonic Audio), visual and hardware design by Roey Tsemah. Original repository: https://github.com/Synthux-Academy/Audrey-II

Every vendored file is byte-identical to its pinned upstream commit. `scripts/verify_vendor.sh` re-fetches the commits and checks this. Licence texts shipped with the port are in `licenses/`.

## 1. Audrey II firmware DSP

| Item | Value |
|---|---|
| Location | `third_party/audrey-ii/` |
| Licence | MIT |
| Copyright lines | `Copyright (c) 2026 Synthux Academy` (official repository `LICENSE`) and `Copyright (c) 2022 Infrasonic Audio, LLC` (archive copy). Both are kept |
| Source URL | https://github.com/Synthux-Academy/Audrey-II |
| Commit | `e5ab79910eb7ccbac5bbc865fae5459d65f5b5e3` |
| Second licence source | https://github.com/Synthux-Academy/simple-designer-instruments, commit `b16423ea875afc7bb3d8c4282e730127f3808d3f`, path `official/audrey-ii/LICENSE` |
| Licence files | `licenses/audrey-ii.LICENSE`, `licenses/audrey-ii.LICENSE.archive-2022` |
| Built into | `audrey_dsp` (static) |
| Credits | `third_party/audrey-ii/CREDITS.md`: Nick Donaldson, concept and firmware; Roey Tsemah, visual and hardware design |

Files: `Source/BiquadFilters.cpp`, `Source/BiquadFilters.h`, `Source/DSPUtils.h`, `Source/EchoDelay.h`, `Source/FeedbackSynthEngine.cpp`, `Source/FeedbackSynthEngine.h`, `Source/KarplusString.cpp`, `Source/KarplusString.h`, `Source/ParameterRegistry.h`, `Source/SmoothedValue.h`, `LICENSE`, `LICENSE.archive-2022`, `CREDITS.md`.

`Source/KarplusString.{h,cpp}` is itself a modified copy of DaisySP's `KarplusString`; its header notes "Original code licensed as MIT Copyright (c) 2020 Electrosmith, Corp." (covered by `licenses/daisysp.LICENSE`).

**Derived code in our own tree.** Two upstream files are not vendored because they are bound to libDaisy: `FeedbackSynth_main.cpp` and `Source/FeedbackSynthControls.{h,cpp}`. Their logic (the parameter table, the knob update and the audio callback) is transcribed into `src/core/ParamTable.cpp` and `src/core/AudreyVoice.cpp`, and a second time, independently and for tests only, into `tests/ref/ReferenceHarness.{h,cpp}` (the reference harness the parity test compares `AudreyVoice` against). All of these files carry the upstream MIT notice with both copyright lines.

## 2. DaisySP, MIT modules

| Item | Value |
|---|---|
| Location | `third_party/daisysp/` |
| Licence | MIT |
| Copyright line | `Copyright (c) 2020 Electrosmith, Corp.` |
| Source URL | https://github.com/electro-smith/DaisySP |
| Commit | `7f67768eb22103e5f7743e709e090ac435a11fae` (the Audrey II submodule pin) |
| Licence file | `licenses/daisysp.LICENSE` |
| Built into | `audrey_dsp` (static) |

Files: `Source/Utility/dsp.h`, `Source/Utility/delayline.h`, `Source/Utility/dcblock.h`, `Source/Utility/dcblock.cpp`, `Source/Dynamics/crossfade.h`, `Source/Dynamics/crossfade.cpp`, `Source/Dynamics/limiter.h`, `Source/Dynamics/limiter.cpp`, `Source/Effects/overdrive.h`, `Source/Effects/overdrive.cpp`, `Source/Noise/whitenoise.h`, `LICENSE`.

Attributions inside these files: `crossfade.h` names Paul Batchelor as original author ("Ported from Soundpipe by Andrew Ikenberry"); `overdrive.h` is "Ported by Ben Sergentanis" from `pichenettes/eurorack/plaits/dsp/fx/overdrive.h`, original code by Emilie Gillet (2014); `limiter.h` was "extracted from pichenettes/stmlib"; in `dsp.h`, `SoftLimit` and `SoftClip` are "extracted from pichenettes/stmlib" and the four `*BlepSample` helpers are "Ported from pichenettes/eurorack/plaits/dsp/oscillator/oscillator.h". The licences of those origins are in section 2a.

## 2a. Origins of DaisySP code (licence notices only; no code vendored from these)

These projects are the sources that DaisySP ported from. Their licence notices are kept in `licenses/` because the ported code carries their copyright. Each was checked at the commit listed (fetched 2026-11-17); the verification script re-fetches and compares them.

| Origin | Used in our copy of DaisySP | Licence, as confirmed | Copyright line | Source, commit | Licence file |
|---|---|---|---|---|---|
| Mutable Instruments stmlib | `Dynamics/limiter.{h,cpp}`; `SoftLimit`, `SoftClip` in `Utility/dsp.h` | MIT. `LICENSE` says "Except when noted otherwise, all code is copyright Emilie Gillet and is released under the MIT License"; the exceptions it names are `third_party/STM` and `programming/serial`, not `dsp/`. `dsp/limiter.h` and `dsp/dsp.h` (which defines `SoftLimit` and `SoftClip`) carry the MIT header themselves | `Copyright 2012 Emilie Gillet.` (`LICENSE`, `dsp/dsp.h`); `Copyright 2015 Emilie Gillet.` (`dsp/limiter.h`) | https://github.com/pichenettes/stmlib, `d18def816c51d1da0c108236928b2bbd25c17481` | `licenses/stmlib.LICENSE` (the whole upstream `LICENSE`, including its notices for directories we do not use) |
| Mutable Instruments Plaits (in pichenettes/eurorack) | `Effects/overdrive.{h,cpp}`; the `*BlepSample` helpers in `Utility/dsp.h` | MIT. The eurorack `README.md` states "Code (STM32F projects): MIT license." (AVR projects are GPL-3.0; Plaits is STM32). `plaits/dsp/fx/overdrive.h` carries the MIT header. The repository has no separate licence file, so the notice kept is that file header | `Copyright 2014 Emilie Gillet.` (`plaits/dsp/fx/overdrive.h`) | https://github.com/pichenettes/eurorack, `f4cfa7697095a28d292bff758beb51deb49b5a86` | `licenses/plaits.NOTICE` (lines 1 to 23 of `plaits/dsp/fx/overdrive.h`, unchanged) |
| Soundpipe | `Dynamics/crossfade.{h,cpp}` | MIT (`LICENSE` at the commit listed). `modules/crossfade.c` has no header of its own | `Copyright (c) 2020 Paul Batchelor` | https://github.com/PaulBatchelor/Soundpipe, `3efb43bdabd0ed23b17c694292b5a79f1692a3ea` | `licenses/soundpipe.LICENSE` |

Limits of what was confirmed:
- The `*BlepSample` helpers are no longer in `plaits/dsp/oscillator/oscillator.h` at the eurorack commit above; they now live in stmlib's `dsp/polyblep.h` (MIT, `Copyright 2017 Emilie Gillet.`). Either origin is MIT under Emilie Gillet's copyright. The exact historical revision DaisySP ported from was not identified. None of these helpers is called by the Audrey II engine.
- The commits above are the current heads of those repositories, not the revisions DaisySP ported from in 2020 and 2021. Licence history was not traced back to those revisions.
- Other origins named in `dsp.h` and `whitenoise.h` (musicdsp.org snippets, an openaudio.blogspot.com post, a CMSIS-DSP-derived `fastlog2f`) were not researched and have no notice here.

## 3. DaisySP, ReverbSc and Tone (treated as LGPL-2.1)

| Item | Value |
|---|---|
| Location | `third_party/daisysp-lgpl/` |
| Licence | LGPL-2.1 (decision D1). MIT-labelled at the pinned DaisySP commit; relabelled LGPL-2.1 upstream in DaisySP-LGPL. See `third_party/daisysp-lgpl/PROVENANCE.md` |
| Copyright lines | At the pinned commit: `Copyright (c) 2020 Electrosmith, Corp.` (MIT). In DaisySP-LGPL: `DaisySP-LGPL, copyright (c) 2023 Electrosmith, Corp.`; `reverbsc.h`: `Copyright (c) 2023 Electrosmith, Corp, Sean Costello, Istvan Varga, Paul Batchelor`; `tone.h`: `Copyright (c) 2023 Electrosmith, Corp, Barry Vercoe, John FFitch, Gabriel Maldonado`; Csound: `Csound is copyright (c) 1991-2020 The Csound Developers` |
| Source URL (code) | https://github.com/electro-smith/DaisySP, commit `7f67768eb22103e5f7743e709e090ac435a11fae` |
| Source URL (licence text) | https://github.com/electro-smith/DaisySP-LGPL, commit `c89d380c62620d6fee9105c5035e081c9b7ea843`, file `LICENSE` |
| Licence files | `licenses/daisysp-lgpl.LICENSE` (LGPL-2.1 with the DaisySP-LGPL, Csound and Faust notices), plus `third_party/daisysp-lgpl/LICENSE.daisysp-mit-7f67768` (the MIT licence the files carried at the pinned commit) |
| Built into | `audrey_lgpl` (**shared** library, so it can be replaced or relinked) |

Files: `Source/Effects/reverbsc.h`, `Source/Effects/reverbsc.cpp`, `Source/Filters/tone.h`, `Source/Filters/tone.cpp`, `LICENSE`, `LICENSE.daisysp-mit-7f67768`, `PROVENANCE.md`.

## 4. dr_wav (tools and tests only)

| Item | Value |
|---|---|
| Location | `third_party/dr_libs/` |
| Licence | Choice of Public Domain (Unlicense) or MIT No Attribution (MIT-0), as stated at the end of `dr_wav.h`. **We use MIT-0** |
| Copyright line | `Copyright 2023 David Reid` (MIT-0 alternative in `dr_wav.h`) |
| Source URL | https://github.com/mackron/dr_libs |
| Commit | `35b5c392cc15456736c440a68dbe073178bbffe0` (dr_wav v0.14.6, 2026-10-31), fetched 2026-11-17. See `third_party/dr_libs/UPSTREAM.md` |
| Licence file | `licenses/dr_wav.LICENSE` (lines 8658 to 8704 of `dr_wav.h`, unchanged) |
| Built into | `audrey_tools` (static, `src/tools`), used by `audrey-render`, `wavcheck`, `audrey-gen-input`, `parity_test` and `tools_test`. **Not** part of `audrey_core` or the audio path, and not shipped in the device app |

Files: `dr_wav.h`, plus our note `UPSTREAM.md`.

## 5. font8x8 (the app's bitmap font)

| Item | Value |
|---|---|
| Location | `third_party/font8x8/` |
| Licence | Public Domain, as stated in the repository's `README` ("License: Public Domain") and in the header comment of `font8x8_basic.h`. The glyphs derive from public domain VGA fonts (Marcel Sondaar; International Business Machines), as the `README` records. The repository has no separate licence file |
| Author | Daniel Hepper (`font8x8_basic.h`: "Author: Daniel Hepper <daniel@hepper.net>") |
| Source URL | https://github.com/dhepper/font8x8 |
| Commit | `8e279d2d864e79128e96188a6b9526cfa3fbfef9` (2019-07-08, the head of `master`), fetched 2026-11-17. See `third_party/font8x8/UPSTREAM.md` |
| Licence file | `licenses/font8x8.README` (the upstream `README`, unchanged) |
| Built into | `audrey_ui_core` (static, `src/ui`): `src/ui/Font.cpp` includes the header once, compiled with `-funsigned-char` (the header is not modified; see `UPSTREAM.md`). Shipped in the app as the on-screen font, ASCII 32 to 126 only |

Files: `font8x8_basic.h`, `README`, plus our note `UPSTREAM.md`.

## Our own stand-ins (not third-party code)

- `src/shim/daisysp.h`: trimmed umbrella header including only the nine DaisySP modules above.
- `src/shim/memory/sdram_alloc.h`: heap-backed replacement for Audrey II's `Source/memory/sdram_alloc.h` (not vendored), with the same `infrasonic::SDRAM::allocate<T>()` interface.
