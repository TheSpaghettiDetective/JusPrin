# GUI-free contract tests for the Agent bridge host. The host sources are
# compiled directly into the test so no wx or Orca GUI linkage is required.
add_executable(agent_bridge_tests
    test_agent_bridge.cpp
    test_agent_setup.cpp
    test_openai_responses_agent.cpp
    test_project_state.cpp
    test_tool_coordinator.cpp
    test_tool_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/AgentHost.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/AgentSetup.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/DeterministicMockAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ManufacturingHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/OpenAIResponsesAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ProjectPersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ProjectStateDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ToolExecutionCoordinator.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ToolRegistry.cpp
)

target_compile_definitions(agent_bridge_tests PRIVATE
    JUSPRIN_SOURCE_DIR=R"\(${CMAKE_SOURCE_DIR}\)"
)
target_include_directories(agent_bridge_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps_src
)
target_link_libraries(agent_bridge_tests PRIVATE Catch2WithMain nlohmann_json OpenSSL::Crypto)

orcaslicer_discover_tests(agent_bridge_tests)
