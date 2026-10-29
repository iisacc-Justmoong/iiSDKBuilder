cmake_minimum_required(VERSION 3.31)
project(Addon LANGUAGES CXX)
add_library(Addon INTERFACE)
install(TARGETS Addon EXPORT AddonTargets)
install(EXPORT AddonTargets NAMESPACE Addon:: DESTINATION lib/cmake/Addon)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/AddonConfig.cmake"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/AddonTargets.cmake\")\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/AddonConfig.cmake" DESTINATION lib/cmake/Addon)
