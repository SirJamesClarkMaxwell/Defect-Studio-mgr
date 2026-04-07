from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .paths import local_config_path


def load_local_config() -> dict[str, Any]:
    path = local_config_path()
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def save_local_config(data: dict[str, Any]) -> Path:
    path = local_config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path
