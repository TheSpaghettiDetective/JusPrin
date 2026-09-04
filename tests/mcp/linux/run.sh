#!/bin/sh
# Read-only checkout, disposable writable build directory, existing generated
# version header. Reuse the actual production/helper and Agent test targets.
set -eu
repository=$1
work_directory=$2
version_header=$3
mkdir -p "$work_directory/source"
cp "$repository/tests/mcp/linux/CMakeLists.txt" "$work_directory/source/CMakeLists.txt"
for directory in src deps_src tests resources; do
    ln -s "$repository/$directory" "$work_directory/source/$directory"
done
cmake -S "$work_directory/source" -B "$work_directory/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DJUSPRIN_VERSION_HEADER="$version_header"
cmake --build "$work_directory/build" --target jusprin-mcp agent_bridge_tests --parallel 3
ctest --test-dir "$work_directory/build" --output-on-failure
JUSPRIN_TEST_BRIDGE="$work_directory/build/jusprin-mcp" PYTHONDONTWRITEBYTECODE=1 \
    python3 -m unittest discover -s "$repository/tests/mcp" -v
