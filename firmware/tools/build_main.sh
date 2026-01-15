#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../apps/main_app"
idf.py build
python3 ../../tools/check_app_size.py build/main_app.bin || true
