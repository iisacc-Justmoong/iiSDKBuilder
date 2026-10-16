set(_stage "${BUILD}/package/stage")
set(_relocated "${BUILD}/package/relocated")
file(REMOVE_RECURSE "${_stage}" "${_relocated}" "${BUILD}/package/consumer/build")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD}" --config "${CONFIG}" --prefix "${_stage}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Package install failed: ${_output}\n${_error}")
endif()
file(RENAME "${_stage}" "${_relocated}")
set(_core_argument)
if(NOT EXISTS "${_relocated}/lib/cmake/iiSDKCore/iiSDKCoreConfig.cmake" AND CORE_DIR)
    # When testing an installed dependency, only Builder belongs to this
    # installation. Core remains a separately installed package.
    list(APPEND _core_argument "-DiiSDKCore_DIR=${CORE_DIR}")
endif()
if(NATIVE_DIR)
    list(APPEND _core_argument "-DiiFileProviderNative_DIR=${NATIVE_DIR}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}/tests/consumer" -B "${BUILD}/package/consumer/build"
    "-DCMAKE_PREFIX_PATH=${_relocated}" -DCMAKE_BUILD_TYPE=Release
    ${_core_argument}
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Relocated consumer configure failed: ${_output}\n${_error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BUILD}/package/consumer/build" --config Release
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Relocated consumer build failed: ${_output}\n${_error}")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${BUILD}/package/consumer/build" -C Release --output-on-failure
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Relocated consumer execution failed: ${_output}\n${_error}")
endif()
execute_process(COMMAND "${_relocated}/bin/${TOOL_NAME}" --version
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _result EQUAL 0 OR NOT _output STREQUAL "iiSDKBuilder ${BUILDER_VERSION}")
    message(FATAL_ERROR "Installed CLI failed: ${_output}\n${_error}")
endif()
