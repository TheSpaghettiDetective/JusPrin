# GUI-free checks on the JusPrin design tokens: the semantic palette must keep
# the contrast the design system promises, in both appearance modes, so a
# token edit cannot silently make text unreadable.
add_executable(brand_tokens_tests
    test_brand_tokens.cpp
)

target_compile_definitions(brand_tokens_tests PRIVATE
    JUSPRIN_SOURCE_DIR=R"\(${CMAKE_SOURCE_DIR}\)"
)
target_include_directories(brand_tokens_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/deps_src
)
target_link_libraries(brand_tokens_tests PRIVATE Catch2WithMain nlohmann_json)

orcaslicer_discover_tests(brand_tokens_tests)
