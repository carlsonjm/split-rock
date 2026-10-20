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
  gives. Codex answers only in its own window, so the two are never mixed.
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
6. Anything else that changes the computer is a bigger change: the answer
   says Codex can take it.

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

| Signal | When |
| --- | --- |
| `Said(u id, s text)` | The next words of the answer, as they are written |
| `Answered(u id, a{sv} result)` | The question is done |
| `ModelChanged(a{sv} model)` | The download moved on, as `Model()` |

`Answered` carries:

| Key | Meaning |
| --- | --- |
| `who` | `computer` |
| `text` | Every word said |
| `routed` | How the desk handled it: `answer`, `gate:<kind>`, `unsafe:<kind>`, `card:<helper>`, `skill:<id>`, `handoff: <why>` |
| `helpers` | The Helpers that fit, by Core's ids (`space`, `install`, …), for `OpenAction` |
| `query` | What Install an app looks for |
| `doIt` | A Do it card from a settings skill, as JSON, or empty |
| `handOff` | Codex can take it |
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
