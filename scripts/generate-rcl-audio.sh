#!/usr/bin/env bash
# Regenerate RCL's original procedural sound set.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON="${PYTHON:-python3}"

command -v "${PYTHON}" >/dev/null 2>&1 || {
  echo "error: Python 3 is required to generate RCL audio" >&2
  exit 1
}

exec "${PYTHON}" "${ROOT}/scripts/generate-rcl-audio.py"
