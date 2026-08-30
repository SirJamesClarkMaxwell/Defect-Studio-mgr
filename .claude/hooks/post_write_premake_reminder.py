#!/usr/bin/env python3
"""PostToolUse hook: remind to regenerate premake projects after a new C++ source file.

Learned the hard way this session (2026-08-28): a freshly Write()-created .cpp/.hpp under
src/ or tests/ silently doesn't compile until scripts/Windows/GenerateProjects.bat runs -
premake globs files at project-generation time, not build time. Non-blocking, best-effort:
any parsing failure just exits quietly so a schema mismatch can never break a tool call.
"""
import json
import re
import sys

def main():
    try:
        data = json.load(sys.stdin)
        if data.get("tool_name") != "Write":
            return
        file_path = data.get("tool_input", {}).get("file_path", "")
        if re.search(r"[\\/](src|tests)[\\/].*\.(cpp|hpp)$", file_path, re.IGNORECASE):
            print(json.dumps({
                "systemMessage": (
                    "New/overwritten C++ source under src/ or tests/ - if this is a NEW file, "
                    "run scripts/Windows/GenerateProjects.bat before building (premake globs "
                    "sources at project-generation time, not build time)."
                )
            }))
    except Exception:
        pass
    finally:
        sys.exit(0)

if __name__ == "__main__":
    main()
