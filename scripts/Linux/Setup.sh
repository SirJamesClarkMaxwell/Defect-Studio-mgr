#!/usr/bin/env bash
set -euo pipefail
WRAPPER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="$(cd "$WRAPPER_DIR/.." && pwd)"
export PYTHONPATH="$SCRIPTS_DIR${PYTHONPATH:+:$PYTHONPATH}"
python3 "$SCRIPTS_DIR/python/setup.py" "$@"
