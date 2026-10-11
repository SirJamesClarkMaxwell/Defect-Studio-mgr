#!/usr/bin/env python3
"""PreToolUse hook: the BSc-thesis folder and the Okeanos mount (O:) are read-only sources.

Blocks (decision "block"):
- Write/Edit of a file under the thesis folder or O: (except O:/_fnv-analysis-*),
- Bash/PowerShell commands that mention a protected path together with a mutating verb
  (rm, mv, sed -i, tee, >, Remove-Item, Set-Content, ...) or copy INTO a protected path,
- copying LOCPOT/CHGCAR/WAVECAR off O: (SSHFS ~10 KB/s; use the packs from pack_data.sh,
  local copies are in test-directory/fnv-diamond/okeanos/data/).
Best-effort: any parsing failure exits quietly. Self-check: python readonly_data_guard.py --selftest
"""
import json
import re
import sys

PROTECTED = re.compile(r"(01_engeenering-studies/praca-in|(?<![\w])o:/|(?<![\w])/o/)")
ALLOWED = re.compile(r"(o:/|/o/)_fnv-analysis-")
MUTATING = re.compile(
    r"(\brm\b|\bmv\b|\bsed\s+-i|\btee\b|>|\bremove-item\b|\bmove-item\b|\brename-item\b|"
    r"\bset-content\b|\badd-content\b|\bout-file\b|\bclear-content\b|\bnew-item\b|\bmkdir\b|\btouch\b)"
)
COPY_INTO = re.compile(r"\b(cp|copy|copy-item|robocopy|rsync|xcopy)\b.*\s['\"]?(\S*)['\"]?\s*$")
BIG_GRID_OFF_O = re.compile(
    r"\b(cp|copy|copy-item|robocopy|rsync|xcopy|dd|cat|type|get-content)\b.*"
    r"((?<![\w])o:/|(?<![\w])/o/)\S*(locpot|chgcar|wavecar)"
)


def norm(text):
    return text.replace("\\", "/").lower()


def protected(path):
    return bool(PROTECTED.search(path)) and not ALLOWED.search(path)


def verdict(tool, tool_input):
    if tool in ("Write", "Edit"):
        path = norm(tool_input.get("file_path", ""))
        if protected(path):
            return f"{tool_input.get('file_path')} is a read-only source (thesis folder / Okeanos O:)."
        return None
    if tool in ("Bash", "PowerShell"):
        cmd = norm(tool_input.get("command", ""))
        if BIG_GRID_OFF_O.search(cmd):
            return ("Do not copy LOCPOT/CHGCAR/WAVECAR off O: (SSHFS ~10 KB/s). Local copies: "
                    "test-directory/fnv-diamond/okeanos/data/; new data: pack_data.sh on Okeanos.")
        if not PROTECTED.search(cmd):
            return None
        copy = COPY_INTO.search(cmd)
        if copy and protected(copy.group(2)):
            return "Copy into a read-only source (thesis folder / Okeanos O:)."
        stripped = ALLOWED.sub("", cmd)
        if PROTECTED.search(stripped) and MUTATING.search(re.sub(r"2>\s*/dev/null|2>&1|2>\$null", "", cmd)):
            return ("Command mutates or redirects near a read-only source (thesis folder / Okeanos O:). "
                    "Write outputs to test-directory/fnv-diamond/ or the scratchpad instead.")
    return None


def main():
    try:
        data = json.load(sys.stdin)
        reason = verdict(data.get("tool_name", ""), data.get("tool_input", {}))
        if reason:
            print(json.dumps({"decision": "block", "reason": reason}))
    except Exception:
        pass
    finally:
        sys.exit(0)


def selftest():
    thesis = r"D:\STUDIA\Fizyka\01_Engeenering-Studies\Praca-Inżynierska\Diament\x.txt"
    assert verdict("Write", {"file_path": thesis})
    assert verdict("Edit", {"file_path": "O:/GeV/INCAR"})
    assert not verdict("Write", {"file_path": "O:/_fnv-analysis-2026-10-06/cases.tsv"})
    assert not verdict("Write", {"file_path": "D:/STUDIA/Fizyka/02_Master-Studies/x.py"})
    assert not verdict("Bash", {"command": "grep TOTEN /o/GeV/Stan_0/OUTCAR | tail -1"})
    assert not verdict("PowerShell", {"command": "Get-ChildItem O:\\ -Name"})
    assert not verdict("Bash", {"command": "ls /o/GeV 2>/dev/null"})
    assert verdict("Bash", {"command": "rm -rf /o/GeV/Stan_0"})
    assert verdict("Bash", {"command": f"echo x > '{thesis}'"})
    assert verdict("Bash", {"command": "cp out.txt /o/GeV/"})
    assert not verdict("Bash", {"command": "cp /o/GeV/Stan_0/OUTCAR test-directory/x/"})
    assert verdict("Bash", {"command": "cp /o/GeV/Stan_0/HSE/LOCPOT test-directory/x/"})
    assert verdict("PowerShell", {"command": "Copy-Item O:\\GeV\\Stan_0\\CHGCAR D:\\x"})
    print("ok")


if __name__ == "__main__":
    selftest() if "--selftest" in sys.argv else main()
