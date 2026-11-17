# Audrey II: upstream record

| Item | Value |
|---|---|
| Project | Audrey II, by Synthux Academy. Concept and firmware: Nick Donaldson (Infrasonic Audio). Visual and hardware design: Roey Tsemah |
| Source URL | https://github.com/Synthux-Academy/Audrey-II |
| Commit | `e5ab79910eb7ccbac5bbc865fae5459d65f5b5e3` (committed 2026-08-03T13:10:55+02:00, "Add Credits section to README") |
| Date fetched | 2026-11-17 |
| Licence | MIT (`LICENSE`, "Copyright (c) 2026 Synthux Academy") |
| Second licence copy | `LICENSE.archive-2022`, from https://github.com/Synthux-Academy/simple-designer-instruments at commit `b16423ea875afc7bb3d8c4282e730127f3808d3f`, path `official/audrey-ii/LICENSE` ("Copyright (c) 2022 Infrasonic Audio, LLC"). The same source files are published there; both notices are kept |

## Files vendored (byte-identical to the commit above)

```
Source/BiquadFilters.cpp
Source/BiquadFilters.h
Source/DSPUtils.h
Source/EchoDelay.h
Source/FeedbackSynthEngine.cpp
Source/FeedbackSynthEngine.h
Source/KarplusString.cpp
Source/KarplusString.h
Source/ParameterRegistry.h
Source/SmoothedValue.h
LICENSE
CREDITS.md
```

Plus `LICENSE.archive-2022` from the archive repository, as above.

## Not vendored, by design

`FeedbackSynth_main.cpp`, `Source/FeedbackSynthControls.{h,cpp}` (bound to libDaisy), `Source/memory/*` (Daisy SDRAM pool; replaced by `src/shim/memory/sdram_alloc.h`), `Makefile`, `.vscode`, `doc/` images, `README.md`, and the `lib/` submodules (DaisySP is vendored separately from its own repository; libDaisy is not used).

## Rules

Do not edit any file in this directory. `scripts/verify_vendor.sh` re-fetches the commit and checks every file byte for byte.
