add_executable(workspace_contract_tests
    test_workspace_contract.cpp
)

target_include_directories(workspace_contract_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(workspace_contract_tests PRIVATE Catch2WithMain)

orcaslicer_discover_tests(workspace_contract_tests)

if (SLIC3R_GUI)
    add_executable(workspace_adapter_integration_harness
        real_adapter_harness.cpp
    )
    target_compile_definitions(workspace_adapter_integration_harness PRIVATE
        JUSPRIN_SOURCE_DIR=R"\(${CMAKE_SOURCE_DIR}\)"
    )
    target_include_directories(workspace_adapter_integration_harness PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/slic3r/GUI
    )
    target_link_libraries(workspace_adapter_integration_harness PRIVATE libslic3r_gui)
    if (APPLE)
        set_target_properties(workspace_adapter_integration_harness PROPERTIES
            OUTPUT_NAME "JusPrinWorkspaceHarness"
            MACOSX_BUNDLE_BUNDLE_NAME "JusPrin Workspace Harness"
            MACOSX_BUNDLE_GUI_IDENTIFIER "io.jusprin.workspace-harness"
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        )
    endif()
endif()
