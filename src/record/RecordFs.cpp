// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "record/RecordFs.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace audreybrick
{
namespace
{
std::string errText(const char* what, int err)
{
    return std::string(what) + ": " + std::strerror(err);
}

bool isDir(const std::string& p)
{
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
} // namespace

bool PosixRecordFs::makeDirs(const std::string& dir, std::string& error)
{
    if(dir.empty())
    {
        error = "no folder";
        return false;
    }
    // Each prefix ending before a '/', then the whole path.
    for(std::size_t i = 1; i <= dir.size(); ++i)
    {
        if(i < dir.size() && dir[i] != '/')
            continue;
        const std::string p = dir.substr(0, i);
        if(::mkdir(p.c_str(), 0777) != 0 && errno != EEXIST)
        {
            error = errText(("mkdir " + p).c_str(), errno);
            return false;
        }
    }
    if(!isDir(dir))
    {
        error = dir + " is not a folder";
        return false;
    }
    return true;
}

bool PosixRecordFs::list(const std::string& dir, std::vector<std::string>& names, std::string& error)
{
    names.clear();
    DIR* d = ::opendir(dir.c_str());
    if(d == nullptr)
    {
        error = errText(("opendir " + dir).c_str(), errno);
        return false;
    }
    while(struct dirent* e = ::readdir(d))
    {
        if(std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0)
            continue;
        names.emplace_back(e->d_name);
    }
    ::closedir(d);
    return true;
}

int PosixRecordFs::createNew(const std::string& path, bool& exists, std::string& error)
{
    exists       = false;
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if(fd < 0)
    {
        exists = errno == EEXIST;
        error  = errText(("create " + path).c_str(), errno);
    }
    return fd;
}

long PosixRecordFs::write(int handle, const void* data, std::size_t bytes, std::string& error)
{
    const ssize_t n = ::write(handle, data, bytes);
    if(n < 0)
    {
        error = errText("write", errno);
        return -1;
    }
    if(static_cast<std::size_t>(n) < bytes)
        error = "short write (" + std::to_string(n) + " of " + std::to_string(bytes) + " bytes)";
    return static_cast<long>(n);
}

bool PosixRecordFs::writeAt(int handle, const void* data, std::size_t bytes, std::uint64_t offset, std::string& error)
{
    const ssize_t n = ::pwrite(handle, data, bytes, static_cast<off_t>(offset));
    if(n < 0)
    {
        error = errText("pwrite", errno);
        return false;
    }
    if(static_cast<std::size_t>(n) != bytes)
    {
        error = "short pwrite";
        return false;
    }
    return true;
}

bool PosixRecordFs::sync(int handle, std::string& error)
{
#if defined(__linux__)
    const int r = ::fdatasync(handle); // also writes the file size (metadata needed to read the data)
#else
    const int r = ::fsync(handle);
#endif
    if(r != 0)
    {
        error = errText("sync", errno);
        return false;
    }
    return true;
}

bool PosixRecordFs::close(int handle, std::string& error)
{
    if(::close(handle) != 0)
    {
        error = errText("close", errno);
        return false;
    }
    return true;
}

bool PosixRecordFs::remove(const std::string& path, std::string& error)
{
    if(::unlink(path.c_str()) != 0)
    {
        error = errText(("unlink " + path).c_str(), errno);
        return false;
    }
    return true;
}

bool PosixRecordFs::freeBytes(const std::string& dir, std::uint64_t& bytes, std::string& error)
{
    struct statvfs st;
    if(::statvfs(dir.c_str(), &st) != 0)
    {
        error = errText(("statvfs " + dir).c_str(), errno);
        return false;
    }
    bytes = static_cast<std::uint64_t>(st.f_bavail) * static_cast<std::uint64_t>(st.f_frsize);
    return true;
}

} // namespace audreybrick
