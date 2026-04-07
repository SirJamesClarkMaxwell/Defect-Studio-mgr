#!/usr/bin/env bash
set -euo pipefail
WRAPPER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="$(cd "$WRAPPER_DIR/.." && pwd)"
REPO_ROOT="$(cd "$SCRIPTS_DIR/.." && pwd)"

PREMAKE_BIN="$REPO_ROOT/Vendor/Binaries/Premake/Linux/premake5"
if [[ -f "$PREMAKE_BIN" ]]; then
	chmod +x "$PREMAKE_BIN"
fi

export PYTHONPATH="$REPO_ROOT${PYTHONPATH:+:$PYTHONPATH}"
python3 "$SCRIPTS_DIR/python/generate_projects.py" "$@"
