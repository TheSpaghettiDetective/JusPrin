# Fork-owned targets that must exist after the OrcaSlicer target is defined.
# src/CMakeLists.txt reaches this file with a single add_subdirectory; add new
# fork targets here, not there.
add_subdirectory(Mcp/Bridge)
# Bridge exports the AppImage packaging hook one scope up. This file is now
# that scope; forward it to src/ where build_linux_image.sh.in is configured.
set(SLIC3R_PACKAGE_EXTRA_SCRIPT "${SLIC3R_PACKAGE_EXTRA_SCRIPT}" PARENT_SCOPE)
add_subdirectory(Web)
