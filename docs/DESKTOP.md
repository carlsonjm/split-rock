# The desktop

How Split Rock meets the desktop it runs on: the entry that opens it, the card,
and the conversation it offers to Shuffle's Search. What Shuffle adds switches
on only where Shuffle is there; on any other Plasma desktop Split Rock opens
from its entry and its card stands over the work.

## The entry

The desktop file id is `io.github.carlsonjm.SplitRock`, and it is every
window's application id, so a card workspace knows Split Rock's windows as
Split Rock's. The desktop file is `data/io.github.carlsonjm.SplitRock.desktop.in`.
Until Milestone 3's package, `install.sh` installs it, with the program, its
icon, the bus service file, the guide, the tour and the assistants' adapters,
for every account; `uninstall.sh` removes them. That is a developer install
for the maintainer's own computer, not the package.

| Start | What opens |
| --- | --- |
| Split Rock's icon, with no arguments | The full window, the view in `mockup/Workspace.dc.html`: an ordinary resizable window that a card workspace can place beside another |
| Ask, in the icon's menu, or `--ask` | The card in Search's shape; in Spread it holds the centre (`DECISIONS.md`) |
| `--background` | Nothing: the conversation waits for Search, as when Search's first call starts Split Rock |

Split Rock runs once. A second start hands its arguments to the one running
and ends, so its icon brings up the same conversation. Split Rock keeps
running after its windows close, so Search can reach the conversation, until
the session ends it; it then quits the usual way, and the assistant's own
record of the conversation goes with it. Without a session bus it ends with its
last window, as before.

## The conversation in Search

Search hosts the conversation as one of its modes (`DECISIONS.md`). It draws the
chat in its own window and keeps nothing: Split Rock keeps the conversation,
and offers it on the session bus. Search loads none of Split Rock's code.

- **Service:** `io.github.carlsonjm.splitrock`, owned by the one running
  Split Rock. A call before Split Rock has started starts it, from its bus
  service file (`data/io.github.carlsonjm.splitrock.service.in`, which starts
  it with `--background`), and is answered once it is ready.
- **Object:** `/Conversation`. **Interface:**
  `io.github.carlsonjm.SplitRock.Conversation`.
- **Version:** `ProtocolVersion()` returns `1`. A caller uses the interface only
  when the number is one it knows. A method or a key added later, which a
  version-1 caller can ignore, keeps the number; any other change raises it.

There is one conversation, the same in Search, the card and the full window.
Every method returns it as Split Rock keeps it, a dictionary of strings to
variants:

| Key | Type | What it is |
| --- | --- | --- |
| `phase` | `s` | `choose`, `sign-in`, `starting`, `ready`, `answering` or `failed` |
| `assistant` | `s` | The assistant's name, as shown; empty until one is chosen |
| `question` | `s` | The question being answered |
| `answer` | `s` | The answer's headline, its one short first line; for an answer that opens with its steps, the first paragraph after them |
| `more` | `as` | The answer's other paragraphs, in order, after the steps |
| `steps` | `as` | The answer's numbered steps, in order |
| `suggestions` | `as` | Questions to start from |
| `canDoIt` | `b` | Do it would make the change the answer asks for; never in Milestone 0 |
| `doItReason` | `s` | Why Do it is unavailable; empty when the answer asks to change nothing |
| `kept` | `b` | Keep this was used on this answer |
| `problem` | `s` | What went wrong, in plain words, while the phase is `failed` |

`more` was added after the first version of this table; a version-1 caller may
ignore it, so the version stays `1`.

Search shows Do it when `canDoIt` is true or `doItReason` is not empty, and
unavailable, with the reason, when only the reason is there.

| Method | What it does |
| --- | --- |
| `ProtocolVersion() → u` | The version, `1` |
| `Start() → a{sv}` | Starts the conversation with the assistant the person chose last time, or resumes the one already going. With none chosen the phase is `choose`, and with the assistant not signed in it becomes `sign-in`: Search offers Expand, which opens the full window to finish it |
| `Ask(s text) → a{sv}` | Asks; the reply's phase is `answering`, and the answer streams through `Changed`. Asked while the assistant is `starting` or at `sign-in`, the question waits, shown in `question`, and is asked as soon as the assistant is ready, as Search asks the moment it opens |
| `Cancel() → a{sv}` | Stops the answer in progress; what was said so far stays. A question still waiting for the assistant is dropped |
| `Act(s action) → a{sv}` | `show-me-how`: the steps to make the asked-for change by hand. `do-it`: the Do it card in the full window, where Do it is unavailable in Milestone 0. `keep`: Keep this, which files the answer in the notebook; `kept` is then true |
| `OpenWindow(s requestToken) → b` | Opens the full window on this conversation, or brings it forward |

| Signal | When |
| --- | --- |
| `Changed(a{sv})` | The conversation changed other than by a call's own reply: an answer as it streams, the assistant starting or signing in, or the person using the card or the full window |
| `WindowShown(s requestToken)` | The full window, asked for by `OpenWindow` with that token, has drawn its first frame |

| Error | When |
| --- | --- |
| `io.github.carlsonjm.SplitRock.Error.NotReady` | `Ask` with no assistant chosen, after it failed, or while an answer is still coming; `show-me-how` before the assistant is ready or with nothing answered |
| `io.github.carlsonjm.SplitRock.Error.NotAvailable` | `keep` with no notebook, and `do-it` on an answer that asks to change nothing |
| `io.github.carlsonjm.SplitRock.Error.Refused` | `keep` on an answer with something in it that is never written down; the message says what, in plain words |
| `org.freedesktop.DBus.Error.InvalidArgs` | An empty question, or an action not listed |

For Expand, Search calls `OpenWindow` with a token of its own and waits for
`WindowShown` with that token: from then the full window, whose application is
`io.github.carlsonjm.SplitRock`, is on screen, and Search can hand the moment
to Kadunce so the window takes the card's place.

Only the person's choice of assistant is written down, in
`split-rockrc` in the person's settings folder, so the next start begins with
the same one. The conversation itself is never saved (`MEMORY.md`).
