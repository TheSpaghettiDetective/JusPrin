add_executable(workspace_contract_tests
    test_workspace_contract.cpp
)

target_include_directories(workspace_contract_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(workspace_contract_tests PRIVATE Catch2WithMain)

orcaslicer_discover_tests(workspace_contract_tests)
