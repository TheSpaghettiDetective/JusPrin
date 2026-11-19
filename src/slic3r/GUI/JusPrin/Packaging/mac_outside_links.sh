#!/usr/bin/env bash
# Usage: mac_outside_links.sh <folder> [<allowed folder>...]
#
# Lists every program and library under <folder> that loads a library from
# outside macOS (/usr/lib, /System), outside itself (@rpath, @loader_path,
# @executable_path) and outside the allowed folders, and exits 1 if there is
# one. A Mac that builds JusPrin can have Homebrew or XQuartz installed, and a
# build that links their libraries runs there but fails on a Mac without them.
set -euo pipefail

dir=$1
shift
allowed=("$@")

found=0
while IFS= read -r -d '' file; do
  # Only the load commands: a library's own install name is not a load.
  while IFS= read -r lib; do
    case "$lib" in
      /usr/lib/* | /System/* | @rpath/* | @loader_path/* | @executable_path/*) continue ;;
    esac
    ok=0
    for prefix in "${allowed[@]+"${allowed[@]}"}"; do
      case "$lib" in "$prefix"/*) ok=1 ;; esac
    done
    if [ "$ok" = 0 ]; then
      echo "$file loads $lib"
      found=1
    fi
  done < <(otool -l "$file" 2>/dev/null |
    awk '/cmd LC_(LOAD|LOAD_WEAK|REEXPORT|LAZY_LOAD|LOAD_UPWARD)_DYLIB$/ { load = 1; next }
         load && $1 == "name" { print $2; load = 0 }' | sort -u)
done < <(find "$dir" -type f \( -name '*.dylib' -o -name '*.so' -o -perm -u+x \) -print0)

# A static library loads nothing itself, but its CMake, pkg-config, libtool
# or wx-config files hand the libraries it needs to whatever links it: a
# library file's absolute path, or a folder after -L. Those must not be in
# Homebrew, MacPorts or XQuartz. (A folder a Find module merely searches is
# neither.)
outside='(-L|^|[[:space:]"'"'"';=(])(/opt/homebrew|/usr/local|/opt/local|/opt/X11|/usr/X11R6|/usr/X11)/[^[:space:]"'"'"';)]*'
while IFS= read -r -d '' file; do
  links=$(grep -E -o "$outside" "$file" |
    sed -E 's/^[[:space:]"'"'"';=(]//' |
    grep -E '^-L|\.(dylib|tbd|so|a|la)$|\.framework' | sort -u || true)
  if [ -n "$links" ]; then
    echo "$links" | sed "s|^|$file links |"
    found=1
  fi
done < <(find "$dir" -type f \( -name '*.cmake' -o -name '*.pc' -o -name '*.la' -o -path '*/wx/config/*' \) -print0)

exit "$found"
