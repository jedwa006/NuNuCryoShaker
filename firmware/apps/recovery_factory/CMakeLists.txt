cmake_minimum_required(VERSION 3.16)

# Shared components directory - but we exclude components that require
# BLE stack and other dependencies not enabled in recovery build
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../components")

# Exclude components that have heavy dependencies (BLE, etc.)
# Recovery app is a minimal OTA portal and doesn't need these
set(EXCLUDE_COMPONENTS
    "ble_gatt"      # Requires NimBLE
    "session_mgr"   # Depends on BLE
    "wire_protocol" # Used by main app BLE
    "telemetry"     # Used by main app
    "relay_ctrl"    # Hardware control for main app
    "status_led"    # LED indicator (could add later if desired)
    "bootctl"       # Used by main app
    "version"       # Versioning for main app only
    "modbus_master" # RS-485/Modbus for main app
    "pid_controller" # PID controller integration for main app
)

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
