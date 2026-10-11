# Assistants

Split Rock offers two assistants: Split Rock on this computer (Genie on this
computer inside Shuffle), which needs no account, and Codex, for bigger fixes
with a paid ChatGPT plan. They are the only two tested to ship
(`DECISIONS.md`, Two assistants: on this computer, and Codex). This page says
how each starts, how a person signs in, and what it costs, and keeps what was
learned about assistants that are not offered.

## Split Rock on this computer

Split Rock's own assistant. It needs no account and no program of the
person's, and nothing the person asks leaves the computer.

- **The model:** Qwen3 4B, by the Qwen team, an open model under the Apache 2.0
  licence. It is the same for every computer, about 2.5 GB, and downloaded once,
  only after the person taps Download. The download can be paused and picks up
  where it stopped; it is checked against the exact size and SHA-256 in
  `data/models.json`, and deleted if it doesn't match. The download card
  credits the model and names its licence, whose text installs beside the
  model list.
- **What runs it:** llama.cpp's `llama-server`, inside the same fence as every
  assistant, with the graphics chip let through so it runs faster where one
  can. Shuffle installs its own build of llama.cpp; on any other desktop
  Split Rock uses a `llama-server` already installed, and says so when there
  is none.
- **The front desk:** `split-rock-desk` speaks the Agent Client Protocol to
  Split Rock like any assistant, and stands in front of the model. A fixed
  safety check answers scams, remote access, passwords, money and the like
  with set words before the model sees the ask; then the model sorts the ask.
  A question is answered word by word, and from the guide, as written, where
  an entry fits. A change opens a settings skill on a Do it card or one of the
  upkeep cards, checked against the skill's own commands and the ask before
  it is shown.
- **Bigger fixes:** anything no skill or card covers ends with Ask Codex, which
  passes the question to Codex. Without Codex, it says that Codex, with a
  ChatGPT account, can make that kind of change, and can be added from the
  list of assistants.
- **Memory:** it reads the notebook like every assistant, but never offers to
  remember something about the person.

## Codex

OpenAI's agent, through its Agent Client Protocol adapter, `codex-acp` 0.16.0,
pinned in `tools/fetch-assistants.sh`. It runs on Node.js 18 or later.

- **Sign-in:** Login with ChatGPT, in the browser. Codex's two key sign-ins
  are not offered, since Split Rock would have to hold the key.
- **What it costs:** a paid ChatGPT plan, billed to the person by OpenAI.
  Whether a free ChatGPT account works is unclear.
- **What it does here:** answers anything, troubleshoots what the guide does
  not cover, and proposes any change on a Do it card, or opens an upkeep card.
- **Its own copy:** Codex keeps its sign-in in a folder of Split Rock's own,
  never the person's own Codex, and its record of each conversation is removed
  when the conversation ends.

The live sign-in check (`SIGN-IN-CHECK.md`) fills in the rest:

| Assistant | Signed in without a terminal | What it costs | Checked |
| --- | --- | --- | --- |
| Codex | Not yet checked live | Not yet checked live | — |

## Sign-in

The Agent Client Protocol registry accepts two kinds of sign-in:

- **In the browser.** The assistant opens a web page and finishes the sign-in
  itself. Nothing for the person to type but their account.
- **In a terminal.** The program that hosts the assistant opens a terminal where
  the person signs in. Split Rock never shows a terminal, so an assistant that
  signs in only this way is not offered.

Split Rock offers only the sign-in methods that need no terminal and no key
held by Split Rock, and reads what each assistant offers every time it starts
it, since this changes often.

## What this means for setup

- Free tiers have mostly ended. A person who wants the bigger fixes needs an
  account they pay for, and setup says so plainly. Split Rock on this computer
  is the one assistant that needs none.
- The easiest sign-in for someone new is a browser sign-in to an account they
  already pay for: ChatGPT, through Codex.
- A key sign-in is never offered: it means a trip to a developer website, and
  Split Rock would have to hold the key.

## Assistants not offered

What was learned about other assistants, from the registry and each
assistant's own source and documentation in early October 2026, kept for when
other paid assistants are looked at again. None of them is offered.

| Assistant | Starts as | Sign-in without a terminal | Notes |
| --- | --- | --- | --- |
| Gemini CLI | Node.js | Yes: Google in the browser, or a Gemini key | The free Google-account tier reportedly ended in June 2026 |
| Claude Agent | Adapter on Node.js 22 or later | Only with an Anthropic key | See below |
| GitHub Copilot | Node.js | No | Its ACP support is a preview |

Several others in the registry ship as programs that need nothing installed
beside them; none is a first candidate.

Claude Agent (`claude-agent-acp` 0.85.1) and Gemini CLI (`gemini-cli` 0.62.0)
were built into Milestone 0 and checked before any account was used. Claude
Agent offered only its Anthropic Console sign-in, which the adapter marks as a
terminal sign-in and which opens the browser but falls back to asking for a
pasted code when the browser cannot hand the sign-in back. Gemini CLI offered
Log in with Google, in the browser; its key, Vertex AI and gateway sign-ins
each need a key Split Rock would hold.

Anthropic does not allow a third-party product to offer sign-in with a Claude
subscription unless Anthropic has approved it, and no approval for ACP clients
other than the editors that co-wrote the adapter is published. Should Claude be
offered again, it starts with subscription sign-in hidden, so it is offered
only with an Anthropic key, billed per use, and is named Claude Agent, never
Claude Code.

## The protocol

- The assistant runs its own tools. Split Rock can refuse a request it is asked
  to approve, but not every assistant asks before acting, which is why the
  assistant runs fenced in (`DECISIONS.md`).
- Split Rock hands the assistant its own tools as programs inside the fence:
  `split-rock-change` to propose a change, `split-rock-skill` to open a
  settings skill, and a proposal can name an upkeep card instead of a command.
- Official libraries exist for Rust, TypeScript, Python, Kotlin and Java. Only
  community libraries exist for C++. Qt Creator has an ACP client; no KDE or
  GNOME client exists.
