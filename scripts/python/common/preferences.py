from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from .paths import repo_root


def preferences_path() -> Path:
    return repo_root() / ".local" / "user-preferences.json"


def load_preferences() -> dict[str, Any]:
    path = preferences_path()
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def save_preferences(data: dict[str, Any]) -> Path:
    path = preferences_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return path


def resolve_preference(
    preferences: dict[str, Any], key: str, explicit: Any, fallback: Any
) -> Any:
    if explicit is not None:
        return explicit
    return preferences.get(key, fallback)
