// Copyright (c) 2026 KhasMek
// SPDX-License-Identifier: MIT
//
// PRESET-A: the presets folder (preset/PresetStore.h) on the real file system,
// in folders under the build tree, with failures injected through PresetFs.
//   - numbering: empty folder, gaps, renamed and non-numbered files, case,
//     preset-9999 -> preset-10000, directories and hidden files count for the
//     number but are not listed
//   - the list: only regular *.json files (any case), no hidden files, no
//     folders, no symbolic links, no recursion; natural, case-insensitive order;
//     the cap of 2000 (2100 files) with numbering beyond the cap
//   - save, load, overwrite, delete, each followed by a fresh scan
//   - the atomic write: a failure injected at each step (create, write, sync,
//     close, rename) leaves no *.json, no temporary file and the old content;
//     a stale temporary file is removed by the next scan; a name taken
//     between scan and rename is not overwritten
//   - folder missing (empty list, state Missing; save creates it with its
//     parents), a file where the folder should be, an unwritable folder (as a
//     non-root user), a folder not set
//   - load failures: broken, too large, not found, a folder named *.json, a
//     newer version; nothing returned from a failing file
//   - PRESET-B (the review of PRESET-A): the list sorted by display name
//     ("preset-0007" above "preset-0007 (copy)", "Drone" above "Drone 2"),
//     numbers used up at preset-999999999.json (a clean refusal, nothing
//     written), the temporary name blocked by a link, a folder or a FIFO
//     (another name is used; all four blocked: "temporary name blocked")

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <string>
#include <vector>

#include "preset/PresetFormat.h"
#include "preset/PresetFs.h"
#include "preset/PresetStore.h"
#include "preset_test_common.h"

using namespace audreybrick;
using ptest::check;
using ptest::fileExists;
using ptest::freshDir;
using ptest::writeFile;

namespace
{
// The real file system with one step made to fail.
class FaultyFs final : public PosixPresetFs
{
  public:
    enum class Step
    {
        None,
        Create,
        Write,
        Sync,
        Close,
        Rename,
        MakeDirs,
    };
    Step failAt   = Step::None;
    int  failErr  = EIO;
    // After the write step: what was written into the temporary file, and
    // whether the target existed unchanged at that moment.
    std::string lastTempPath;
    // A file another process creates after the temporary file is closed and
    // before the rename (the name is taken in between).
    std::string createAfterClose;

    int createTemp(const std::string& path, std::string& error) override
    {
        lastTempPath = path;
        if(failAt == Step::Create)
            return fakeFail(error, -1);
        return PosixPresetFs::createTemp(path, error);
    }
    bool writeAll(int h, const char* d, std::size_t n, std::string& error) override
    {
        if(failAt == Step::Write)
        {
            // Half the data reaches the file, then the card is full.
            PosixPresetFs::writeAll(h, d, n / 2, error);
            return fakeFail(error, false);
        }
        return PosixPresetFs::writeAll(h, d, n, error);
    }
    bool sync(int h, std::string& error) override
    {
        if(failAt == Step::Sync)
            return fakeFail(error, false);
        return PosixPresetFs::sync(h, error);
    }
    bool close(int h, std::string& error) override
    {
        const bool r = PosixPresetFs::close(h, error); // always really closed
        if(!createAfterClose.empty())
            ptest::writeFile(createAfterClose, "someone else's");
        if(failAt == Step::Close)
            return fakeFail(error, false);
        return r;
    }
    bool rename(const std::string& from, const std::string& to, std::string& error) override
    {
        if(failAt == Step::Rename)
            return fakeFail(error, false);
        return PosixPresetFs::rename(from, to, error);
    }
    bool makeDirs(const std::string& dir, std::string& error) override
    {
        if(failAt == Step::MakeDirs)
            return fakeFail(error, false);
        return PosixPresetFs::makeDirs(dir, error);
    }

  private:
    template <typename T>
    T fakeFail(std::string& error, T r)
    {
        error = "injected failure";
        errno = failErr;
        return r;
    }
};

std::vector<std::string> names(const PresetStore& s)
{
    std::vector<std::string> v;
    for(const PresetEntry& e : s.entries())
        v.push_back(e.name);
    return v;
}

std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for(const std::string& x : v)
        s += (s.empty() ? "" : ",") + x;
    return s;
}

