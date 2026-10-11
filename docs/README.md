# Documentation index

Paths are from the repository root.

| Path | What it is for |
| --- | --- |
| `README.md` | What Split Rock is, in plain words |
| `AGENTS.md` | Startup, the holds, and how to write here |
| `CLAUDE.md` | How to communicate with the product owner |
| `TRADEMARKS.md` | What the licence does not cover: the names and marks |
| `docs/CONCEPT.md` | Who Split Rock is for, what it does, its rules, and what it relies on |
| `docs/ASSISTANTS.md` | The two assistants, Split Rock on this computer and Codex: how each starts, how a person signs in, what it costs; and what was learned about those not offered |
| `docs/SIGN-IN-CHECK.md` | Milestone 0's live check: signing in to Codex, step by step |
| `docs/MEMORY.md` | How Split Rock remembers: the notebook's five kinds of memory, and what is never written down |
| `assets/mark.svg` | The assistant's mark, the genie lamp: the one file every surface draws it from |
| `docs/mockup/` | The approved mock-up: five screens as design source, and which milestone each applies to |
| `docs/DECISIONS.md` | Every settled ruling and what it rejected |
| `docs/ROADMAP.md` | What is planned, in order, and what is not |
| `docs/INPUT.md` | Every input, opening with its controls map |
| `docs/DESKTOP.md` | The entry, the card, and the conversation offered to Search on the session bus |
| `data/` | The desktop file and the session bus's service file, filled in at build time; `models.json`, the model Split Rock on this computer downloads, with its size and checksum; and the model's licence in `licences/` |
| `src/` | The protocol layer (`acp/`); the assistants, the conversation, changes and the upkeep cards (`app/`); Split Rock on this computer's front desk (`desk/`); the guide reader and the skill shelf (`guide/`); where the card stands and Kadunce's companion guest (`desktop/`); the pretend assistant (`pretend/`) |
| `qml/` | The windows: the card in Search's shape, the full window with This Device, the Do it card, the upkeep cards and the model's download |
| `tests/` | The tests, each against the pretend assistant only, and its scripts in `tests/pretend-scripts/`; `tests/bus/` drives the conversation's bus interface on a private bus |
| `guide/plasma/` | The troubleshooting guide's general Plasma entries, one known problem each |
| `tour/plasma/` | The tour's general Plasma steps |
| `skills/plasma/` | The settings skills: the look, the pointer, the fans, startup and services |
| `tests/verify-guide.py` | Keeps the guide and the tour in the format Split Rock reads, with nothing that shows a terminal; the same script as a desktop's own |
| `tests/verify-public.py` | Keeps public text fit to publish; the same script as the other components |
| `tests/verify-skills.py` | Keeps every skill's look-around to programs that only read |
| `tools/fetch-assistants.sh` | Downloads Codex's pinned adapter into one folder |
| `tools/desk-trial/` | The trial that runs the real front desk against the real model, and how it chose the model |
| `run.sh` | Builds Split Rock and opens its window from the checkout |
| `install.sh`, `uninstall.sh` | The developer install for the maintainer's own computer, for every account, and its removal; not Milestone 3's package |
| `verify.sh` | Runs every check: public text, the guide, the skills, the build and the tests |
