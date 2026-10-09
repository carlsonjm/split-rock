#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Prints the trial's results side by side, then every case that failed with its answer.

    report.py [results folder] [all]    (default: results/ beside this file;
    "all" shows passing cases too, "table" only the table)
"""

import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KINDS = [("talk", "Conversation"), ("skill", "Picks the skill"), ("change", "Proposes a change"), ("refuse", "Stays safe")]


def gb(n):
    return f"{n / 2**30:.1f} GB" if n else "?"


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "results")
    runs = [json.load(open(p, encoding="utf-8")) for p in sorted(glob.glob(os.path.join(folder, "*.json")))]
    if not runs:
        sys.exit("No results in " + folder)
    print("| Run | All | " + " | ".join(name for _, name in KINDS) +
          " | Safety failures | Writes (tok/s) | Reads (tok/s) | Median answer | Memory | Graphics memory |")
    print("|" + "---|" * (len(KINDS) + 8))
    for run in runs:
        s = run["summary"]
        cells = []
        for kind, _ in KINDS:
            k = s["by_kind"].get(kind)
            cells.append(f"{k['passed']}/{k['cases']}" if k else "-")
        loaded = s["loaded"][0]["size"] if s.get("loaded") else 0
        print(f"| {s['label']} | {s['passed']}/{s['cases']} | " + " | ".join(cells) +
              f" | {len(s['critical_failures'])} | {s['write_tokens_per_second']} | {s['read_tokens_per_second']}"
              f" | {s['median_seconds_per_answer']} s | {gb(max(s['memory_peak_bytes'], loaded))}"
              f" | {gb(s.get('graphics_memory_peak_bytes', 0))} |")
    if "table" in sys.argv[2:]:
        return
    print()
    for run in runs:
        s = run["summary"]
        print(f"## {s['label']}  ({s.get('machine', {})})")
        for r in run["results"]:
            if r["passed"] and "all" not in sys.argv[2:]:
                continue
            failed = "; ".join(c["check"] + (" [SAFETY]" if c["critical"] else "") for c in r["checks"] if not c["ok"])
            print(f"- {'pass' if r['passed'] else 'FAIL'} {r['id']} {r['ask']!r}\n    failed: {failed}\n    skills: {r['skills_opened']}  "
                  f"acted itself: {r['acted_directly']}")
            if "route" in r:
                print(f"    desk: gate={r['gate']} route={r['route']} skill={r['skill']} codex={r['handoff']}")
            for p in r["proposals"]:
                print(f"    proposed: {json.dumps(p)[:400]}")
            if r.get("error"):
                print("    error: " + r["error"][:300])
            print("    answer: " + r["answer"].replace("\n", " / ")[:700])
        print()


if __name__ == "__main__":
    main()
