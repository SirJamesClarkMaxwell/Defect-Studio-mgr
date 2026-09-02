#!/usr/bin/env python3
"""PostToolUse hook: warn when a project .cpp/.hpp file exceeds the ~500-line cap.

Project rule (docs/work/project/TODO.md, "Zasady pracy"): ".cpp" files max ~500 lines -
split into modules. Non-blocking, best-effort: any parsing/read failure exits quietly so a
schema mismatch or unreadable file can never break a tool call.
"""
import json
import re
import sys

LINE_CAP = 500

def main():
    try:
        data = json.load(sys.stdin)
        if data.get("tool_name") not in ("Write", "Edit"):
            return
        file_path = data.get("tool_input", {}).get("file_path", "")
        if not re.search(r"[\\/](src|tests)[\\/].*\.(cpp|hpp)$", file_path, re.IGNORECASE):
            return
        with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
            line_count = sum(1 for _ in f)
        if line_count > LINE_CAP:
            print(json.dumps({
                "systemMessage": (
                    f"{file_path} is {line_count} lines (project cap ~{LINE_CAP}, "
                    "TODO.md \"Zasady pracy\") - consider splitting into modules."
                )
            }))
    except Exception:
        pass
    finally:
        sys.exit(0)

if __name__ == "__main__":
    main()
