add_library(jusprin_mcp_bridge_core STATIC
    Bridge.cpp HttpExchange.cpp
    ../McpProtocol.cpp ../McpDiscoveryFile.cpp
    ../../Agent/ToolRegistry.cpp ../../Agent/ToolResults.cpp
)
target_include_directories(jusprin_mcp_bridge_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/deps_src ${CMAKE_BINARY_DIR}/src/libslic3r
)
target_link_libraries(jusprin_mcp_bridge_core PUBLIC nlohmann_json Boost::system Threads::Threads)
if(WIN32)
    target_link_libraries(jusprin_mcp_bridge_core PUBLIC ws2_32)
endif()

add_executable(jusprin-mcp main.cpp)
set_target_properties(jusprin-mcp PROPERTIES MACOSX_BUNDLE FALSE
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:OrcaSlicer>")
target_link_libraries(jusprin-mcp PRIVATE jusprin_mcp_bridge_core)
add_dependencies(OrcaSlicer jusprin-mcp)
if(NOT APPLE)
    if(WIN32)
        install(TARGETS jusprin-mcp RUNTIME DESTINATION ".")
    else()
        install(TARGETS jusprin-mcp RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
        set(SLIC3R_PACKAGE_EXTRA_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/package_linux.sh" PARENT_SCOPE)
    endif()
endif()
