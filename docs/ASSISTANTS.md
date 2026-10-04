# Assistants

What each assistant offers Split Rock: how it starts, how a person signs in,
and what it costs. Checked on 3 October 2026 from the Agent Client Protocol
registry and each assistant's own source and documentation. This changes often;
Milestone 0 confirms it live, and setup reads it again on every run.

## Sign-in

The registry accepts two kinds of sign-in:

- **In the browser.** The assistant opens a web page and finishes the sign-in
  itself. Nothing for the person to type but their account.
- **In a terminal.** The program that hosts the assistant opens a terminal where
  the person signs in. Split Rock never shows a terminal, so an assistant that
  signs in only this way is not offered at setup.

| Assistant | Starts as | Sign-in without a terminal | Notes |
| --- | --- | --- | --- |
| Codex | Adapter on Node.js | Yes: ChatGPT in the browser, or an OpenAI key | Whether a free ChatGPT account works is unclear |
| Gemini CLI | Node.js | Yes: Google in the browser, or a Gemini key | The free Google-account tier reportedly ended in June 2026 |
| Claude Agent | Adapter on Node.js 22 or later | Only with an Anthropic key | See the rule below |
| GitHub Copilot | Node.js | No | Its ACP support is a preview |

Several others in the registry ship as programs that need nothing installed
beside them; none is a first candidate.

### What Milestone 0 offers, and the live check

Milestone 0 starts each assistant's pinned adapter and offers only the sign-in
methods that need no terminal and no key held by Split Rock. On 4 October 2026,
before any account was used, each pinned adapter offered this:

| Assistant | Pinned version | Offered | Not offered, and why |
| --- | --- | --- | --- |
| Codex | codex-acp 0.16.0 | Login with ChatGPT, in the browser | Two key methods: the key would have to be handed over by Split Rock |
| Claude Agent | claude-agent-acp 0.85.1, subscription sign-in hidden | Anthropic Console, which the adapter marks as a terminal sign-in; Split Rock runs it without one | Nothing else is advertised |
| Gemini CLI | gemini-cli 0.62.0 | Log in with Google, in the browser | Gemini key, Vertex AI and gateway: each needs a key Split Rock would hold |

Claude's Console sign-in opens the browser, but falls back to asking for a
pasted code when the browser cannot hand the sign-in back. Whether it finishes
without that is the open question the live check answers.

The live check (`SIGN-IN-CHECK.md`) fills in the rest:

| Assistant | Signed in without a terminal | What it costs | Checked |
| --- | --- | --- | --- |
| Codex | Not yet checked live | Not yet checked live | — |
| Claude Agent | Not yet checked live | Not yet checked live | — |
| Gemini CLI | Not yet checked live | Not yet checked live | — |

## Claude

Anthropic does not allow a third-party product to offer sign-in with a Claude
subscription unless Anthropic has approved it, and no approval for ACP clients
other than the editors that co-wrote the adapter is published. Split Rock
starts Claude's adapter with subscription sign-in hidden, so Claude is offered
only with an Anthropic key, billed per use. It is named Claude Agent, never
Claude Code.

## What this means for setup

- Free tiers have mostly ended. Setup should expect the person to need an
  account they pay for, and say so plainly.
- The easiest sign-in for someone new is a browser sign-in to an account they
  already pay for: today, ChatGPT through Codex.
- Asking for a key means a trip to a developer website. It is offered, but not
  first, to someone new.

## The protocol

- The assistant runs its own tools. Split Rock can refuse a request it is asked
  to approve, but not every assistant asks before acting, which is why the
  assistant runs fenced in (`DECISIONS.md`).
- Split Rock hands the assistant its own tools when a conversation starts.
- Official libraries exist for Rust, TypeScript, Python, Kotlin and Java. Only
  community libraries exist for C++. Qt Creator has an ACP client; no KDE or
  GNOME client exists.
