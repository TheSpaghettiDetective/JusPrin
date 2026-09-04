# Sourced by the package launcher before locale, graphics, or WebKit setup.
# Leave ordinary GUI launches and their arguments entirely to Orca's launcher.
if [ "${1:-}" = "--mcp-bridge" ]; then
    shift
    # The helper needs only its packaged C++/Boost runtime, not GUI libraries.
    export LD_LIBRARY_PATH="$DIR/lib/orca-runtime:$DIR/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    exec "$DIR/bin/jusprin-mcp" "$@"
fi
