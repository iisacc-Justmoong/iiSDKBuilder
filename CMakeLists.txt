cmake_minimum_required(VERSION 3.31)
project(iiSDKBuilder VERSION 0.2.1 LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)
# File creation is owned by iiFileProvider.
find_package(iiFileProviderNative 0.6 CONFIG REQUIRED HINTS "$ENV{IIFILEPROVIDER_PREFIX}" "$ENV{HOME}/.local/SDK/iiFileProvider")





include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(GenerateExportHeader)
include(CTest)
option(BUILD_SHARED_LIBS "Build a shared iiSDKBuilder library" OFF)
set(IISDKBUILDER_CORE_SOURCE_DIR "" CACHE PATH "Optional iiSDKCore source checkout (0.18.1 or newer)")

if(IISDKBUILDER_CORE_SOURCE_DIR)
    get_filename_component(IISDKBUILDER_CORE_SOURCE_DIR "${IISDKBUILDER_CORE_SOURCE_DIR}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(_builder_testing "${BUILD_TESTING}")
    set(_builder_shared "${BUILD_SHARED_LIBS}")
    set(BUILD_TESTING OFF)
    set(BUILD_SHARED_LIBS OFF)
    set(IISDKCORE_BUILD_EXAMPLES OFF)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    add_subdirectory("${IISDKBUILDER_CORE_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/core/build")
    set(BUILD_TESTING "${_builder_testing}")
    set(BUILD_SHARED_LIBS "${_builder_shared}")
else()
    find_package(iiSDKCore 0.18.1 CONFIG REQUIRED)
endif()

add_library(iiSDKBuilder src/iiSDKBuilder/Json.cpp src/iiSDKBuilder/Manifest.cpp src/iiSDKBuilder/Plan.cpp src/iiSDKBuilder/Process.cpp src/iiSDKBuilder/Builder.cpp src/iiSDKBuilder/Artifact.cpp)
target_sources(iiSDKBuilder PRIVATE src/iiSDKBuilder/Resources.cpp)
add_library(iiSDKBuilder::iiSDKBuilder ALIAS iiSDKBuilder)
target_compile_features(iiSDKBuilder PUBLIC cxx_std_23)
target_link_libraries(iiSDKBuilder PUBLIC iiSDKCore::iiSDKCore)
target_link_libraries(iiSDKBuilder PRIVATE iiFileProvider::Native)
target_include_directories(iiSDKBuilder PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
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
    src/iiSDKBuilder/Builder.h
    src/iiSDKBuilder/Resources.h
    src/iiSDKBuilder/iiSDKBuilder.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiSDKBuilder)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/iiSDKBuilder/Export.h"
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/iiSDKBuilder)
install(EXPORT iiSDKBuilderTargets NAMESPACE iiSDKBuilder:: DESTINATION "${_package_dir}")
configure_package_config_file(cmake/iiSDKBuilderConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/iiSDKBuilderConfig.cmake" INSTALL_DESTINATION "${_package_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/iiSDKBuilderConfigVersion.cmake"
    VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES cmake/iiSDKBuilderResources.cmake DESTINATION "${_package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiSDKBuilderConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/iiSDKBuilderConfigVersion.cmake"
    cmake/iiSDKBuilderMatrix.cmake DESTINATION "${_package_dir}")
# The installed native CLI needs its SDK shared libraries after relocation.
foreach(dependency IN ITEMS iiSDKCore::iiSDKCore iiFileProvider::Native)
    get_target_property(kind ${dependency} TYPE)
    get_target_property(imported ${dependency} IMPORTED)
    if(imported AND kind STREQUAL "SHARED_LIBRARY")
        install(IMPORTED_RUNTIME_ARTIFACTS ${dependency}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
    endif()
endforeach()
install(FILES README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/iiSDKBuilder)
install(DIRECTORY docs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/iiSDKBuilder/docs)

if(BUILD_TESTING)
    add_executable(iiSDKBuilderResourceTests tests/resources.cpp)
    target_link_libraries(iiSDKBuilderResourceTests PRIVATE iiSDKBuilder::iiSDKBuilder iiFileProvider::Native)
    add_test(NAME iiSDKBuilder.resources COMMAND iiSDKBuilderResourceTests
        "${CMAKE_CURRENT_BINARY_DIR}/tests/resources")
    set(_builder_test_core "${IISDKBUILDER_CORE_SOURCE_DIR}")
    if(NOT _builder_test_core AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../iiSDKCore/CMakeLists.txt")
        get_filename_component(_builder_test_core "${CMAKE_CURRENT_SOURCE_DIR}/../iiSDKCore" ABSOLUTE)
    endif()
    set(_builder_real_core "${_builder_test_core}")
    if(NOT _builder_test_core)
        set(_builder_test_core "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sdk/iiSDKCore")
    endif()
    add_executable(iiSDKBuilderTests tests/contracts.cpp src/iiSDKBuilder/Json.cpp)
    target_include_directories(iiSDKBuilderTests PRIVATE src/iiSDKBuilder)
    target_link_libraries(iiSDKBuilderTests PRIVATE iiSDKBuilder::iiSDKBuilder iiFileProvider::Native)
    add_executable(iiSDKBuilderArtifactTests tests/artifacts.cpp)
    target_link_libraries(iiSDKBuilderArtifactTests PRIVATE iiSDKBuilder::iiSDKBuilder)
    add_test(NAME iiSDKBuilder.native_contract COMMAND iiSDKBuilderArtifactTests
        "${CMAKE_CURRENT_BINARY_DIR}/tests/native-contract")
    set_tests_properties(iiSDKBuilder.native_contract PROPERTIES TIMEOUT 60)
    foreach(_case json manifest graph kits commands failure cancellation file_api process)
        add_test(NAME iiSDKBuilder.${_case} COMMAND iiSDKBuilderTests "${_case}"
            "${CMAKE_CURRENT_SOURCE_DIR}" "${_builder_test_core}"
            "${CMAKE_CURRENT_BINARY_DIR}/tests/${_case}" "${CMAKE_COMMAND}")
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
        add_test(NAME iiSDKBuilder.resource_integration COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/resources_integration.py"
            "$<TARGET_FILE:iiSDKBuilderCLI>" "${CMAKE_CURRENT_SOURCE_DIR}"
            "${_builder_real_core}" "${CMAKE_CURRENT_BINARY_DIR}/resource-integration" "${CMAKE_COMMAND}")
        set_tests_properties(iiSDKBuilder.resource_integration PROPERTIES TIMEOUT 300)
        add_test(NAME iiSDKBuilder.integration COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/integration.py"
            "$<TARGET_FILE:iiSDKBuilderCLI>" "${CMAKE_CURRENT_SOURCE_DIR}"
            "${_builder_real_core}" "${CMAKE_CURRENT_BINARY_DIR}/integration" "${CMAKE_COMMAND}")
        set_tests_properties(iiSDKBuilder.integration PROPERTIES TIMEOUT 300)
    else()
        message(STATUS "Native SDK build integration test requires an iiSDKCore source checkout")
    endif()
    add_test(NAME iiSDKBuilder.package COMMAND "${CMAKE_COMMAND}"
        "-DBUILD=${CMAKE_CURRENT_BINARY_DIR}" "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DCORE_DIR=${iiSDKCore_DIR}" "-DNATIVE_DIR=${iiFileProviderNative_DIR}" "-DTOOL_NAME=$<TARGET_FILE_NAME:iiSDKBuilderCLI>"
        "-DCONFIG=$<CONFIG>" "-DBUILDER_VERSION=${PROJECT_VERSION}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/package.cmake")
    set_tests_properties(iiSDKBuilder.package PROPERTIES TIMEOUT 120)
endif()
