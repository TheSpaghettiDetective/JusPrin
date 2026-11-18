# JusPrin: Sentry's native SDK, which files JusPrin's error reports and
# catches crashes (src/slic3r/GUI/JusPrin/Support/SentryClient.cpp).
#
# The release zip, not the tag's archive: it carries crashpad and the other
# submodules. Crashpad writes a crash's minidump from its own process,
# crashpad_handler, which ships beside the app.
#
# The SDK sends nothing itself (SENTRY_TRANSPORT none): the app hands its
# events to OrcaSlicer's own HTTP client, which already knows the system's
# certificates and proxy on every platform. Crashpad uploads a crash from its
# handler with the system's own HTTP on macOS and Windows, and with curl on
# Linux, hence the curl dependency there.
set(_sentry_depends ${ZLIB_PKG})
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND _sentry_depends ${CURL_PKG})
endif ()

# Crashpad names IOKit's default main port kIOMainPortDefault, which exists
# only from macOS 12, so the 10.15 build stops on deps-macos.cmake's
# -Werror=unguarded-availability-new. The patch passes MACH_PORT_NULL, which
# IOKitLib.h documents as the same default.
if (IN_GIT_REPO)
    set(SENTRY_DIRECTORY_FLAG --directory ${BINARY_DIR_REL}/dep_Sentry-prefix/src/dep_Sentry)
endif ()

orcaslicer_add_cmake_project(Sentry
    URL https://github.com/getsentry/sentry-native/releases/download/0.17.5/sentry-native.zip
    URL_HASH SHA256=64d1e29f43116d236076fd751f11305e3b9e31ab14ecbb8d7058246b56f43991
    PATCH_COMMAND git apply ${SENTRY_DIRECTORY_FLAG} --verbose --ignore-space-change --whitespace=fix ${CMAKE_CURRENT_LIST_DIR}/0001-crashpad-default-main-port-before-macos-12.patch
    DEPENDS ${_sentry_depends}
    CMAKE_ARGS
        -DSENTRY_BACKEND:STRING=crashpad
        -DSENTRY_TRANSPORT:STRING=none
        -DSENTRY_BUILD_SHARED_LIBS:BOOL=OFF
        -DSENTRY_BUILD_TESTS:BOOL=OFF
        -DSENTRY_BUILD_EXAMPLES:BOOL=OFF
        -DSENTRY_BUILD_BENCHMARKS:BOOL=OFF
        -DSENTRY_INTEGRATION_QT:BOOL=OFF
)
