cmake_minimum_required(VERSION 3.31)
project(iiSDKBuilder VERSION 0.4.0 LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
# File creation is owned by iiFileProvider.
find_package(iiFileProviderNative 0.6 CONFIG REQUIRED HINTS "$ENV{IIFILEPROVIDER_PREFIX}" "$ENV{HOME}/.local/SDK/iiFileProvider")





include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)
include(CTest)
option(BUILD_SHARED_LIBS "Build a shared iiSDKBuilder library" ON)
set(IISDKBUILDER_CORE_SOURCE_DIR "" CACHE PATH "Optional iiSDKCore source checkout (0.20.0 or newer)")

if(IISDKBUILDER_CORE_SOURCE_DIR)
    get_filename_component(IISDKBUILDER_CORE_SOURCE_DIR "${IISDKBUILDER_CORE_SOURCE_DIR}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(_builder_testing "${BUILD_TESTING}")
    set(_builder_shared "${BUILD_SHARED_LIBS}")
    set(BUILD_TESTING OFF)
    set(BUILD_SHARED_LIBS "${_builder_shared}")
    set(IISDKCORE_BUILD_EXAMPLES OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_subdirectory("${IISDKBUILDER_CORE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/core/build")
    set(BUILD_TESTING "${_builder_testing}")
    set(BUILD_SHARED_LIBS "${_builder_shared}")
else()
    find_package(iiSDKCore 0.20.0 CONFIG REQUIRED)
endif()

set(_builder_public_headers
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Build/Builder.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Build/NativeAot.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Build/iiSDKBuilder.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources/Resources.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/HLS/Hls.h")
foreach(_header IN LISTS _builder_public_headers)
    get_filename_component(_name "${_header}" NAME)
    file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/iiSDKBuilder/${_name}"
        CONTENT "#pragma once\n#include \"${_header}\"\n")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/iiSDKBuilderPackageRuntime.cmake")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmake/iiSDKBuilderPackageRuntime.cmake" IISDKBUILDER_PACKAGE_SCRIPT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/iiSDKBuilderPackageAndroid.cmake")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmake/iiSDKBuilderPackageAndroid.cmake" IISDKBUILDER_ANDROID_PACKAGE_SCRIPT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/iiSDKBuilderPackageIOS.cmake")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmake/iiSDKBuilderPackageIOS.cmake" IISDKBUILDER_IOS_PACKAGE_SCRIPT)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/iiSDKBuilderPackageWasm.cmake")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/cmake/iiSDKBuilderPackageWasm.cmake" IISDKBUILDER_WASM_PACKAGE_SCRIPT)
