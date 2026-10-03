#!/usr/bin/env python3
"""Keep this public repository's text fit to publish.

The same script runs in Kadunce, Tettegouche, Temperance and Split Rock; each
repository's tests/verify-public.json says what it allows. It reads tracked text files and
the messages of commits not yet pushed:

- No attribution by initial, such as "J ruled" or "(J, 28 September)". Who
  decided what, and when, is kept in the private suite record. The copyright
  holder's full name in licence headers and metadata is legal identity and
  stays.
- No paths into a real home folder; a fixture under /home/test is fine.
- docs/INPUT.md opens with its controls map, a table headed Task, Touch and
  Keyboard: each destination by its product name, with the touch and key that
  reach it. Every Task names a section below, and that section defines the
  touch and key the map gives. A quick start elsewhere, such as in the README,
  repeats rows of the map and nothing else.

Attribution still waiting to be rewritten is listed in the settings, a count per
file. A count may only fall, and the list is meant to reach empty. A branch the
settings name as private is not public text and is skipped.
"""

import fnmatch
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SETTINGS = json.loads((ROOT / "tests" / "verify-public.json").read_text())
ATTRIBUTION = re.compile(r"\bJ\b")
MACHINE_PATH = re.compile(r"/home/(?!(?:test|user)\b)[\w.-]+")
errors: list[str] = []


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True,
                          text=True).stdout


def tracked_text() -> dict[str, str]:
    files = {}
    for path in git("ls-files").splitlines():
        # This check quotes what it looks for, so it does not read itself.
        if path.startswith("tests/verify-public."):
            continue
        if any(fnmatch.fnmatch(path, pattern) for pattern in SETTINGS["exclude"]):
            continue
        try:
            files[path] = (ROOT / path).read_text(encoding="utf-8")
        except (UnicodeDecodeError, IsADirectoryError, FileNotFoundError):
            continue
    return files


def check_counts(files: dict[str, str], pattern: re.Pattern, allowed: dict[str, int],
                 what: str, advice: str) -> int:
    remaining = 0
    for path, text in sorted(files.items()):
        count = len(pattern.findall(text))
        limit = allowed.get(path, 0)
        if count > limit:
            errors.append(f"{path} has {count} {what}; {limit} allowed. {advice}")
        remaining += min(count, limit)
    for path in allowed:
        if path not in files:
            errors.append(f"tests/verify-public.json lists {path}, which is not tracked text")
    return remaining


def check_machine_paths(files: dict[str, str]) -> None:
    for path, text in sorted(files.items()):
        for match in MACHINE_PATH.finditer(text):
            line = text.count("\n", 0, match.start()) + 1
            errors.append(f"{path}:{line} names a machine path; write it relative to a checkout")


MAP_HEADER = re.compile(r"^\|\s*Task\s*\|\s*Touch\s*\|\s*Keyboard\s*\|\s*$", re.IGNORECASE)


def table_rows(lines: list[str], start: int) -> list[list[str]]:
    rows = []
    for line in lines[start + 2:]:
        if not line.startswith("|"):
            break
        rows.append([cell.strip() for cell in line.strip().strip("|").split("|")])
    return rows


def words(text: str) -> list[str]:
    return re.findall(r"[a-z0-9+]+", text.replace("`", "").lower())


def follows(short: str, long: str) -> bool:
    """Every word of short appears in long, in order: "Tap a card" fits
    "Tap or click a card"."""
    rest = iter(words(long))
    return all(word in rest for word in words(short))


def normal(row: list[str]) -> tuple[str, ...]:
    return tuple(" ".join(cell.split()) for cell in row[:3])


def check_controls_map(files: dict[str, str]) -> None:
    text = files.get("docs/INPUT.md")
    if text is None:
        errors.append("docs/INPUT.md is missing")
        return
    lines = text.splitlines()
    first_section = next((i for i, line in enumerate(lines) if line.startswith("## ")), len(lines))
    start = next((i for i in range(first_section) if MAP_HEADER.match(lines[i])), None)
    if start is None:
        errors.append("docs/INPUT.md does not open with its controls map (Task, Touch, Keyboard)")
        return
    # Each section's input cells, a subsection's counting toward its section.
    sections: dict[str, list[str]] = {}
    current = None
    for line in lines[first_section:]:
        if line.startswith("## "):
            current = line[3:].strip()
            sections.setdefault(current, [])
        elif current and line.startswith("|") and not re.match(r"\|\s*-", line):
            cell = line.strip().strip("|").split("|")[0].strip()
            if cell and cell != "Input":
                sections[current].append(cell.replace("`", ""))
    controls = table_rows(lines, start)
    for task, touch, keyboard in (row[:3] for row in controls):
        if task not in sections:
            errors.append(f"docs/INPUT.md: the map's '{task}' has no section of that name")
            continue
        cells = sections[task]
        if touch.strip(" .") not in {"", "—", "-"} and not any(follows(touch, cell) for cell in cells):
            errors.append(f"docs/INPUT.md: the map says '{touch}' for {task}, which § {task} does not define")
        for key in re.findall(r"`([^`]+)`", keyboard):
            if not any(key in cell for cell in cells):
                errors.append(f"docs/INPUT.md: the map gives `{key}` for {task}, which § {task} does not define")
    allowed = {normal(row) for row in controls}
    for path, other in sorted(files.items()):
        if not path.endswith(".md") or path == "docs/INPUT.md":
            continue
        other_lines = other.splitlines()
        for index, line in enumerate(other_lines):
            if MAP_HEADER.match(line):
                for row in table_rows(other_lines, index):
                    if normal(row) not in allowed:
                        errors.append(f"{path}: quick start row '{row[0]}' is not a row of docs/INPUT.md's map")


def unpushed_commits() -> list[str]:
    base = git("rev-parse", "--abbrev-ref", "--symbolic-full-name", "@{upstream}").strip()
    if not base and git("rev-parse", "--verify", "--quiet", "origin/main").strip():
        base = "origin/main"
    return git("rev-list", f"{base}..HEAD").split() if base else []


def check_commit_messages() -> None:
    for commit in unpushed_commits():
        message = git("log", "-1", "--format=%B", commit)
        body = "\n".join(line for line in message.splitlines()
                         if not line.startswith("Co-Authored-By:"))
        subject = message.splitlines()[0] if message else commit
        for pattern, what in [(ATTRIBUTION, "attribution by initial"),
                              (MACHINE_PATH, "a machine path")]:
            if pattern.search(body):
                errors.append(f"unpushed commit {commit[:7]} '{subject}' carries {what}")


branch = git("rev-parse", "--abbrev-ref", "HEAD").strip()
if branch in SETTINGS.get("private_branches", []):
    print(f"Public text checks skipped: {branch} is a private branch.")
    sys.exit(0)

files = tracked_text()
pending = check_counts(files, ATTRIBUTION, SETTINGS["attribution_pending"],
                       "attributions by initial",
                       "State the rule or behavior itself; who decided belongs in the private suite record.")
check_machine_paths(files)
check_controls_map(files)
check_commit_messages()

if errors:
    for error in errors:
        print(f"public text: {error}", file=sys.stderr)
    sys.exit(1)
note = f"; {pending} attributions left to rewrite" if pending else ""
print(f"Public text checks passed{note}.")
