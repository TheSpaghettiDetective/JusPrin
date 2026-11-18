#!/usr/bin/env bash
# Gives Sentry what it needs to name the functions in a JusPrin crash or error
# report, with their file and line: the debug files of one platform's build
# (jusprin_release_build.yml), filed under their build ids.
#
#   sentry_debug_files.sh upload <main file> <folder or file>...
#       Uploads every debug file under the folders, then verifies <main file>.
#   sentry_debug_files.sh verify <main file>
#       Fails unless Sentry holds debug information (the "debug" feature:
#       functions with their file and line) for <main file>'s build id, each
#       architecture's in a universal Mac file. A symbol table alone names
#       functions only, so it does not count. The release skill runs it on a
#       released copy of the app.
#
# <main file> is the file that holds the app's own code: OrcaSlicer.dll on
# Windows (the Breakpad file made from its PDB has the same build id), the app's executable on macOS (its
# dSYM) and Linux (its split .debug file). Reads SENTRY_AUTH_TOKEN, SENTRY_URL, SENTRY_ORG and SENTRY_PROJECT,
# and fails without any of them.
set -euo pipefail

# The last 2.x: 3.x supports only self-hosted Sentry 24.11.1 and later.
cli=(npx --yes @sentry/cli@2.58.6)

missing=()
for name in SENTRY_AUTH_TOKEN SENTRY_URL SENTRY_ORG SENTRY_PROJECT; do
    [ -n "${!name:-}" ] || missing+=("$name")
done
if [ ${#missing[@]} -gt 0 ]; then
    echo "::error::Sentry cannot get this build's debug files without the secret(s) ${missing[*]}; crash and error reports from it would show addresses only."
    exit 1
fi

command=${1:-}
main=${2:-}
if [ "$command" != upload ] && [ "$command" != verify ] || [ -z "$main" ]; then
    echo "usage: $0 upload <main file> <folder or file>... | verify <main file>" >&2
    exit 2
fi
shift 2
if [ ! -f "$main" ]; then
    echo "::error::$main does not exist."
    exit 1
fi

debug_ids=$("${cli[@]}" debug-files check --json "$main" |
    node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{const v=JSON.parse(s).variants||[];if(!v.length)process.exit(1);for(const x of v)console.log(x.debug_id)})')

if [ "$command" = upload ]; then
    if [ $# -eq 0 ]; then
        echo "usage: $0 upload <main file> <folder or file>..." >&2
        exit 2
    fi
    "${cli[@]}" debug-files upload --wait-for 600 "$@"
fi

# What Sentry holds under each build id, and whether any of it has file and line.
for debug_id in $debug_ids; do
    held=$(curl -fsS -H "Authorization: Bearer $SENTRY_AUTH_TOKEN" \
        "${SENTRY_URL%/}/api/0/projects/$SENTRY_ORG/$SENTRY_PROJECT/files/dsyms/?query=$debug_id")
    named=$(printf '%s' "$held" |
        node -e 'let s="";process.stdin.on("data",d=>s+=d).on("end",()=>{console.log(JSON.parse(s).filter(f=>(f.data?.features||[]).includes("debug")).length)})')
    if [ "$named" = 0 ]; then
        echo "::error::Sentry holds no debug information for $main (build id $debug_id), so its crash and error reports would show no file or line."
        exit 1
    fi
    echo "Sentry holds debug information for $main (build id $debug_id)."
done