// Every name in `dir` (for "no *.json and no temporary file left").
std::vector<std::string> listing(const std::string& dir)
{
    std::vector<std::string> v;
    std::error_code          ec;
    for(const auto& e : std::filesystem::directory_iterator(dir, ec))
        v.push_back(e.path().filename().string());
    std::sort(v.begin(), v.end());
    return v;
}

PresetData sample(float x)
{
    PresetData p = presetDefaults();
    p.knobs[1]   = x;
    p.knobs[5]   = 1.0f - x;
    p.delaySwitch = x > 0.5f;
    p.mod[3].target = ModTarget::EchoDelayTime;
    p.mod[3].depth  = x - 0.5f;
    return sanitizePreset(p);
}

void testNames()
{
    check(presetNumber("preset-0001.json") == 1 && presetNumber("PRESET-0042.JSON") == 42 &&
              presetNumber("preset-1.json") == 1 && presetNumber("preset-123456789.json") == 123456789 &&
              presetNumber("preset-1234567890.json") == -1 && presetNumber("preset-.json") == -1 &&
              presetNumber("preset-12a.json") == -1 && presetNumber("preset-0001.json.tmp") == -1 &&
              presetNumber("my preset-0003.json") == -1 && presetNumber("preset-0003.jsn") == -1,
          "presetNumber: preset-<1..9 digits>.json, case-insensitive, anything else -1");
    check(presetFileName(1) == "preset-0001.json" && presetFileName(9999) == "preset-9999.json" &&
              presetFileName(10000) == "preset-10000.json",
          "presetFileName: four digits, wider past 9999");
    check(presetDisplayName("preset-0007.json") == "preset-0007" && presetDisplayName("Drone.JSON") == "Drone",
          "display name: the file name without .json");
    check(presetListedName("a.json") && presetListedName("A.JSON") && !presetListedName(".json") &&
              !presetListedName(".hidden.json") && !presetListedName("._preset-0001.json") &&
              !presetListedName("a.json.tmp") && !presetListedName("a.txt") && !presetListedName("json"),
          "listed names: *.json in any case, not hidden");
    const auto lt = [](const char* a, const char* b) { return presetNameCompare(a, b) < 0; };
    check(lt("preset-2.json", "preset-10.json") && lt("preset-0009.json", "preset-0010.json") &&
              lt("a.json", "B.json") && lt("B.json", "c.json") && lt("drone 2.json", "drone 10.json") &&
              lt("x9.json", "x10a.json") && lt("abc.json", "abcd.json") &&
              presetNameCompare("same.json", "same.json") == 0,
          "natural, case-insensitive order");
    check(presetNameCompare("preset-0002.json", "preset-2.json") != 0 &&
              presetNameCompare("A.json", "a.json") != 0 &&
              (presetNameCompare("A.json", "a.json") < 0) == (presetNameCompare("a.json", "A.json") > 0),
          "names equal in the natural order are ordered by their bytes (a total order)");
    check(lt("v99999999999999999999.json", "v100000000000000000000.json"), "very long digit runs compared as numbers");
}

