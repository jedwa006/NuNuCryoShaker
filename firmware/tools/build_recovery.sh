#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../apps/recovery_factory"
idf.py build
