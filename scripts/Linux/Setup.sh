#!/usr/bin/env bash
set -euo pipefail

WRAPPER_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR="$(cd "$WRAPPER_DIR/.." && pwd)"

PYTHON_VERSION="${DEFECT_STUDIO_PYTHON_VERSION:-3.12}"

echo
echo "[setup] Bootstrap start (Linux)"
echo "[setup] Required Python version: ${PYTHON_VERSION}"

if ! command -v uv >/dev/null 2>&1; then
	echo "[setup] uv not found. Installing via official installer..."
	curl -LsSf https://astral.sh/uv/install.sh | sh
	export PATH="$HOME/.local/bin:$HOME/.cargo/bin:$PATH"
fi

if ! command -v uv >/dev/null 2>&1; then
	echo "[error] uv is still not available in PATH after installation." >&2
	exit 1
fi

MANAGED_PYTHON="$(uv python find "$PYTHON_VERSION" 2>/dev/null || true)"
if [[ -z "$MANAGED_PYTHON" ]]; then
	echo "[setup] Installing Python ${PYTHON_VERSION} via uv..."
	uv python install "$PYTHON_VERSION" --default
	MANAGED_PYTHON="$(uv python find "$PYTHON_VERSION")"
fi

if [[ -z "$MANAGED_PYTHON" ]]; then
	echo "[error] Unable to resolve managed Python executable for ${PYTHON_VERSION}." >&2
	exit 1
fi

if [[ ! -x "$MANAGED_PYTHON" ]]; then
	echo "[error] Managed Python path is not executable: $MANAGED_PYTHON" >&2
	exit 1
fi

"$MANAGED_PYTHON" - <<'PY'
import pathlib
import sys
import sysconfig

include_dir = pathlib.Path(sysconfig.get_path("include") or "")
plat_include_dir = pathlib.Path(sysconfig.get_path("platinclude") or "")
has_header = (include_dir / "Python.h").exists() or (plat_include_dir / "Python.h").exists()
if not has_header:
		raise SystemExit("Python.h not found in include directories")

if sys.platform.startswith("win"):
		libs_dir = pathlib.Path(sys.base_prefix) / "libs"
		lib_name = f"python{sys.version_info[0]}{sys.version_info[1]}.lib"
		lib_path = libs_dir / lib_name
		if not lib_path.exists():
				raise SystemExit(f"Missing Python import library: {lib_path}")
else:
		lib_dir = pathlib.Path(sysconfig.get_config_var("LIBDIR") or "")
		lib_name = sysconfig.get_config_var("LDLIBRARY")
		if lib_name:
				lib_path = lib_dir / lib_name
				if not lib_path.exists():
						raise SystemExit(f"Missing Python library: {lib_path}")

print("Python native build prerequisites: OK")
PY

export PYTHONPATH="$SCRIPTS_DIR${PYTHONPATH:+:$PYTHONPATH}"
"$MANAGED_PYTHON" "$SCRIPTS_DIR/python/setup.py" --python-version "$PYTHON_VERSION" "$@"
