#pragma once

/**
 * @file fw_version.h
 * @brief Centralized firmware version definition
 *
 * This header is the single source of truth for firmware versioning.
 * Update these values when releasing new firmware versions.
 *
 * Versioning scheme: MAJOR.MINOR.PATCH
 * - MAJOR: Breaking changes to BLE protocol or hardware compatibility
 * - MINOR: New features, backwards-compatible changes
 * - PATCH: Bug fixes, minor improvements
 *
 * BUILD_ID: Auto-incremented or set by CI, used for tracking specific builds
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * VERSION NUMBERS - Update these when releasing new firmware
 * ============================================================================ */

#define FW_VERSION_MAJOR        0
#define FW_VERSION_MINOR        3
#define FW_VERSION_PATCH        10

/**
 * Build ID - can be set by CI or manually incremented
 * Format: 0xYYMMDDNN where NN is build number for that day
 * Example: 0x26012001 = 2026-01-20, build 1
 */
#define FW_BUILD_ID             0x26012011

/* ============================================================================
 * DERIVED VERSION STRINGS AND MACROS
 * ============================================================================ */

/* String helpers */
#define FW_STRINGIFY(x)         #x
#define FW_TOSTRING(x)          FW_STRINGIFY(x)

/* Version as string: "0.2.0" */
#define FW_VERSION_STRING       FW_TOSTRING(FW_VERSION_MAJOR) "." \
                                FW_TOSTRING(FW_VERSION_MINOR) "." \
                                FW_TOSTRING(FW_VERSION_PATCH)

/* Full version with build: "0.2.0+26011901" */
#define FW_VERSION_FULL         FW_VERSION_STRING "+" FW_TOSTRING(FW_BUILD_ID)

/* Packed version for comparison: 0x00MMNNPP */
#define FW_VERSION_PACKED       ((FW_VERSION_MAJOR << 16) | \
                                 (FW_VERSION_MINOR << 8) | \
                                 FW_VERSION_PATCH)

/* ============================================================================
 * RUNTIME FUNCTIONS
 * ============================================================================ */

/**
 * @brief Get firmware version string
 * @return Pointer to static string "MAJOR.MINOR.PATCH"
 */
const char* fw_version_string(void);

/**
 * @brief Get full firmware version with build ID
 * @return Pointer to static string "MAJOR.MINOR.PATCH+BUILD_ID"
 */
const char* fw_version_full(void);

/**
 * @brief Get build ID as hex string
 * @return Pointer to static string "XXXXXXXX"
 */
const char* fw_build_id_string(void);

/**
 * @brief Log firmware version info at startup
 */
void fw_version_log(void);

#ifdef __cplusplus
}
#endif
