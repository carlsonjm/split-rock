# Split Rock

A desktop assistant for KDE Plasma, built for people leaving decades of Windows.

Ask a question in your own words. Split Rock explains the answer, or makes the
change after you approve it, and keeps what it learns in a notebook of plain
notes so the next problem starts from what is already known.

**Status:** early. There is nothing to install yet. Milestone 0, a test program
that talks to real assistants and changes nothing, is built and waiting on its
live sign-in check. The concept is in [docs/CONCEPT.md](docs/CONCEPT.md) and the
plan in [docs/ROADMAP.md](docs/ROADMAP.md). The working name is Jack.

## What it does

Out of the box, with no account, it gives a tour of the desktop and a
troubleshooting guide: known problems and their fixes, kept up to date with
each release. Sign in with an assistant you already pay for, such as ChatGPT,
and it becomes a helper on your desktop that can do the following, with that
use billed to your own account:

- **Answers.** "How do I make the writing bigger?" It explains in plain words,
  one step at a time.
- **Does it, with permission.** It shows a card saying what will change and how
  to undo it, and changes nothing until you tap Do it.
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

Split Rock does not include an AI of its own, sells no account and pays for no
use. You choose an assistant you already use, and that assistant signs you in
through its own window. Split Rock talks to it through the Agent Client
Protocol, so it works with any assistant that supports it, and never sees your
account or your bill.

Your notebook is an ordinary folder of notes. Split Rock shows it for you; any
notes app that reads plain notes can open it too. Where Gooseberry is
installed, what you keep becomes notes there, beside your own.

## With Shuffle

Split Rock works on any KDE Plasma desktop. Inside Shuffle it is the workspace
assistant, and it can also arrange your workspace when Shuffle is installed.

## Development

Read [AGENTS.md](AGENTS.md) before working in this repository. The documentation
index is [docs/README.md](docs/README.md). Run `./verify.sh` before proposing a
change; it needs CMake, a C++20 compiler and Qt 6.4 or later.

`./run.sh` builds Milestone 0 and opens its window. Its first run downloads the
assistants' adapters into `build/m0`, which needs Node.js and npm.
`./run.sh --pretend tests/pretend-scripts/signed-in.json` adds the pretend
assistant, which needs no account.

## License

Split Rock is licensed under GPL-2.0-or-later. See [LICENSE](LICENSE).

The project names and marks are not covered by that licence. See
[TRADEMARKS.md](TRADEMARKS.md).
