# Ask

Ask answers plain questions on the computer itself: "my Wi-Fi keeps
dropping", "how do I take a screenshot", "the fan is so loud". It is a
program of its own, `split-rock-ask`, beside Core on the session bus. The bus
starts it on the first question, and it ends on its own after ten minutes
with nothing to answer, which gives the model's memory back. Search shows
what it says; the Helpers and the Do it card still do the work, after the
person's yes.

Core never calls a model (`CORE.md`). Ask is where the model lives.

## What holds

- Nothing the person asks leaves the computer. The model is Split Rock's own
  download, run by llama.cpp's `llama-server`.
- It changes nothing. An answer is words, the Helpers that fit, and at most
  one Do it card from a settings skill, which waits for the person's tap.
- Every answer says who gave it: `who` is `computer` for every answer Ask
  gives. Codex answers only in its own window, Ask Codex, so the two are
  never mixed.
- Each question is asked afresh, with no earlier question in it, as Search
  asks one at a time.
- `split-rock-corerc` switches Ask off: `[Ask] Enabled=false`. Off, it answers
  only that it is off.

## The front desk

The desk decides, not the model alone (`DECISIONS.md`, Ask on this computer):

1. A fixed safety check (`src/desk/Gate.cpp`) answers scams, remote access,
   passwords, money, wiping, piracy, spying, blocked updates, scareware and
   switching protection off with set words. It needs no model, so it answers
   even before the model is downloaded.
2. The model sorts the question: an answer, a change and the skill or Helper
   that covers it, or an unsafe ask and its kind.
3. An answer is written in words, with no tools, and sent as it is written. A
   claim that something was changed, or advice to use a terminal, is put right
   beneath it.
4. For a settings skill (`skills/plasma/`), the desk opens it, runs its
   read-only look-around, and the model fills in the Do it card. The desk
   checks the card against the skill's own commands and the names found on
   this computer, and asks the model once more whether it does what was
   asked, before the card is offered.
5. A Helper is named by its id, and goes under the answer.
6. Anything else that changes the computer is a bigger change. Once Codex
   is signed in, the answer says so and `handOff` is set, and Search offers
   Ask Codex; until then, it says where to sign in.

## The model and its download

- `data/models.json` lists the model: Qwen3 4B, about 2.5 GB, the same for
  every computer, with its exact size and SHA-256. A model without them is
  never downloaded, and a download that doesn't match is deleted. Its licence,
  Apache 2.0, installs beside the list.
- It downloads only when a surface asks with `DownloadModel`, after the
  person's tap, into `split-rock/models` in the person's data folder. A
  download that stops carries on where it stopped.
- `RemoveModel` deletes it again.
- `llama-server` runs inside a fence (`src/ask/Fence.cpp`): the system
  read-only, the home folder hidden except the model, no message bus, no
  display, and the graphics chip let through. It listens on a free local
  port with a key only Ask knows.
- Ask finds `llama-server` in Shuffle's own engine folder first
  (`lib/split-rock/engine`), then wherever the system installed it.

## Codex

The bigger fixes: OpenAI's Codex, with the person's own ChatGPT plan, through
its Agent Client Protocol adapter, `codex-acp` (`DECISIONS.md`, Codex comes
back in a window of its own). Shuffle installs the adapter in
`lib/split-rock/codex`; elsewhere Split Rock uses one on the PATH, and says
Codex isn't installed when there is none.

- **When:** only on the person's tap on Ask Codex, under an answer, or
  straight from an answer that is a bigger change once the person switches
  on Hand bigger questions to Codex (`split-rock-corerc`,
  `[Ask] HandToCodex=true`). `Answered` then carries `handedToCodex`.
- **Where:** `split-rock --codex --question TEXT` opens Ask Codex, a window
  drawn like a Helper with one conversation in it, under Codex's name. It
  ends when the window closes, and what Codex wrote about it is removed.
  Keep as a note keeps it first: in Notes, in its Ask folder, where Gooseberry
  is installed, and in the notebook otherwise.
- **Sign-in:** Codex's own, Login with ChatGPT, through its page in the
  browser. A way that needs a key or a terminal is never offered.
  `split-rock --codex` with no question only signs in, as Settings asks.
  Codex keeps its sign-in in a folder of its own, `split-rock/codex` in the
  person's data folder, never the person's own Codex; `SignOutCodex` deletes
  it.
- **Changes:** Codex runs in read-only mode, and every request it makes to
  act is refused. A change it proposes with `split-rock-change` goes on a
  Do it row; Split Rock makes it outside the fence, as the person and never
  as the administrator, written in the notebook with its way back first, and
  tells Codex how it went. Upkeep goes to the Helpers instead.
- **The fence:** the one `llama-server` runs in. Codex reads the desktop's
  own settings, and writes only its own folder and a working folder of its
  own; it keeps the network to reach OpenAI and to finish its sign-in.

## On the bus

Name `io.github.carlsonjm.splitrock.Ask`, object `/Ask`, interface
`io.github.carlsonjm.SplitRock.Ask`, version 1.

| Call | What it does |
| --- | --- |
| `ProtocolVersion() → u` | 1 |
| `Enabled() → b` | Whether Ask is switched on |
| `Ask(s question) → u` | Starts answering and returns the question's id. A new question stops the one before it |
| `Cancel(u id)` | Stops a question |
| `Model() → a{sv}` | `state` (missing, downloading, verifying, ready, failed), `progress` from 0 to 1, the model's `name`, `size` and `credit` in words, `problem` |
| `DownloadModel()` | Downloads the model, after the person's tap |
| `CancelDownload()` | Stops the download; it carries on from there next time |
| `RemoveModel()` | Deletes the model from this computer |
| `Codex() → a{sv}` | Codex: `installed`, `signedIn`, `handOff` (the switch in Settings), and `problem` when it can't start |
| `OpenCodex(s question) → b` | Opens Ask Codex with the question; with none, it only signs in |
| `SignOutCodex() → b` | Forgets Codex's sign-in |

| Signal | When |
| --- | --- |
| `Said(u id, s text)` | The next words of the answer, as they are written |
| `Answered(u id, a{sv} result)` | The question is done |
| `ModelChanged(a{sv} model)` | The download moved on, as `Model()` |
| `CodexChanged(a{sv} codex)` | Codex signed in or out, as `Codex()` |

`Answered` carries:

| Key | Meaning |
| --- | --- |
| `who` | `computer` |
| `text` | Every word said |
| `routed` | How the desk handled it: `answer`, `gate:<kind>`, `unsafe:<kind>`, `card:<helper>`, `skill:<id>`, `handoff: <why>` |
| `helpers` | The Helpers that fit, by Core's ids (`space`, `install`, …), for `OpenAction` |
| `query` | What Install an app looks for |
| `doIt` | A Do it card from a settings skill, as JSON, or empty |
| `handOff` | A bigger change, which Codex, signed in, can take |
| `handedToCodex` | It went straight on to Codex, as the person switched on |
| `needsModel` | The model isn't downloaded yet; `text` says what it needs |
| `off` | Ask is switched off |
| `cancelled` | A newer question, or `Cancel`, stopped it |
| `problem` | The model couldn't start, and why |

## The trial

`tools/desk-trial/` asks the front desk the trial's 144 questions against the
real model, on a computer that has it: `split-rock-ask --framing` gives the
words the model sorts with, and `split-rock-ask --trial` answers each
question with the skills' look-around taken from a pretend computer, so
nothing on the computer is read or changed.
