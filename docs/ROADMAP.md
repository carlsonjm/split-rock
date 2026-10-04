# Roadmap

What is planned for Split Rock, in order, and what is not. Each milestone ends
with what it has to prove before the next one starts. Decisions still open are
at the end, each named with what it holds up.

**Where it sits in the suite:** scheduled in the suite record, next after
Shuffle's current 1.0 work, starting with Milestone 0.

## Milestone 0: the conversation works

A small test program, not yet a widget, that talks to real assistants over the
Agent Client Protocol. Its window is the Just Type sheet in `mockup/`, and a
refused change request shows as the Do it card with Do it unavailable. Built in
one pass from the concept, decisions and mock-up, then tested against the proof
(`DECISIONS.md`).

- Connect to Codex, Gemini CLI and Claude Agent; sign in; ask; get an answer.
- Receive a request to change something, and refuse it.
- A pretend assistant that follows a script, so every later test runs without
  an account, a network or a real computer to change.

**Proves:** the sign-in table in `ASSISTANTS.md`, checked live: for each
assistant, whether a person can sign in without ever seeing a terminal, and
what it costs.

## Milestone 1: ask and remember, on the maintainer's machine

Nothing on the computer changes yet. The assistant can look and explain.

- The panel widget: open it, ask in plain words, read the answer.
- A background service that holds the conversation, so closing the widget or
  restarting the panel loses nothing.
- The notebook and its memory (`MEMORY.md`): `This computer.md` with the
  hardware and fixed problems, `About me.md`, Keep this, Forget, and the
  refusal to write down passwords and card numbers.
- Sign-in with ChatGPT through Codex, and with an Anthropic key for Claude.
- The assistant runs fenced in: it can read what it needs to explain the
  computer, and can write only to the notebook.

**Proves:** a week of the maintainer's own daily use, including troubleshooting
an unusual device: a tablet such as the ASUS ROG Flow Z13, with its own power
firmware and kernel quirks. The history lands in `This computer.md`.

## Milestone 2: changes, with a card

Still on the maintainer's machine.

- The Do it card: what will change, why, how to undo it, and whether it needs
  the password. Do it, Show me how instead, Not now.
- A fixed set of changes the assistant may propose, each with a tested undo.
  Anything outside the set gets Show me how instead of Do it.
- Before every change: the undo written to `This computer.md`, the changed
  settings files copied aside, and a snapshot where the disk supports one.
- Administrator rights through KDE's own password prompt, one action at a time.
- Undo from the notebook: tap a past change, then Undo, which gets its own card.

**Proves:** every change in the set is made and undone in a test session, and
one real change is undone on the maintainer's machine.

## Milestone 3: safe to hand to someone else

- First run: Split Rock opens once after installation and says hello.
- The tour and the troubleshooting guide, in the box and working with no
  account: Split Rock's general Plasma entries in the guide's format
  (`DECISIONS.md`), read alongside any entries a desktop adds.
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

## Milestone 4: everyday help

More changes for the card, each with its undo: text size, Wi-Fi, printers,
updates with a snapshot first, finding a download, installing an app.

**Proves:** the common jobs from real use are covered without Show details ever
being needed.

## Later

- Scenes: "get me ready for work" opens and arranges a saved set of
  applications. Needs a request interface in the window manager; on Shuffle,
  Kadunce decides and arranges.
- Voice: speaking to the assistant, and answers read aloud.
- Seeing the screen: "what is this window?", with permission each time.
- Languages other than English.
- Gooseberry's notes tool: kept answers and recallable memory become notes
  where Gooseberry is installed (`MEMORY.md`).

## Not planned

- An AI model of Split Rock's own.
- Handling anyone's account, payment or password.
- Selling an assistant account, or paying for anyone's use.
- Acting without a card.

## Open decisions

The maintainer's to make.

- **The consumer name** (holds up a public release). The working name is Jack,
  pending a registered trademark search.
- **Who writes a device's guide entries** (holds up the device entries in
  Milestone 3's guide). Split Rock owns the general Plasma entries and a
  desktop adds its own; neither ruling says whose entries for a particular
  device, such as the Z13, are.

Shuffle's own question, whether the assistant is in the bundle and is the reason
to buy it, is Shuffle's and is recorded there.
