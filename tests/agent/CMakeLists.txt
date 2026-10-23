# GUI-free contract tests for the Agent bridge host. The host sources are
# compiled directly into the test so no wx or Orca GUI linkage is required.
add_executable(agent_bridge_tests
    test_agent_bridge.cpp
    test_agent_setup.cpp
    test_openai_responses_agent.cpp
    test_provider_agent.cpp
    test_provider_configuration.cpp
    test_model_catalog.cpp
    test_anthropic_codec.cpp
    test_provider_conformance.cpp
    test_project_state.cpp
    test_tool_coordinator.cpp
    test_tool_registry.cpp
    test_printer_conversation.cpp
    test_primary_print_action.cpp
    test_auto_slice_policy.cpp
    test_mcp_protocol.cpp
    test_mcp_runtime.cpp
    test_mcp_discovery.cpp
    test_mcp_bridge.cpp
    test_mcp_connections.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Mcp/McpCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Mcp/McpConfigFile.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Mcp/McpDiscoveryFile.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Mcp/McpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Mcp/McpServer.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Mcp/McpRuntime.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/AgentHost.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/AgentSetup.cpp
    ${CMAKE_SOURCE_DIR}/tests/jusprin_support/DeterministicMockAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ManufacturingHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/PrinterSetup/PrinterConversation.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/PrinterSetup/PrinterCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/OpenAIResponsesAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/AnthropicCodec.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ProviderAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ProviderFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ModelCatalog.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ResponsesCodec.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ProjectPersistence.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ProjectStateDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Workspace/PrinterFactsStore.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ToolExecutionCoordinator.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ToolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ToolResults.cpp
)

target_compile_definitions(agent_bridge_tests PRIVATE
    JUSPRIN_SOURCE_DIR=R"\(${CMAKE_SOURCE_DIR}\)"
)
target_include_directories(agent_bridge_tests PRIVATE
    ${CMAKE_BINARY_DIR}/src/libslic3r
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps_src
)
target_link_libraries(agent_bridge_tests PRIVATE Catch2WithMain nlohmann_json OpenSSL::Crypto Boost::system Threads::Threads)
target_link_libraries(agent_bridge_tests PRIVATE jusprin_mcp_bridge_core)

orcaslicer_discover_tests(agent_bridge_tests)

# The registry's own argument check as a line-oriented command, so the prompt
# test runner refuses a model's call exactly as ProviderAgent does.
add_executable(tool_call_check
    tool_call_check.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ToolRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/slic3r/GUI/JusPrin/Agent/ToolResults.cpp
)
set_target_properties(tool_call_check PROPERTIES MACOSX_BUNDLE OFF)
target_include_directories(tool_call_check PRIVATE
    ${CMAKE_BINARY_DIR}/src/libslic3r
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps_src
)
target_link_libraries(tool_call_check PRIVATE nlohmann_json)

# Live smoke against real providers. It links the GUI library for the app's
# own HTTP transport. Every case is hidden and skips without a key, and none
# is registered with ctest: it runs only when asked for by tag, because every
# run is billed (see the file's header).
if (SLIC3R_GUI)
    add_executable(agent_live_tests test_provider_live.cpp)
    target_include_directories(agent_live_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(agent_live_tests PRIVATE Catch2WithMain libslic3r_gui)
    orcaslicer_copy_test_dlls()
endif()
