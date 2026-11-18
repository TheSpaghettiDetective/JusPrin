#!/usr/bin/env bash
# Brings a dependency build folder (deps/build/<arch>) up to date with the
# deps/ recipe of the checkout you run it from, before you build the app
# against that folder:
#
#   scripts/deps_current.sh <deps build folder>
#
# The recipe's fingerprint is the git tree of deps/ (`git rev-parse HEAD:deps`),
# the same one OrcaSlicer's CI keys its deps cache on (scripts/ci_deps_cache.sh).
# After each successful build this script saves it in the folder as
# jusprin-deps-recipe.txt. When the saved fingerprint differs from this
# checkout's, or none is saved yet, it builds the folder's `deps` target, which
# rebuilds only the libraries whose recipe changed, then saves the new one.
#
# The folder builds from the deps/ it was configured with (its CMakeCache's
# CMAKE_HOME_DIRECTORY), often another checkout. If that checkout's recipe is
# not this checkout's, building cannot help, and the script says so and fails.
# It also fails on uncommitted changes to either deps/, which no fingerprint
# describes.
set -euo pipefail

folder=${1:?usage: scripts/deps_current.sh <deps build folder>}
folder=$(cd "$folder" && pwd)
cache="$folder/CMakeCache.txt"
[ -f "$cache" ] || { echo "deps_current: $folder has no CMakeCache.txt; configure it first (build_release_macos.sh -d)." >&2; exit 1; }

here=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
source_deps=$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$cache")
source_repo=$(git -C "$source_deps" rev-parse --show-toplevel)
cmake_bin=$(sed -n 's/^CMAKE_COMMAND:INTERNAL=//p' "$cache")

for repo in "$here" "$source_repo"; do
    if ! git -C "$repo" diff --quiet HEAD -- deps; then
        echo "deps_current: $repo has uncommitted changes under deps/; commit or drop them first." >&2
        exit 1
    fi
done

want=$(git -C "$here" rev-parse HEAD:deps)
source_recipe=$(git -C "$source_repo" rev-parse HEAD:deps)
if [ "$want" != "$source_recipe" ]; then
    echo "deps_current: $folder builds from $source_repo, whose deps/ recipe ($source_recipe) is not this checkout's ($want)." >&2
    echo "deps_current: bring that checkout to this recipe, or build a deps folder of your own." >&2
    exit 1
fi

record="$folder/jusprin-deps-recipe.txt"
have=$(cat "$record" 2>/dev/null || true)
if [ "$have" = "$want" ]; then
    echo "deps_current: $folder is up to date with deps/ $want."
    exit 0
fi

echo "deps_current: $folder is out of date (built from ${have:-an unrecorded recipe}, now $want); building its deps target."
# Two builds in one folder compete and can leave it inconsistent.
lock="$folder/jusprin-deps-build.lock"
if ! mkdir "$lock" 2>/dev/null; then
    echo "deps_current: another run is building $folder ($lock exists); wait for it, then run this again." >&2
    echo "deps_current: if no build is running there, remove $lock." >&2
    exit 1
fi
trap 'rmdir "$lock"' EXIT
config=$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$cache")
# On a folder built before, deps/python3/python3.cmake relinks CPython's _ssl
# and _hashlib with CPython's own Makefile, which calls the toolchain's cc with
# no SDK, so the link fails with "library 'System' not found" unless SDKROOT
# names one. Use the SDK the folder was configured with.
sysroot=$(sed -n 's/^CMAKE_OSX_SYSROOT:PATH=//p' "$cache")
if [ -z "${SDKROOT:-}" ] && [ -n "$sysroot" ]; then
    export SDKROOT="$sysroot"
fi
"$cmake_bin" --build "$folder" --config "${config:-Release}" --target deps -- -j4
echo "$want" > "$record"
echo "deps_current: $folder is now up to date with deps/ $want."
