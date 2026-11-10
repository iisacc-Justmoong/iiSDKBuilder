set(_stage "${WORK_ROOT}/package/stage")
set(_relocated "${WORK_ROOT}/package/relocated")
file(REMOVE_RECURSE "${_stage}" "${_relocated}" "${WORK_ROOT}/package/consumer/build")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${BUILD}" --config "${CONFIG}" --prefix "${_stage}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Package install failed: ${_output}\n${_error}")
endif()
file(RENAME "${_stage}" "${_relocated}")
foreach(_required IN ITEMS
        "include/iiSDKBuilder/NativeAot.h"
        "lib/cmake/iiSDKBuilder/CompileStaticPlugin.py"
        "share/iiSDKBuilder/python/iisdkbuilder.py")
    if(NOT EXISTS "${_relocated}/${_required}")
        message(FATAL_ERROR "Installed static AOT support is missing ${_required}")
    endif()
endforeach()
find_program(_python_executable NAMES python3 python REQUIRED)
execute_process(COMMAND "${_python_executable}" -B
    "${_relocated}/lib/cmake/iiSDKBuilder/CompileStaticPlugin.py"
    --source "${SOURCE}/tests/fixtures/static-python-app/App/transform.py"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _result EQUAL 0 OR NOT _output MATCHES "ii_sdk_plugin_v1")
    message(FATAL_ERROR "Relocated native Python compiler failed: ${_error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "PYTHONPATH=${_relocated}/share/iiSDKBuilder/python"
    "${_python_executable}" -B -c "from iisdkbuilder import Builder; print(Builder.__name__)"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _result EQUAL 0 OR NOT _output STREQUAL "Builder")
    message(FATAL_ERROR "Relocated Python API failed: ${_error}")
endif()
set(_core_argument)
if(NOT EXISTS "${_relocated}/lib/cmake/iiSDKCore/iiSDKCoreConfig.cmake" AND CORE_DIR)
    # When testing an installed dependency, only Builder belongs to this
    # installation. Core remains a separately installed package.
    list(APPEND _core_argument "-DiiSDKCore_DIR=${CORE_DIR}")
endif()
if(NATIVE_DIR)
    list(APPEND _core_argument "-DiiFileProviderNative_DIR=${NATIVE_DIR}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SOURCE}/tests/consumer" -B "${WORK_ROOT}/package/consumer/build"
    "-DCMAKE_PREFIX_PATH=${_relocated}" -DCMAKE_BUILD_TYPE=Release
    ${_core_argument}
    -DCMAKE_DISABLE_FIND_PACKAGE_Qt6=ON -DCMAKE_DISABLE_FIND_PACKAGE_Qt5=ON
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Relocated consumer configure failed: ${_output}\n${_error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK_ROOT}/package/consumer/build" --config Release
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Relocated consumer build failed: ${_output}\n${_error}")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" --test-dir "${WORK_ROOT}/package/consumer/build" -C Release --output-on-failure
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Relocated consumer execution failed: ${_output}\n${_error}")
endif()
execute_process(COMMAND "${_relocated}/bin/${TOOL_NAME}" --version
    RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT _result EQUAL 0 OR NOT _output STREQUAL "iiSDKBuilder ${BUILDER_VERSION}")
    message(FATAL_ERROR "Installed CLI failed: ${_output}\n${_error}")
endif()
