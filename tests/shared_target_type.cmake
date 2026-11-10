if(NOT TARGET_TYPE STREQUAL "SHARED_LIBRARY")
    message(FATAL_ERROR "Expected a shared SDK library, got ${TARGET_TYPE}")
endif()
