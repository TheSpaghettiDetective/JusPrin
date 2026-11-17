#!/usr/bin/env bash
# Gives Sentry what it needs to name the Agent and Home pages' own code in a
# page error's stack: each page's index.html and its source map
# (WebShared/inlinedScriptSourceMap.ts), filed under the app's release and the
# page's build (WebShared/errorReports.ts), at the names the page's frames
# carry: app:///agent/index.html and app:///home/index.html.
#
#   sentry_page_maps.sh upload
#       Run after a build, from the repository's top folder. Files this
#       build's pages and maps under JUSPRIN_PAGE_BUILD, the build the pages
#       were built with, then verifies them. A file Sentry already holds for
#       that build must be the same file; another platform's build of the same
#       commit uploads the same pages.
#   sentry_page_maps.sh verify [<resources folder>]
#       Fails unless Sentry holds, for this version, exactly these index.html
#       files (by content) and a source map beside each. <resources folder> is
#       the one that holds jusprin/agent and jusprin/home, by default the
#       repository's; the release skill passes a released app's.
#
# Reads SENTRY_AUTH_TOKEN, SENTRY_URL, SENTRY_ORG and SENTRY_PROJECT, and
# fails without any of them. Sentry 23.1.1 finds a page's map by its frames'
# file name, release and dist only; it has no debug ids for JavaScript.
set -euo pipefail

top=$(cd "$(dirname "$0")/../../../../.." && pwd)

missing=()
for name in SENTRY_AUTH_TOKEN SENTRY_URL SENTRY_ORG SENTRY_PROJECT; do
    [ -n "${!name:-}" ] || missing+=("$name")
done
if [ ${#missing[@]} -gt 0 ]; then
    echo "::error::Sentry cannot get the pages' source maps without the secret(s) ${missing[*]}; page error reports from this build would not name the pages' code."
    exit 1
fi

command=${1:-}
if [ "$command" != upload ] && [ "$command" != verify ]; then
    echo "usage: $0 upload | verify [<resources folder>]" >&2
    exit 2
fi
resources=${2:-$top/resources}

# The release the app's events carry (Updates/Version.hpp version_label()).
version=$(sed -n 's/.*kVersion\[\] = "\(.*\)";.*/\1/p' "$top/src/slic3r/GUI/JusPrin/Updates/Version.hpp")
orca=$(sed -n 's/^set(SoftFever_VERSION "\(.*\)")/\1/p' "$top/version.inc")
release="JusPrin $version (OrcaSlicer $orca)"
encoded=$(node -e 'console.log(encodeURIComponent(process.argv[1]))' "$release")
api="${SENTRY_URL%/}/api/0"
files_api="$api/projects/$SENTRY_ORG/$SENTRY_PROJECT/releases/$encoded/files/"
auth=(-H "Authorization: Bearer $SENTRY_AUTH_TOKEN")

page_html() { printf '%s/jusprin/%s/index.html' "$resources" "$1"; }
page_map() {
    case "$1" in
        agent) printf '%s/src/slic3r/GUI/JusPrin/AgentUI/dist/index.html.map' "$top" ;;
        home) printf '%s/src/slic3r/GUI/JusPrin/HomeUI/dist/index.html.map' "$top" ;;
    esac
}
sha1_of() { node -e 'console.log(require("crypto").createHash("sha1").update(require("fs").readFileSync(process.argv[1])).digest("hex"))' "$1"; }

# Every file Sentry holds for the release, one "<name>\t<dist>\t<sha1>\t<Sourcemap header>" per line.
held_files() {
    local cursor="" page
    while :; do
        page=$(curl -fsS -D "$headers_file" "${auth[@]}" "$files_api?per_page=100${cursor:+&cursor=$cursor}") || return 1
        printf '%s' "$page" | node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{for(const f of JSON.parse(s))console.log([f.name,f.dist??"",f.sha1,(f.headers||{}).Sourcemap??""].join("\t"))})'
        cursor=$(grep -i '^link:' "$headers_file" | sed -n 's/.*rel="next"; results="true"; cursor="\([^"]*\)".*/\1/p')
        [ -n "$cursor" ] || return 0
    done
}
headers_file=$(mktemp)
trap 'rm -f "$headers_file"' EXIT

if [ "$command" = upload ]; then
    build=${JUSPRIN_PAGE_BUILD:-}
    if [ -z "$build" ]; then
        echo "::error::JUSPRIN_PAGE_BUILD names no build, so the pages carry none and Sentry could not tell their maps from another build's."
        exit 1
    fi
    for page in agent home; do
        for file in "$(page_html $page)" "$(page_map $page)"; do
            [ -f "$file" ] || { echo "::error::$file does not exist."; exit 1; }
        done
        grep -q "\"$build\"" "$(page_html $page)" || {
            echo "::error::$(page_html $page) was not built with JUSPRIN_PAGE_BUILD=$build."
            exit 1
        }
    done
    # An existing release answers 208; either way it is there afterwards.
    curl -fsS -o /dev/null "${auth[@]}" -H 'Content-Type: application/json' \
        -d "$(node -e 'console.log(JSON.stringify({version:process.argv[1],projects:[process.argv[2]]}))' "$release" "$SENTRY_PROJECT")" \
        "$api/organizations/$SENTRY_ORG/releases/"
    held=$(held_files)
    for page in agent home; do
        for kind in html map; do
            if [ $kind = html ]; then file=$(page_html $page); name="app:///$page/index.html"; header=(-F "header=Sourcemap:$name.map")
            else file=$(page_map $page); name="app:///$page/index.html.map"; header=(); fi
            sha=$(sha1_of "$file")
            existing=$(printf '%s\n' "$held" | awk -F'\t' -v n="$name" -v d="$build" '$1==n && $2==d {print $3}')
            if [ -n "$existing" ]; then
                [ "$existing" = "$sha" ] || {
                    echo "::error::Sentry already holds a different $name for build $build; this platform's page differs from another's of the same commit, and one map cannot serve both."
                    exit 1
                }
                echo "Sentry already holds this $name for build $build."
                continue
            fi
            curl -fsS -o /dev/null "${auth[@]}" -F "file=@$file" -F "name=$name" -F "dist=$build" ${header[@]+"${header[@]}"} "$files_api" || {
                # Another platform's job may have filed it a moment ago.
                again=$(held_files | awk -F'\t' -v n="$name" -v d="$build" '$1==n && $2==d {print $3}')
                [ "$again" = "$sha" ] || { echo "::error::Sentry refused $name for build $build."; exit 1; }
            }
            echo "Filed $name for build $build."
        done
    done
fi

# What Sentry holds for these pages: the same index.html, its Sourcemap header,
# and the map beside it under the same build.
held=$(held_files)
for page in agent home; do
    file=$(page_html $page)
    [ -f "$file" ] || { echo "::error::$file does not exist."; exit 1; }
    sha=$(sha1_of "$file")
    build=$(printf '%s\n' "$held" | awk -F'\t' -v n="app:///$page/index.html" -v s="$sha" '$1==n && $3==s && $4==n".map" {print $2; exit}')
    if [ -z "$build" ] || ! printf '%s\n' "$held" | awk -F'\t' -v n="app:///$page/index.html.map" -v d="$build" '$1==n && $2==d {f=1} END {exit !f}'; then
        echo "::error::Sentry holds no source map for this $page page ($file) in release \"$release\", so its page error reports would not name the page's code."
        exit 1
    fi
    echo "Sentry holds the $page page's source map (release \"$release\", build $build)."
done
