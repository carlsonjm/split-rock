# Documentation index

Paths are from the repository root.

| Path | What it is for |
| --- | --- |
| `README.md` | What Split Rock is, in plain words |
| `AGENTS.md` | Startup, the holds, and how to write here |
| `CLAUDE.md` | How to communicate with the product owner |
| `TRADEMARKS.md` | What the licence does not cover: the names and marks |
| `docs/CONCEPT.md` | Who Split Rock is for, what it does, its rules, and what it relies on |
| `docs/MEMORY.md` | The notebook: its kinds of memory, and what is never written down |
| `assets/mark.svg` | Split Rock's mark, a lighthouse, and the file its icon is drawn from |
| `docs/mockup/` | The approved mock-up: five screens as design source, and which milestone each applies to |
| `docs/DECISIONS.md` | Every settled ruling and what it rejected |
| `docs/LIMITATIONS.md` | What Split Rock does not do yet, and what it will not do |
| `docs/INPUT.md` | Every input a Helper answers, opening with its controls map |
| `docs/CORE.md` | Core, the front desk: what it watches, when it speaks, the files it keeps, and its bus interface |
| `docs/DESKTOP.md` | The entry, and how a Helper opens in a window of its own |
| `data/` | The desktop file, Core's bus service, its systemd unit and its autostart entry, filled in at build time |
| `src/` | The Helpers, the change record and the notebook (`app/`); the guide reader (`guide/`); how a second start hands over (`desktop/`); `split-rock-admin`, the administrator's jobs (`admin/`); Core, the front desk (`core/`) |
| `qml/` | The Helpers' window and each Helper's card |
| `tests/` | The tests, each run in its own home folder; `tests/bus/` holds the private bus Core's test runs on |
| `guide/plasma/` | The troubleshooting guide's general Plasma entries, one known problem each |
| `tour/plasma/` | The tour's general Plasma steps |
| `tests/verify-guide.py` | Checks that the guide and the tour use the format Split Rock reads and show no terminal commands; a desktop runs the same script on its own entries |
| `tests/verify-public.py` | Checks that public text is fit to publish; the other components run the same script |
| `run.sh` | Builds Split Rock and opens one Helper from the checkout |
| `install.sh`, `uninstall.sh` | The developer install for the maintainer's own computer, for every account, and its removal; not Milestone 3's package |
| `verify.sh` | Runs every check: public text, the guide, the build and the tests |
