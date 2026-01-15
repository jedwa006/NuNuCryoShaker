cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.defaults.common"
    "${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.defaults.recovery_factory"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

idf_build_set_property(PARTITION_TABLE_FILENAME
    "${CMAKE_CURRENT_LIST_DIR}/../../partitions/partitions_16mb_recovery_ota.csv"
    REPLACE)
idf_build_set_property(PARTITION_TABLE_OFFSET 0x8000 REPLACE)

project(recovery_factory)
