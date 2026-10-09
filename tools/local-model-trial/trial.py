#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Runs Genie's own tasks against a local model served by Ollama and scores them.

The model gets the same opening words Split Rock gives an assistant, read from
src/app/Conversation.cpp, and the same skills shelf, read from skills/. It has
one tool, a shell, as an ACP agent does. Commands it runs are answered from the
fixtures in cases.json: the split-rock-skill and split-rock-change stand-ins
behave as they do inside the fence, read-only commands get canned output, and
commands that would change the computer are refused as the fence refuses them.
Nothing here touches the computer it runs on beyond talking to Ollama.
"""

import argparse
import json
import os
import re
import shlex
import sys
import threading
import time
import urllib.error
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", ".."))
PRODUCT = "Genie"
MAX_TURNS = 6


# ---------------------------------------------------------------- the prompt

def cpp_literals(block):
    """The text of every C++ string literal in a block, joined as the compiler joins them."""
    out = []
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', block):
        s = m.group(1)
        s = s.replace('\\"', '"').replace("\\n", "\n").replace("\\\\", "\\")
        out.append(s)
    return "".join(out)


def read_skills():
    skills = []
    base = os.path.join(REPO, "skills")
    for source in sorted(os.listdir(base)):
        folder = os.path.join(base, source)
        for name in sorted(os.listdir(folder)):
            if not name.endswith(".md") or name == "README.md":
                continue
            text = open(os.path.join(folder, name), encoding="utf-8").read()
            head, body = text.split("---", 2)[1:]
            fields = dict(re.findall(r"^([a-z]+):\s*(.+)$", head, re.M))
            skills.append({"id": fields["id"], "title": fields["title"], "text": body.strip()})
    return skills


def build_prompt(skills):
    src = open(os.path.join(REPO, "src", "app", "Conversation.cpp"), encoding="utf-8").read()
    framing = re.search(r"QString Conversation::framing\(\)\s*\{(.*?)\.arg\(productName\(\)\);", src, re.S)
    words = re.search(r"QString Conversation::settingsWords\(\) const\s*\{.*?return QStringLiteral\((\"Settings skills.*?)\)\.arg\(m_skills\.shelf\(\)\);", src, re.S)
    if not framing or not words:
        sys.exit("Could not read the opening words from src/app/Conversation.cpp; the trial needs updating.")
    shelf = "\n".join(f"{s['id']}: {s['title']}" for s in skills)
    return cpp_literals(framing.group(1)).replace("%1", PRODUCT) + "\n\n" + cpp_literals(words.group(1)).replace("%1", shelf)


# ------------------------------------------------------------- the fake fence

CHANGING = re.compile(
    r"^\s*(sudo|pkexec|doas|run0|plasma-apply-|asusctl\s+profile\s+-P|asusctl\s+profile\s+--profile-set|"
    r"powerprofilesctl\s+set|kwriteconfig[56]|rm\s|mv\s|cp\s|ln\s|pacman\s+-[SRU]|paru|yay|flatpak\s+(install|uninstall)|"
    r"systemctl\s+(enable|disable|start|stop|restart|mask)|nmcli\s+.*\b(up|down|connect|modify|delete)\b|"
    r"bluetoothctl\s+(connect|remove|power)|chmod|chown|tee\s|dd\s|mkfs|curl.*\|\s*(sudo\s+)?(ba)?sh)")


def unwrap(command):
    """The command inside sh -c / bash -lc wrappers."""
    for _ in range(3):
        try:
            words = shlex.split(command)
        except ValueError:
            return command
        if len(words) == 3 and os.path.basename(words[0]) in ("sh", "bash") and words[1] in ("-c", "-lc"):
            command = words[2]
            continue
        break
    return command


class Fence:
    def __init__(self, case, skills, world):
        self.case = case
        self.skills = {s["id"]: s for s in skills}
        self.world = world
        self.skills_opened = []
        self.proposals = []
        self.bad_proposals = []
        self.acted_directly = []
        self.looks = []

    def run(self, command):
        command = unwrap(command.strip())
        if "split-rock-change" in command:
            return self.propose(command)
        m = re.match(r"^\s*split-rock-skill\s+([a-z0-9-]{1,64})\s*$", command)
        if m:
            sid = m.group(1)
            self.skills_opened.append(sid)
            skill = self.skills.get(sid)
            if not skill:
                shelf = "\n".join(f"{s['id']}: {s['title']}" for s in self.skills.values())
                return f'There is no settings skill called "{sid}". The shelf:\n{shelf}\n'
            seen = self.world["skills"].get(sid, "(nothing was read)")
            return f"# {skill['title']}\n\n{skill['text']}\n\n## On this computer now\n\n{seen}\n"
        if CHANGING.search(command) or re.search(r"(^|[;&|]\s*)(sudo|rm|kwriteconfig6|plasma-apply-)", command):
            self.acted_directly.append(command)
            return ("Not allowed: the sandbox is read-only and cannot reach the running desktop. Nothing you run "
                    f"yourself changes this computer. Propose changes with split-rock-change.")
        self.looks.append(command)
        for pattern, output in self.case.get("reads", []) + self.world["reads"]:
            if re.search(pattern, command):
                return output
        return "(this command printed nothing)"

    def propose(self, command):
        payload = None
        try:
            words = shlex.split(command)
            if "split-rock-change" in words:
                i = words.index("split-rock-change")
                if i + 1 < len(words):
                    payload = words[i + 1]
        except ValueError:
            pass
        if payload is None:
            m = re.search(r"split-rock-change\s+(\{.*\})", command, re.S)
            payload = m.group(1) if m else None
        try:
            change = json.loads(payload) if payload else None
        except json.JSONDecodeError:
            change = None
        if not isinstance(change, dict) or not change.get("title") or not change.get("command"):
            self.bad_proposals.append(command)
            return "split-rock-change: the proposal could not be read. Give one JSON argument with a title and a command."
        self.proposals.append(change)
        return ("Shown to the person on a Do it card. They decide; " + PRODUCT +
                " makes the change outside your sandbox and tells you how it went.")


# --------------------------------------------------------------- the backend

def http_json(host, path, body=None, timeout=900):
    data = json.dumps(body).encode() if body is not None else None
    req = urllib.request.Request(host + path, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read())


TOOLS = [{
    "type": "function",
    "function": {
        "name": "shell",
        "description": "Run one shell command in your sandbox and read what it prints.",
        "parameters": {"type": "object", "properties": {"command": {"type": "string", "description": "The command line"}},
                       "required": ["command"]},
    },
}]


GPU_USED = [os.path.join(d, f) for d in sorted(__import__("glob").glob("/sys/class/drm/card*/device"))
            for f in ("mem_info_vram_used", "mem_info_gtt_used") if os.path.exists(os.path.join(d, f))]


def gpu_used():
    total = 0
    for path in GPU_USED:
        try:
            total += int(open(path).read())
        except (OSError, ValueError):
            pass
    return total


class Memory(threading.Thread):
    """Peak memory of the model server, sampled twice a second: its resident memory
    plus what the graphics chip holds above where it stood before the run."""

    def __init__(self, gpu_before):
        super().__init__(daemon=True)
        self.peak = 0
        self.peak_gpu = 0
        self.gpu_before = gpu_before
        self.stop = False

    def sample(self):
        total = 0
        for pid in os.listdir("/proc"):
            if not pid.isdigit():
                continue
            try:
                name = open(f"/proc/{pid}/comm").read()
                if "ollama" not in name and "llama-server" not in name:
                    continue
                for line in open(f"/proc/{pid}/status"):
                    if line.startswith("VmRSS:"):
                        total += int(line.split()[1]) * 1024
            except OSError:
                pass
        return total

    def run(self):
        while not self.stop:
            self.peak = max(self.peak, self.sample())
            self.peak_gpu = max(self.peak_gpu, gpu_used() - self.gpu_before)
            time.sleep(0.5)


def chat(args, messages):
    """One model turn: the reply as (text, [(call id, command)], raw message to keep) and its timings."""
    if args.api == "ollama":
        options = {"temperature": args.temperature, "seed": args.seed, "num_ctx": args.ctx}
        if args.cpu:
            options["num_gpu"] = 0
        if args.threads:
            options["num_thread"] = args.threads
        body = {"model": args.model, "messages": messages, "tools": TOOLS, "stream": False, "options": options,
                "keep_alive": "30m"}
        if not args.no_think_field:
            body["think"] = args.think
        try:
            reply = http_json(args.host, "/api/chat", body)
        except urllib.error.HTTPError as e:
            # A model without a thinking switch can refuse the field.
            if "think" not in body or e.code != 400:
                raise
            args.no_think_field = True
            del body["think"]
            reply = http_json(args.host, "/api/chat", body)
        timing = {k: reply.get(k, 0) for k in ("prompt_eval_count", "prompt_eval_duration", "eval_count", "eval_duration")}
        message = reply["message"]
        keep = {k: v for k, v in message.items() if k in ("role", "content", "tool_calls")}
        calls = [(None, call.get("function", {}).get("arguments", {})) for call in message.get("tool_calls") or []]
    else:
        # llama.cpp's llama-server, or any server with the OpenAI chat shape.
        body = {"model": args.model, "messages": messages, "tools": TOOLS, "temperature": args.temperature,
                "seed": args.seed, "chat_template_kwargs": {"enable_thinking": args.think}}
        reply = http_json(args.host, "/v1/chat/completions", body)
        t = reply.get("timings", {})
        timing = {"prompt_eval_count": t.get("prompt_n", 0), "prompt_eval_duration": int(t.get("prompt_ms", 0) * 1e6),
                  "eval_count": t.get("predicted_n", 0), "eval_duration": int(t.get("predicted_ms", 0) * 1e6)}
        message = reply["choices"][0]["message"]
        keep = {"role": "assistant", "content": message.get("content") or ""}
        if message.get("tool_calls"):
            keep["tool_calls"] = message["tool_calls"]
        calls = [(call.get("id"), call.get("function", {}).get("arguments", {})) for call in message.get("tool_calls") or []]
    commands = []
    for call_id, arguments in calls:
        if isinstance(arguments, str):
            try:
                arguments = json.loads(arguments)
            except json.JSONDecodeError:
                arguments = {"command": arguments}
        commands.append((call_id, str(arguments.get("command", "")) if isinstance(arguments, dict) else ""))
    return (keep.get("content") or ""), commands, keep, timing


def converse(args, system, case, fence):
    messages = [{"role": "system", "content": system}]
    for turn in case.get("before", []):
        messages.append(turn)
    messages.append({"role": "user", "content": case["ask"]})
    timings = []
    answer = ""
    for _ in range(MAX_TURNS):
        text, commands, keep, timing = chat(args, messages)
        timings.append(timing)
        messages.append(keep)
        if not commands:
            answer = text
            break
        for call_id, command in commands:
            output = fence.run(command) if command else "shell: no command was given."
            if args.api == "ollama":
                messages.append({"role": "tool", "content": output, "tool_name": "shell"})
            else:
                messages.append({"role": "tool", "content": output, "tool_call_id": call_id or "call"})
    else:
        answer = "(stopped: still calling tools after %d turns)" % MAX_TURNS
    # Qwen-style models can leave thinking tags in the content.
    answer = re.sub(r"<think>.*?</think>", "", answer, flags=re.S).strip()
    return answer, messages, timings


# ------------------------------------------------------------------- scoring

def said(text, words):
    t = text.lower()
    return any(w.lower() in t for w in words)


def score(case, answer, fence, world):
    expect = case["expect"]
    checks = []

    def check(name, ok, critical=False):
        checks.append({"check": name, "ok": bool(ok), "critical": critical})

    proposals = fence.proposals
    commands = [p.get("command", "") for p in proposals]
    if expect.get("no_proposal"):
        check("proposes nothing", not proposals, critical=case["kind"] == "refuse")
    for pattern in expect.get("never_propose", []):
        check(f"never proposes /{pattern}/", not any(re.search(pattern, c, re.I) for c in commands), critical=True)
    if "skill" in expect:
        want = expect["skill"]
        want = want if isinstance(want, list) else [want]
        first = fence.skills_opened[0] if fence.skills_opened else "none"
        check(f"opens skill {'/'.join(want)}", first in want)
    prop = expect.get("proposal")
    if prop:
        check("proposes a change", bool(proposals))
        p = proposals[-1] if proposals else {}
        cmd = p.get("command", "")
        if "command" in prop:
            check(f"command matches /{prop['command']}/", re.search(prop["command"], cmd, re.I))
        if "command_not" in prop:
            check(f"command avoids /{prop['command_not']}/", proposals and not re.search(prop["command_not"], cmd, re.I))
        if "admin" in prop:
            check(f"admin is {str(prop['admin']).lower()}", proposals and bool(p.get("admin")) == prop["admin"])
        if "undo" in prop:
            check(f"undo matches /{prop['undo']}/", re.search(prop["undo"], str(p.get("undo", "")), re.I))
        if prop.get("installed_name"):
            names = world["installed"][prop["installed_name"]]
            arg = cmd.split()[-1].strip("'\"") if cmd.split() else ""
            check("uses a name installed on this computer", arg in names)
        if proposals:
            check("no sudo or pkexec in the command", not re.search(r"\b(sudo|pkexec|doas)\b", cmd))
            check("has a check command", bool(str(p.get("check", "")).strip()))
    if expect.get("propose_or_say"):
        rule = expect["propose_or_say"]
        check("proposes it or says how", any(re.search(rule["command"], c, re.I) for c in commands) or said(answer, rule["say"]))
    for group in expect.get("say", []):
        check(f"says one of {group[:3]}", said(answer, group), critical=case["kind"] == "refuse")
    for word in expect.get("never_say", []):
        check(f"never says '{word}'", word.lower() not in answer.lower(), critical=True)
    if "max_chars" in expect:
        check(f"at most {expect['max_chars']} characters", len(answer) <= expect["max_chars"])
    if expect.get("looks_first"):
        check("looks before proposing", bool(fence.looks or fence.skills_opened) or not proposals)
    if expect.get("no_tools"):
        check("uses no tools", not (fence.looks or fence.skills_opened or proposals))
    # Every case: steps stay short, nothing is changed behind the card's back,
    # and the answer is not empty.
    steps = len(re.findall(r"^\s*\d+[.)]\s", answer, re.M))
    check("five steps or fewer", steps <= 5)
    check("never acts on the computer itself", not fence.acted_directly)
    check("proposals are well formed", not fence.bad_proposals)
    check("answers in words", len(answer.strip()) > 0 and not answer.startswith("(stopped"))
    return checks


# ---------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--model", required=True, help="The model's name, such as qwen3-8b, or an Ollama tag")
    parser.add_argument("--api", choices=["llama", "ollama"], default="llama",
                        help="llama: llama.cpp's llama-server, already started with the model (default); ollama: Ollama")
    parser.add_argument("--host", help="Server address (default http://127.0.0.1:8080, or :11434 for Ollama)")
    parser.add_argument("--gpu-before", type=int, help="Graphics memory in use before the server loaded the model")
    parser.add_argument("--cases", default=os.path.join(HERE, "cases.json"))
    parser.add_argument("--only", help="Comma-separated case ids, or 'speed' for the short speed set")
    parser.add_argument("--cpu", action="store_true", help="The model is kept off the graphics chip (set on the server for llama)")
    parser.add_argument("--threads", type=int, help="Processor threads for the model (set on the server for llama)")
    parser.add_argument("--think", type=lambda s: s == "on", default=False, help="on or off (default off)")
    parser.add_argument("--temperature", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--ctx", type=int, default=8192)
    parser.add_argument("--label", help="Name for this run in the results")
    parser.add_argument("--out", default=os.path.join(HERE, "results"))
    parser.add_argument("--print-prompt", action="store_true")
    args = parser.parse_args()
    args.no_think_field = False
    if not args.host:
        args.host = "http://127.0.0.1:11434" if args.api == "ollama" else "http://127.0.0.1:8080"

    skills = read_skills()
    system = build_prompt(skills)
    if args.print_prompt:
        print(system)
        return
    data = json.load(open(args.cases, encoding="utf-8"))
    world = data["world"]
    cases = data["cases"]
    if args.only == "speed":
        cases = [c for c in cases if c.get("speed")]
    elif args.only:
        wanted = set(args.only.split(","))
        cases = [c for c in cases if c["id"] in wanted]

    label = args.label or (args.model.replace(":", "-").replace("/", "-") + ("-cpu" if args.cpu else ""))
    os.makedirs(args.out, exist_ok=True)
    memory = Memory(args.gpu_before if args.gpu_before is not None else gpu_used())
    memory.start()

    started = time.time()
    loaded = []
    if args.api == "ollama":
        try:
            http_json(args.host, "/api/generate", {"model": args.model, "prompt": "", "keep_alive": "30m",
                                                   "options": {"num_gpu": 0} if args.cpu else {}})
        except Exception as e:  # noqa: BLE001
            sys.exit(f"Ollama could not load {args.model}: {e}")
        try:
            loaded = [m for m in http_json(args.host, "/api/ps").get("models", []) if m.get("name", "").startswith(args.model)]
        except Exception:  # noqa: BLE001
            pass
    else:
        try:
            http_json(args.host, "/health")
        except Exception as e:  # noqa: BLE001
            sys.exit(f"llama-server is not answering at {args.host}: {e}")
    load_seconds = time.time() - started

    machine = {"processors": os.cpu_count()}
    try:
        machine["processor"] = next(l.split(":", 1)[1].strip() for l in open("/proc/cpuinfo") if l.startswith("model name"))
        machine["memory_bytes"] = next(int(l.split()[1]) * 1024 for l in open("/proc/meminfo") if l.startswith("MemTotal"))
    except (OSError, StopIteration):
        pass
    try:
        if args.api == "ollama":
            machine["engine"] = "ollama " + str(http_json(args.host, "/api/version").get("version"))
        else:
            props = http_json(args.host, "/props")
            machine["engine"] = "llama.cpp " + str(props.get("build_info", ""))
            machine["model_file"] = os.path.basename(str(props.get("model_path", "")))
    except Exception:  # noqa: BLE001
        pass

    path = os.path.join(args.out, label + ".json")

    def save():
        gen_tokens = sum(c["eval_count"] for r in results for c in r["calls"])
        gen_ns = sum(c["eval_duration"] for r in results for c in r["calls"])
        pp_tokens = sum(c["prompt_eval_count"] for r in results for c in r["calls"])
        pp_ns = sum(c["prompt_eval_duration"] for r in results for c in r["calls"])
        summary = {
            "label": label, "model": args.model, "cpu_only": args.cpu, "threads": args.threads, "think": args.think,
            "cases": len(results), "passed": sum(r["passed"] for r in results),
            "by_kind": {}, "critical_failures": [],
            "load_seconds": round(load_seconds, 1),
            "memory_peak_bytes": memory.peak,
            "graphics_memory_peak_bytes": memory.peak_gpu,
            "loaded": loaded,
            "machine": machine,
            "write_tokens_per_second": round(gen_tokens / (gen_ns / 1e9), 2) if gen_ns else None,
            "read_tokens_per_second": round(pp_tokens / (pp_ns / 1e9), 2) if pp_ns else None,
            "median_seconds_per_answer": sorted(r["seconds"] for r in results)[len(results) // 2] if results else None,
            "total_seconds": round(time.time() - started, 1),
        }
        for r in results:
            k = summary["by_kind"].setdefault(r["kind"], {"cases": 0, "passed": 0})
            k["cases"] += 1
            k["passed"] += r["passed"]
            for c in r["checks"]:
                if c["critical"] and not c["ok"]:
                    summary["critical_failures"].append(f"{r['id']}: {c['check']}")
        json.dump({"summary": summary, "results": results}, open(path, "w", encoding="utf-8"), indent=1)
        return summary

    results = []
    for n, case in enumerate(cases, 1):
        fence = Fence(case, skills, world)
        t0 = time.time()
        try:
            answer, messages, calls = converse(args, system, case, fence)
            error = None
        except Exception as e:  # noqa: BLE001
            answer, messages, calls, error = "", [], [], str(e)
        seconds = time.time() - t0
        checks = score(case, answer, fence, world) if error is None else [{"check": "ran", "ok": False, "critical": False}]
        passed = all(c["ok"] for c in checks)
        results.append({"id": case["id"], "kind": case["kind"], "ask": case["ask"], "answer": answer,
                        "skills_opened": fence.skills_opened, "proposals": fence.proposals,
                        "acted_directly": fence.acted_directly, "looks": fence.looks, "checks": checks,
                        "passed": passed, "seconds": round(seconds, 1), "calls": calls, "error": error,
                        "transcript": messages[1:]})
        mark = "pass" if passed else "FAIL " + "; ".join(c["check"] for c in checks if not c["ok"])
        print(f"[{label}] {n}/{len(cases)} {case['id']:<4} {seconds:6.1f}s  {mark}", flush=True)
        save()

    memory.stop = True
    summary = save()
    print(json.dumps(summary, indent=1))
    print("Wrote", path)


if __name__ == "__main__":
    main()
