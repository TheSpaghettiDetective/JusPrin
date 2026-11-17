#!/bin/sh
# Called by the Linux packager before its dependency-closure scan.
set -eu
binary_directory=$(dirname -- "$1")
package_directory=$2
script_directory=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cp -fL "$binary_directory/jusprin-mcp" "$package_directory/bin/jusprin-mcp"
cp -f "$script_directory/launch-extra.sh" "$package_directory/libexec/launch-extra"
chmod a+x "$package_directory/bin/jusprin-mcp"
# Sentry's crash handler, which SentryClient.cpp looks for beside the app.
cp -fL "$binary_directory/crashpad_handler" "$package_directory/bin/crashpad_handler"
chmod a+x "$package_directory/bin/crashpad_handler"
