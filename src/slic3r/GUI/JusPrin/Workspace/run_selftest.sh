#!/usr/bin/env bash
# Runs the JusPrin workspace adapter self test and reports its exit status.
# Usage: src/slic3r/GUI/JusPrin/Workspace/run_selftest.sh [path-to-OrcaSlicer-binary]
set -uo pipefail

BINARY="${1:-build/arm64/src/RelWithDebInfo/OrcaSlicer.app/Contents/MacOS/OrcaSlicer}"

if [[ ! -x "$BINARY" ]]; then
    echo "No OrcaSlicer binary at $BINARY" >&2
    echo "Build it first: cmake --build build/arm64 --config RelWithDebInfo --target OrcaSlicer" >&2
    exit 2
fi

JUSPRIN_WORKSPACE_SELFTEST=1 "$BINARY"
status=$?
echo "selftest exit=$status"
exit $status
