# liblzma for the bundled Python's _lzma module on macOS (python3.cmake).
# Without it Python links whatever liblzma the build Mac has, such as
# Homebrew's, which a Mac without Homebrew lacks. Static, library only.
orcaslicer_add_cmake_project(xz
  URL https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.gz
  URL_HASH SHA256=0014c7886930454fe8bd4228665b51af55eeae560ea135c9c4cd33f55b2591d9
  CMAKE_ARGS
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DXZ_NLS=OFF
    -DXZ_DOC=OFF
    -DXZ_TOOL_XZ=OFF
    -DXZ_TOOL_XZDEC=OFF
    -DXZ_TOOL_LZMADEC=OFF
    -DXZ_TOOL_LZMAINFO=OFF
    -DXZ_TOOL_SCRIPTS=OFF
    -DBUILD_TESTING=OFF
)
