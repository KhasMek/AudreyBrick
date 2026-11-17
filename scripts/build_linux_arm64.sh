#!/usr/bin/env bash
# Linux aarch64 build check of the DSP core (task M1-D, docs/briefs/M1.md).
#
#   scripts/build_linux_arm64.sh [--out <dir>] [--no-sanitize] [--libm-probe]
#
# Shows that the DSP libraries, the offline tools and every test build and pass with
# gcc on Linux aarch64 (Debian stable, docker/Dockerfile.arm64-test), and measures how
# far the Linux renders are from the macOS reference renders. It does not produce a
# device binary.
#
# Steps:
#   1. Check that the macOS reference renders exist in reference/ (git-ignored; make
#      them on the Mac with scripts/make_reference.sh, see reference/README.md).
#   2. Build the image audreybrick-arm64-test:local from docker/ (linux/arm64; never pushed).
#   3. Container 1 (Release): configure, build, ctest; render every script with
#      scripts/make_reference.sh --build <container dir> --out /out/renders (which runs
#      wavcheck --script on each); check the hashes against reference/SHA256SUMS (report
#      only, they are expected to differ); compare every render with the macOS render of
#      the same name (docker/wavdiff.cpp, compiled in the container) and print the table.
#   4. Container 2 (Debug, ASan + UBSan, with LeakSanitizer): configure, build, ctest,
#      plus three canary programs that prove ASan, LSan and UBSan report in the container.
#   5. Only with --libm-probe: container 1 also records every distinct argument of every
#      libm call in every render (LD_PRELOAD shim docker/libm_log.c) and evaluates them
#      with glibc (docker/libm_probe.cpp); the host then compiles the same probe with the
#      Mac's compiler, evaluates the same arguments with Apple's libm and prints, per
#      function, how many results differ in their bits, and per render how many of its
#      libm arguments give different results. Analysis only; it never fails the run
#      unless the shim changes a render.
#
# Output directory (--out): refused if it is the repository, inside it, or contains it
# (compared as resolved physical paths), and refused if it already holds anything not
# written by this script (it must be new, empty, or carry the marker file
# .audreybrick-linux-arm64-out from an earlier run). Validation happens before anything
# is created; only this script's own output names are ever removed from it.
#
# Toolchain versions: only the base image is pinned (by digest). The apt packages (g++,
# cmake, ninja-build, locales) are NOT pinned, so a rebuild of the image can pick up a
# newer gcc, libstdc++ or glibc from a Debian point release. The script prints the
# versions it actually used. For that reason the Linux render hashes are not kept as a
# reference; the comparison with the macOS renders is by sample difference.
#
# The repository is mounted READ-ONLY at /src. Builds live in the container's /tmp and
# vanish with it (--rm). The only writable mount is the output directory: by default a
# new temporary directory outside the repository, printed at the end; it holds image-build.log, logs/,
# renders/ (the Linux WAVs), diff_profiles/ (per-second difference per render) and
# diff_table.txt. Containers run without network and as the calling user.
#
# Sanitizers under Docker: ASan, UBSan and LeakSanitizer run in the container with
# Docker's default seccomp profile and capabilities; no --cap-add or --security-opt is
# needed (checked on Docker Desktop 29.5, Apple Silicon, 2026-11-17).
#
# Exit status: 0 only if both containers succeed: configure, build and ctest pass,
# every render succeeds and passes wavcheck, and no render holds NaN/inf. A difference
# between the Linux and the macOS renders is reported, not treated as a failure.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
IMAGE="audreybrick-arm64-test:local"
OUT=""
SANITIZE=1

LIBM_PROBE=0

usage() { echo "usage: scripts/build_linux_arm64.sh [--out <dir>] [--no-sanitize] [--libm-probe]" >&2; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --out)         [ $# -ge 2 ] || usage; OUT="$2"; shift ;;
        --no-sanitize) SANITIZE=0 ;;
        --libm-probe)  LIBM_PROBE=1 ;;
        *) usage ;;
    esac
    shift
done

command -v docker >/dev/null 2>&1 || { echo "build_linux_arm64: docker is not installed" >&2; exit 1; }

