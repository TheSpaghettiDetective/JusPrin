# libslic3r_gui embeds the bundled Python (pybind11::embed), so every test
# executable that links it needs that runtime beside it: on Windows the loader
# looks for python312.dll in the executable's folder, and
# find_bundled_python_home() looks for <exe dir>/python on every platform.
# The same copies OrcaSlicer's tests/slic3rutils/CMakeLists.txt makes for its
# own GUI tests, except Flatpak, which JusPrin does not build.
function(jusprin_copy_python_runtime target)
    if (WIN32)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/python"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_PREFIX_PATH}/libpython" "$<TARGET_FILE_DIR:${target}>/python"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_PREFIX_PATH}/libpython/python${_bundled_python_abi}.dll"
                "${CMAKE_PREFIX_PATH}/libpython/vcruntime140.dll"
                "${CMAKE_PREFIX_PATH}/libpython/vcruntime140_1.dll"
                "$<TARGET_FILE_DIR:${target}>"
            COMMENT "Copying the Python runtime for ${target}"
            VERBATIM
        )
    else ()
        if (APPLE)
            target_link_options(${target} PRIVATE "LINKER:-rpath,@executable_path/python/lib")
        else ()
            set_property(TARGET ${target} APPEND PROPERTY BUILD_RPATH "$ORIGIN/python/lib")
        endif ()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:${target}>/python"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_PREFIX_PATH}/libpython" "$<TARGET_FILE_DIR:${target}>/python"
            COMMENT "Copying the Python runtime for ${target}"
            VERBATIM
        )
    endif ()
endfunction()