void testScanAndNumbering()
{
    PosixPresetFs fs;
    const std::string dir = freshDir("scan");
    PresetStore       s(fs, dir, "test");
    s.scan();
    check(s.state() == PresetFolderState::Ok && s.entries().empty() && s.nextNumber() == 1 && s.totalFound() == 0,
          "empty folder: no entries, next number 1");

    for(const char* n : {"preset-0001.json", "preset-0003.json", "Drone.json", "zz top.JSON", "preset-10.json",
                         "notes.txt", "preset-0002.json.bak", ".hidden.json", "._preset-0050.json", "a.json.tmp"})
        writeFile(dir + "/" + n, "{}");
    std::filesystem::create_directories(dir + "/folder.json/inner.json"); // a folder named *.json, with content
    writeFile(dir + "/folder.json/preset-0400.json", "{}");              // no recursion
    std::filesystem::create_directories(dir + "/preset-0020.json");      // a folder with a preset number
    ::symlink((dir + "/Drone.json").c_str(), (dir + "/link.json").c_str());
    s.scan();
    check(join(names(s)) == "Drone,preset-0001,preset-0003,preset-10,zz top",
          "listed: regular *.json only, sorted naturally and case-insensitively (got " + join(names(s)) + ")");
    check(s.nextNumber() == 21,
          "next number: one more than the highest preset-N.json entry of any kind (the folder preset-0020.json), "
          "hidden ._preset-0050.json and the nested preset-0400.json not counted (got " +
              std::to_string(s.nextNumber()) + ")");
    check(s.indexOf("preset-0003.json") == 2 && s.indexOf("nope.json") == -1, "indexOf");

    // Renaming a preset on a computer shows the new name and frees its number.
    std::filesystem::remove_all(dir + "/preset-0020.json");
    std::filesystem::rename(dir + "/preset-10.json", dir + "/Big Drone.json");
    s.scan();
    check(join(names(s)) == "Big Drone,Drone,preset-0001,preset-0003,zz top" && s.nextNumber() == 4,
          "a renamed file shows its new name; the next number follows the highest left (4)");

    const std::string d2 = freshDir("scan9999");
    PresetStore       s2(fs, d2, "test");
    writeFile(d2 + "/preset-9999.json", "{}");
    s2.scan();
    check(s2.nextNumber() == 10000, "after preset-9999 the next number is 10000");
    const PresetResult r = s2.saveNew(sample(0.3f));
    check(r.ok && r.file == "preset-10000.json" && fileExists(d2 + "/preset-10000.json") &&
              join(names(s2)) == "preset-9999,preset-10000",
          "saved as preset-10000.json, listed after preset-9999");
}

void testCap()
{
    PosixPresetFs     fs;
    const std::string dir = freshDir("cap");
    for(int i = 1; i <= 2100; ++i)
        writeFile(dir + "/" + presetFileName(i), "{}");
    writeFile(dir + "/aaa.json", "{}");
    PresetStore s(fs, dir, "test");
    s.scan();
    check(s.entries().size() == PresetStore::kMaxListed && s.totalFound() == 2101 && s.truncated(),
          "2101 presets: 2000 listed, totalFound 2101, truncated");
    check(s.entries().front().name == "aaa" && s.entries().back().name == "preset-1999",
          "the first 2000 in order are listed (aaa .. preset-1999)");
    check(s.nextNumber() == 2101, "numbering counts the files beyond the cap (next 2101)");
    const PresetResult r = s.saveNew(sample(0.1f));
    check(r.ok && r.file == "preset-2101.json" && s.totalFound() == 2102 && s.indexOf("preset-2101.json") == -1,
          "a save beyond the cap works; the new file is counted but not listed");
}

