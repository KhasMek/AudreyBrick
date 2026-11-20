// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// RecordFs: the file and free-space operations the recording's writer thread
// uses (docs/briefs/REC.md), behind an interface so the tests can inject full
// cards, failing writes and size limits without writing gigabytes. SDL-free.
// PosixRecordFs is the real one (POSIX: mkdir, opendir, open with O_EXCL, write,
// pwrite, fsync/fdatasync, close, unlink, statvfs). Writer thread only.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace audreybrick
{

class RecordFs
{
  public:
    virtual ~RecordFs() = default;

    // Creates `dir` and any missing parents. True if it exists afterwards.
    virtual bool makeDirs(const std::string& dir, std::string& error) = 0;
    // The names of the entries in `dir` (no "." or "..").
    virtual bool list(const std::string& dir, std::vector<std::string>& names, std::string& error) = 0;
    // Creates `path`, failing if it exists. Returns a handle >= 0, or -1 with
    // `exists` set if the name was taken (and `error` set otherwise).
    virtual int createNew(const std::string& path, bool& exists, std::string& error) = 0;
    // Appends at the end. Returns the bytes written, or -1 (error set).
    virtual long write(int handle, const void* data, std::size_t bytes, std::string& error) = 0;
    // Writes at `offset` without moving the append position. True if all were written.
    virtual bool writeAt(int handle, const void* data, std::size_t bytes, std::uint64_t offset, std::string& error) = 0;
    // Flushes the file's data (and size) to the card.
    virtual bool sync(int handle, std::string& error) = 0;
    virtual bool close(int handle, std::string& error) = 0;
    virtual bool remove(const std::string& path, std::string& error) = 0;
    // Bytes free for an unprivileged user on the filesystem holding `dir`.
    virtual bool freeBytes(const std::string& dir, std::uint64_t& bytes, std::string& error) = 0;
};

class PosixRecordFs final : public RecordFs
{
  public:
    bool makeDirs(const std::string& dir, std::string& error) override;
    bool list(const std::string& dir, std::vector<std::string>& names, std::string& error) override;
    int  createNew(const std::string& path, bool& exists, std::string& error) override;
    long write(int handle, const void* data, std::size_t bytes, std::string& error) override;
    bool writeAt(int handle, const void* data, std::size_t bytes, std::uint64_t offset, std::string& error) override;
    bool sync(int handle, std::string& error) override;
    bool close(int handle, std::string& error) override;
    bool remove(const std::string& path, std::string& error) override;
    bool freeBytes(const std::string& dir, std::uint64_t& bytes, std::string& error) override;
};

} // namespace audreybrick
