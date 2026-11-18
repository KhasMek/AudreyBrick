// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// M3-B: writeRestore (--restore-on-exit <file> <content> <expected>) in a
// temporary directory:
//   - writes the content and a newline when the file still holds <expected>
//     (replacing what was there, as ">" does), and says so;
//   - does NOT write when the file holds something else (changed by someone else)
//     and says so;
//   - a second restore after the first finds the content, not <expected>, so it is
//     harmless and writes nothing;
//   - a missing file is NOT created; a missing directory neither;
//   - an unreadable file (mode 000; skipped as root), a read-only file (mode 0444;
//     skipped as root) and a directory fail cleanly;
//   - an empty file name fails cleanly;
//   - isRestoreValue: [a-z0-9_]{1,32} only.

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "app/RestoreOnExit.h"
#include "platform_test_common.h"

using namespace audreybrick;
using ptest::check;

namespace
{
std::string readAll(const std::string& path)
{
    std::ifstream     in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool exists(const std::string& path)
{
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

void put(const std::string& path, const std::string& text)
{
    std::ofstream out(path);
    out << text;
}

RestoreAction act(const std::string& file, const char* content, const char* expected)
{
    RestoreAction a;
    a.file     = file;
    a.content  = content;
    a.expected = expected;
    return a;
}
} // namespace

int main()
{
    // --- isRestoreValue ----------------------------------------------------------------
    check(isRestoreValue("schedutil") && isRestoreValue("performance") && isRestoreValue("a") &&
              isRestoreValue("abc_123") && isRestoreValue(std::string(32, 'x')),
          "isRestoreValue accepts governor names");
    check(!isRestoreValue("") && !isRestoreValue(std::string(33, 'x')) && !isRestoreValue("--swap-ab") &&
              !isRestoreValue("Performance") && !isRestoreValue("perf ormance") && !isRestoreValue("a;b") &&
              !isRestoreValue("../x") && !isRestoreValue("a\n"),
          "isRestoreValue rejects empty, too long, options, capitals, spaces and punctuation");

    char        tmpl[] = "/tmp/audreybrick-restore-XXXXXX";
    const char* dir    = mkdtemp(tmpl);
    if(dir == nullptr)
    {
        check(false, "temporary directory");
        return ptest::finish();
    }
    const std::string d   = dir;
    const std::string gov = d + "/scaling_governor";
    put(gov, "performance\n");

    std::string msg;
    check(writeRestore(act(gov, "schedutil", "performance"), msg) && readAll(gov) == "schedutil\n",
          "still holds the expected value: writes the content and a newline");
    check(msg == "restore-on-exit: wrote 'schedutil' to " + gov + " (it held 'performance')", "message: " + msg);
    check(!writeRestore(act(gov, "schedutil", "performance"), msg) && readAll(gov) == "schedutil\n" &&
              msg.find("changed by someone else to 'schedutil'") != std::string::npos,
          "a second restore finds the content, writes nothing (harmless): " + msg);
    put(gov, "powersave\n");
    check(!writeRestore(act(gov, "schedutil", "performance"), msg) && readAll(gov) == "powersave\n",
          "changed by someone else: not restored");
    check(msg == "restore-on-exit: " + gov + ": changed by someone else to 'powersave' (expected 'performance'); not "
                 "restoring 'schedutil'",
          "message: " + msg);
    put(gov, "performance and a much longer previous content\n");
    check(writeRestore(act(gov, "ondemand", "performance"), msg) && readAll(gov) == "ondemand\n",
          "only the first word is compared; the file is truncated");

    const std::string missing = d + "/no-such-file";
    check(!writeRestore(act(missing, "schedutil", "performance"), msg) && !exists(missing),
          "a missing file is not created");
    check(msg.find(missing) != std::string::npos && msg.find("No such file") != std::string::npos &&
              msg.find("not fatal") != std::string::npos,
          "the failure is described: " + msg);
    const std::string missingDir = d + "/no-dir/scaling_governor";
    check(!writeRestore(act(missingDir, "x", "y"), msg) && !exists(d + "/no-dir"), "a missing directory is not created");

    if(::geteuid() == 0)
    {
        std::printf("(skipped) unreadable and read-only file cases: running as root\n");
    }
    else
    {
        const std::string ro = d + "/read-only";
        put(ro, "performance\n");
        ::chmod(ro.c_str(), 0444);
        check(!writeRestore(act(ro, "schedutil", "performance"), msg) && readAll(ro) == "performance\n",
              "a read-only file: not written, reported: " + msg);
        check(msg.find("Permission denied") != std::string::npos, "permission denied named");
        ::chmod(ro.c_str(), 0);
        check(!writeRestore(act(ro, "schedutil", "performance"), msg) &&
                  msg.find("cannot read the current value (Permission denied); not restoring") != std::string::npos,
              "an unreadable file: not written: " + msg);
        ::chmod(ro.c_str(), 0644);
        check(readAll(ro) == "performance\n", "the unreadable file was left as it was");
        std::remove(ro.c_str());
    }
    check(!writeRestore(act(d, "x", "y"), msg), "a directory: fails cleanly: " + msg);
    check(!writeRestore(act("", "x", "y"), msg) && msg.find("empty file name") != std::string::npos, "empty file name");

    std::remove(gov.c_str());
    ::rmdir(dir);
    return ptest::finish();
}