void testSaveLoadOverwriteDelete()
{
    PosixPresetFs     fs;
    const std::string dir = freshDir("ops") + "/nested/presets"; // does not exist yet
    PresetStore       s(fs, dir, "v1.2-test");
    s.scan();
    check(s.state() == PresetFolderState::Missing && s.entries().empty() && !s.stateDetail().empty(),
          "a missing folder: state Missing, empty list, a reason");

    const PresetData a = sample(0.2f);
    PresetResult     r = s.saveNew(a);
    check(r.ok && r.file == "preset-0001.json" && s.state() == PresetFolderState::Ok &&
              join(names(s)) == "preset-0001",
          "save creates the folder and its parents, writes preset-0001.json, re-scans");
    check(ptest::readFile(dir + "/preset-0001.json") == encodePreset(a, "v1.2-test"),
          "the file holds the canonical text with the app version");
    PresetData back;
    r = s.load("preset-0001.json", back);
    check(r.ok && presetBitsEqual(back, a), "load returns exactly what was saved");

    const PresetData b = sample(0.9f);
    r                  = s.saveNew(b);
    check(r.ok && r.file == "preset-0002.json" && join(names(s)) == "preset-0001,preset-0002", "second save: 0002");

    r = s.overwrite("preset-0001.json", b);
    check(r.ok && s.load("preset-0001.json", back).ok && presetBitsEqual(back, b) &&
              join(names(s)) == "preset-0001,preset-0002",
          "overwrite replaces the content, keeps the name");

    r = s.remove("preset-0001.json");
    check(r.ok && !fileExists(dir + "/preset-0001.json") && join(names(s)) == "preset-0002" && s.nextNumber() == 3,
          "delete removes the file and re-scans (the number of a deleted top preset is not reused here: 3)");
    r = s.remove("preset-0001.json");
    check(!r.ok && r.error.reason == "not found", "deleting a missing file: not found");
    r = s.overwrite("preset-0001.json", a);
    check(!r.ok && r.error.reason == "not found" && !fileExists(dir + "/preset-0001.json"),
          "overwriting a file that is gone: not found, and nothing created under its name");
    r = s.saveNew(a);
    check(r.ok && r.file == "preset-0003.json", "the next save after deleting 0001 is 0003 (highest + 1)");
    for(const char* bad : {"../x.json", "a/b.json", ".hidden.json", "x.txt", ""})
    {
        PresetData untouched = sample(0.5f);
        const PresetData before = untouched;
        ptest::quiet(!s.load(bad, untouched).ok && presetBitsEqual(untouched, before) && !s.remove(bad).ok &&
                         !s.overwrite(bad, a).ok,
                     std::string("bad name refused: ") + bad);
    }
    check(true, "names with '/', hidden names, non-.json names and the empty name are refused");
    check(listing(dir) == std::vector<std::string>({"preset-0002.json", "preset-0003.json"}),
          "no temporary files left after the operations");
}

void testLoadFailures()
{
    PosixPresetFs     fs;
    const std::string dir = freshDir("loadfail");
    PresetStore       s(fs, dir, "t");
    writeFile(dir + "/broken.json", "{\"format\":\"audreybrick-preset\",\"version\":1,");
    writeFile(dir + "/newer.json", "{\"format\":\"audreybrick-preset\",\"version\":2}");
    writeFile(dir + "/other.json", "{\"name\":\"not ours\"}");
    writeFile(dir + "/big.json", encodePreset(presetDefaults(), "t") + std::string(kMaxPresetBytes, ' '));
    writeFile(dir + "/limit.json",
              [] {
                  std::string t = encodePreset(presetDefaults(), "t");
                  return t + std::string(kMaxPresetBytes - t.size(), '\n');
              }());
    std::filesystem::create_directories(dir + "/dir.json");
    s.scan();
    struct C
    {
        const char* file;
        const char* reason;
    };
    for(const C& c : {C{"broken.json", "syntax error"}, C{"newer.json", "made by a newer version"},
                      C{"other.json", "not a preset"}, C{"big.json", "too large"}, C{"missing.json", "not found"},
                      C{"dir.json", "not a file"}})
    {
        PresetData       out    = sample(0.7f);
        const PresetData before = out;
        const PresetResult r    = s.load(c.file, out);
        check(!r.ok && r.error.reason == c.reason && !r.error.detail.empty() && presetBitsEqual(out, before),
              std::string("load ") + c.file + ": \"" + r.error.reason + "\" (" + r.error.detail +
                  "), nothing returned");
    }
    PresetData out;
    check(s.load("limit.json", out).ok, "a file of exactly 64 KiB loads");
}

