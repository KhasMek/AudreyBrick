# ReverbSc and Tone: provenance and licence

## What is here

```
Source/Effects/reverbsc.h
Source/Effects/reverbsc.cpp
Source/Filters/tone.h
Source/Filters/tone.cpp
```

These four files are byte-identical copies from **DaisySP** (https://github.com/electro-smith/DaisySP) at commit `7f67768eb22103e5f7743e709e090ac435a11fae` (2023-02-13), the version pinned by Audrey II at `e5ab799`. They were fetched on 2026-11-17. They are **not** taken from DaisySP-LGPL; the code that Audrey II was built with is used, unchanged.

## Why they are treated as LGPL-2.1

At commit `7f67768` the whole of DaisySP, including these files, carried a single MIT licence ("Copyright (c) 2020 Electrosmith, Corp."). That licence file is kept here as `LICENSE.daisysp-mit-7f67768`.

In December 2023 Electrosmith moved the Csound-derived modules, these two among them, into a separate repository, **DaisySP-LGPL** (https://github.com/electro-smith/DaisySP-LGPL), under the LGPL-2.1. At DaisySP-LGPL commit `c89d380c62620d6fee9105c5035e081c9b7ea843` (2023-12-21):

- `reverbsc.cpp` and `tone.cpp` are byte-identical to the copies here.
- `reverbsc.h` and `tone.h` differ only by an added licence header and a shortened doc comment. The added headers read "Copyright (c) 2023 Electrosmith, Corp, Sean Costello, Istvan Varga, Paul Batchelor" (`reverbsc.h`) and "Copyright (c) 2023 Electrosmith, Corp, Barry Vercoe, John FFitch, Gabriel Maldonado" (`tone.h`), and point to the LGPL V2.1.

ReverbSc is ported from Csound / Soundpipe (original authors Sean Costello and Istvan Varga), and Tone from Csound. Csound itself is LGPL-2.1. Because of this, the project decided (decision D1, 2026-11-17) to treat these files as LGPL-2.1 regardless of the MIT label at the pinned commit.

## How they are built

They are compiled into the **shared** library `audrey_lgpl` and nothing else, so that users can replace or relink it. The rest of the DSP (`audrey_dsp`, static) links against it. Small inline setters defined in `tone.h` and `reverbsc.h` are compiled into consumers that include those headers; all object code from `reverbsc.cpp` and `tone.cpp` lives only in the shared library.

## Licence text

`LICENSE` is the licence file of DaisySP-LGPL at commit `c89d380c62620d6fee9105c5035e081c9b7ea843`, copied byte for byte. It contains the full LGPL-2.1 text together with the DaisySP-LGPL, Csound and Faust notices.

## Rules

Do not edit any file under `Source/` or either licence file. `scripts/verify_vendor.sh` re-fetches both commits and checks every file byte for byte.
