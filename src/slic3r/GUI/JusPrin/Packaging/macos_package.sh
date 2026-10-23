#!/usr/bin/env bash
# Turns OrcaSlicer's universal app into JusPrin.app and puts it in a disk image.
#
#   macos_package.sh <OrcaSlicer.app> <version> <output dir>
#
# OrcaSlicer's build names the bundle OrcaSlicer.app with OrcaSlicer's bundle
# identifier, icon and a "JusPrin <build id>" version string. This copies it to
# JusPrin.app and sets JusPrin's identity before anything is signed, because
# editing Info.plist afterwards would break the signature.
#
# Signing: with JUSPRIN_SIGN=1 the app and the disk image are signed with the
# Developer ID certificate in MACOS_CERTIFICATE_ID (already in the keychain)
# and notarised with APPLE_DEV_ACCOUNT, TEAM_ID and APP_PWD. Otherwise the app
# gets an ad-hoc signature, which keeps the edited bundle consistent but which
# Gatekeeper rejects for a download: that build is for testing only.
set -euo pipefail

source_app="$1"
version="$2"
out_dir="$3"

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "${script_dir}/../../../../.." && pwd)"
entitlements="${repo_root}/scripts/disable_validation.entitlements"
# shellcheck source=/dev/null
source "${repo_root}/scripts/retry.sh"

work="$(mktemp -d)"
stage="${work}/dmg"
app="${stage}/JusPrin.app"
dmg="${out_dir}/JusPrin_Mac_universal_V${version}.dmg"
mkdir -p "${stage}" "${out_dir}"
cp -R "${source_app}" "${app}"

plist="${app}/Contents/Info.plist"
buddy=/usr/libexec/PlistBuddy
set_key() {
    "${buddy}" -c "Set :$1 $2" "${plist}" 2>/dev/null || "${buddy}" -c "Add :$1 string $2" "${plist}"
}
set_key CFBundleIdentifier io.obico.jusprin
set_key CFBundleName JusPrin
set_key CFBundleDisplayName JusPrin
set_key CFBundleShortVersionString "${version}"
set_key CFBundleVersion "${version}"
set_key CFBundleGetInfoString "JusPrin ${version}"
cp "${repo_root}/resources/jusprin/images/JusPrin.icns" "${app}/Contents/Resources/images/JusPrin.icns"
set_key CFBundleIconFile images/JusPrin.icns
plutil -lint "${plist}"

if [ "${JUSPRIN_SIGN:-0}" = "1" ]; then
    codesign --deep --force --verbose --options runtime --timestamp \
        --entitlements "${entitlements}" --sign "${MACOS_CERTIFICATE_ID}" "${app}"
else
    codesign --deep --force --sign - "${app}"
fi
codesign --verify --deep --strict --verbose=2 "${app}"

ln -sfn /Applications "${stage}/Applications"
retry hdiutil create -volname "JusPrin" -srcfolder "${stage}" -ov -format UDZO "${dmg}"

if [ "${JUSPRIN_SIGN:-0}" = "1" ]; then
    codesign --force --verbose --timestamp --sign "${MACOS_CERTIFICATE_ID}" "${dmg}"
    xcrun notarytool submit "${dmg}" --apple-id "${APPLE_DEV_ACCOUNT}" --team-id "${TEAM_ID}" \
        --password "${APP_PWD}" --wait
    xcrun stapler staple "${dmg}"
fi

rm -rf "${work}"
echo "${dmg}"
