// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT

#include "preset/PresetStore.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <utility>

namespace audreybrick
{
namespace
{
char lower(char c) noexcept
{
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool isDigit(char c) noexcept
{
    return c >= '0' && c <= '9';
}

bool endsWithIgnoreCase(const std::string& s, const char* suffix) noexcept
{
    const std::size_t n = std::strlen(suffix);
    if(s.size() < n)
        return false;
    for(std::size_t i = 0; i < n; ++i)
        if(lower(s[s.size() - n + i]) != suffix[i])
            return false;
    return true;
}

// ".<anything>.json.tmp": what writeAtomic creates (".preset-0003.json.tmp").
bool staleTempName(const std::string& s) noexcept
{
    return s.size() > 10 && s[0] == '.' && endsWithIgnoreCase(s, ".json.tmp");
}

// PRESET-A review: an errno from createTemp() that means "something that is
// not a regular file has this name" (a symbolic link: O_NOFOLLOW; a folder; a
// FIFO or a device: O_NONBLOCK), so another temporary name is tried.
bool tempNameOccupied(int err) noexcept
{
    switch(err)
    {
        case ELOOP:
        case EISDIR:
        case ENXIO:
#ifdef EFTYPE
        case EFTYPE:
#endif
#if defined(EMLINK) && defined(__FreeBSD__)
        case EMLINK:
#endif
            return true;
        default: return false;
    }
}

// The short reason for an errno from a failed write step.
const char* writeReason(int err) noexcept
{
    switch(err)
    {
        case ENOSPC:
#ifdef EDQUOT
        case EDQUOT:
#endif
            return "card full";
        case EROFS: return "read-only";
        case EACCES:
        case EPERM: return "not allowed";
        case ENOENT:
        case ENOTDIR: return "folder missing";
        default: return "write failed";
    }
}
} // namespace

long presetNumber(const std::string& name) noexcept
{
    // preset-<1..9 digits>.json
    const std::size_t n = name.size();
    if(n < 13 || n > 21)
        return -1;
    const char* pre = "preset-";
    for(std::size_t i = 0; i < 7; ++i)
        if(lower(name[i]) != pre[i])
            return -1;
    if(!endsWithIgnoreCase(name, ".json"))
        return -1;
    long v = 0;
    for(std::size_t i = 7; i < n - 5; ++i)
    {
        if(!isDigit(name[i]))
            return -1;
        v = v * 10 + (name[i] - '0');
    }
    return v;
}

std::string presetFileName(long number)
{
    char buf[40];
    std::snprintf(buf, sizeof buf, "preset-%04ld.json", number);
    return buf;
}

int presetNameCompare(const std::string& a, const std::string& b) noexcept
{
    std::size_t i = 0, j = 0;
    while(i < a.size() && j < b.size())
    {
        if(isDigit(a[i]) && isDigit(b[j]))
        {
            while(i < a.size() && a[i] == '0')
                ++i;
            while(j < b.size() && b[j] == '0')
                ++j;
            std::size_t ei = i, ej = j;
            while(ei < a.size() && isDigit(a[ei]))
                ++ei;
            while(ej < b.size() && isDigit(b[ej]))
                ++ej;
            const std::size_t la = ei - i, lb = ej - j;
            if(la != lb)
                return la < lb ? -1 : 1;
            const int c = la == 0 ? 0 : std::memcmp(a.data() + i, b.data() + j, la);
            if(c != 0)
                return c < 0 ? -1 : 1;
            i = ei;
            j = ej;
            continue;
        }
        const unsigned char ca = static_cast<unsigned char>(lower(a[i]));
        const unsigned char cb = static_cast<unsigned char>(lower(b[j]));
        if(ca != cb)
            return ca < cb ? -1 : 1;
        ++i;
        ++j;
    }
    const bool aDone = i >= a.size(), bDone = j >= b.size();
    if(aDone != bDone)
        return aDone ? -1 : 1;
    // Equal in the natural order ("preset-2" and "preset-0002", "A" and "a"):
    // the bytes decide, so the order is total.
    const int c = a.compare(b);
    return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

bool presetListedName(const std::string& name) noexcept
{
    return name.size() > 5 && name[0] != '.' && endsWithIgnoreCase(name, ".json");
}

std::string presetDisplayName(const std::string& fileName)
{
    return endsWithIgnoreCase(fileName, ".json") ? fileName.substr(0, fileName.size() - 5) : fileName;
}

PresetStore::PresetStore(PresetFs& fs, std::string dir, std::string appVersion)
    : fs_(fs), dir_(std::move(dir)), appVersion_(std::move(appVersion))
{
    // Without a trailing '/', so paths are dir_ + "/" + name.
    while(dir_.size() > 1 && dir_.back() == '/')
        dir_.pop_back();
}

long PresetStore::indexOf(const std::string& file) const noexcept
{
    for(std::size_t i = 0; i < entries_.size(); ++i)
        if(entries_[i].file == file)
            return static_cast<long>(i);
    return -1;
}

void PresetStore::scan()
{
    entries_.clear();
    total_        = 0;
    next_         = 1;
    staleRemoved_ = 0;
    stateDetail_.clear();
    if(dir_.empty())
    {
        state_ = PresetFolderState::NotSet;
        return;
    }
    std::vector<PresetFs::Entry> list;
    std::string                  err;
    switch(fs_.list(dir_, list, err))
    {
        case PresetFs::Status::Ok: break;
        case PresetFs::Status::Missing:
            state_       = PresetFolderState::Missing;
            stateDetail_ = err;
            return;
        case PresetFs::Status::WrongKind:
            state_       = PresetFolderState::NotAFolder;
            stateDetail_ = err;
            return;
        default:
            state_       = PresetFolderState::Unreadable;
            stateDetail_ = err;
            return;
    }
    state_ = PresetFolderState::Ok;

    long highest = 0;
    for(const PresetFs::Entry& e : list)
    {
        const long n = presetNumber(e.name);
        if(n > highest)
            highest = n;
        if(e.kind == PresetFs::Kind::File && staleTempName(e.name))
        {
            std::string rerr;
            if(fs_.remove(dir_ + "/" + e.name, rerr))
                ++staleRemoved_;
            continue;
        }
        if(e.kind == PresetFs::Kind::File && presetListedName(e.name))
            entries_.push_back(PresetEntry{e.name, presetDisplayName(e.name)});
    }
    next_ = highest + 1;
    // PRESET-A review: by the name shown (without ".json"), so "preset-0007"
    // lists above "preset-0007 (copy)" and "Drone" above "Drone 2"; two files
    // with the same display name ("a.json", "a.JSON") by their file names.
    std::sort(entries_.begin(), entries_.end(), [](const PresetEntry& a, const PresetEntry& b) {
        const int c = presetNameCompare(a.name, b.name);
        return c != 0 ? c < 0 : presetNameCompare(a.file, b.file) < 0;
    });
    total_ = entries_.size();
    if(entries_.size() > kMaxListed)
        entries_.resize(kMaxListed);
}

PresetResult PresetStore::fail(const std::string& file, const char* reason, const std::string& detail) const
{
    PresetResult r;
    r.ok           = false;
    r.file         = file;
    r.error.reason = reason;
    r.error.detail = detail;
    return r;
}

bool PresetStore::validName(const std::string& file, PresetResult& r) const
{
    if(dir_.empty())
    {
        r = fail(file, "no preset folder", "no preset folder was given");
        return false;
    }
    if(!presetListedName(file) || file.find('/') != std::string::npos || file.find('\0') != std::string::npos)
    {
        r = fail(file, "bad name", "\"" + file + "\" is not a preset file name");
        return false;
    }
    return true;
}

bool PresetStore::writeAtomic(const std::string& file, const std::string& text, bool mustBeNew, PresetResult& r)
{
    const std::string target = dir_ + "/" + file;
    // The temporary name: ".<file>.tmp"; if something that is not a regular
    // file has that name (a link or a folder someone made), ".1.<file>.tmp" up
    // to ".<kTempNames - 1>.<file>.tmp". Every candidate starts with '.' and
    // ends in ".json.tmp", so it is never listed and a stale one (a regular
    // file) is removed by the next scan.
    std::string tmp;
    std::string err;
    int         handle = -1;
    for(int i = 0; i < kTempNames && handle < 0; ++i)
    {
        tmp    = dir_ + "/." + (i == 0 ? std::string() : std::to_string(i) + ".") + file + ".tmp";
        handle = fs_.createTemp(tmp, err);
        if(handle < 0 && !tempNameOccupied(errno))
        {
            r = fail(file, writeReason(errno), err);
            return false;
        }
    }
    if(handle < 0)
    {
        r = fail(file, "temporary name blocked",
                 "every temporary name for " + file + " is taken by something that is not a file (last: " + err + ")");
        return false;
    }
    bool        ok   = true;
    const char* step = nullptr;
    int         code = 0;
    if(!fs_.writeAll(handle, text.data(), text.size(), err))
    {
        ok   = false;
        code = errno;
    }
    else if(!fs_.sync(handle, err))
    {
        ok   = false;
        code = errno;
    }
    std::string cerr;
    if(!fs_.close(handle, cerr) && ok)
    {
        ok   = false;
        code = errno;
        err  = cerr;
    }
    if(ok && mustBeNew && fs_.exists(target))
    {
        ok   = false;
        step = "name taken";
        err  = target + " already exists";
    }
    if(ok && !fs_.rename(tmp, target, err))
    {
        ok   = false;
        code = errno;
    }
    if(!ok)
    {
        std::string rerr;
        fs_.remove(tmp, rerr); // never leave the temporary file behind
        r = fail(file, step != nullptr ? step : writeReason(code), err);
        return false;
    }
    std::string derr;
    fs_.syncDir(dir_, derr); // best effort (see PresetFs.h)
    return true;
}

PresetResult PresetStore::load(const std::string& file, PresetData& out)
{
    PresetResult r;
    if(!validName(file, r))
        return r;
    std::string text, err;
    switch(fs_.readFile(dir_ + "/" + file, kMaxPresetBytes, text, err))
    {
        case PresetFs::Status::Ok: break;
        case PresetFs::Status::Missing: return fail(file, "not found", err);
        case PresetFs::Status::WrongKind: return fail(file, "not a file", err);
        case PresetFs::Status::TooLarge: return fail(file, "too large", err + " (64 KiB)");
        default: return fail(file, "unreadable", err);
    }
    PresetData  p;
    PresetError pe;
    if(!decodePreset(text, p, pe))
    {
        r.ok    = false;
        r.file  = file;
        r.error = pe;
        return r;
    }
    out   = p;
    r.ok  = true;
    r.file = file;
    return r;
}

PresetResult PresetStore::saveNew(const PresetData& p)
{
    if(dir_.empty())
        return fail("", "no preset folder", "no preset folder was given");
    std::string err;
    if(!fs_.makeDirs(dir_, err))
    {
        const int code = errno;
        scan();
        return fail("", code == EROFS ? "read-only" : (code == EACCES || code == EPERM ? "not allowed" : "no folder"),
                    err);
    }
    scan(); // the next number from the folder as it is now
    if(state_ != PresetFolderState::Ok)
        return fail("", "no folder", stateDetail_);
    // PRESET-A review: numbers have at most kMaxPresetDigits digits (what
    // presetNumber() recognises); past preset-999999999.json there is no next one.
    if(next_ > kMaxPresetNumber)
        return fail("", "numbers used up",
                    "preset-" + std::to_string(kMaxPresetNumber) + ".json exists; rename or delete it to save new presets");
    const std::string file = presetFileName(next_);
    PresetResult      r;
    const bool        ok = writeAtomic(file, encodePreset(p, appVersion_), true, r);
    scan();
    if(!ok)
        return r;
    r.ok   = true;
    r.file = file;
    return r;
}

PresetResult PresetStore::overwrite(const std::string& file, const PresetData& p)
{
    PresetResult r;
    if(!validName(file, r))
        return r;
    // The list may be stale (a file renamed or deleted on a computer): only an
    // existing regular file is replaced, never a new one created under its name.
    std::vector<PresetFs::Entry> list;
    std::string                  err;
    const PresetFs::Status       st = fs_.list(dir_, list, err);
    if(st != PresetFs::Status::Ok)
    {
        scan();
        return fail(file, st == PresetFs::Status::Missing ? "folder missing" : "unreadable", err);
    }
    const auto it = std::find_if(list.begin(), list.end(), [&](const PresetFs::Entry& e) { return e.name == file; });
    if(it == list.end() || it->kind != PresetFs::Kind::File)
    {
        scan();
        return fail(file, "not found", dir_ + "/" + file + " is not there any more");
    }
    const bool ok = writeAtomic(file, encodePreset(p, appVersion_), false, r);
    scan();
    if(!ok)
        return r;
    r.ok   = true;
    r.file = file;
    return r;
}

PresetResult PresetStore::remove(const std::string& file)
{
    PresetResult r;
    if(!validName(file, r))
        return r;
    std::string err;
    const bool  ok   = fs_.remove(dir_ + "/" + file, err);
    const int   code = errno;
    scan();
    if(!ok)
        return fail(file, code == ENOENT ? "not found" : writeReason(code), err);
    r.ok   = true;
    r.file = file;
    return r;
}

} // namespace audreybrick
