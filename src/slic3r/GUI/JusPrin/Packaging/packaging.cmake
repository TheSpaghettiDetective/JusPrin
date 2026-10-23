# JusPrin's package identity. The top-level CMakeLists.txt includes this file
# just before include(CPack), so each value set here replaces OrcaSlicer's.
#
# Without it the Windows installer calls itself OrcaSlicer by SoftFever,
# version 2.4.2, and uses OrcaSlicer's uninstall key. Because OrcaSlicer turns
# on CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL, that installer would first
# uninstall a real OrcaSlicer. With the key "JusPrin", the one JusPrin 1.x
# used, installing 2.x replaces 1.x instead.
#
# The macOS app and the Linux AppImage get the same identity in the release
# workflow (.github/workflows/jusprin_release.yml), which renames and edits
# them after the build.

# JusPrin's version lives in one place, Version.hpp; the release workflow
# reads it from there too.
set(_jusprin_version_header "${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Updates/Version.hpp")
file(STRINGS "${_jusprin_version_header}" _jusprin_version_line REGEX "kVersion\\[\\] = \"")
if (NOT _jusprin_version_line MATCHES "\"(([0-9]+)\\.([0-9]+)\\.([0-9]+)[^\"]*)\"")
    message(FATAL_ERROR "No kVersion = \"X.Y.Z\" line in ${_jusprin_version_header}")
endif ()
set(JUSPRIN_VERSION "${CMAKE_MATCH_1}")
set(CPACK_PACKAGE_VERSION_MAJOR "${CMAKE_MATCH_2}")
set(CPACK_PACKAGE_VERSION_MINOR "${CMAKE_MATCH_3}")
set(CPACK_PACKAGE_VERSION_PATCH "${CMAKE_MATCH_4}")
set(CPACK_PACKAGE_VERSION "${JUSPRIN_VERSION}")

set(CPACK_PACKAGE_NAME "JusPrin")
set(CPACK_PACKAGE_VENDOR "Obico")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "JusPrin, the AI-piloted 3D printing slicer built on OrcaSlicer")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/TheSpaghettiDetective/JusPrin")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "JusPrin")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "JusPrin")
set(CPACK_NSIS_DISPLAY_NAME "JusPrin")
set(CPACK_NSIS_PACKAGE_NAME "JusPrin")
set(CPACK_NSIS_URL_INFO_ABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")
# NSIS needs a backslash before the file name, as in OrcaSlicer's value.
set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/resources/jusprin/images\\\\JusPrin.ico")
set(CPACK_NSIS_MUI_ICON "${CPACK_PACKAGE_ICON}")
set(CPACK_NSIS_MUI_UNIICON "${CPACK_PACKAGE_ICON}")
# The executable keeps OrcaSlicer's file name; the shortcuts are called JusPrin.
set(CPACK_PACKAGE_EXECUTABLES "orca-slicer;JusPrin")
# Not used (the generator is NSIS), but never OrcaSlicer's upgrade code.
set(CPACK_WIX_UPGRADE_GUID "4f6d1c0e-6a3b-4b0e-9a57-3d2c8e1f7b42")

# Keep OrcaSlicer's architecture suffix (_x64, _arm64) and swap the rest.
set(_jusprin_orca_file_prefix "OrcaSlicer_Windows_Installer_V${SoftFever_VERSION}")
string(FIND "${CPACK_PACKAGE_FILE_NAME}" "${_jusprin_orca_file_prefix}" _jusprin_prefix_at)
if (NOT _jusprin_prefix_at EQUAL 0)
    message(FATAL_ERROR "OrcaSlicer's installer name changed to '${CPACK_PACKAGE_FILE_NAME}'; "
                        "update src/slic3r/GUI/JusPrin/Packaging/packaging.cmake to match.")
endif ()
string(REPLACE "${_jusprin_orca_file_prefix}" "JusPrin_Windows_Installer_V${JUSPRIN_VERSION}"
       CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}")
