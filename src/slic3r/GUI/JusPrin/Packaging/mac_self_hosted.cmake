# CMake toolchain file for the deps and the app on the self-hosted Mac
# (jusprin_release_build.yml sets CMAKE_TOOLCHAIN_FILE to it there).
#
# That Mac can have Homebrew, MacPorts or XQuartz installed. CMake would take
# their libraries, and XQuartz's OpenGL ahead of macOS's own, and the app
# would then need them on every Mac. build_release_macos.sh's ignore list
# joins these folders with colons, which CMake reads as one folder that does
# not exist. deps/CMakeLists.txt hands the toolchain file on to every dep.
list(APPEND CMAKE_SYSTEM_IGNORE_PREFIX_PATH /opt/local /usr/local /opt/homebrew /usr/X11R6 /usr/X11 /opt/X11)
