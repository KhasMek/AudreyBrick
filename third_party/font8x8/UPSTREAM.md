# font8x8: upstream record

| Item | Value |
|---|---|
| Project | font8x8 by Daniel Hepper (dhepper) |
| Source URL | https://github.com/dhepper/font8x8 |
| Commit | `8e279d2d864e79128e96188a6b9526cfa3fbfef9` (committed 2019-07-08, "Merge pull request #4 from WillGreen/master"), the head of `master` when fetched |
| Date fetched | 2026-11-17 |
| Licence | Public Domain. The repository has no separate licence file; the statement is in `README` ("Author: Daniel Hepper <daniel@hepper.net>", "License: Public Domain", and in its credits the original header: Marcel Sondaar, International Business Machines (public domain VGA fonts), "License: Public Domain") and in the header comment of `font8x8_basic.h` |
| Licence file | `licenses/font8x8.README`: the upstream `README`, unchanged |

## Files vendored (byte-identical to the commit above)

```
font8x8_basic.h
README
```

## Use

`src/ui/Font.cpp` includes `font8x8_basic.h` once, inside an unnamed namespace, so the table has internal linkage. Only ASCII 32 to 126 are used. The table is declared `char` with initialisers up to `0xFF`, which is a narrowing error in C++ where `char` is signed (macOS arm64), so that one source file is compiled with `-funsigned-char`; the header itself is not modified.