void testAtomicWrite()
{
    struct C
    {
        FaultyFs::Step step;
        int            err;
        const char*    reason;
        const char*    what;
    };
    const C cases[] = {
        {FaultyFs::Step::Create, EROFS, "read-only", "create the temporary file"},
        {FaultyFs::Step::Write, ENOSPC, "card full", "write (half written, then the card is full)"},
        {FaultyFs::Step::Sync, EIO, "write failed", "flush"},
        {FaultyFs::Step::Close, ENOSPC, "card full", "close"},
        {FaultyFs::Step::Rename, EIO, "write failed", "rename (between writing and renaming)"},
    };
    for(const C& c : cases)
    {
        FaultyFs          fs;
        const std::string dir = freshDir("atomic");
        PresetStore       s(fs, dir, "t");
        const PresetData  old = sample(0.25f);
        check(s.saveNew(old).ok, "setup: preset-0001.json saved");
        const std::string oldText = ptest::readFile(dir + "/preset-0001.json");
        fs.failAt                 = c.step;
        fs.failErr                = c.err;

        PresetResult r = s.overwrite("preset-0001.json", sample(0.75f));
        check(!r.ok && r.error.reason == c.reason && ptest::readFile(dir + "/preset-0001.json") == oldText &&
                  listing(dir) == std::vector<std::string>({"preset-0001.json"}),
              std::string("overwrite failing at ") + c.what + ": \"" + r.error.reason +
                  "\", the old file intact, no temporary file left");
        r = s.saveNew(sample(0.75f));
        check(!r.ok && r.error.reason == c.reason && listing(dir) == std::vector<std::string>({"preset-0001.json"}) &&
                  join(names(s)) == "preset-0001",
              std::string("save failing at ") + c.what + ": no new *.json, no temporary file");
        check(fs.lastTempPath == dir + "/.preset-0002.json.tmp", "the temporary file is .<name>.tmp in the same folder");
    }

    // The name is taken between the scan and the rename: not overwritten.
    // (A writer racing between the check and the rename itself could still be
    // overwritten; the app is the only writer, on one thread.)
    {
        FaultyFs          fs;
        const std::string dir = freshDir("taken");
        PresetStore       s(fs, dir, "t");
        fs.createAfterClose = dir + "/preset-0001.json";
        const PresetResult r  = s.saveNew(sample(0.5f));
        check(!r.ok && r.error.reason == "name taken" && ptest::readFile(dir + "/preset-0001.json") == "someone else's" &&
                  listing(dir) == std::vector<std::string>({"preset-0001.json"}),
              "a name taken just before the rename is not overwritten; the temporary file is removed");
    }

    // A stale temporary file (a power cut between write and rename) is removed by the next scan.
    {
        PosixPresetFs     fs;
        const std::string dir = freshDir("stale");
        writeFile(dir + "/.preset-0004.json.tmp", "{\"format\":");
        writeFile(dir + "/.My Drone.JSON.tmp", "half");
        writeFile(dir + "/.keep.tmp", "not ours");
        writeFile(dir + "/preset-0003.json", "{}");
        std::filesystem::create_directories(dir + "/.dir.json.tmp");
        PresetStore s(fs, dir, "t");
        s.scan();
        check(s.staleRemoved() == 2 && !fileExists(dir + "/.preset-0004.json.tmp") &&
                  !fileExists(dir + "/.My Drone.JSON.tmp") && fileExists(dir + "/.keep.tmp") &&
                  fileExists(dir + "/.dir.json.tmp") && join(names(s)) == "preset-0003" && s.nextNumber() == 4,
              "a scan removes stale .<name>.json.tmp files (regular files only) and nothing else");
    }

    // makeDirs fails (the folder cannot be created).
    {
        FaultyFs          fs;
        const std::string dir = freshDir("mkdirfail") + "/sub";
        fs.failAt             = FaultyFs::Step::MakeDirs;
        fs.failErr            = EROFS;
        PresetStore        s(fs, dir, "t");
        const PresetResult r = s.saveNew(sample(0.5f));
        check(!r.ok && r.error.reason == "read-only" && s.state() == PresetFolderState::Missing,
              "save when the folder cannot be created: read-only, state Missing");
    }
}