# 1. The macOS reference renders must exist (one per script, plus the generated inputs).
missing=0
for script in "$REPO_ROOT"/scripts/*.txt; do
    name="$(basename "$script" .txt)"
    [ -f "$REPO_ROOT/reference/$name.wav" ] || { echo "missing: reference/$name.wav" >&2; missing=1; }
done
if [ "$missing" -ne 0 ]; then
    echo "build_linux_arm64: the macOS reference renders are missing. On the Mac, run:" >&2
    echo "  cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release && cmake --build build/host -j && scripts/make_reference.sh" >&2
    exit 1
fi

# Output directory. All validation happens before anything is created or deleted:
#   - compared as resolved physical paths (symlinks, "..", trailing slashes), the output
#     directory must not be the repository, lie inside it, or contain it (a parent of
#     the repository would make the repository writable inside the container);
#   - it must not exist yet, or be an empty directory, or be a directory that a previous
#     run of this script marked with $MARKER. Only then is old output removed, and only
#     the names this script writes.
MARKER=".audreybrick-linux-arm64-out"
OUR_OUTPUTS=(logs renders diff_profiles diff_table.txt libm image-build.log)

# Physical absolute path of $1, which need not exist: the longest existing ancestor is
# resolved with pwd -P and the rest is appended. A "." or ".." in the non-existing
# part is refused, because it cannot be resolved without creating directories.
resolve_path() {
    local p="$1" rest="" base comp
    case "$p" in /*) ;; *) p="$PWD/$p" ;; esac
    while [ ! -e "$p" ] && [ ! -L "$p" ]; do
        comp="$(basename "$p")"
        case "$comp" in .|..) return 1 ;; esac
        rest="/$comp$rest"
        p="$(dirname "$p")"
    done
    [ -d "$p" ] || { [ -z "$rest" ] && [ -e "$p" ] && { echo "$p"; return 0; }; return 1; }
    base="$(cd "$p" && pwd -P)" || return 1
    [ "$base" = "/" ] && base=""
    base="$base$rest"
    echo "${base:-/}"
}

# Refuses (exit 2) a resolved path $2 that is the root, the repository, inside it, or
# contains it. $1 names the path in the message.
check_location() {
    local what="$1" p="$2"
    if [ "$p" = "/" ] || [ -z "$p" ] || [ "$p" = "$REPO_ROOT" ]; then
        echo "build_linux_arm64: $what ($p) is the repository or the root directory; refused" >&2; exit 2
    fi
    case "$p/" in
        "$REPO_ROOT"/*) echo "build_linux_arm64: $what ($p) is inside the repository ($REPO_ROOT); refused" >&2; exit 2 ;;
    esac
    case "$REPO_ROOT/" in
        "$p"/*) echo "build_linux_arm64: $what ($p) contains the repository ($REPO_ROOT); refused" >&2; exit 2 ;;
    esac
}

case "$REPO_ROOT" in *,*) echo "build_linux_arm64: the repository path contains a comma, which docker --mount cannot take" >&2; exit 2 ;; esac
AUTO_OUT=0
if [ -z "$OUT" ]; then
    AUTO_OUT=1
    tmpbase="$(resolve_path "${TMPDIR:-/tmp}")" && [ -d "$tmpbase" ] \
        || { echo "build_linux_arm64: temporary directory '${TMPDIR:-/tmp}' does not exist" >&2; exit 2; }
    check_location "the temporary directory" "$tmpbase" # the new directory goes inside it
    case "$tmpbase" in *,*) echo "build_linux_arm64: the temporary directory path contains a comma; set TMPDIR or use --out" >&2; exit 2 ;; esac
else
    req="$OUT"
    OUT="$(resolve_path "$req")" \
        || { echo "build_linux_arm64: cannot resolve --out '$req' (a '.' or '..' after a missing directory, a dangling symlink, or a file in the path); refused" >&2; exit 2; }
    check_location "--out '$req'" "$OUT"
    case "$OUT" in *,*) echo "build_linux_arm64: --out '$req' contains a comma, which docker --mount cannot take; refused" >&2; exit 2 ;; esac
    if [ -e "$OUT" ] || [ -L "$OUT" ]; then
        [ -d "$OUT" ] || { echo "build_linux_arm64: --out '$req' ($OUT) exists and is not a directory; refused" >&2; exit 2; }
        if [ ! -f "$OUT/$MARKER" ] && [ -n "$(ls -A "$OUT")" ]; then
            echo "build_linux_arm64: --out '$req' ($OUT) is not empty and was not made by this script" >&2
            echo "  (no $MARKER in it); refused, so that nothing in it is deleted. Use a new or empty directory." >&2
            exit 2
        fi
    fi
fi

# Validation done; from here on the output directory may be created.
CREATED_OUT=0   # 1 if this run created the output directory
USEFUL=0        # 1 once a container has run (its logs are worth keeping)
cleanup_on_exit() {
    local rc=$?
    if [ "$rc" -ne 0 ] && [ "$CREATED_OUT" -eq 1 ] && [ "$USEFUL" -eq 0 ] && [ -n "${OUT:-}" ]; then
        rm -rf "$OUT"
        echo "build_linux_arm64: removed the output directory $OUT (nothing useful was written)" >&2
    elif [ "$rc" -ne 0 ] && [ -n "${OUT:-}" ] && [ -d "$OUT" ]; then
        echo "build_linux_arm64: logs kept in $OUT" >&2
    fi
}
trap cleanup_on_exit EXIT

if [ "$AUTO_OUT" -eq 1 ]; then
    OUT="$(mktemp -d "$tmpbase/audreybrick-linux-arm64.XXXXXX")"
    CREATED_OUT=1
elif [ ! -d "$OUT" ]; then
    mkdir -p "$OUT"
    CREATED_OUT=1
fi
for f in "${OUR_OUTPUTS[@]}"; do rm -rf "${OUT:?}/$f"; done # re-runnable: only our own names
printf 'Output directory of scripts/build_linux_arm64.sh (AudreyBrick M1-D).\n' > "$OUT/$MARKER"

echo "build_linux_arm64: repository $REPO_ROOT (mounted read-only)"
echo "build_linux_arm64: output     $OUT"

# 2. Image (cached by Docker after the first run). The build log stays in the output
# directory; on failure it is also printed, and an auto-created directory is removed.
echo "==== Build image $IMAGE ===="
docker build --platform linux/arm64 -t "$IMAGE" -f "$REPO_ROOT/docker/Dockerfile.arm64-test" \
    "$REPO_ROOT/docker" >"$OUT/image-build.log" 2>&1 \
    || { cat "$OUT/image-build.log" >&2; echo "build_linux_arm64: docker build failed" >&2; exit 1; }
echo "base image: $(grep -m1 '^FROM' "$REPO_ROOT/docker/Dockerfile.arm64-test" | cut -d' ' -f2)"
echo "image id:   $(docker image inspect --format '{{.Id}}' "$IMAGE")"

run_container() { # <mode>
    docker run --rm --platform linux/arm64 --network none \
        --user "$(id -u):$(id -g)" -e HOME=/tmp -e AB_LIBM_PROBE="$LIBM_PROBE" \
        --mount "type=bind,src=$REPO_ROOT,dst=/src,readonly" \
        --mount "type=bind,src=$OUT,dst=/out" \
        "$IMAGE" bash /src/docker/in_container.sh "$1"
}

status=0
USEFUL=1
# 3. Release: build, tests, renders, comparison.
run_container release || status=1
# 5. libm probe, host half: the same arguments through Apple's libm.
if [ "$LIBM_PROBE" -eq 1 ] && [ -f "$OUT/libm/results_linux.txt" ]; then
    echo
    echo "==== libm probe: glibc (Linux) against Apple libm (macOS) on the recorded arguments ===="
    c++ -std=c++17 -O2 -fno-builtin -o "$OUT/libm/libm_probe_host" "$REPO_ROOT/docker/libm_probe.cpp"
    "$OUT/libm/libm_probe_host" < "$OUT/libm/all_args.txt" > "$OUT/libm/results_host.txt"
    echo "host: $(uname -sm), $(c++ --version | head -n1)"
    # Columns of each results file: function arg1 arg2 result cr (hex bits).
    perl -e '
        my ($lin, $mac, $dir) = @ARGV;
        open L, "<", $lin or die; open M, "<", $mac or die;
        my (%n, %diff, %linok, %macok, %maxulp, %ex, %bad);
        while (defined(my $l = <L>)) {
            my $m = <M>; my @a = split " ", $l; my @b = split " ", $m;
            die "probe outputs out of step\n" if "$a[0] $a[1] $a[2]" ne "$b[0] $b[1] $b[2]";
            my $f = $a[0]; $n{$f}++;
            next if $a[3] eq $b[3];
            $diff{$f}++; $bad{"$a[0] $a[1] $a[2]"} = 1;
            $linok{$f}++ if $a[4] ne "-" && $a[3] eq $a[4];
            $macok{$f}++ if $a[4] ne "-" && $b[3] eq $a[4];
            my $u = abs(hex($a[3]) - hex($b[3])); $maxulp{$f} = $u if !defined $maxulp{$f} || $u > $maxulp{$f};
            $ex{$f} //= sprintf("%s(%s%s): linux %s mac %s", $f, $a[1], $a[2] eq "0" ? "" : ", $a[2]", $a[3], $b[3]);
        }
        printf "   %-8s %9s %9s %8s %12s %12s  %s\n", "function", "distinct", "differ", "max ulp", "linux = cr", "mac = cr", "first example (hex bits)";
        for my $f (sort keys %n) {
            printf "   %-8s %9d %9d %8s %12s %12s  %s\n", $f, $n{$f}, $diff{$f} // 0, $maxulp{$f} // "-",
                $diff{$f} ? ($linok{$f} // 0) : "-", $diff{$f} ? ($macok{$f} // 0) : "-", $ex{$f} // "";
        }
        print "   (cr = double-precision result rounded to float, glibc; counted over the differing results only)\n";
        print "   per render: distinct libm arguments, and how many of them give different bits on the two platforms\n";
        for my $file (sort glob("$dir/args_*.txt")) {
            my ($name) = $file =~ m{args_(.*)\.txt$}; open A, "<", $file or die;
            my ($t, $d, %by) = (0, 0);
            while (<A>) { chomp; $t++; if ($bad{$_}) { $d++; $by{(split " ")[0]}++; } }
            printf "   %-26s %7d %6d  %s\n", $name, $t, $d, join(" ", map { "$_:$by{$_}" } sort keys %by);
        }
    ' "$OUT/libm/results_linux.txt" "$OUT/libm/results_host.txt" "$OUT/libm" | tee "$OUT/libm/summary.txt"
fi

# 4. Sanitizers.
if [ "$SANITIZE" -eq 1 ]; then
    run_container sanitize || status=1
fi

echo
echo "build_linux_arm64: outputs in $OUT (logs/, renders/, diff_profiles/, diff_table.txt)"
if [ "$status" -eq 0 ]; then echo "build_linux_arm64: OK"; else echo "build_linux_arm64: FAILED"; fi
exit "$status"
