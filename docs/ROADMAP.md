# Roadmap

What is planned for Split Rock, in order, and what is not. Each milestone ends
with what it has to prove before the next one starts. Decisions still open are
at the end, each named with what it holds up.

**Where it sits in the suite:** the suite's maintained roadmap holds its order;
inside Shuffle, Split Rock is Genie.

## Milestone 0: the conversation works

A small test program, not yet a widget, that talks to real assistants over the
Agent Client Protocol. Its icon opens the full window, the view of
`mockup/Workspace.dc.html`, as an ordinary window; Ask in its menu opens the
card the size of Shuffle's Search, centred over the work or holding Spread's
centre under Cards, with the answer as `mockup/JustType.dc.html` draws it and
an Expand that hands over to the full window (`DECISIONS.md`). It offers the
conversation to Search on the session bus (`DESKTOP.md`). A
refused change request shows as the Do it card with Do it unavailable. Built in
one pass from the concept, decisions and mock-up, then tested against the proof
(`DECISIONS.md`).

- Connect to real assistants; sign in; ask; get an answer. Built with Codex,
  Gemini CLI and Claude Agent; Codex is the one kept (`DECISIONS.md`, Two
  assistants: on this computer, and Codex).
- Receive a request to change something, and refuse it.
- Search's Genie mode: the conversation, version 1 of its bus interface,
  tested with a client that is not Split Rock.
- A pretend assistant that follows a script, so every later test runs without
  an account, a network or a real computer to change.

**Proves:** the sign-in table in `ASSISTANTS.md`, checked live: whether a person
can sign in to Codex without ever seeing a terminal, and what it costs. And, on
the tablet, that the card keeps clear of the on-screen keys while a question is
typed, that it holds Spread's centre, and that Expand reads as the same window
grown.

Signing in happens in the full window, so the browser a sign-in opens can come
in front of it; the card sits above every window.

**Status:** built. Waiting on the live sign-in check (`SIGN-IN-CHECK.md`).

## Milestone 1: ask and remember, on the maintainer's machine

Nothing on the computer changes yet. The assistant can look and explain.

- A developer install for the maintainer's computer (`install.sh`), so the
  week of use runs from the launcher and Search, not a terminal. It is not
  Milestone 3's package.
- The panel widget: open it, ask in plain words, read the answer. For the
  week of use, Split Rock's icon pinned to the panel (`DECISIONS.md`).
- A background service that holds the conversation, so closing the widget or
  restarting the panel loses nothing. Its first version, which Search's Genie
  mode uses, is built in Milestone 0 (`DESKTOP.md` § The conversation in
  Search); this milestone carries it to the widget.
- The notebook and its memory (`MEMORY.md`): `This computer.md` with the
  hardware and fixed problems, the `Device/` folder that starts from what Split
  Rock reads on the computer and grows with use, `About me.md`, Keep this, and
  the refusal to write down passwords and card numbers.
- Sign-in with ChatGPT through Codex.
- The assistant runs fenced in: it can read what it needs to explain the
  computer, and can write only to the notebook.

**Built so far:** the developer install; the notebook with `ASSISTANT.md`,
`This computer.md` and `Device/DEVICE.md` written at the first start from what
the computer reports; the device's own entries offered by the guide; the
notebook's memory given to the assistant as a conversation starts; Keep this;
"Remember that?"; That fixed it; the refusal to write down passwords, card
numbers, bank details and codes; the fence (`DECISIONS.md`, The fence is
bubblewrap); Codex's ChatGPT sign-in; and the This Device drawer beside the
conversation (`DECISIONS.md`, This Device: one drawer, and the side follows the
card). The panel widget is Split Rock's icon pinned to the panel
(`DECISIONS.md`, No panel widget in Milestone 1).

**Proves:** a week of the maintainer's own daily use, including troubleshooting
an unusual device: a tablet such as the ASUS ROG Flow Z13, with its own power
firmware and kernel quirks. The history lands in `This computer.md`, and what
was learned about the device in its `Device/` folder.

## Milestone 2: changes, with a card

Still on the maintainer's machine.

- The Do it card: what will change, why, how to undo it, and whether it needs
  the password. Do it, Show me how instead, Not now.