void testFolderStates()
{
    PosixPresetFs fs;
    {
        PresetStore s(fs, "", "t");
        s.scan();
        PresetData out = sample(0.4f);
        check(s.state() == PresetFolderState::NotSet && s.entries().empty() && !s.saveNew(out).ok &&
                  s.saveNew(out).error.reason == "no preset folder" &&
                  s.load("a.json", out).error.reason == "no preset folder",
              "no folder set: NotSet, every operation fails with \"no preset folder\"");
    }
    {
        const std::string base = freshDir("notafolder");
        writeFile(base + "/presets", "a file");
        PresetStore s(fs, base + "/presets", "t");
        s.scan();
        const PresetResult r = s.saveNew(sample(0.4f));
        check(s.state() == PresetFolderState::NotAFolder && !r.ok && r.error.reason == "no folder" &&
                  ptest::readFile(base + "/presets") == "a file",
              "a file where the folder should be: NotAFolder, save fails, the file untouched");
    }
    {
        const std::string base = freshDir("trailing");
        PresetStore       s(fs, base + "/", "t");
        check(s.dir() == base && s.saveNew(sample(0.4f)).ok && fileExists(base + "/preset-0001.json"),
              "a trailing '/' on the folder is dropped");
    }
    if(::geteuid() == 0)
    {
        check(true, "unwritable and unreadable folder checks skipped (running as root)");
        return;
    }
    {
        const std::string dir = freshDir("readonly");
        PresetStore       s(fs, dir, "t");
        check(s.saveNew(sample(0.4f)).ok, "setup: one preset");
        ::chmod(dir.c_str(), 0555);
        PresetResult r = s.saveNew(sample(0.6f));
        check(!r.ok && r.error.reason == "not allowed" && join(names(s)) == "preset-0001",
              "save into an unwritable folder: \"" + r.error.reason + "\" (" + r.error.detail + ")");
        r = s.overwrite("preset-0001.json", sample(0.6f));
        check(!r.ok && r.error.reason == "not allowed", "overwrite in an unwritable folder: not allowed");
        r = s.remove("preset-0001.json");
        check(!r.ok && r.error.reason == "not allowed" && fileExists(dir + "/preset-0001.json"),
              "delete in an unwritable folder: not allowed, the file stays");
        PresetData out;
        check(s.load("preset-0001.json", out).ok, "load from an unwritable folder still works");
        ::chmod(dir.c_str(), 0700);
        check(listing(dir) == std::vector<std::string>({"preset-0001.json"}), "nothing left behind");
    }
    {
        const std::string dir = freshDir("unreadable");
        writeFile(dir + "/preset-0001.json", encodePreset(presetDefaults(), "t"));
        ::chmod(dir.c_str(), 0);
        PresetStore s(fs, dir, "t");
        s.scan();
        check(s.state() == PresetFolderState::Unreadable && s.entries().empty() && !s.stateDetail().empty(),
              "an unreadable folder: Unreadable, empty list, a reason (" + s.stateDetail() + ")");
        ::chmod(dir.c_str(), 0700);
    }
    {
        const std::string dir = freshDir("unreadablefile");
        writeFile(dir + "/p.json", encodePreset(presetDefaults(), "t"));
        ::chmod((dir + "/p.json").c_str(), 0);
        PresetStore s(fs, dir, "t");
        s.scan();
        PresetData out;
        const PresetResult r = s.load("p.json", out);
        check(!r.ok && r.error.reason == "unreadable" && join(names(s)) == "p",
              "an unreadable file is listed; loading it fails as unreadable");
        ::chmod((dir + "/p.json").c_str(), 0600);
    }
}
} // namespace

