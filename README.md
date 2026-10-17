# Split Rock

Helpers that look after a KDE Plasma computer, built for people leaving decades
of Windows.

Each Helper does one job, in plain words, and changes nothing until you tap its
button. Core watches the computer quietly in the background and opens the
Helper that fits when something needs a look.

**Status:** early. On its own there is no package yet, only a developer install
(below). The concept is in [docs/CONCEPT.md](docs/CONCEPT.md) and the plan in
[docs/ROADMAP.md](docs/ROADMAP.md).

## What it does

- **Update.** Checks for updates and runs them with a restore point first.
- **Free up space.** Shows exactly what it would remove, by name, before it
  removes anything.
- **Install or remove an app.** Finds the app for what you need, or removes the
  one you name.
- **Health check.** Looks the computer over and says what is fine and what
  needs a look.
- **What changed.** The last two weeks, with a way to undo a change or go back
  to before.
- **Restore points** and **community apps**, reviewed before they are built.
- **Core.** A background program with no window that notices when space runs
  low or updates have waited too long, and says so without nagging
  ([docs/CORE.md](docs/CORE.md)).

Each Helper writes what it is about to change in your notebook, an ordinary
folder of notes, with how to undo it, before it changes anything.

## What it never does

- Show you a terminal.
- Change anything without asking first.
- Hold your password. Anything that needs one uses the system's own prompt.
- Ask for money or card details.

## With Shuffle

Split Rock works on any KDE Plasma desktop. Inside Shuffle its Helpers open
from Search and from Core's notices.

## Development

Read [AGENTS.md](AGENTS.md) before working in this repository. The documentation
index is [docs/README.md](docs/README.md). Run `./verify.sh` before proposing a
change; it needs CMake, a C++20 compiler, Qt 6.4 or later and Python 3.

`./run.sh space` builds Split Rock and opens Free up space from this checkout;
any other Helper's name works the same way.

## Installing on your own computer

`./install.sh` builds Split Rock, runs every check, and installs it for every
account on this computer: the Helpers' program, Core, the guide and the tour.
What an earlier install placed is removed first. It asks for the administrator
password once, to write under `/usr`. `./uninstall.sh` removes exactly that,
and leaves the notebook where it is.

This is a developer install for the maintainer's own computer. It is not the
package Milestone 3 brings, and nobody else should be handed it.

## License

Split Rock is licensed under GPL-2.0-or-later. See [LICENSE](LICENSE).

The project names and marks are not covered by that licence. See
[TRADEMARKS.md](TRADEMARKS.md).
