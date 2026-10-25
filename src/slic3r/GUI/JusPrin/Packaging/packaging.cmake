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
#
# The version and publisher come from identity.cmake, which the Windows
# version resource of orca-slicer.exe reads too.

set(CPACK_PACKAGE_VERSION_MAJOR "${JUSPRIN_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${JUSPRIN_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${JUSPRIN_VERSION_PATCH}")
set(CPACK_PACKAGE_VERSION "${JUSPRIN_VERSION}")

set(CPACK_PACKAGE_NAME "JusPrin")
set(CPACK_PACKAGE_VENDOR "${JUSPRIN_VENDOR}")
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

# On Windows, OrcaSlicer makes itself the handler of orcaslicer:// links each
# time it starts and when its Preferences dialog closes (GUI_App.cpp), without
# asking. JusPrin leaves those links to OrcaSlicer; the links it opens are the
# ones a person chooses in its Preferences (JusPrin/Preferences).
if (SLIC3R_GUI)
    target_compile_definitions(libslic3r_gui PRIVATE SLIC3R_NO_ORCASLICER_URL_ASSOCIATION)
endif ()

# CMake writes every macOS app bundle's Info.plist from OrcaSlicer's
# cmake/modules/MacOSXBundleInfo.plist.in. JusPrin's bundles use a copy that
# has JusPrin's bundle identifier and no CFBundleURLTypes. That template asks
# macOS to send orcaslicer:// links, such as Thingiverse's and MakerWorld's
# "Open in Orca Slicer" buttons, to the app; JusPrin must leave them to
# OrcaSlicer. JusPrin answers no link on macOS: its "Open in slicer" links
# (Printables, Thingiverse) are a Windows feature, see JusPrin/Preferences.
if (APPLE)
    set(_jusprin_plist "${CMAKE_BINARY_DIR}/jusprin/MacOSXBundleInfo.plist.in")
    configure_file("${CMAKE_SOURCE_DIR}/cmake/modules/MacOSXBundleInfo.plist.in" "${_jusprin_plist}.orca" COPYONLY)
    configure_file("${_jusprin_plist}.orca" "${_jusprin_plist}.edit" COPYONLY)
    foreach (_jusprin_plist_edit "-remove;CFBundleURLTypes" "-replace;CFBundleIdentifier;-string;io.jusprin.JusPrin")
        execute_process(COMMAND plutil ${_jusprin_plist_edit} "${_jusprin_plist}.edit"
                        RESULT_VARIABLE _jusprin_plist_result ERROR_VARIABLE _jusprin_plist_error)
        if (NOT _jusprin_plist_result EQUAL 0)
            string(REPLACE ";" " " _jusprin_plist_edit "${_jusprin_plist_edit}")
            message(FATAL_ERROR "OrcaSlicer's MacOSXBundleInfo.plist.in changed (plutil ${_jusprin_plist_edit}: "
                                "${_jusprin_plist_error}); update src/slic3r/GUI/JusPrin/Packaging/packaging.cmake to match.")
        endif ()
    endforeach ()
    # Rewrite the template only when it changes, so a reconfigure does not
    # rebuild every bundle's Info.plist.
    configure_file("${_jusprin_plist}.edit" "${_jusprin_plist}" COPYONLY)

    # A target that names its own template keeps it.
    function(_jusprin_use_bundle_plist dir)
        get_property(_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
        foreach (_target IN LISTS _targets)
            get_target_property(_type ${_target} TYPE)
            get_target_property(_bundle ${_target} MACOSX_BUNDLE)
            get_target_property(_own ${_target} MACOSX_BUNDLE_INFO_PLIST)
            if (_type STREQUAL "EXECUTABLE" AND _bundle AND NOT _own)
                set_target_properties(${_target} PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${_jusprin_plist}")
            endif ()
        endforeach ()
        get_property(_subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
        foreach (_subdir IN LISTS _subdirs)
            _jusprin_use_bundle_plist("${_subdir}")
        endforeach ()
    endfunction ()
    _jusprin_use_bundle_plist("${CMAKE_SOURCE_DIR}")
endif ()