configure_file(cmake/PackageScript.h.in "${CMAKE_CURRENT_BINARY_DIR}/generated/PackageScript.h" @ONLY)
add_library(iiSDKBuilder src/Core/Json.cpp src/Build/Manifest.cpp src/Build/Plan.cpp src/Build/Process.cpp src/Build/Builder.cpp src/Build/Artifact.cpp)
target_sources(iiSDKBuilder PRIVATE src/Build/NativeAot.cpp src/Build/CoffArchive.cpp src/Build/WasmDylink.cpp)
target_sources(iiSDKBuilder PRIVATE src/Resources/Resources.cpp src/Targets/Targets.cpp src/HLS/Hls.cpp src/HLS/HlsIR.cpp src/HLS/HlsRtl.cpp)
add_library(iiSDKBuilder::iiSDKBuilder ALIAS iiSDKBuilder)
target_compile_features(iiSDKBuilder PUBLIC cxx_std_23)
target_link_libraries(iiSDKBuilder PUBLIC iiSDKCore::iiSDKCore)
target_link_libraries(iiSDKBuilder PRIVATE iiFileProvider::Native)
target_include_directories(iiSDKBuilder PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(iiSDKBuilder PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Build" "${CMAKE_CURRENT_SOURCE_DIR}/src/Core"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Resources" "${CMAKE_CURRENT_SOURCE_DIR}/src/Targets"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/HLS" "${CMAKE_CURRENT_BINARY_DIR}/generated")
generate_export_header(iiSDKBuilder BASE_NAME IISDKBUILDER
    EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/include/iiSDKBuilder/Export.h")
if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(iiSDKBuilder PUBLIC IISDKBUILDER_STATIC_DEFINE)
endif()
if(MSVC)
    target_compile_options(iiSDKBuilder PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(iiSDKBuilder PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(iiSDKBuilder PRIVATE _GNU_SOURCE)
endif()
set_target_properties(iiSDKBuilder PROPERTIES CXX_EXTENSIONS OFF
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
if(BUILD_SHARED_LIBS)
    if(APPLE)
        set_property(TARGET iiSDKBuilder APPEND PROPERTY INSTALL_RPATH "@loader_path")
    elseif(UNIX)
        set_property(TARGET iiSDKBuilder APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()
    if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
        add_test(NAME iiSDKBuilder.shared_type COMMAND ${CMAKE_COMMAND}
            "-DTARGET_TYPE=$<TARGET_PROPERTY:iiSDKBuilder,TYPE>" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/shared_target_type.cmake")
    endif()
endif()

add_executable(iiSDKBuilderCLI tools/main.cpp)
if(WIN32 AND NOT MSVC)
    target_link_options(iiSDKBuilderCLI PRIVATE -municode)
endif()
target_link_libraries(iiSDKBuilderCLI PRIVATE iiSDKBuilder::iiSDKBuilder)
target_compile_definitions(iiSDKBuilderCLI PRIVATE IISDKBUILDER_VERSION="${PROJECT_VERSION}")
set_target_properties(iiSDKBuilderCLI PROPERTIES OUTPUT_NAME iisdk-builder CXX_EXTENSIONS OFF)
if(APPLE)
    set_target_properties(iiSDKBuilderCLI PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
    set_target_properties(iiSDKBuilderCLI PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

set(_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/iiSDKBuilder")
install(TARGETS iiSDKBuilder iiSDKBuilderCLI EXPORT iiSDKBuilderTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES
    src/Build/Builder.h
    src/Build/NativeAot.h
    src/Resources/Resources.h
    src/HLS/Hls.h
    src/Build/iiSDKBuilder.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiSDKBuilder)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/iiSDKBuilder/Export.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiSDKBuilder)
install(EXPORT iiSDKBuilderTargets NAMESPACE iiSDKBuilder:: DESTINATION "${_package_dir}")
configure_package_config_file(cmake/iiSDKBuilderConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiSDKBuilderConfig.cmake" INSTALL_DESTINATION "${_package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiSDKBuilderConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(DIRECTORY cmake/toolchains/ DESTINATION "${_package_dir}/toolchains"
    PATTERN "*.iisacc-preview" EXCLUDE PATTERN ".iisacc-temporary.lock" EXCLUDE PATTERN ".DS_Store" EXCLUDE)
install(DIRECTORY examples/EmbeddedHello examples/FPGAHello examples/HLSHello DESTINATION ${CMAKE_INSTALL_DATADIR}/iiSDKBuilder/examples
    PATTERN "*.iisacc-preview" EXCLUDE PATTERN ".iisacc-temporary.lock" EXCLUDE PATTERN ".DS_Store" EXCLUDE)
install(FILES cmake/iiSDKBuilderResources.cmake cmake/iiSDKBuilderPackageRuntime.cmake
    cmake/iiSDKBuilderPackageAndroid.cmake cmake/iiSDKBuilderPackageIOS.cmake
    cmake/iiSDKBuilderPackageWasm.cmake DESTINATION "${_package_dir}")
install(FILES cmake/iiSDKBuilderCPython.cmake src/Python/CompileAot.py src/Python/CompileStaticPlugin.py DESTINATION "${_package_dir}")
install(FILES src/Python/iisdkbuilder.py DESTINATION "${CMAKE_INSTALL_DATADIR}/iiSDKBuilder/python")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiSDKBuilderConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiSDKBuilderConfigVersion.cmake"
    cmake/iiSDKBuilderMatrix.cmake DESTINATION "${_package_dir}")
# The installed native CLI needs its SDK shared libraries after relocation.
foreach(dependency IN ITEMS iiSDKCore::iiSDKCore iiSDKCore::Observation iiFileProvider::Native)
    if(NOT TARGET ${dependency})
        continue()
    endif()
    get_target_property(kind ${dependency} TYPE)
    get_target_property(imported ${dependency} IMPORTED)
    if(imported AND kind STREQUAL "SHARED_LIBRARY")
        install(IMPORTED_RUNTIME_ARTIFACTS ${dependency}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
endforeach()
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiSDKBuilder)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iiSDKBuilder/docs
    PATTERN "*.iisacc-preview" EXCLUDE PATTERN ".iisacc-temporary.lock" EXCLUDE PATTERN ".DS_Store" EXCLUDE)

if(BUILD_TESTING)
    option(IISDKBUILDER_TEST_CPYTHON_AOT "Test CPython AOT with an iiXml consumer" OFF)
    if(IISDKBUILDER_TEST_CPYTHON_AOT)
        set(IISDKBUILDER_CPYTHON_EXECUTABLE "" CACHE FILEPATH "CPython for the AOT integration test")
        set(IISDKBUILDER_NUITKA_LAUNCHER "" CACHE FILEPATH "Nuitka 2.8.9 launcher")
        if(NOT TARGET iiSDKCore::CPython)
            find_package(iiSDKCoreCPython CONFIG REQUIRED)
        endif()
        find_package(iiXmlNative CONFIG REQUIRED)
        if(NOT TARGET iiSDKCore::CPython OR NOT EXISTS "${IISDKBUILDER_CPYTHON_EXECUTABLE}"
                OR NOT EXISTS "${IISDKBUILDER_NUITKA_LAUNCHER}" OR NOT TARGET iiXml::MatrixC
                OR NOT EXISTS "${iiXmlNative_MATRIX_PYTHON_SOURCE}")
            message(FATAL_ERROR "CPython AOT integration needs the Core adapter, CPython, Nuitka and installed iiXml package")
        endif()
        include(cmake/iiSDKBuilderCPython.cmake)
        set(_cpython_test_modules "${CMAKE_CURRENT_BINARY_DIR}/cpython-aot")
        iisdk_builder_cpython_aot(TARGET iiSDKBuilderCPythonModules
            PYTHON_EXECUTABLE "${IISDKBUILDER_CPYTHON_EXECUTABLE}"
            NUITKA_LAUNCHER "${IISDKBUILDER_NUITKA_LAUNCHER}"
            OUTPUT_DIRECTORY "${_cpython_test_modules}"
            RUNTIME_TARGETS iiXml::MatrixC
            SOURCES "${iiXmlNative_MATRIX_PYTHON_SOURCE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/resources/cpython-aot/iixml_aot_probe.py")
        add_executable(iiSDKBuilderCPythonIntegration tests/cpython_integration.cpp)
        target_link_libraries(iiSDKBuilderCPythonIntegration PRIVATE iiSDKCore::CPython)
        add_dependencies(iiSDKBuilderCPythonIntegration iiSDKBuilderCPythonModules)
        execute_process(COMMAND "${IISDKBUILDER_CPYTHON_EXECUTABLE}" -c "import sys; print(sys.base_prefix)"
            OUTPUT_VARIABLE _cpython_test_home OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)
        add_test(NAME iiSDKBuilder.CPython.iiXml COMMAND "${CMAKE_COMMAND}" -E env --unset=IIXML_MATRIX_LIBRARY
            "$<TARGET_FILE:iiSDKBuilderCPythonIntegration>" "${_cpython_test_modules}")
        set_tests_properties(iiSDKBuilder.CPython.iiXml PROPERTIES
            ENVIRONMENT "PYTHONHOME=${_cpython_test_home};PYTHONDONTWRITEBYTECODE=1")
        add_test(NAME iiSDKBuilder.CPython.BackendFailures COMMAND "${IISDKBUILDER_CPYTHON_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/cpython_aot_backend.py")
    endif()
    find_package(Python3 3.9 COMPONENTS Interpreter REQUIRED)
    set(_test_workspace "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/tests/workspace_runner.py"
        "${iiFileProviderNative_PYTHON_DIRECTORY}" "${CMAKE_CURRENT_SOURCE_DIR}/resources")
    add_test(NAME iiSDKBuilder.workspace_runner COMMAND "${CMAKE_COMMAND}" -E env
        "PYTHONPATH=${iiFileProviderNative_PYTHON_DIRECTORY}" "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_workspace_runner.py")
    if(UNIX)
        add_test(NAME iiSDKBuilder.timeout_cleanup COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_builder_subprocess.py" "@WORK@")
        set_tests_properties(iiSDKBuilder.timeout_cleanup PROPERTIES TIMEOUT 60)
    endif()
    add_test(NAME iiSDKBuilder.source_layout COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/source_layout.cmake")
    include(cmake/HlsClang.cmake)
    iisdk_builder_find_hls_clang()
    add_test(NAME iiSDKBuilder.hls_clang_discovery COMMAND ${_test_workspace} "${CMAKE_COMMAND}"
        "-DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR}" "-DTEST_ROOT=@WORK@/hls-clang"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/hls/clang_discovery.cmake")
    add_executable(iiSDKBuilderHlsContracts tests/hls/contracts.cpp)
    target_link_libraries(iiSDKBuilderHlsContracts PRIVATE iiSDKBuilder::iiSDKBuilder)
    add_test(NAME iiSDKBuilder.hls_contracts COMMAND iiSDKBuilderHlsContracts)
    add_executable(iiSDKBuilderHlsTests tests/hls/tests.cpp src/Core/Json.cpp)
    target_link_libraries(iiSDKBuilderHlsTests PRIVATE iiSDKBuilder::iiSDKBuilder iiFileProvider::Native)
    target_compile_definitions(iiSDKBuilderHlsTests PRIVATE IISDK_HLS_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}" IISDK_HLS_CLANG="${IISDK_HLS_CLANG}" IISDK_HLS_CMAKE="${CMAKE_COMMAND}")
    foreach(case synthesis reject manifest pipeline)
        add_test(NAME iiSDKBuilder.hls_${case} COMMAND ${_test_workspace} "$<TARGET_FILE:iiSDKBuilderHlsTests>" ${case} "@WORK@/hls-${case}")
        set_tests_properties(iiSDKBuilder.hls_${case} PROPERTIES TIMEOUT 60)
    endforeach()
    find_program(IISDK_HLS_VERILATOR NAMES verilator)
    find_program(IISDK_HLS_YOSYS NAMES yosys)
    if(IISDK_HLS_VERILATOR AND IISDK_HLS_YOSYS)
        find_package(Python3 3.9 COMPONENTS Interpreter REQUIRED)
        add_test(NAME iiSDKBuilder.hls_equivalence COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/hls/verify.py"
            --builder "$<TARGET_FILE:iiSDKBuilderCLI>" --source "${CMAKE_CURRENT_SOURCE_DIR}"
            --output "@WORK@/hls-equivalence"
            --clang "${IISDK_HLS_CLANG}" --verilator "${IISDK_HLS_VERILATOR}" --yosys "${IISDK_HLS_YOSYS}")
        set_tests_properties(iiSDKBuilder.hls_equivalence PROPERTIES TIMEOUT 300 LABELS "hls-execution")
    endif()
    add_executable(iiSDKBuilderEmbeddedTests tests/embedded_targets.cpp)
    target_link_libraries(iiSDKBuilderEmbeddedTests PRIVATE iiSDKBuilder::iiSDKBuilder iiFileProvider::Native)
    target_compile_definitions(iiSDKBuilderEmbeddedTests PRIVATE IISDK_TEST_SDK="${CMAKE_CURRENT_SOURCE_DIR}/.." IISDK_TEST_CMAKE="${CMAKE_COMMAND}")
    foreach(case profiles plans binary manifest deployment arduino)
        add_test(NAME iiSDKBuilder.embedded_${case} COMMAND ${_test_workspace} "$<TARGET_FILE:iiSDKBuilderEmbeddedTests>" ${case} "@WORK@/embedded-${case}")
        set_tests_properties(iiSDKBuilder.embedded_${case} PROPERTIES TIMEOUT 60)
    endforeach()
    add_executable(iiSDKBuilderResourceTests tests/resources.cpp)
    target_link_libraries(iiSDKBuilderResourceTests PRIVATE iiSDKBuilder::iiSDKBuilder iiFileProvider::Native)
    add_test(NAME iiSDKBuilder.resources COMMAND ${_test_workspace} "$<TARGET_FILE:iiSDKBuilderResourceTests>"
        "@WORK@/resources")
    set(_builder_test_core "${IISDKBUILDER_CORE_SOURCE_DIR}")
    if(NOT _builder_test_core AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../iiSDKCore/CMakeLists.txt")
        get_filename_component(_builder_test_core "${CMAKE_CURRENT_SOURCE_DIR}/../iiSDKCore" ABSOLUTE)
    endif()
    set(_builder_real_core "${_builder_test_core}")
    if(NOT _builder_test_core)
        set(_builder_test_core "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sdk/iiSDKCore")
    endif()
    add_executable(iiSDKBuilderTests tests/contracts.cpp src/Core/Json.cpp)
    target_include_directories(iiSDKBuilderTests PRIVATE src/Core)
    target_link_libraries(iiSDKBuilderTests PRIVATE iiSDKBuilder::iiSDKBuilder iiFileProvider::Native)
    add_executable(iiSDKBuilderNativeDiscovery tests/native_discovery.cpp)
    target_link_libraries(iiSDKBuilderNativeDiscovery PRIVATE iiSDKBuilder::iiSDKBuilder)
    add_test(NAME iiSDKBuilder.native_discovery COMMAND ${_test_workspace} "$<TARGET_FILE:iiSDKBuilderNativeDiscovery>" "@WORK@/native-discovery")
    set_tests_properties(iiSDKBuilder.native_discovery PROPERTIES TIMEOUT 30)
    add_test(NAME iiSDKBuilder.native_python_compiler COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/native_python_compiler.py" "${CMAKE_CURRENT_SOURCE_DIR}" "@WORK@/native-python")
    set_tests_properties(iiSDKBuilder.native_python_compiler PROPERTIES TIMEOUT 30)
    add_executable(iiSDKBuilderIndependentProjects tests/independent_projects.cpp)
    target_link_libraries(iiSDKBuilderIndependentProjects PRIVATE iiSDKBuilder::iiSDKBuilder)
    add_test(NAME iiSDKBuilder.independent_projects COMMAND ${_test_workspace} "$<TARGET_FILE:iiSDKBuilderIndependentProjects>"
        "${CMAKE_CURRENT_SOURCE_DIR}" "${_builder_test_core}"
        "@WORK@/independent-projects/build" "${CMAKE_COMMAND}")
    set_tests_properties(iiSDKBuilder.independent_projects PROPERTIES TIMEOUT 60)
    add_test(NAME iiSDKBuilder.dynamic_package COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/dynamic_package.py" "$<TARGET_FILE:iiSDKBuilderCLI>"
        "@WORK@/dynamic-package" "${CMAKE_COMMAND}")
    set_tests_properties(iiSDKBuilder.dynamic_package PROPERTIES TIMEOUT 240)
    add_test(NAME iiSDKBuilder.installed_dynamic_package COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/installed_dynamic_package.py" "${CMAKE_COMMAND}"
        "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}" "@WORK@/installed-dynamic"
        "${Python3_EXECUTABLE}" "$<TARGET_FILE_NAME:iiSDKBuilderCLI>")
    set_tests_properties(iiSDKBuilder.installed_dynamic_package PROPERTIES TIMEOUT 240)
    set(IISDKBUILDER_TEST_ANDROID_SDK "" CACHE PATH "Android SDK for package structure tests")
    set(IISDKBUILDER_TEST_ANDROID_NDK "" CACHE PATH "Android NDK for package structure tests")
    if(IISDKBUILDER_TEST_ANDROID_SDK AND IISDKBUILDER_TEST_ANDROID_NDK)
        add_test(NAME iiSDKBuilder.android_package COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/android_package.py" "${CMAKE_COMMAND}"
            "@WORK@/android-package" "${IISDKBUILDER_TEST_ANDROID_SDK}" "${IISDKBUILDER_TEST_ANDROID_NDK}"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/iiSDKBuilderPackageAndroid.cmake")
        set_tests_properties(iiSDKBuilder.android_package PROPERTIES TIMEOUT 180)
        add_test(NAME iiSDKBuilder.android_builder COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/android_builder.py" "$<TARGET_FILE:iiSDKBuilderCLI>"
            "@WORK@/android-builder" "${IISDKBUILDER_TEST_ANDROID_SDK}" "${IISDKBUILDER_TEST_ANDROID_NDK}" "${CMAKE_COMMAND}")
        set_tests_properties(iiSDKBuilder.android_builder PROPERTIES TIMEOUT 240)
    endif()
    if(APPLE)
        add_test(NAME iiSDKBuilder.ios_package COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/ios_package.py" "${CMAKE_COMMAND}"
            "@WORK@/ios-package" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/iiSDKBuilderPackageIOS.cmake")
        set_tests_properties(iiSDKBuilder.ios_package PROPERTIES TIMEOUT 180)
        add_test(NAME iiSDKBuilder.ios_builder COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/ios_builder.py" "$<TARGET_FILE:iiSDKBuilderCLI>"
            "@WORK@/ios-builder" "${CMAKE_COMMAND}")
        set_tests_properties(iiSDKBuilder.ios_builder PROPERTIES TIMEOUT 240)
    endif()
    add_executable(iiSDKBuilderIndependentBuild tests/independent_build.cpp)
    target_link_libraries(iiSDKBuilderIndependentBuild PRIVATE iiSDKBuilder::iiSDKBuilder)
    add_test(NAME iiSDKBuilder.independent_build COMMAND ${_test_workspace} "$<TARGET_FILE:iiSDKBuilderIndependentBuild>"
        "${CMAKE_CURRENT_SOURCE_DIR}/resources/independent-projects"
        "@WORK@/independent-build/build" "${CMAKE_COMMAND}")
    set_tests_properties(iiSDKBuilder.independent_build PROPERTIES TIMEOUT 180
        ENVIRONMENT "CPATH=${CMAKE_CURRENT_SOURCE_DIR}/resources/independent-projects/sdk/iiSDKCore;CPLUS_INCLUDE_PATH=${CMAKE_CURRENT_SOURCE_DIR}/resources/independent-projects/sdk/iiSDKCore;CMAKE_INCLUDE_PATH=${CMAKE_CURRENT_SOURCE_DIR}/resources/independent-projects/sdk/iiSDKCore")
    # These parsers are private implementation details, not part of the DLL ABI.
    add_executable(iiSDKBuilderCoffTests tests/coff_archive.cpp src/Build/CoffArchive.cpp)
    target_include_directories(iiSDKBuilderCoffTests PRIVATE src/Build)
    target_link_libraries(iiSDKBuilderCoffTests PRIVATE iiSDKBuilder::iiSDKBuilder)
    add_test(NAME iiSDKBuilder.coff_archive COMMAND ${_test_workspace} "$<TARGET_FILE:iiSDKBuilderCoffTests>" "@WORK@/coff")
    set_tests_properties(iiSDKBuilder.coff_archive PROPERTIES TIMEOUT 30)
    find_program(IISDKBUILDER_COFF_CLANG NAMES clang)
    find_program(IISDKBUILDER_COFF_LLD NAMES lld HINTS "$ENV{EMSDK}/upstream/bin" /opt/homebrew/share/emsdk/upstream/bin)
    find_program(IISDKBUILDER_COFF_AR NAMES llvm-ar HINTS "$ENV{EMSDK}/upstream/bin" /opt/homebrew/share/emsdk/upstream/bin)
    if(IISDKBUILDER_COFF_CLANG AND IISDKBUILDER_COFF_LLD AND IISDKBUILDER_COFF_AR)
        add_test(NAME iiSDKBuilder.coff_toolchain COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/coff_toolchain.py" "$<TARGET_FILE:iiSDKBuilderCoffTests>"
            "@WORK@/coff-toolchain" "${IISDKBUILDER_COFF_CLANG}" "${IISDKBUILDER_COFF_LLD}" "${IISDKBUILDER_COFF_AR}")
        set_tests_properties(iiSDKBuilder.coff_toolchain PROPERTIES TIMEOUT 90)
    endif()
    set(IISDKBUILDER_TEST_EMSCRIPTEN_TOOLCHAIN "" CACHE FILEPATH "Emscripten toolchain for package tests")
    if(EXISTS "${IISDKBUILDER_TEST_EMSCRIPTEN_TOOLCHAIN}")
        add_test(NAME iiSDKBuilder.wasm_builder COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/wasm_builder.py" "$<TARGET_FILE:iiSDKBuilderCLI>"
            "@WORK@/wasm-builder" "${CMAKE_COMMAND}" "${IISDKBUILDER_TEST_EMSCRIPTEN_TOOLCHAIN}")
        set_tests_properties(iiSDKBuilder.wasm_builder PROPERTIES TIMEOUT 240)
    endif()
    add_executable(iiSDKBuilderWasmLinkTests tests/wasm_link.cpp src/Build/WasmDylink.cpp)
    target_include_directories(iiSDKBuilderWasmLinkTests PRIVATE src/Build)
    target_link_libraries(iiSDKBuilderWasmLinkTests PRIVATE iiSDKBuilder::iiSDKBuilder)
    add_test(NAME iiSDKBuilder.wasm_link COMMAND ${_test_workspace} "$<TARGET_FILE:iiSDKBuilderWasmLinkTests>" "@WORK@/wasm-link")
    set_tests_properties(iiSDKBuilder.wasm_link PROPERTIES TIMEOUT 30)
    add_executable(iiSDKBuilderArtifactTests tests/artifacts.cpp)
    target_link_libraries(iiSDKBuilderArtifactTests PRIVATE iiSDKBuilder::iiSDKBuilder)
    add_test(NAME iiSDKBuilder.native_contract COMMAND ${_test_workspace} "$<TARGET_FILE:iiSDKBuilderArtifactTests>"
        "@WORK@/native-contract")
    set_tests_properties(iiSDKBuilder.native_contract PROPERTIES TIMEOUT 60)
    foreach(_case json manifest graph kits commands failure cancellation file_api mobile_packaging windows_library_policy process)
        add_test(NAME iiSDKBuilder.${_case} COMMAND ${_test_workspace} "$<TARGET_FILE:iiSDKBuilderTests>" "${_case}"
            "${CMAKE_CURRENT_SOURCE_DIR}" "${_builder_test_core}"
            "@WORK@/${_case}" "${CMAKE_COMMAND}")
        set_tests_properties(iiSDKBuilder.${_case} PROPERTIES TIMEOUT 60)
    endforeach()
    if(_builder_real_core)
        find_package(Python3 3.9 COMPONENTS Interpreter REQUIRED)
        add_test(NAME iiSDKBuilder.native_kit COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_kit.py")
        add_test(NAME iiSDKBuilder.native_kit_freeze COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_native_kit_freeze.py")
        if(IISDKBUILDER_CORE_SOURCE_DIR)
            add_test(NAME iiSDKBuilder.core_foundation COMMAND "${Python3_EXECUTABLE}" -B
                "${IISDKBUILDER_CORE_SOURCE_DIR}/tests/core_boundary.py"
                --source "${IISDKBUILDER_CORE_SOURCE_DIR}"
                --build "${CMAKE_CURRENT_BINARY_DIR}/core/build")
        endif()
        add_test(NAME iiSDKBuilder.resource_integration COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/resources_integration.py"
            "$<TARGET_FILE:iiSDKBuilderCLI>" "${CMAKE_CURRENT_SOURCE_DIR}"
            "${_builder_real_core}" "@WORK@/resource-integration" "${CMAKE_COMMAND}")
        set_tests_properties(iiSDKBuilder.resource_integration PROPERTIES TIMEOUT 300)
        add_test(NAME iiSDKBuilder.integration COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py"
            "$<TARGET_FILE:iiSDKBuilderCLI>" "${CMAKE_CURRENT_SOURCE_DIR}"
            "${_builder_real_core}" "@WORK@/integration" "${CMAKE_COMMAND}")
        set_tests_properties(iiSDKBuilder.integration PROPERTIES TIMEOUT 300)
        add_test(NAME iiSDKBuilder.static_aot_integration COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/static_aot_integration.py"
            "$<TARGET_FILE:iiSDKBuilderCLI>" "${CMAKE_CURRENT_SOURCE_DIR}"
            "${_builder_real_core}" "@WORK@/static-aot" "${CMAKE_COMMAND}")
        set_tests_properties(iiSDKBuilder.static_aot_integration PROPERTIES TIMEOUT 420)
        add_test(NAME iiSDKBuilder.static_python_integration COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/static_python_integration.py"
            "$<TARGET_FILE:iiSDKBuilderCLI>" "${CMAKE_CURRENT_SOURCE_DIR}"
            "${_builder_real_core}" "@WORK@/static-python" "${CMAKE_COMMAND}")
        set_tests_properties(iiSDKBuilder.static_python_integration PROPERTIES TIMEOUT 420)
        add_test(NAME iiSDKBuilder.python_client COMMAND ${_test_workspace} "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/builder_python_api.py"
            "$<TARGET_FILE:iiSDKBuilderCLI>" "${CMAKE_CURRENT_SOURCE_DIR}"
            "${_builder_real_core}" "@WORK@/python-client" "${CMAKE_COMMAND}")
        set_tests_properties(iiSDKBuilder.python_client PROPERTIES TIMEOUT 30)
    else()
        message(STATUS "Native SDK build integration test requires an iiSDKCore source checkout")
    endif()
    add_test(NAME iiSDKBuilder.package COMMAND ${_test_workspace} "${CMAKE_COMMAND}"
        "-DWORK_ROOT=@WORK@" "-DBUILD=${CMAKE_CURRENT_BINARY_DIR}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DCORE_DIR=${iiSDKCore_DIR}" "-DNATIVE_DIR=${iiFileProviderNative_DIR}" "-DTOOL_NAME=$<TARGET_FILE_NAME:iiSDKBuilderCLI>"
        "-DCONFIG=$<CONFIG>" "-DBUILDER_VERSION=${PROJECT_VERSION}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/package.cmake")
    set_tests_properties(iiSDKBuilder.package PROPERTIES TIMEOUT 120)
endif()
