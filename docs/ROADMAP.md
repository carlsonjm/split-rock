# Roadmap

What is planned for Split Rock, in order, and what is not. Each milestone ends
with what it has to prove before the next one starts. Decisions still open are
at the end, each named with the milestone it holds up.

**Where it sits in the suite:** not scheduled. Shuffle's own 1.0 work comes
first; Split Rock starts when the maintainer adds it to the suite record.

## Milestone 0: the conversation works

A small test program, not yet a widget, that talks to real assistants over the
Agent Client Protocol.

- Connect to Claude Agent, Gemini CLI and Codex; sign in; ask; get an answer.
- Receive a request to change something, and refuse it.
- A pretend assistant that follows a script, so every later test runs without
  an account, a network or a real computer to change.

**Proves:** for each assistant, whether a person can sign in without ever seeing
a terminal. That table decides what setup can offer.

## Milestone 1: ask and remember, on the maintainer's machine

Nothing on the computer changes yet. The assistant can look and explain.

- The panel widget: open it, ask in plain words, read the answer.
- A background service that holds the conversation, so closing the widget or
  restarting the panel loses nothing.
- The notebook: a chosen folder, shown in the widget's own Notebook tab.
  `This computer.md` starts with the hardware.
- The assistant runs fenced in: it can read what it needs to explain the
  computer, and can write only to the notebook.

**Proves:** a week of the maintainer's own daily use, including real hardware
troubleshooting, with the history landing in `This computer.md`.

## Milestone 2: changes, with a card

Still on the maintainer's machine.

- The Do it card: what will change, why, how to undo it, and whether it needs
  the password. Do it, Show me how instead, Not now.
- The first set of changes the assistant may propose, each with a tested undo.
  What that set is waits on an open decision.
- Before every change: the undo written to `This computer.md`, the changed
  settings files copied aside, and a snapshot where the disk supports one.
- Administrator rights through KDE's own password prompt, one action at a time.
- Undo from the notebook: tap a past change, then Undo, which gets its own card.

**Proves:** every change in the set is made and undone in a test session, and
one real change is undone on the maintainer's machine.

## Milestone 3: safe to hand to someone else

- First run: Split Rock opens once after installation and says hello.
- The built-in guide to the desktop, which works with no account.
- Setup in three screens: choose an assistant, choose the notebook folder,
  choose what the assistant may look at.
- Says "I don't know", and offers Send this to someone: a plain summary of the
  problem and what was tried, for the person who helps.
- Scam questions answered with care: "is this email real?" and "a window says my
  computer is infected" get a firm, plain answer and never a link to click.
- An installable package, with what the assistant needs to run installed
  silently.

**Proves:** someone who has never used Linux gets through the first five minutes
alone, watched and not helped.

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

## Not planned

- An AI model of Split Rock's own.
- Handling anyone's account, payment or password.
- Acting without a card.

## Open decisions

The maintainer's to make. Each names what it holds up.

1. **What Do it may do** (holds up Milestone 2). Only a fixed set of changes
   that Split Rock knows how to undo, or anything the assistant proposes, with a
   snapshot as the safety net.
2. **Which assistant setup offers first** (holds up Milestone 3). Decided from
   Milestone 0's sign-in table and each provider's rules on sign-in.
3. **Whether conversations are kept** (holds up Milestone 1). Forgotten when the
   window closes, with Keep this to file an answer; or every conversation saved
   to the notebook.
4. **Whether anything small may change without a card** (holds up
   Milestone 2), such as text size.
5. **The consumer name** (holds up a public release). Inside Shuffle it is the
   workspace assistant for now.

Shuffle's own question, whether the assistant is in the bundle and is the reason
to buy it, is Shuffle's and is recorded there.
