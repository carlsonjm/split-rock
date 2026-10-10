#!/usr/bin/env python3
"""Keep the settings skills' look-around read-only.

Split Rock runs each skill's "Look around" block outside the assistant's fence
when the assistant opens the skill, without a card (docs/DECISIONS.md, The
assistant opens settings skills from a shelf). So that block may only read:
every command in it is one of the programs below, and the ones that can also
change something appear only with the options that read.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
READS = {"echo", "kreadconfig6", "basename", "sort", "command", "printf", "ls", "cat", "grep", "cut"}
# Programs that change things too, with the only options allowed here.
READ_OPTIONS = {
    "plasma-apply-lookandfeel": {"--list", "-l"},
    "plasma-apply-colorscheme": {"--list-schemes", "-l"},
    "plasma-apply-cursortheme": {"--list-themes"},
    "asusctl": {"profile -p", "profile -l"},
    "powerprofilesctl": {"get", "list"},
    "systemctl": {"--failed", "--user --failed", "list-unit-files", "--user list-unit-files"},
    "journalctl": {"-b", "--user -b"},
}
SHELL_WORDS = {"if", "then", "fi", "do", "done", "else", "true", "case", "esac", "continue", "!"}
# Lines that start a loop or a test name no program.
NO_PROGRAM = {"for", "[", "test"}
errors: list[str] = []

for path in sorted((ROOT / "skills").glob("*/*.md")):
    text = path.read_text(encoding="utf-8")
    match = re.search(r"^## Look around\s*\n+```sh\n(.*?)^```", text, re.M | re.S)
    if not match:
        errors.append(f"{path}: no sh block under \"Look around\"")
        continue
    look = match.group(1)
    if re.search(r">\s*[^&/]|>>|\brm\b|\bmv\b|\bcp\b|\btee\b|\bsudo\b|\bpkexec\b|\bkwriteconfig", look.replace("2>&1", "").replace(">/dev/null", "")):
        errors.append(f"{path}: the look-around writes or removes something")
    # Text in single quotes is never run, and a case's patterns are not
    # commands; neither may hold a pipe that would split a command below.
    # Nothing that runs quoted text (sh -c, eval) is on the list above.
    commands = re.sub(r"'[^']*'", "''", look)
    commands = re.sub(r"\bin\s+[^)\n]*\)", "in )", commands)
    # Each command: what follows the start of a line, a pipe, a semicolon,
    # && or ||, or the opening of $(, after any shell keyword.
    for segment in re.split(r"\n|\|\||&&|\||;|\$\(", commands):
        words = segment.replace(")", " ").replace('"', " ").split()
        while words and words[0] in SHELL_WORDS:
            words.pop(0)
        if not words or words[0] in NO_PROGRAM:
            continue
        program = words[0]
        if program in READS or "=" in program or program.startswith("$"):
            continue
        if program in READ_OPTIONS:
            if not any(" ".join(words[1:]).startswith(option) for option in READ_OPTIONS[program]):
                errors.append(f"{path}: {program} without a reading option: {segment.strip()}")
            continue
        errors.append(f"{path}: {program} is not on the list of programs that only read")

if errors:
    print("\n".join(errors), file=sys.stderr)
    sys.exit(1)
print("Skills verification passed.")
