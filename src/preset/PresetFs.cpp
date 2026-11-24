// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "preset/PresetFs.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace audreybrick
{
namespace
{
// Leaves errno as `err`, so the store can classify the failure (PresetFs.h).
std::string errText(const std::string& what, int err)
{
    std::string s = what + ": " + std::strerror(err);
    errno         = err;
    return s;
}

PresetFs::Kind kindOf(mode_t m) noexcept
{
    if(S_ISREG(m))
        return PresetFs::Kind::File;
    if(S_ISDIR(m))
        return PresetFs::Kind::Dir;
    return PresetFs::Kind::Other;
}
} // namespace

PresetFs::Status PosixPresetFs::list(const std::string& dir, std::vector<Entry>& out, std::string& error)
{
    out.clear();
    struct stat st;
    if(::stat(dir.c_str(), &st) != 0)
    {
        const int e = errno;
        error       = errText("stat " + dir, e);
        return e == ENOENT || e == ENOTDIR ? Status::Missing : Status::Failed;
    }
    if(!S_ISDIR(st.st_mode))
    {
        error = dir + " is not a folder";
        return Status::WrongKind;
    }
    DIR* d = ::opendir(dir.c_str());
    if(d == nullptr)
    {
        error = errText("opendir " + dir, errno);
        return Status::Failed;
    }
    errno = 0;
    while(struct dirent* e = ::readdir(d))
    {
        if(std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0)
            continue;
        Entry en;
        en.name = e->d_name;
        struct stat es;
        // d_type is DT_UNKNOWN on some file systems: lstat decides (links are not followed).
        if(::lstat((dir + "/" + en.name).c_str(), &es) == 0)
            en.kind = kindOf(es.st_mode);
        out.push_back(std::move(en));
        errno = 0;
    }
    const int rerr = errno;
    ::closedir(d);
    if(rerr != 0)
    {
        error = errText("readdir " + dir, rerr);
        return Status::Failed;
    }
    return Status::Ok;
}

PresetFs::Status PosixPresetFs::readFile(const std::string& path, std::size_t maxBytes, std::string& out,
                                         std::string& error)
{
    out.clear();
    // O_NONBLOCK: opening a FIFO never waits; O_NOFOLLOW: a link is not a preset.
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
    if(fd < 0)
    {
        const int e = errno;
        error       = errText("open " + path, e);
        if(e == ENOENT)
            return Status::Missing;
        if(e == ELOOP)
            return Status::WrongKind;
        return Status::Failed;
    }
    struct stat st;
    if(::fstat(fd, &st) != 0)
    {
        error = errText("fstat " + path, errno);
        ::close(fd);
        return Status::Failed;
    }
    if(!S_ISREG(st.st_mode))
    {
        error = path + " is not a regular file";
        ::close(fd);
        return Status::WrongKind;
    }
    if(st.st_size > static_cast<off_t>(maxBytes))
    {
        error = path + " is larger than the limit";
        ::close(fd);
        return Status::TooLarge;
    }
    // Read up to maxBytes + 1, so a file that grew after fstat is caught too.
    char buf[4096];
    for(;;)
    {
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if(n < 0)
        {
            if(errno == EINTR)
                continue;
            error = errText("read " + path, errno);
            ::close(fd);
            return Status::Failed;
        }
        if(n == 0)
            break;
        out.append(buf, static_cast<std::size_t>(n));
        if(out.size() > maxBytes)
        {
            error = path + " is larger than the limit";
            ::close(fd);
            return Status::TooLarge;
        }
    }
    ::close(fd);
    return Status::Ok;
}

bool PosixPresetFs::makeDirs(const std::string& dir, std::string& error)
{
    if(dir.empty())
    {
        error = "no folder";
        return false;
    }
    for(std::size_t i = 1; i <= dir.size(); ++i)
    {
        if(i < dir.size() && dir[i] != '/')
            continue;
        const std::string p = dir.substr(0, i);
        if(::mkdir(p.c_str(), 0777) != 0 && errno != EEXIST)
        {
            error = errText("mkdir " + p, errno);
            return false;
        }
    }
    struct stat st;
    if(::stat(dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
    {
        error = dir + " is not a folder";
        return false;
    }
    return true;
}

bool PosixPresetFs::exists(const std::string& path)
{
    struct stat st;
    return ::lstat(path.c_str(), &st) == 0;
}

int PosixPresetFs::createTemp(const std::string& path, std::string& error)
{
    // O_NOFOLLOW: a symbolic link with this name fails (ELOOP); O_NONBLOCK: a
    // FIFO with this name fails at once (ENXIO) instead of waiting for a
    // reader. The store then tries another temporary name (PresetStore.h).
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0666);
    if(fd < 0)
    {
        error = errText("create " + path, errno);
        return fd;
    }
    const int fl = ::fcntl(fd, F_GETFL);
    if(fl != -1)
        ::fcntl(fd, F_SETFL, fl & ~O_NONBLOCK); // ordinary blocking writes from here
    return fd;
}

bool PosixPresetFs::writeAll(int handle, const char* data, std::size_t bytes, std::string& error)
{
    std::size_t done = 0;
    while(done < bytes)
    {
        const ssize_t n = ::write(handle, data + done, bytes - done);
        if(n < 0)
        {
            if(errno == EINTR)
                continue;
            error = errText("write", errno);
            return false;
        }
        if(n == 0)
        {
            error = "write: no progress";
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

bool PosixPresetFs::sync(int handle, std::string& error)
{
    if(::fsync(handle) != 0)
    {
        error = errText("fsync", errno);
        return false;
    }
    return true;
}

bool PosixPresetFs::close(int handle, std::string& error)
{
    if(::close(handle) != 0)
    {
        error = errText("close", errno);
        return false;
    }
    return true;
}

bool PosixPresetFs::rename(const std::string& from, const std::string& to, std::string& error)
{
    if(std::rename(from.c_str(), to.c_str()) != 0)
    {
        error = errText("rename", errno);
        return false;
    }
    return true;
}

bool PosixPresetFs::syncDir(const std::string& dir, std::string& error)
{
    const int fd = ::open(dir.c_str(), O_RDONLY | O_CLOEXEC);
    if(fd < 0)
    {
        error = errText("open " + dir, errno);
        return false;
    }
    const bool ok = ::fsync(fd) == 0;
    if(!ok)
        error = errText("fsync " + dir, errno);
    ::close(fd);
    return ok;
}

bool PosixPresetFs::remove(const std::string& path, std::string& error)
{
    if(::unlink(path.c_str()) != 0)
    {
        error = errText("unlink " + path, errno);
        return false;
    }
    return true;
}

} // namespace audreybrick