- Changes Split Rock knows get a tested, one-tap Undo. Any other change the
  assistant proposes can be made too, on a card that says how it would be
  undone: from a snapshot or copy taken first, or by the way back the assistant
  gave (`DECISIONS.md`).
- Before every change: the undo written to `This computer.md`, the changed
  settings files copied aside, and a snapshot where the disk supports one.
- Administrator rights through KDE's own password prompt, one action at a time.
- Undo in one tap, from Changes in This Device or from the What changed card.
- Settings skills the assistant opens from a short shelf, each showing it the
  setting's value and installed names on this computer: the look, the pointer,
  the fans, startup and services.

**Built:** all of the above. The assistant proposes through the
`split-rock-change` stand-in, and Split Rock makes the change outside the fence
(`DECISIONS.md`, The assistant proposes a change through a stand-in, and Split
Rock makes it).

**Proves:** every known change is made and undone in a test session, and real
changes from the maintainer's daily problems are made and undone on the
maintainer's machine. Building does not wait on Milestone 1's week of use.

## Milestone 3: safe to hand to someone else

- First run: Split Rock opens once after installation and says hello.
- The tour and the troubleshooting guide, in the box and working with no
  account: Split Rock's general Plasma entries in the guide's format
  (`DECISIONS.md`), read alongside any entries a desktop adds and the
  computer's own `Device/` folder.
- Setup in three screens: choose an assistant, choose the notebook folder,
  choose what the assistant may look at.
- Says "I don't know", and offers Send this to someone: a plain summary of the
  problem and what was tried, for the person who helps.
- Scam questions answered with care: "is this email real?" and "a window says my
  computer is infected" get a firm, plain answer and never a link to click.
- An installable package, with what the assistant needs to run installed
  silently.

**Proves:** someone leaving Windows who has never used Linux gets through the
first five minutes alone, watched and not helped.

**Built ahead, in Milestone 0's test program:** the guide reader and the tour.
The guide answers from the general and a desktop's entries with no account,
and Show me around steps through the tours. Where Split Rock on this computer
runs, it answers from the guide itself, and the guide is a choice of its own
only where it can't run (`DECISIONS.md`, Two assistants: on this computer, and
Codex).

## Milestone 4: everyday help

More changes for the card, each with its undo: text size, Wi-Fi, printers and
finding a download.

**Built ahead:** the Update card (`DECISIONS.md`, The Update card updates the
computer), and Free up space, Install an app (and remove one), Restore points,
Health check and Community apps (`DECISIONS.md`, Upkeep has cards of its own;
Apps are removed from the app card). Then Maintenance
and Recovery: the What changed card, where "undo that" goes, the startup and
services skills, and the health check's restart, package and downloads checks
(`DECISIONS.md`, Recovery starts from what changed). Each still needs its first
run on a spare machine.

**Brought into 1.0:** Split Rock on this computer, the assistant that needs no
account (`DECISIONS.md`, Genie on this computer). Built: the front desk, the
model's download and the hand-off to Codex, with the one model the third trial
chose against the trial's questions and both blind sets of unsafe asks. Still
to prove: its first real cards on a freshly installed computer.

**Proves:** the common jobs from real use are covered without Show details ever
being needed.

## Later

- Scenes: "get me ready for work" opens and arranges a saved set of
  applications. Needs a request interface in the window manager; on Shuffle,
  Cards decides and arranges.
- Voice: speaking to the assistant, and answers read aloud.
- Seeing the screen: "what is this window?", with permission each time.
- Languages other than English.
- Gooseberry's notes tool: kept answers and recallable memory become notes
  where Gooseberry is installed, Notes inside Shuffle (`MEMORY.md`).

## Not planned

- Training an AI model of Split Rock's own. Split Rock on this computer runs
  an open model as it is published.
- Handling anyone's account, payment or password.
- Selling an assistant account, or paying for anyone's use.
- Acting without a card.

## Open decisions

The product owner's to make.

- **The consumer name** (holds up a public release). Inside Shuffle it is
  Shuffle Genie, and on its own Split Rock; Genie waits on a registered
  trademark search, since other AI assistants are already called Genie.

Shuffle's own question, whether the assistant is in the bundle and is the reason
to buy it, is Shuffle's and is recorded there.
