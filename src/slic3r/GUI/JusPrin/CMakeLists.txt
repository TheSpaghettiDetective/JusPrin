# Fork-owned targets that must exist after the OrcaSlicer target is defined.
# src/CMakeLists.txt reaches this file with a single add_subdirectory; add new
# fork targets here, not there.
add_subdirectory(Mcp/Bridge)
# Bridge exports the AppImage packaging hook one scope up. This file is now
# that scope; forward it to src/ where build_linux_image.sh.in is configured.
set(SLIC3R_PACKAGE_EXTRA_SCRIPT "${SLIC3R_PACKAGE_EXTRA_SCRIPT}" PARENT_SCOPE)
add_subdirectory(Web)

# No wxString::ToStdString() in JusPrin's code: CheckWxStringConversions.cmake
# says why. It runs before the GUI library compiles, and again whenever a file
# here changes or is added.
file(GLOB_RECURSE jusprin_checked_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/*.mm")
list(FILTER jusprin_checked_sources EXCLUDE REGEX "/node_modules/")
set(jusprin_conversion_stamp "${CMAKE_CURRENT_BINARY_DIR}/wxstring_conversions.checked")
add_custom_command(OUTPUT "${jusprin_conversion_stamp}"
    COMMAND ${CMAKE_COMMAND} -DJUSPRIN_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DSTAMP=${jusprin_conversion_stamp}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/CheckWxStringConversions.cmake"
    DEPENDS ${jusprin_checked_sources} "${CMAKE_CURRENT_SOURCE_DIR}/CheckWxStringConversions.cmake"
    COMMENT "Checking JusPrin's wxString conversions"
    VERBATIM)
add_custom_target(jusprin-wxstring-conversions DEPENDS "${jusprin_conversion_stamp}")
add_dependencies(libslic3r_gui jusprin-wxstring-conversions)

# Sentry's native SDK (deps/Sentry), which Support/SentryClient.cpp starts:
# the GUI library links it, and crashpad_handler, the process that writes and
# uploads a crash's minidump, sits beside the app's executable, where
# SentryClient looks for it.
find_package(sentry 0.17 REQUIRED CONFIG)
# The plain signature, as src/slic3r/CMakeLists.txt links this target.
target_link_libraries(libslic3r_gui sentry::sentry)
# Named by jusprin-mcp's folder, which is the executable's (Mcp/Bridge): a
# target that names OrcaSlicer's own depends on OrcaSlicer, and OrcaSlicer
# depends on this one.
add_custom_target(jusprin-crashpad-handler
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:sentry_crashpad::crashpad_handler>" "$<TARGET_FILE_DIR:jusprin-mcp>/$<TARGET_FILE_NAME:sentry_crashpad::crashpad_handler>"
    VERBATIM)
add_dependencies(jusprin-crashpad-handler jusprin-mcp)
add_dependencies(OrcaSlicer jusprin-crashpad-handler)
if (WIN32)
    install(PROGRAMS "$<TARGET_FILE:sentry_crashpad::crashpad_handler>" DESTINATION ".")
elseif (NOT APPLE)
    include(GNUInstallDirs)
    install(PROGRAMS "$<TARGET_FILE:sentry_crashpad::crashpad_handler>" DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif ()
