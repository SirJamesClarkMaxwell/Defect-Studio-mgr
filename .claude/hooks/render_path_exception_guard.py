#!/usr/bin/env python3
"""PostToolUse hook: warn when exception-handling code lands under src/Renderer/.

Project rule (AGENTS.md / TODO.md "Granice architektoniczne"): the renderer is a documented
exception-free zone. Non-blocking, best-effort: any parsing/read failure exits quietly.
"""
import json
import re
import sys

EXCEPTION_PATTERN = re.compile(r"\b(throw|try\s*\{|catch\s*\()")

def main():
    try:
        data = json.load(sys.stdin)
        if data.get("tool_name") not in ("Write", "Edit"):
            return
        file_path = data.get("tool_input", {}).get("file_path", "")
        if not re.search(r"[\\/]src[\\/]Renderer[\\/].*\.(cpp|hpp)$", file_path, re.IGNORECASE):
            return
        with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
            content = f.read()
        if EXCEPTION_PATTERN.search(content):
            print(json.dumps({
                "systemMessage": (
                    f"{file_path} contains throw/try/catch - renderer is a documented "
                    "exception-free zone (AGENTS.md \"Granice architektoniczne\"). Verify this "
                    "is intentional, or use Result/StructuredError instead."
                )
            }))
    except Exception:
        pass
    finally:
        sys.exit(0)

if __name__ == "__main__":
    main()
