# GUI-free contract tests for the Agent bridge host. The host sources are
# compiled directly into the test so no wx or Orca GUI linkage is required.
add_executable(agent_bridge_tests
    test_agent_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/AgentHost.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/DeterministicMockAgent.cpp
)

target_compile_definitions(agent_bridge_tests PRIVATE
    JUSPRIN_SOURCE_DIR=R"\(${CMAKE_SOURCE_DIR}\)"
)
target_include_directories(agent_bridge_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps_src
)
target_link_libraries(agent_bridge_tests PRIVATE Catch2WithMain nlohmann_json)

orcaslicer_discover_tests(agent_bridge_tests)
