from __future__ import annotations

import json
import pathlib
import sys


def _append_bridge_build_path() -> None:
    repo_root = pathlib.Path(__file__).resolve().parents[3]
    build_root = repo_root / "build" / "bin"
    if not build_root.exists():
        return

    for candidate in build_root.rglob("ds_python_bridge*.pyd"):
        sys.path.insert(0, str(candidate.parent))
        return
    for candidate in build_root.rglob("ds_python_bridge*.so"):
        sys.path.insert(0, str(candidate.parent))
        return


def main() -> int:
    _append_bridge_build_path()

    import ds_python_bridge

    left = ds_python_bridge.BridgeVector3()
    left.x = 0.0
    left.y = 0.0
    left.z = 0.0

    right = ds_python_bridge.BridgeVector3()
    right.x = 1.0
    right.y = 2.0
    right.z = 2.0

    payload = {
        "add_result": ds_python_bridge.add(2, 5),
        "distance_result": ds_python_bridge.distance(left, right),
        "message": ds_python_bridge.hello_from_cpp(),
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
