// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PRESET-A (docs/briefs/PRESET.md): the file operations the preset store uses,
// behind an interface so the tests can inject failures at any step (a failed
// write, a failed rename between writing and renaming, a full card). SDL-free.
// PosixPresetFs is the real one. Called on the UI thread (or a tool's main
// thread), never on the audio thread.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace audreybrick
{

class PresetFs
{
  public:
    enum class Kind
    {
        File,  // a regular file (not followed through a symbolic link)
        Dir,
        Other, // symbolic link, FIFO, device, socket, or unknown
    };
    struct Entry
    {
        std::string name;
        Kind        kind = Kind::Other;
    };
    enum class Status
    {
        Ok,
        Missing,  // the path does not exist
        WrongKind, // list: not a folder; readFile: not a regular file
        TooLarge, // readFile: more than maxBytes
        Failed,   // anything else (error set)
    };

    virtual ~PresetFs() = default;

    // The entries of `dir` (no "." or ".."), in no particular order.
    virtual Status list(const std::string& dir, std::vector<Entry>& out, std::string& error) = 0;
    // Reads the whole of a regular file of at most `maxBytes` bytes.
    virtual Status readFile(const std::string& path, std::size_t maxBytes, std::string& out, std::string& error) = 0;
    // Creates `dir` and any missing parents. True if it is a folder afterwards.
    virtual bool makeDirs(const std::string& dir, std::string& error) = 0;
    // Whether anything exists at `path` (symbolic links not followed).
    virtual bool exists(const std::string& path) = 0;

    // The atomic write, step by step (PresetStore::writeAtomic calls them in
    // this order and removes the temporary file if any step fails). A failing
    // step leaves errno set to the cause (ENOSPC, EROFS, EACCES, ...), which
    // the store turns into the short reason:
    // Creates or truncates `path` for writing. Returns a handle >= 0, or -1.
    virtual int  createTemp(const std::string& path, std::string& error) = 0;
    // Writes all `bytes`; false (error set) if it could not.
    virtual bool writeAll(int handle, const char* data, std::size_t bytes, std::string& error) = 0;
    virtual bool sync(int handle, std::string& error) = 0;
    virtual bool close(int handle, std::string& error) = 0;
    // Replaces `to` with `from` in one step (POSIX rename).
    virtual bool rename(const std::string& from, const std::string& to, std::string& error) = 0;
    // Flushes the folder's entries (so the rename survives a power cut).
    // Best effort: some file systems refuse it; the store ignores the result.
    virtual bool syncDir(const std::string& dir, std::string& error) = 0;

    virtual bool remove(const std::string& path, std::string& error) = 0;
};

class PosixPresetFs : public PresetFs
{
  public:
    Status list(const std::string& dir, std::vector<Entry>& out, std::string& error) override;
    Status readFile(const std::string& path, std::size_t maxBytes, std::string& out, std::string& error) override;
    bool   makeDirs(const std::string& dir, std::string& error) override;
    bool   exists(const std::string& path) override;
    int    createTemp(const std::string& path, std::string& error) override;
    bool   writeAll(int handle, const char* data, std::size_t bytes, std::string& error) override;
    bool   sync(int handle, std::string& error) override;
    bool   close(int handle, std::string& error) override;
    bool   rename(const std::string& from, const std::string& to, std::string& error) override;
    bool   syncDir(const std::string& dir, std::string& error) override;
    bool   remove(const std::string& path, std::string& error) override;
};

} // namespace audreybrick
