if (SLIC3R_GUI)
    add_executable(shell_integration_harness
        shell_harness.cpp
    )
    target_compile_definitions(shell_integration_harness PRIVATE
        JUSPRIN_SOURCE_DIR=R"\(${CMAKE_SOURCE_DIR}\)"
        JUSPRIN_MCP_BRIDGE_PATH="$<TARGET_FILE:jusprin-mcp>"
    )
    add_dependencies(shell_integration_harness jusprin-mcp)
    add_custom_command(TARGET shell_integration_harness POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:jusprin-mcp>"
                "$<TARGET_FILE_DIR:shell_integration_harness>/$<TARGET_FILE_NAME:jusprin-mcp>"
        VERBATIM
    )
    target_include_directories(shell_integration_harness PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/slic3r/GUI
    )
    target_link_libraries(shell_integration_harness PRIVATE libslic3r_gui)
    if (APPLE)
        target_sources(shell_integration_harness PRIVATE harness_appearance.mm)
        set_target_properties(shell_integration_harness PROPERTIES
            OUTPUT_NAME "JusPrinShellHarness"
            MACOSX_BUNDLE_BUNDLE_NAME "JusPrin Shell Harness"
            MACOSX_BUNDLE_GUI_IDENTIFIER "io.jusprin.shell-harness"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        )
    endif()
endif()
