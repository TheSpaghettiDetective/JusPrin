#!/bin/sh
# Sets up OctoPrint on the first start (settings, an admin user, and the API
# key from OCTOPRINT_API_KEY), then starts it. Later starts keep what the data
# volume holds, except the camera addresses, which every start sets from
# WEBCAM_STREAM and WEBCAM_SNAPSHOT.
set -e
base=/octoprint/octoprint
if [ ! -f "$base/users.yaml" ]; then
  mkdir -p "$base/data/appkeys"
  # The image ships a starter config.yaml owned by root: replace the file,
  # as OctoPrint does when it saves, rather than write into it.
  rm -f "$base/config.yaml"
  cp /seed/config.yaml "$base/config.yaml"
  octoprint --basedir "$base" user add --password jusprin-test --admin admin
  printf 'admin:\n- api_key: %s\n  app_id: jusprin-test\n' "$OCTOPRINT_API_KEY" > "$base/data/appkeys/keys.yaml"
fi
# Without _config_version the Classic Webcam plugin takes these settings for
# ones from before OctoPrint 1.9 and replaces them with the old defaults.
octoprint --basedir "$base" config set --json plugins.classicwebcam \
  "{\"_config_version\": 1, \"stream\": \"$WEBCAM_STREAM\", \"snapshot\": \"$WEBCAM_SNAPSHOT\", \"streamRatio\": \"4:3\"}"
exec octoprint --basedir "$base" serve --host 0.0.0.0 --port 5000
