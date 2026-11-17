# dr_wav: upstream record

| Item | Value |
|---|---|
| Project | dr_libs by David Reid (mackron) |
| Source URL | https://github.com/mackron/dr_libs |
| Commit | `35b5c392cc15456736c440a68dbe073178bbffe0` (committed 2026-10-31, "dr_wav: Version 0.14.6") |
| Version | dr_wav v0.14.6 (2026-10-31), the last released version at the date fetched. The later commit `ebfea72` is marked "v0.14.7 - TBD" and changes only metadata parsing, which we do not use |
| Date fetched | 2026-11-17 |
| Licence | The file offers a choice of Public Domain (Unlicense) or MIT No Attribution (MIT-0). **We use MIT-0** (Alternative 2, "Copyright 2023 David Reid") |
| Licence file | `licenses/dr_wav.LICENSE`: lines 8658 to 8704 of `dr_wav.h`, the licence statement at the end of the file, unchanged (both alternatives, as published) |

## Files vendored (byte-identical to the commit above)

```
dr_wav.h
```

## Use

Only by the offline tools and tests (`src/tools`, `tests/parity`): reading and writing WAV files. Never by `audrey_core`, `audrey_dsp`, `audrey_lgpl` or anything on the audio path. The implementation is compiled once, in `src/tools/dr_wav_impl.cpp`, with warnings disabled.
