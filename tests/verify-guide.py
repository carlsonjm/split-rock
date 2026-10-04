#!/usr/bin/env python3
"""Keep the guide and the tour in the format Jack reads.

The same script runs in Split Rock and in any desktop that adds entries of its
own. It reads every entry under guide/<source>/ and tour/<source>/ from the
repository root and checks what docs/DECISIONS.md § "The guide's entries" and
§ "The tour is written like the guide" lay down:

- A header between two `---` lines, with `id`, `title`, `checked` and `format`;
  `id` matches the file name and is unique in its folder; `format` is one this
  script knows; `also` is a list; `replaces` names `<source>/<id>`.
- A guide entry's body has, in order, "What you see", "Why" and "What to do",
  the last as steps numbered from 1. A tour's body ends in steps numbered from
  1, each opening with its action in bold.
- Nothing the person reads asks for a terminal: no code, no command line, no
  administrator command. Split Rock promises the person never sees one.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FORMATS = {"1"}
REQUIRED = ("id", "title", "checked", "format")
GUIDE_SECTIONS = ["What you see", "Why", "What to do"]
TERMINAL = re.compile(
    r"`|^\s{4}\S|\b(sudo|pkexec|pacman|apt|dnf|zypper|qdbus6?|systemctl|journalctl|"
    r"kwriteconfig6?|chmod|chown)\b|\b\w[\w-]*\.sh\b|\bterminal\b|\bconsole command\b",
    re.IGNORECASE | re.MULTILINE)
ID = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
errors: list[str] = []


def parse_header(path: Path, text: str) -> tuple[dict, str] | None:
    lines = text.split("\n")
    if not lines or lines[0] != "---" or "---" not in lines[1:]:
        errors.append(f"{path}: no header between two --- lines")
        return None
    end = lines.index("---", 1)
    header: dict = {}
    key = None
    for line in lines[1:end]:
        if not line.strip():
            continue
        item = re.match(r"^\s+-\s+(.*)$", line)
        if item and key:
            if not isinstance(header.get(key), list):
                header[key] = []
            header[key].append(item.group(1).strip())
            continue
        pair = re.match(r"^([a-z]+):\s*(.*)$", line)
        if not pair:
            errors.append(f"{path}: header line not understood: {line!r}")
            continue
        key, value = pair.group(1), pair.group(2).strip()
        header[key] = value if value else []
    return header, "\n".join(lines[end + 1:])


def numbered_steps(path: Path, block: str, what: str) -> list[str]:
    steps = re.findall(r"^(\d+)\. (.*)$", block, re.MULTILINE)
    if not steps:
        errors.append(f"{path}: {what} has no numbered steps")
        return []
    numbers = [int(n) for n, _ in steps]
    if numbers != list(range(1, len(numbers) + 1)):
        errors.append(f"{path}: {what} steps are numbered {numbers}, not from 1 in order")
    return [s for _, s in steps]


def check_header(path: Path, header: dict, kind: str, seen: set) -> None:
    for key in REQUIRED:
        if not header.get(key):
            errors.append(f"{path}: header has no {key}")
    entry_id = header.get("id", "")
    if isinstance(entry_id, str) and entry_id:
        if not ID.match(entry_id):
            errors.append(f"{path}: id {entry_id!r} is not lower-case words joined by hyphens")
        if entry_id != path.stem:
            errors.append(f"{path}: id {entry_id!r} does not match the file name")
        if entry_id in seen:
            errors.append(f"{path}: id {entry_id!r} is used twice in its folder")
        seen.add(entry_id)
    if header.get("format") and header.get("format") not in FORMATS:
        errors.append(f"{path}: format {header.get('format')!r} is not one this check knows")
    if "also" in header and not isinstance(header["also"], list):
        errors.append(f"{path}: also must be a list")
    if kind == "tour" and "also" in header:
        errors.append(f"{path}: a tour has no also")
    replaces = header.get("replaces")
    if replaces and not re.match(r"^[a-z0-9-]+/[a-z0-9-]+$", str(replaces)):
        errors.append(f"{path}: replaces must name <source>/<id>")


def check_guide_body(path: Path, body: str) -> None:
    headings = re.findall(r"^## (.+)$", body, re.MULTILINE)
    if headings != GUIDE_SECTIONS:
        errors.append(f"{path}: sections are {headings}, not {GUIDE_SECTIONS}")
        return
    parts = re.split(r"^## .+$", body, flags=re.MULTILINE)
    for name, part in zip(GUIDE_SECTIONS, parts[1:]):
        if not part.strip():
            errors.append(f"{path}: {name} is empty")
    numbered_steps(path, parts[3], "What to do")


def check_tour_body(path: Path, body: str) -> None:
    for step in numbered_steps(path, body, "the tour"):
        if not step.startswith("**"):
            errors.append(f"{path}: tour step {step[:30]!r}... does not open with its action in bold")


def check_plain(path: Path, header: dict, body: str) -> None:
    shown = "\n".join([str(header.get("title", ""))] + list(header.get("also") or []) + [body])
    for match in TERMINAL.finditer(shown):
        line = body.count("\n", 0, max(0, match.start())) + 1
        errors.append(f"{path}: shows the person a command or a terminal near {match.group(0)!r} (line {line} of the body)")


def check_tree(kind: str) -> int:
    count = 0
    base = ROOT / kind
    if not base.is_dir():
        return 0
    for source in sorted(p for p in base.iterdir() if p.is_dir()):
        seen: set = set()
        for path in sorted(source.glob("*.md")):
            if path.name == "README.md":
                continue
            count += 1
            parsed = parse_header(path.relative_to(ROOT), path.read_text(encoding="utf-8"))
            if parsed is None:
                continue
            header, body = parsed
            rel = path.relative_to(ROOT)
            check_header(rel, header, kind, seen)
            if kind == "guide":
                check_guide_body(rel, body)
            else:
                check_tour_body(rel, body)
            check_plain(rel, header, body)
    return count


guides = check_tree("guide")
tours = check_tree("tour")
if errors:
    for error in errors:
        print(f"guide: {error}", file=sys.stderr)
    sys.exit(1)
print(f"Guide checks passed: {guides} entries, {tours} tours.")
