if(NOT SOURCE_ROOT)
    message(FATAL_ERROR "SOURCE_ROOT is required")
endif()
if(EXISTS "${SOURCE_ROOT}/src/iiSDKBuilder")
    message(FATAL_ERROR "The source tree must not repeat the project name")
endif()
foreach(_file Build/Builder.h Build/Builder.cpp Build/iiSDKBuilder.h
        Build/Artifact.cpp Build/Plan.cpp Build/Manifest.cpp Build/Process.cpp
        Core/Json.h Core/Json.cpp Resources/Resources.h Resources/Resources.cpp
        Targets/Targets.h Targets/Targets.cpp HLS/Hls.h HLS/Hls.cpp
        HLS/HlsIR.h HLS/HlsIR.cpp HLS/HlsRtl.cpp)
    if(NOT EXISTS "${SOURCE_ROOT}/src/${_file}")
        message(FATAL_ERROR "Missing source in its responsibility directory: ${_file}")
    endif()
endforeach()
