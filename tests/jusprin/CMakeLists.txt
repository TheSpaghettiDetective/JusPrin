# GUI-free contract tests for fork-owned JusPrin data structures that carry no
# wx or Orca types. The sources under test are compiled directly into the test
# binary, so nothing here links libslic3r_gui.
add_executable(jusprin_tests
    test_spool_store.cpp
    test_host_locale.cpp
    test_home_snapshot.cpp
    test_home_host.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Workspace/SpoolStore.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Home/HomeSnapshot.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Home/HomeHost.cpp
)

target_compile_definitions(jusprin_tests PRIVATE
    JUSPRIN_SOURCE_DIR=R"\(${CMAKE_SOURCE_DIR}\)"
)
target_include_directories(jusprin_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps_src
)
# test_common carries the macOS framework link (CoreFoundation) that
# HostLocale.hpp needs there; on Windows and Linux it adds nothing but the
# shared test defines.
target_link_libraries(jusprin_tests PRIVATE test_common Catch2WithMain nlohmann_json)

orcaslicer_discover_tests(jusprin_tests)

# ShellTheme reads the same token file but hands out wxColour and wxFont, and
# its fonts go through OrcaSlicer's Label::sysFont, so its tests link the GUI
# library and initialise wx themselves.
if (SLIC3R_GUI)
    add_executable(shell_theme_tests
        test_shell_theme.cpp
    )
    target_compile_definitions(shell_theme_tests PRIVATE
        JUSPRIN_SOURCE_DIR=R"\(${CMAKE_SOURCE_DIR}\)"
    )
    target_include_directories(shell_theme_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    target_link_libraries(shell_theme_tests PRIVATE Catch2WithMain libslic3r_gui)

    orcaslicer_discover_tests(shell_theme_tests)

    add_executable(jusprin_fake_bambu_tests
        test_fake_bambu_agent.cpp
    )
    target_include_directories(jusprin_fake_bambu_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/slic3r/GUI
    )
    target_link_libraries(jusprin_fake_bambu_tests PRIVATE test_common libslic3r_gui libslic3r Catch2::Catch2WithMain)
    set_property(TARGET jusprin_fake_bambu_tests PROPERTY FOLDER "tests")
    orcaslicer_copy_test_dlls()
    orcaslicer_discover_tests(jusprin_fake_bambu_tests)
endif()
