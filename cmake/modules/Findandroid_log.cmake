#.rst:
# Findandroid_log.cmake
# ----------
# Finds the Android log library. Based on a helper function in the Hunter
# package manager.
#
# This will define the following imported target:
#
#   android_log::android_log - The Android log library
#

set(ANDROID_LOG_TARGET_NAME "android_log::android_log")

find_library(ANDROID_LOG_LIBRARY "log")
find_path(ANDROID_LOG_HEADER_DIR "android/log.h")

if (NOT TARGET ${ANDROID_LOG_TARGET_NAME})
  message(STATUS "Creating target '${ANDROID_LOG_TARGET_NAME}':")
  message(STATUS "* library: '${ANDROID_LOG_LIBRARY}'")
  message(STATUS "* header dir: '${ANDROID_LOG_HEADER_DIR}'")

  add_library(${ANDROID_LOG_TARGET_NAME} UNKNOWN IMPORTED)
  set_target_properties(
    ${ANDROID_LOG_TARGET_NAME}
    PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGE "CXX"
    IMPORTED_LOCATION "${ANDROID_LOG_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ANDROID_LOG_HEADER_DIR}"
  )
endif()

mark_as_advanced(ANDROID_LOG_LIBRARY ANDROID_LOG_HEADER_DIR)
