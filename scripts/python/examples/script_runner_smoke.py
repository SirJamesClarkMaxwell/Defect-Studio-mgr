from __future__ import annotations

import json
import sys


def main() -> int:
    payload = {
        "argv_count": max(len(sys.argv) - 1, 0),
        "argv": sys.argv[1:],
    }
    print(json.dumps(payload))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
