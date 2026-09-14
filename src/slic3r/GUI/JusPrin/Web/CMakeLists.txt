option(JUSPRIN_BUILD_WEB "Build the Agent and Home web bundles with npm" ON)
if (NOT JUSPRIN_BUILD_WEB)
    return()   # a prebuilt resources/jusprin/<page>/index.html must then be supplied by hand
endif()

if (WIN32)
    find_program(JUSPRIN_NPM NAMES npm.cmd npm)
else()
    find_program(JUSPRIN_NPM NAMES npm)
endif()
if (NOT JUSPRIN_NPM)
    message(FATAL_ERROR "npm not found. Node.js is required to build the JusPrin Agent and Home pages "
                        "(or configure with -DJUSPRIN_BUILD_WEB=OFF and supply the bundles yourself).")
endif()

set(_jusprin_web_shared_inputs
    ${CMAKE_SOURCE_DIR}/resources/jusprin/ui/design-tokens.json)
file(GLOB_RECURSE _jusprin_web_shared_glob CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../WebShared/*
    ${CMAKE_SOURCE_DIR}/resources/jusprin/ui/icons/*)
list(APPEND _jusprin_web_shared_inputs ${_jusprin_web_shared_glob})

function(jusprin_web_bundle name project_dir output)
    file(GLOB_RECURSE _src CONFIGURE_DEPENDS ${project_dir}/src/*)
    set(_stamp ${CMAKE_CURRENT_BINARY_DIR}/${name}-node_modules.stamp)
    # npm ci writes node_modules/.package-lock.json. Listing it as OUTPUT
    # means deleting node_modules retriggers the install; the stamp in the
    # build tree is what the bundle command depends on.
    set(_modules ${project_dir}/node_modules/.package-lock.json)
    add_custom_command(OUTPUT ${_stamp} ${_modules}
        COMMAND ${JUSPRIN_NPM} ci --no-audit --no-fund
        COMMAND ${CMAKE_COMMAND} -E touch ${_stamp}
        DEPENDS ${project_dir}/package.json ${project_dir}/package-lock.json
        WORKING_DIRECTORY ${project_dir}
        COMMENT "npm ci (${name})"
        VERBATIM)
    add_custom_command(OUTPUT ${output}
        COMMAND ${JUSPRIN_NPM} run build
        DEPENDS ${_stamp} ${_src} ${_jusprin_web_shared_inputs}
                ${project_dir}/index.html ${project_dir}/vite.config.ts ${project_dir}/tsconfig.json
        WORKING_DIRECTORY ${project_dir}
        COMMENT "vite build (${name}) -> ${output}"
        VERBATIM)
endfunction()

jusprin_web_bundle(agent ${CMAKE_CURRENT_SOURCE_DIR}/../AgentUI ${CMAKE_SOURCE_DIR}/resources/jusprin/agent/index.html)
jusprin_web_bundle(home  ${CMAKE_CURRENT_SOURCE_DIR}/../HomeUI  ${CMAKE_SOURCE_DIR}/resources/jusprin/home/index.html)

add_custom_target(jusprin_web ALL DEPENDS
    ${CMAKE_SOURCE_DIR}/resources/jusprin/agent/index.html
    ${CMAKE_SOURCE_DIR}/resources/jusprin/home/index.html)
add_dependencies(OrcaSlicer jusprin_web)
