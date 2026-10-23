#!/usr/bin/env bash
# Builds JusPrin's AppImage from the package directory build_linux.sh makes.
#
#   appimage.sh <build/package> <version> <output file>
#
# OrcaSlicer's own AppImage script (build_appimage.sh.in) names everything
# after SLIC3R_APP_KEY, which is JusPrin2 here, and looks for icon and
# metainfo files by that name that only OrcaSlicer has. This does the same
# steps with JusPrin's name, icon and desktop entry.
set -euo pipefail

package_dir="$1"
version="$2"
output="$(realpath -m "$3")"

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
app_id=io.obico.JusPrin
work="$(mktemp -d)"
appdir="${work}/JusPrin.AppDir"
tool="${work}/appimagetool.AppImage"

cp -a "${package_dir}" "${appdir}"
mv "${appdir}/orca-slicer" "${appdir}/AppRun"
chmod +x "${appdir}/AppRun"

cp "${script_dir}/JusPrin.png" "${appdir}/JusPrin.png"
mkdir -p "${appdir}/usr/share/icons/hicolor/256x256/apps" "${appdir}/usr/share/applications"
cp "${script_dir}/JusPrin.png" "${appdir}/usr/share/icons/hicolor/256x256/apps/JusPrin.png"
cat > "${appdir}/${app_id}.desktop" <<EOF
[Desktop Entry]
Name=JusPrin
GenericName=3D Printing Software
Comment=AI-piloted 3D printing slicer
Exec=AppRun %F
Icon=JusPrin
Type=Application
Terminal=false
PrefersNonDefaultGPU=true
X-KDE-RunOnDiscreteGpu=true
Categories=Graphics;3DGraphics;Engineering;
MimeType=model/stl;model/3mf;application/vnd.ms-3mfdocument;application/prs.wavefront-obj;application/x-amf;model/step;
X-AppImage-Version=${version}
EOF
cp "${appdir}/${app_id}.desktop" "${appdir}/usr/share/applications/${app_id}.desktop"

# The same tool OrcaSlicer's script downloads.
wget -q "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-$(uname -m).AppImage" -O "${tool}"
chmod +x "${tool}"
mkdir -p "$(dirname "${output}")"
ARCH="$(uname -m)" "${tool}" --appimage-extract-and-run "${appdir}" "${output}"
chmod +x "${output}"

rm -rf "${work}"
echo "${output}"
