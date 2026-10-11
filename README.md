# Split Rock

A desktop assistant for KDE Plasma, built for people leaving decades of Windows.

Ask a question in your own words. Split Rock explains the answer, or makes the
change after you approve it, and keeps what it learns in a notebook of plain
notes so the next problem starts from what is already known.

**Status:** early. Shuffle's packages carry it as Genie; on its own there is no
package yet, only a developer install (below). The concept is in
[docs/CONCEPT.md](docs/CONCEPT.md) and the plan in
[docs/ROADMAP.md](docs/ROADMAP.md).

## What it does

Out of the box, with no account, Split Rock on this computer answers and helps:
an open model that runs on the computer itself after a one-time download, so
nothing you ask leaves it. For bigger fixes, sign in to Codex with the ChatGPT
plan you already pay for, and that use is billed to your own account.

- **Answers.** "How do I make the writing bigger?" It explains in plain words,
  one step at a time, and answers known problems from a troubleshooting guide
  written ahead of time and kept up to date with each release.
- **Does it, with permission.** It shows a card saying what will change and how
  to undo it, and changes nothing until you tap Do it.
- **Looks after the computer.** Cards of their own check for updates and run
  them with a restore point first, free up space, install or remove an app,
  give the computer a health check, show what changed lately with a way to
  undo it or go back to before, and review updates to community (AUR) apps
  before building them.
- **Remembers.** What it changed, what you asked it to keep, and what it learned
  about this computer go into your notebook.
- **Keeps this computer working.** Unusual hardware too, such as a tablet with
  its own power and firmware quirks: it troubleshoots, and every fix goes into
  the notebook for next time.

## What it never does

- Show you a terminal.
- Change anything without asking first.
- Hold your password. Anything that needs one uses the system's own prompt.
- Ask for money or card details.

## How it works

Split Rock sells no account and pays for no use. Its own assistant, Split Rock
on this computer, runs Qwen3 4B, an open model by the Qwen team under the
Apache 2.0 licence, with llama.cpp: about 2.5 GB, downloaded once and checked
before use. It answers, opens the cards for everyday jobs, and passes anything
bigger to Codex. Codex signs you in to ChatGPT through its own window, and
Split Rock never sees your account or your bill. Both talk to Split Rock
through the Agent Client Protocol, and both run fenced in: they can read what
they need to explain the computer, and change it only through a card you
approve.

Your notebook is an ordinary folder of notes. Split Rock shows it for you; any
notes app that reads plain notes can open it too.

## With Shuffle

Split Rock works on any KDE Plasma desktop. Inside Shuffle it is Genie, the
workspace assistant: Search hosts its conversation, its card holds the centre
of Spread, and Shuffle installs the llama.cpp it runs on.

## Development

Read [AGENTS.md](AGENTS.md) before working in this repository. The documentation
index is [docs/README.md](docs/README.md). Run `./verify.sh` before proposing a
change; it needs CMake, a C++20 compiler, Qt 6.4 or later and Python 3.

`./run.sh` builds Split Rock and opens its window from this checkout. Its first
run downloads Codex's adapter into `build/m0`, which needs Node.js and npm.
Every assistant runs in bubblewrap, and Split Rock on this computer needs
llama.cpp's `llama-server`.
`./run.sh --pretend tests/pretend-scripts/signed-in.json` adds the pretend
assistant, which needs no account.

## Installing on your own computer

`./install.sh` builds Split Rock, runs every check, and installs it for every
account on this computer: the program, its entry in the launcher, the service
Search starts it with, the guide, the tour and the settings skills, Codex's
adapter, and the front desk of Split Rock on this computer with its model list
and the model's licence. It does not install llama.cpp. It asks for the
administrator password once, to write under `/usr`. `./uninstall.sh` removes
exactly that, and leaves the notebook, every assistant's sign-in and the
downloaded model where they are. The installed Split Rock keeps its sign-ins in
its own folder, not in this checkout, so sign in once more after installing.
It needs bubblewrap, the sandbox the assistants run in.

This is a developer install for the maintainer's own computer, so Milestone 1
can be used every day. It is not the package Milestone 3 brings, and nobody
else should be handed it.

## License

Split Rock is licensed under GPL-2.0-or-later. See [LICENSE](LICENSE).

The project names and marks are not covered by that licence. See
[TRADEMARKS.md](TRADEMARKS.md).
