// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PRESET-A (docs/briefs/PRESET.md): the presets folder. No SDL. UI thread (or
// a tool's main thread) only; it allocates and does file I/O, so it must never
// be called from the audio thread. Every call is synchronous: a preset is a few
// hundred bytes, and the slowest single write measured on the Brick's card was
// 28 ms, so the worst case costs a UI frame or two and never audio (the audio
// thread does not wait for the UI thread).
//
// The folder:
//   - Listed: regular files (symbolic links are not followed) whose name ends
//     in ".json" in any case and does not start with '.' (hidden files, macOS's
//     "._" files on exFAT cards and our own temporary files are not presets).
//     No recursion.
//   - Order: by the display name (below), case-insensitive, with runs of
//     digits compared as numbers ("preset-2" before "preset-10";
//     "preset-0002" and "preset-2" sort together, ties broken by the bytes);
//     equal display names ("a.json", "a.JSON") by the file name. So the list
//     is the same on every file system, and "preset-0007" lists above
//     "preset-0007 (copy)", "Drone" above "Drone 2".
//   - At most kMaxListed (2000) entries: beyond that the first 2000 in that
//     order are listed, totalFound() says how many there are and truncated()
//     is true. Numbering counts every file, listed or not.
//   - Display name: the file name without its last five characters (".json").
//   - A missing folder is state() Missing and an empty list (not an error);
//     saving creates it (and its parents) if it can.
//
// Numbering: new presets are preset-NNNN.json (%04ld: preset-10000.json after
// preset-9999.json). The next number is one more than the highest number of
// any entry named preset-<1 to 9 digits>.json (case-insensitive) in the folder,
// whatever its kind; 1 when there is none. Numbers stop at kMaxPresetNumber
// (999999999, nine digits, the widest presetNumber() recognises): with
// preset-999999999.json present saveNew() fails with "numbers used up" (and
// changes nothing) until that file is renamed or deleted.
//
// Writing (saveNew, overwrite): the text goes to a temporary file in the same
// folder, is flushed (fsync) and closed, and is then renamed over <name>, and
// the folder is flushed (best effort). The temporary name is ".<name>.tmp";
// if something that is not a regular file has that name (a symbolic link, a
// folder, a FIFO), ".1.<name>.tmp", ".2.<name>.tmp" and ".3.<name>.tmp" are
// tried; if all four are blocked the write fails with "temporary name blocked".
// On a file system where rename() replaces the target atomically (ext4 and
// other POSIX file systems: the KNULLI SHARE partition when it is ext4) a power
// cut leaves the old file or the new one, never half a file under a *.json
// name. On an exFAT (or FAT) card, which the Brick's card may be, rename-over
// is the best technique available but the file system makes no such promise:
// a power cut in the middle of the rename can still lose or damage that one
// file. If any step fails the temporary file is removed and the reason is
// returned; a stale ".<something>.json.tmp" (a regular file) left by a power
// cut is removed by the next scan.
//
// Failure reasons (PresetResult::error.reason, short, for the screen; the
// detail is for the log):
//   every operation: "no preset folder" (no folder was given), "bad name"
//   load:      "not found", "not a file", "too large", "unreadable", and the
//              decoder's reasons (PresetFormat.h: "syntax error", "not a
//              preset", "made by a newer version", "bad value", ...)
//   saveNew:   "no folder" (it could not be created or listed), "read-only",
//              "not allowed", "numbers used up", "name taken" (another
//              program created the new name between the scan and the rename),
//              and the write reasons below
//   overwrite: "folder missing", "unreadable", "not found" (the file has gone),
//              and the write reasons below
//   writes:    "card full", "read-only", "not allowed", "folder missing",
//              "write failed", "temporary name blocked"
//   remove:    "not found", "card full", "read-only", "not allowed",
//              "folder missing", "write failed"
//
// After each change (save, overwrite, delete) the folder is scanned again.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "preset/PresetData.h"
#include "preset/PresetFormat.h"
#include "preset/PresetFs.h"

namespace audreybrick
{

enum class PresetFolderState
{
    NotSet,     // no folder given (the app was started without --preset-dir)
    Ok,
    Missing,    // the folder does not exist (yet)
    NotAFolder, // something that is not a folder has its name
    Unreadable, // it exists but could not be listed (stateDetail() says why)
};

struct PresetEntry
{
    std::string file; // "preset-0007.json"
    std::string name; // "preset-0007"
};

struct PresetResult
{
    bool        ok = false;
    std::string file; // the file acted on (saveNew: the new name)
    PresetError error; // when !ok
};

// "preset-0007.json" -> 7; -1 for any other name (case-insensitive; 1 to 9 digits).
long presetNumber(const std::string& fileName) noexcept;
// 7 -> "preset-0007.json".
std::string presetFileName(long number);
// The list order (see above): < 0, 0 (only for identical names) or > 0.
int presetNameCompare(const std::string& a, const std::string& b) noexcept;
// A name the store lists (not hidden, ends in ".json" in any case).
bool presetListedName(const std::string& fileName) noexcept;
// "preset-0007.json" -> "preset-0007".
std::string presetDisplayName(const std::string& fileName);

// The highest preset number (nine digits; see Numbering above).
constexpr long kMaxPresetNumber = 999999999L;

class PresetStore
{
  public:
    static constexpr std::size_t kMaxListed = 2000;
    // Temporary names tried per write (see Writing above).
    static constexpr int kTempNames = 4;

    // `dir` empty: NotSet (every operation fails with "no preset folder").
    // `appVersion` is written into every saved preset ("app"). Does not scan.
    PresetStore(PresetFs& fs, std::string dir, std::string appVersion);

    const std::string& dir() const noexcept { return dir_; }

    // Lists the folder again (and removes stale temporary files).
    void scan();

    PresetFolderState               state() const noexcept { return state_; }
    const std::string&              stateDetail() const noexcept { return stateDetail_; }
    const std::vector<PresetEntry>& entries() const noexcept { return entries_; }
    std::size_t                     totalFound() const noexcept { return total_; }
    bool                            truncated() const noexcept { return total_ > entries_.size(); }
    long                            nextNumber() const noexcept { return next_; }
    // Temporary files the last scan removed.
    std::size_t staleRemoved() const noexcept { return staleRemoved_; }
    // The index of `file` in entries(), or -1.
    long indexOf(const std::string& file) const noexcept;

    // Reads and decodes `file` (a name in the folder). On failure `out` is unchanged.
    PresetResult load(const std::string& file, PresetData& out);
    // Writes `p` as the next preset-NNNN.json (creating the folder if needed).
    PresetResult saveNew(const PresetData& p);
    // Replaces `file` (which must exist) with `p`.
    PresetResult overwrite(const std::string& file, const PresetData& p);
    // Deletes `file`.
    PresetResult remove(const std::string& file);

  private:
    bool         validName(const std::string& file, PresetResult& r) const;
    bool         writeAtomic(const std::string& file, const std::string& text, bool mustBeNew, PresetResult& r);
    PresetResult fail(const std::string& file, const char* reason, const std::string& detail) const;

    PresetFs&                fs_;
    std::string              dir_;
    std::string              appVersion_;
    PresetFolderState        state_ = PresetFolderState::NotSet;
    std::string              stateDetail_;
    std::vector<PresetEntry> entries_;
    std::size_t              total_        = 0;
    long                     next_         = 1;
    std::size_t              staleRemoved_ = 0;
};

} // namespace audreybrick
