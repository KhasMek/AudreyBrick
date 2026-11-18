// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// Restore-on-exit (M3-B): `--restore-on-exit <file> <content> <expected>` asks the
// app to write <content> back to <file> when it shuts down, but ONLY if the file
// still holds <expected> (the value the launcher wrote there). The launcher
// passes one per CPU frequency policy whose governor it changed: the governor it
// found (<content>) and the one it set (<expected>). So the system's governor
// comes back even if the launcher itself was killed with SIGKILL (the app then
// gets SIGTERM through --exit-with-parent and shuts down normally), and a change
// made meanwhile by someone else (KNULLI's gameStop hook can run while the app
// shuts down) is left alone. The launcher restores with the same rule; the second
// restore then finds nothing to do.
//
// writeRestore():
//   - reads the file's current content (first word); if it cannot be read, or is
//     not <expected>, nothing is written;
//   - otherwise opens the EXISTING file for writing and truncates it, as the
//     shell's ">" does; it never creates a file (no O_CREAT);
//   - writes <content> followed by a newline in one write() call;
//   - never throws and never ends the app: every outcome is returned with a
//     sentence for the log in `message` (true only if the content was written).
// isRestoreValue(): the values the app accepts on the command line for <content>
// and <expected>: 1 to 32 characters from [a-z0-9_] (governor names).
// Runs on the main thread during shutdown; never from the audio callback.

#pragma once

#include <string>

namespace audreybrick
{

struct RestoreAction
{
    std::string file;     // absolute path
    std::string content;  // the value to write back
    std::string expected; // write only if the file holds this now
};

bool isRestoreValue(const std::string& v) noexcept;

bool writeRestore(const RestoreAction& a, std::string& message);

} // namespace audreybrick
