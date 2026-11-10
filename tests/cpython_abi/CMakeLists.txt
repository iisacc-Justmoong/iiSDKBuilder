cmake_minimum_required(VERSION 3.20)
project(CPythonAbiMismatch NONE)

add_library(iiSDKCore::CPython INTERFACE IMPORTED)
set_property(TARGET iiSDKCore::CPython PROPERTY IISDK_CPYTHON_EXTENSION_SUFFIX ".incompatible-abi.so")
include("${IISDK_HELPER}")
iisdk_builder_cpython_aot(TARGET rejected_modules
    PYTHON_EXECUTABLE "${IISDK_PYTHON}"
    NUITKA_LAUNCHER "${IISDK_LAUNCHER}"
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules"
    SOURCES "${IISDK_SOURCE}")
