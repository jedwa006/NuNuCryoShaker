cmake_minimum_required(VERSION 3.16)

set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.defaults.common"
    "${CMAKE_CURRENT_LIST_DIR}/../../sdkconfig.defaults.recovery_factory"
)

set(PARTITION_TABLE_FILENAME
    "${CMAKE_CURRENT_LIST_DIR}/../../partitions/partitions_16mb_recovery_ota.csv"
)
set(PARTITION_TABLE_OFFSET 0x8000)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(recovery)
