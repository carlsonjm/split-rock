# The sign-in check

Milestone 0's proof: whether a person can sign in to Codex without ever seeing a
terminal, and what it costs. Codex is the one assistant that needs a sign-in;
Split Rock on this computer needs none (`ASSISTANTS.md`). Signing in to real
accounts is the maintainer's to do, so this is the checklist for it. What it
finds goes into the sign-in table in `ASSISTANTS.md`.

## Before you start

- **What you need on the computer:** Node.js 18 or later with npm, CMake, Qt 6
  and bubblewrap. A machine that builds Shuffle has CMake and Qt.
- **What you need to own:** a paid ChatGPT plan.
- **What it touches:** only the `build/m0` folder in this checkout. The first
  run downloads Codex's adapter there, and Codex keeps its sign-in there.
  Split Rock's Codex never reads or replaces the sign-in of any Codex already
  on the computer. Delete `build/m0` to remove all of it.
- **What it changes:** nothing on the computer, unless you tap Do it on a card,
  which this check never asks you to do.

## Run it

From this checkout:

```
./run.sh
```

With Shuffle installed the window is called Genie; on a desktop without it,
Split Rock.

To rehearse without any account first, run
`./run.sh --pretend tests/pretend-scripts/signed-in.json` and choose
Pretend assistant. Its three suggestions each have a scripted answer, and Make
the writing bigger shows the Do it card.

Split Rock keeps running after its window closes, so Search can reach the
conversation; press Ctrl+C in the terminal to end it before starting it again
with other arguments. It starts with the assistant chosen last time; tap the
assistant's name to choose another.

## Codex

Choose Codex from the list, then note:

1. **The sign-in screen.** Which buttons did it show? Open Not offered here and
   note what is listed there.
2. **Signing in.** Tap the first button.
   - Did a browser window open on its own?
   - Did you have to type or paste anything anywhere other than that browser
     page?
   - Did a terminal window appear at any point?
   - Did Genie's full window open before the browser, and could you get back
     to it when you finished?
3. **What it costs.** What did the sign-in pages say about plans, trials or
   billing? Were you asked for card details?
4. **Ask.** Tap How do I connect to Wi-Fi?. Did an answer arrive as one short
   line and numbered steps? About how long did it take?
5. **A change.** Tap Make the writing bigger.
   - Did the Do it card appear, saying what will change and how to undo it?
   - Tap Not now. Is the writing on screen still the same size? (It must be.)
   - If no card appeared, what did the answer say instead?
6. **Show me how instead.** Tap it on the card. Did you get steps to do it
   yourself?
7. **Anything odd.** Did a line ever say the assistant changed something
   without asking? Did anything else surprise you?

## The window, once, on the tablet

With Codex signed in, and the on-screen keys:

1. **The keys.** Tap the question field so the keys come up. Did the card stay
   clear of them, with the field and the answer still readable? Did it move
   smoothly?
2. **Spread.** Open Spread, then open Genie's card: `./run.sh --ask`, the same
   as Ask in its icon's menu once it is installed. Did the card take Spread's centre,
   with the cards beside it moving aside? Tap a card beside it: did Genie
   close?
3. **Expand.** Tap Expand, in Spread and over an open application. Did the
   card grow to fill the screen above the panel, then become the full window,
   with the conversation on the left and This computer on the right, with no
   flash between them? Did it feel like the same window grown?
4. **Closing.** Tap beside the card, or press Esc. Did it close?

## Report

Copy this and fill it in.

| Assistant | Signed in without a terminal? | Anything typed outside the browser? | What it costs | Answer in steps? | Do it card on a change? | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Codex | | | | | | |

And for the window: clear of the keys? Holds Spread's centre? Expand feels like
the same window, with no flash? Closes on a tap beside it and on Esc?

## Signing out again

Delete `build/m0/sign-ins/codex`, or the whole of `build/m0`.
