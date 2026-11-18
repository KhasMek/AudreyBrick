// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "app/RestoreOnExit.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <string>

namespace audreybrick
{
namespace
{
// The file's content up to the first whitespace (leading whitespace skipped).
// False with errno's text in `why` if it cannot be opened or read.
bool readFirstWord(const std::string& path, std::string& word, std::string& why)
{
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if(fd < 0)
    {
        why = std::strerror(errno);
        return false;
    }
    char    buf[128];
    ssize_t n = -1;
    do
        n = ::read(fd, buf, sizeof buf - 1);
    while(n < 0 && errno == EINTR);
    const int err = errno;
    ::close(fd);
    if(n < 0)
    {
        why = std::strerror(err);
        return false;
    }
    buf[n]          = '\0';
    const char* b   = buf;
    while(*b == ' ' || *b == '\t' || *b == '\n' || *b == '\r')
        ++b;
    const char* e = b;
    while(*e != '\0' && *e != ' ' && *e != '\t' && *e != '\n' && *e != '\r')
        ++e;
    word.assign(b, static_cast<std::size_t>(e - b));
    return true;
}
} // namespace

bool isRestoreValue(const std::string& v) noexcept
{
    if(v.empty() || v.size() > 32)
        return false;
    for(const char c : v)
        if(!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
            return false;
    return true;
}

bool writeRestore(const RestoreAction& a, std::string& message)
{
    if(a.file.empty())
    {
        message = "restore-on-exit: empty file name (nothing written)";
        return false;
    }
    std::string current;
    std::string why;
    if(!readFirstWord(a.file, current, why))
    {
        message = "restore-on-exit: " + a.file + ": cannot read the current value (" + why +
                  "); not restoring (not fatal)";
        return false;
    }
    if(current != a.expected)
    {
        message = "restore-on-exit: " + a.file + ": changed by someone else to '" + current + "' (expected '" +
                  a.expected + "'); not restoring '" + a.content + "'";
        return false;
    }
    const int fd = ::open(a.file.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC); // never O_CREAT
    if(fd < 0)
    {
        message = "restore-on-exit: " + a.file + ": " + std::strerror(errno) + " (not written; not fatal)";
        return false;
    }
    const std::string text = a.content + "\n";
    ssize_t           n    = -1;
    do
        n = ::write(fd, text.data(), text.size());
    while(n < 0 && errno == EINTR);
    const int writeErr = n < 0 ? errno : 0;
    const int closeRc  = ::close(fd);
    const int closeErr = closeRc != 0 ? errno : 0;
    if(n < 0 || static_cast<std::size_t>(n) != text.size() || closeRc != 0)
    {
        const char* reason = n < 0           ? std::strerror(writeErr)
                             : closeRc != 0 ? std::strerror(closeErr)
                                            : "short write";
        message = "restore-on-exit: writing '" + a.content + "' to " + a.file + " failed: " + reason + " (not fatal)";
        return false;
    }
    message = "restore-on-exit: wrote '" + a.content + "' to " + a.file + " (it held '" + current + "')";
    return true;
}

} // namespace audreybrick
