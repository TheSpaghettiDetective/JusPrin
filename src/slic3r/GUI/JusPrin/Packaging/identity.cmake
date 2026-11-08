# JusPrin's publisher, copyright and version, for everything the build stamps
# with them: the Windows version resource
# (src/dev-utils/platform/msw/OrcaSlicer.rc.in), the macOS Info.plist and the
# package (both in packaging.cmake). The top-level CMakeLists.txt includes
# this file right after version.inc, before any of them is configured.
#
# The product name is SLIC3R_APP_NAME, which version.inc already sets to
# JusPrin.

# The brand people know; the copyright names the company that holds it.
set(JUSPRIN_VENDOR "Obico")
# ASCII only: the Windows resource compiler reads the file in code page 1252.
set(JUSPRIN_COPYRIGHT "Copyright (C) 2026 TSD Technology, LLC")

# JusPrin's version lives in one place, Version.hpp; the release workflow
# reads it from there too.
set(_jusprin_version_header "${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Updates/Version.hpp")
file(STRINGS "${_jusprin_version_header}" _jusprin_version_line REGEX "kVersion\\[\\] = \"")
if (NOT _jusprin_version_line MATCHES "\"(([0-9]+)\\.([0-9]+)\\.([0-9]+)[^\"]*)\"")
    message(FATAL_ERROR "No kVersion = \"X.Y.Z\" line in ${_jusprin_version_header}")
endif ()
set(JUSPRIN_VERSION "${CMAKE_MATCH_1}")
set(JUSPRIN_VERSION_MAJOR "${CMAKE_MATCH_2}")
set(JUSPRIN_VERSION_MINOR "${CMAKE_MATCH_3}")
set(JUSPRIN_VERSION_PATCH "${CMAKE_MATCH_4}")
# Re-run CMake when the version changes, so the resource and package follow it.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_jusprin_version_header}")
