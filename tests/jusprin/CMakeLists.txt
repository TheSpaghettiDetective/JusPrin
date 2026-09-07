# GUI-free contract tests for fork-owned JusPrin data structures that carry no
# wx or Orca types. The sources under test are compiled directly into the test
# binary, so nothing here links libslic3r_gui.
add_executable(jusprin_tests
    test_spool_store.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Workspace/SpoolStore.cpp
)

target_compile_definitions(jusprin_tests PRIVATE
    JUSPRIN_SOURCE_DIR=R"\(${CMAKE_SOURCE_DIR}\)"
)
target_include_directories(jusprin_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps_src
)
target_link_libraries(jusprin_tests PRIVATE Catch2WithMain nlohmann_json)

orcaslicer_discover_tests(jusprin_tests)
