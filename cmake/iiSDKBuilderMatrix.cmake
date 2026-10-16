include_guard(GLOBAL)

# This helper invokes a desktop host tool. It does not link iiSDKBuilder into
# the application's target binaries and is safe to include in cross projects.
function(iisdkbuilder_add_matrix)
    cmake_parse_arguments(PARSE_ARGV 0 MATRIX "" "TARGET;MANIFEST;BUILDER" "PLATFORMS")
    if(MATRIX_UNPARSED_ARGUMENTS OR MATRIX_KEYWORDS_MISSING_VALUES OR NOT MATRIX_TARGET OR NOT MATRIX_MANIFEST)
        message(FATAL_ERROR "iisdkbuilder_add_matrix requires TARGET and MANIFEST; optional BUILDER and PLATFORMS")
    endif()
    get_filename_component(_manifest "${MATRIX_MANIFEST}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    if(NOT EXISTS "${_manifest}")
        message(FATAL_ERROR "iiSDKBuilder manifest does not exist: ${_manifest}")
    endif()
    if(MATRIX_BUILDER)
        set(_builder "${MATRIX_BUILDER}")
    elseif(TARGET iiSDKBuilderCLI)
        set(_builder "$<TARGET_FILE:iiSDKBuilderCLI>")
    elseif(TARGET iiSDKBuilder::iiSDKBuilderCLI)
        set(_builder "$<TARGET_FILE:iiSDKBuilder::iiSDKBuilderCLI>")
    else()
        find_program(_builder NAMES iisdk-builder NO_CMAKE_FIND_ROOT_PATH NO_CACHE REQUIRED)
    endif()
    set(_arguments build --manifest "${_manifest}")
    if(MATRIX_PLATFORMS)
        list(JOIN MATRIX_PLATFORMS "," _platforms)
        list(APPEND _arguments --platform "${_platforms}")
    endif()
    add_custom_target("${MATRIX_TARGET}"
        COMMAND "${_builder}" ${_arguments}
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        COMMENT "Building iiSDK and application target matrix"
        USES_TERMINAL VERBATIM)
endfunction()
