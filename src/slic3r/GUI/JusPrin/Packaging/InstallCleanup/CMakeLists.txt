# deps_src/stb_dxt installs its header and CMake export files, which only a
# program built against it needs, so JusPrin's install folder, installer and
# portable zip would carry include\stb_dxt and lib\cmake\stb_dxt. This removes
# them, and the folders left empty, after every other install rule:
# packaging.cmake adds this directory after all of OrcaSlicer's.
install(CODE [[
    set(_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
    file(REMOVE_RECURSE "${_root}/include/stb_dxt" "${_root}/lib/cmake/stb_dxt")
    foreach (_dir "${_root}/include" "${_root}/lib/cmake" "${_root}/lib")
        file(GLOB _left "${_dir}/*")
        if (IS_DIRECTORY "${_dir}" AND NOT _left)
            file(REMOVE_RECURSE "${_dir}")
        endif ()
    endforeach ()
]])
