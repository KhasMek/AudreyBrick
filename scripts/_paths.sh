# Copyright (c) 2026 KhasMek
# SPDX-License-Identifier: MIT
#
# Path safety helpers shared by the M3 scripts (build_device.sh,
# test_on_knulli_rootfs.sh, package_port.sh). Sourced, not run. The same rules as
# scripts/build_linux_arm64.sh, which keeps its own copy:
#   - paths are compared as resolved physical paths (symlinks, "..", trailing
#     slashes);
#   - an output directory outside the repository must not be the root, the
#     repository, inside it, or contain it;
#   - a directory is only reused if it is new, empty, or carries the calling
#     script's marker file, and only the script's own output names are removed.
#
# The caller sets REPO_ROOT and AB_SCRIPT (its name, for messages).

# Physical absolute path of $1, which need not exist: the longest existing ancestor
# is resolved with pwd -P and the rest is appended. A "." or ".." in the
# non-existing part is refused, because it cannot be resolved without creating
# directories.
ab_resolve_path() {
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

# Exits 2 if the resolved path $2 is the root, the home directory, the repository,
# inside it, or contains it. $1 names the path in the message.
ab_check_outside_repo() {
    local what="$1" p="$2" home
    home="$(cd "$HOME" 2>/dev/null && pwd -P || echo "")"
    if [ "$p" = "/" ] || [ -z "$p" ] || [ "$p" = "$REPO_ROOT" ] || { [ -n "$home" ] && [ "$p" = "$home" ]; }; then
        echo "$AB_SCRIPT: $what ($p) is the root, the home directory or the repository; refused" >&2; exit 2
    fi
    case "$p/" in
        "$REPO_ROOT"/*) echo "$AB_SCRIPT: $what ($p) is inside the repository ($REPO_ROOT); refused" >&2; exit 2 ;;
    esac
    case "$REPO_ROOT/" in
        "$p"/*) echo "$AB_SCRIPT: $what ($p) contains the repository ($REPO_ROOT); refused" >&2; exit 2 ;;
    esac
    case "$p" in *,*) echo "$AB_SCRIPT: $what ($p) contains a comma, which docker --mount cannot take; refused" >&2; exit 2 ;; esac
}

# Exits 2 unless the resolved directory $2 does not exist, is empty, or holds the
# marker file $3. $1 names it in the message.
ab_check_reusable() {
    local what="$1" p="$2" marker="$3"
    if [ -e "$p" ] || [ -L "$p" ]; then
        [ -d "$p" ] && [ ! -L "$p" ] || { echo "$AB_SCRIPT: $what ($p) exists and is not a directory; refused" >&2; exit 2; }
        if [ ! -f "$p/$marker" ] && [ -n "$(ls -A "$p")" ]; then
            echo "$AB_SCRIPT: $what ($p) is not empty and was not made by this script (no $marker in it);" >&2
            echo "  refused, so that nothing in it is deleted. Use a new or empty directory." >&2
            exit 2
        fi
    fi
}
