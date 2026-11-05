#!/bin/sh
# Prints the API keys as shell exports, for --print-host-check's cases file:
#   eval "$(tests/printer_hosts/keys.sh)"
# Moonraker makes its own key on first start and keeps it until `down -v`.
set -e
cd "$(dirname "$0")"
moonraker=$(docker compose exec -T moonraker-key python3 -c \
  "import json, urllib.request; print(json.load(urllib.request.urlopen('http://127.0.0.1:7125/access/api_key'))['result'])")
octoprint=$(docker compose exec -T octoprint printenv OCTOPRINT_API_KEY)
echo "export JP_MOONRAKER_KEY=$moonraker"
echo "export JP_OCTOPRINT_KEY=$octoprint"
echo "export JP_BAD_KEY=not-the-key"
