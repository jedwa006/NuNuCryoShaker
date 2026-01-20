#include "fw_version.h"
#include "esp_log.h"
#include <stdio.h>

static const char *TAG = "fw_version";

/* Static buffers for version strings */
static char s_version_string[16];
static char s_version_full[32];
static char s_build_id_string[12];
static bool s_initialized = false;

static void init_strings(void)
{
    if (s_initialized) return;

    snprintf(s_version_string, sizeof(s_version_string),
             "%d.%d.%d", FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH);

    snprintf(s_version_full, sizeof(s_version_full),
             "%d.%d.%d+%08lx",
             FW_VERSION_MAJOR, FW_VERSION_MINOR, FW_VERSION_PATCH,
             (unsigned long)FW_BUILD_ID);

    snprintf(s_build_id_string, sizeof(s_build_id_string),
             "%08lx", (unsigned long)FW_BUILD_ID);

    s_initialized = true;
}

const char* fw_version_string(void)
{
    init_strings();
    return s_version_string;
}

const char* fw_version_full(void)
{
    init_strings();
    return s_version_full;
}

const char* fw_build_id_string(void)
{
    init_strings();
    return s_build_id_string;
}

void fw_version_log(void)
{
    init_strings();
    ESP_LOGI(TAG, "Firmware version: %s (build %s)",
             s_version_string, s_build_id_string);
}
