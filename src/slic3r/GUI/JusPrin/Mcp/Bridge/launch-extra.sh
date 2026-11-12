# Sourced by the package launcher before locale, graphics, or WebKit setup.
# Leave ordinary GUI launches and their arguments entirely to Orca's launcher.
if [ "${1:-}" = "--mcp-bridge" ]; then
    shift
    # The helper needs only its packaged C++/Boost runtime, not GUI libraries.
    export LD_LIBRARY_PATH="$DIR/lib/orca-runtime:$DIR/bin${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    exec "$DIR/bin/jusprin-mcp" "$@"
fi

# The bundled OpenSSL looks for certificates under the build machine's prefix.
# When it finds none, OrcaSlicer's startup (CurlGlobalInit) picks the distro's
# CA bundle itself but first asks "use system SSL certificate ... Do you want
# to continue?" on every launch. Name the bundle here, from the same list in
# the same order, so the app finds it and never asks. A bundle the user set is
# left alone.
if [ -z "${SSL_CERT_FILE:-}" ]; then
    for bundle in /etc/pki/tls/certs/ca-bundle.crt /etc/ssl/certs/ca-certificates.crt \
        /usr/share/ssl/certs/ca-bundle.crt /usr/local/share/certs/ca-root-nss.crt \
        /etc/ssl/cert.pem /etc/ssl/ca-bundle.pem; do
        if [ -f "$bundle" ]; then
            export SSL_CERT_FILE="$bundle"
            break
        fi
    done
fi
