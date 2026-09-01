#!/usr/bin/env python3
"""PreToolUse hook: nudge the project rule "merge to main only after a full Debug + Release
build" (docs/work/project/TODO.md, "Zasady pracy") whenever a Bash call looks like a merge or
push into main. Non-blocking (systemMessage only, no "decision": "block") - this is a reminder,
not an enforced gate, since the hook can't itself verify a build actually happened. Best-effort:
any parsing failure exits quietly.
"""
import json
import re
import sys

MERGE_INTO_MAIN = re.compile(
    r"git\s+(merge\s+\S*\bmain\b|checkout\s+main.*merge|push\s+\S*\s*(origin\s+)?main\b)",
    re.IGNORECASE,
)

def main():
    try:
        data = json.load(sys.stdin)
        if data.get("tool_name") != "Bash":
            return
        command = data.get("tool_input", {}).get("command", "")
        if MERGE_INTO_MAIN.search(command):
            print(json.dumps({
                "systemMessage": (
                    "Project rule (TODO.md, \"Zasady pracy\"): merge to main only after a full "
                    "Debug + Release build (DefectStudio.exe + DefectStudioTests.exe) with the test "
                    "suite passing in both configs, AND after active review of AI-generated code. "
                    "Confirm both ran before this merge/push."
                )
            }))
    except Exception:
        pass
    finally:
        sys.exit(0)

if __name__ == "__main__":
    main()
