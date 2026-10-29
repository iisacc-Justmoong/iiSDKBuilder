cmake_minimum_required(VERSION 3.31)
project(IndependentCoreFixture LANGUAGES NONE)
add_library(FixtureCore INTERFACE)
set_target_properties(FixtureCore PROPERTIES EXPORT_NAME iiSDKCore)
install(TARGETS FixtureCore EXPORT CoreTargets)
install(EXPORT CoreTargets NAMESPACE iiSDKCore:: DESTINATION lib/cmake/iiSDKCore)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/iiSDKCoreConfig.cmake"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/CoreTargets.cmake\")\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/iiSDKCoreConfig.cmake" DESTINATION lib/cmake/iiSDKCore)
