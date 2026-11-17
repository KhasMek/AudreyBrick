# DaisySP (MIT modules): upstream record

| Item | Value |
|---|---|
| Source URL | https://github.com/electro-smith/DaisySP |
| Commit | `7f67768eb22103e5f7743e709e090ac435a11fae` (committed 2023-02-13T07:59:23-08:00, "Don't pass Tone float params by reference (#180)"). This is the `lib/DaisySP` submodule pin of Audrey II at `e5ab799` |
| Date fetched | 2026-11-17 |
| Licence | MIT (`LICENSE`, "Copyright (c) 2020 Electrosmith, Corp.") |

## Files vendored (byte-identical to the commit above)

```
Source/Utility/dsp.h
Source/Utility/delayline.h
Source/Utility/dcblock.h
Source/Utility/dcblock.cpp
Source/Dynamics/crossfade.h
Source/Dynamics/crossfade.cpp
Source/Dynamics/limiter.h
Source/Dynamics/limiter.cpp
Source/Effects/overdrive.h
Source/Effects/overdrive.cpp
Source/Noise/whitenoise.h
LICENSE
```

`Effects/reverbsc.{h,cpp}` and `Filters/tone.{h,cpp}` from the same commit are kept separately in `third_party/daisysp-lgpl/` (see the provenance note there). The umbrella `daisysp.h` is not vendored; `src/shim/daisysp.h` replaces it with only the nine modules used.

## Rules

Do not edit any file in this directory. `scripts/verify_vendor.sh` re-fetches the commit and checks every file byte for byte.
