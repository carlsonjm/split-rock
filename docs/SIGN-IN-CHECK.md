# The sign-in check

Milestone 0's proof: for each assistant, whether a person can sign in without
ever seeing a terminal, and what it costs. Signing in to real accounts is the
maintainer's to do, so this is the checklist for it. What it finds goes into
the sign-in table in `ASSISTANTS.md`.

## Before you start

- **What you need on the computer:** Node.js 22 or later (Claude Agent needs 22;
  the others need less), CMake and Qt 6. A machine that builds Shuffle has CMake
  and Qt.
- **What you need to own:** a paid ChatGPT plan for Codex, an Anthropic Console
  account for Claude Agent, and a Google account for Gemini CLI. Skip any you
  don't have and say so in the report.
- **What it touches:** only the `build/m0` folder in this checkout. The first
  run downloads the three assistants' programs there, about 600 MB, and each
  assistant keeps its sign-in there. Jack's Claude never reads or replaces the
  sign-in of any Claude Code already on the computer, and the same holds for
  Codex and Gemini. Delete `build/m0` to remove all of it.
- **What it cannot do:** change anything on the computer. Every request to
  change something is refused before you see it.

## Run it

From this checkout:

```
./run.sh
```

To rehearse without any account first, run
`./run.sh --pretend tests/pretend-scripts/signed-in.json` and choose
Pretend assistant. Its three suggestions each have a scripted answer, and Make
the writing bigger shows the Do it card.

## For each assistant: Codex, then Claude Agent, then Gemini CLI

Choose it from the list, then note:

1. **The sign-in screen.** Which buttons did it show? Open Not offered here and
   note what is listed there.
2. **Signing in.** Tap the first button.
   - Did a browser window open on its own?
   - Did you have to type or paste anything anywhere other than that browser
     page? (For Claude Agent: did the page give you a code to paste? If so,
     this sign-in needs a terminal; tap Cancel.)
   - Did a terminal window appear at any point?
   - Did Jack go on to show the field when you finished?
3. **What it costs.** What did the sign-in pages say about plans, trials or
   billing? Were you asked for card details?
4. **Ask.** Tap How do I connect to Wi-Fi?. Did an answer arrive as one short
   line and numbered steps? About how long did it take?
5. **A change.** Tap Make the writing bigger.
   - Did the Do it card appear, with Do it greyed out?
   - Is the writing on screen still the same size? (It must be.)
   - If no card appeared, what did the answer say instead?
6. **Show me how instead.** Tap it on the card. Did you get steps to do it
   yourself?
7. **Anything odd.** Did a line ever say the assistant changed something
   without asking? Did anything else surprise you?

## Report

Copy this and fill it in; one row per assistant.

| Assistant | Signed in without a terminal? | Anything typed outside the browser? | What it costs | Answer in steps? | Do it card on a change? | Notes |
| --- | --- | --- | --- | --- | --- | --- |
| Codex | | | | | | |
| Claude Agent | | | | | | |
| Gemini CLI | | | | | | |

## Signing out again

Delete that assistant's folder in `build/m0/sign-ins` (`codex`, `claude` or
`gemini`), or the whole of `build/m0`.
