# Compile the pinned component with only the checked mount guard changed.
# The registry component and its lock/hash stay untouched.
set(lfs_component "${CMAKE_SOURCE_DIR}/managed_components/joltwallet__littlefs")
set(lfs_original "${lfs_component}/src/littlefs/lfs.c")
set(lfs_patched "${CMAKE_BINARY_DIR}/esp32base-littlefs/lfs.c")
execute_process(
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/patch_littlefs_mount.py"
            "${lfs_original}" "${lfs_patched}"
    RESULT_VARIABLE lfs_patch_result)
if(NOT lfs_patch_result EQUAL 0)
    message(FATAL_ERROR "LittleFS mount guard could not be applied")
endif()
get_target_property(lfs_sources __idf_joltwallet__littlefs SOURCES)
list(FIND lfs_sources "${lfs_original}" lfs_source_index)
if(lfs_source_index EQUAL -1)
    message(FATAL_ERROR "LittleFS source target changed")
endif()
list(REMOVE_ITEM lfs_sources "${lfs_original}")
set_property(TARGET __idf_joltwallet__littlefs PROPERTY SOURCES "${lfs_sources}")
target_sources(__idf_joltwallet__littlefs PRIVATE "${lfs_patched}")
set_source_files_properties("${lfs_patched}" PROPERTIES COMPILE_FLAGS "-DLFS_CONFIG=lfs_config.h")
target_include_directories(__idf_joltwallet__littlefs PRIVATE "${lfs_component}/src/littlefs")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/patch_littlefs_mount.py" "${lfs_original}")
