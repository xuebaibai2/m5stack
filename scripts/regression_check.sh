#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cd "$ROOT_DIR"
pio run
python3 scripts/regression_check.py

cd "$ROOT_DIR/mac/StickLinkMenuBar"
swift run StickLinkValidation
