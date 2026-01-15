#!/usr/bin/env bash
set -euo pipefail

ESP_IDF_DIR="/Users/joshuaedwards/.espressif/v5.5.2/esp-idf"

if [[ ! -f "${ESP_IDF_DIR}/export.sh" ]]; then
  echo "ERROR: export.sh not found at: ${ESP_IDF_DIR}/export.sh"
  exit 2
fi

# shellcheck disable=SC1090
source "${ESP_IDF_DIR}/export.sh"

echo "ESP-IDF environment activated."
echo "IDF_PATH=${IDF_PATH}"