// PRESET-B: the review of PRESET-A.
void testReviewFixes()
{
    PosixPresetFs fs;
    {
        const std::string dir = freshDir("order");
        for(const char* n : {"preset-0007 (copy).json", "preset-0007.json", "Drone 2.json", "Drone.json",
                             "preset-0007a.json", "Drone 10.json"})
            writeFile(dir + "/" + n, "{}");
        PresetStore s(fs, dir, "test");
        s.scan();
        const std::vector<std::string> n = names(s);
        check(join(n) == "Drone,Drone 2,Drone 10,preset-0007,preset-0007 (copy),preset-0007a",
              "sorted by display name: Drone above Drone 2 above Drone 10, preset-0007 above preset-0007 (copy) (got " +
                  join(n) + ")");
        // The same display name twice ("a.JSON", "a.json") exists only on a
        // case-sensitive file system (the Linux containers, an ext4 card); the
        // Mac's is usually not, and then this part is skipped.
        const std::string d2 = freshDir("order-ties");
        writeFile(d2 + "/a.JSON", "{}");
        writeFile(d2 + "/a.json", "{}");
        if(listing(d2).size() == 2)
        {
            PresetStore s2(fs, d2, "test");
            s2.scan();
            check(s2.entries().size() == 2 && s2.entries()[0].name == "a" && s2.entries()[1].name == "a" &&
                      s2.entries()[0].file == "a.JSON" && s2.entries()[1].file == "a.json",
                  "the same display name twice (a.JSON, a.json): ordered by file name");
        }
        else
        {
            std::printf("note: case-insensitive file system here: the a.JSON / a.json tie is checked on Linux\n");
        }
    }
    {
        const std::string dir = freshDir("used-up");
        writeFile(dir + "/preset-999999999.json", "{}");
        PresetStore s(fs, dir, "test");
        s.scan();
        const std::vector<std::string> before = listing(dir);
        const PresetResult             r      = s.saveNew(sample(0.2f));
        check(!r.ok && r.error.reason == "numbers used up" && listing(dir) == before,
              "preset-999999999.json present: saveNew fails with \"numbers used up\" and writes nothing (got \"" +
                  r.error.reason + "\")");
        const PresetResult r2 = s.saveNew(sample(0.2f));
        check(!r2.ok && r2.error.reason == "numbers used up", "and again on the next try (no \"name taken\" loop)");
        std::filesystem::rename(dir + "/preset-999999999.json", dir + "/Big.json");
        const PresetResult r3 = s.saveNew(sample(0.2f));
        check(r3.ok && r3.file == "preset-0001.json", "renamed away: numbering starts again at preset-0001");
        const std::string d2 = freshDir("used-up-2");
        writeFile(d2 + "/preset-999999998.json", "{}");
        PresetStore s2(fs, d2, "test");
        const PresetResult r4 = s2.saveNew(sample(0.2f));
        check(r4.ok && r4.file == "preset-999999999.json" && s2.indexOf("preset-999999999.json") >= 0,
              "preset-999999998 present: the last number, preset-999999999.json, is still saved and listed");
    }
    {
        const std::string dir = freshDir("tmp-blocked");
        PresetStore       s(fs, dir, "test");
        // The first temporary name is a symbolic link (to a file that must not
        // be written through), the second a folder, the third a FIFO.
        writeFile(dir + "/victim.txt", "keep me");
        ::symlink((dir + "/victim.txt").c_str(), (dir + "/.preset-0001.json.tmp").c_str());
        std::filesystem::create_directories(dir + "/.1.preset-0001.json.tmp");
        ::mkfifo((dir + "/.2.preset-0001.json.tmp").c_str(), 0600);
        const PresetResult r = s.saveNew(sample(0.4f));
        PresetData         back;
        check(r.ok && r.file == "preset-0001.json" && s.load("preset-0001.json", back).ok &&
                  presetBitsEqual(back, sample(0.4f)),
              "temporary names blocked by a link, a folder and a FIFO: the fourth name is used, the save works");
        check(ptest::readFile(dir + "/victim.txt") == "keep me" && !fileExists(dir + "/.3.preset-0001.json.tmp"),
              "the link's target is untouched and the used temporary name is gone (renamed)");
        // All four blocked.
        ::symlink((dir + "/victim.txt").c_str(), (dir + "/.preset-0002.json.tmp").c_str());
        std::filesystem::create_directories(dir + "/.1.preset-0002.json.tmp");
        std::filesystem::create_directories(dir + "/.2.preset-0002.json.tmp");
        ::mkfifo((dir + "/.3.preset-0002.json.tmp").c_str(), 0600);
        const PresetResult r2 = s.saveNew(sample(0.5f));
        check(!r2.ok && r2.error.reason == "temporary name blocked" && !fileExists(dir + "/preset-0002.json") &&
                  ptest::readFile(dir + "/victim.txt") == "keep me",
              "all four temporary names blocked: \"temporary name blocked\", no preset-0002.json (got \"" +
                  r2.error.reason + "\")");
        // A stale regular temporary file with a numbered name is cleaned by the scan.
        writeFile(dir + "/.1.preset-0009.json.tmp", "half");
        s.scan();
        check(!fileExists(dir + "/.1.preset-0009.json.tmp") && s.staleRemoved() == 1,
              "a stale numbered temporary file (.1.preset-0009.json.tmp) is removed by the next scan");
    }
}

int main()
{
    testNames();
    testScanAndNumbering();
    testCap();
    testSaveLoadOverwriteDelete();
    testLoadFailures();
    testAtomicWrite();
    testFolderStates();
    testReviewFixes();
    return ptest::finish();
}
