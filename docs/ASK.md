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
  one Do it card, from a settings skill or an app's tool, which waits for the
  person's tap.
- Every answer says who gave it: `who` is `computer` for every answer Ask
  gives. Codex answers only in its own window, Ask Codex, so the two are
  never mixed.
- Each question is asked afresh, with no earlier question in it, as Search
  asks one at a time. Only the last answer in words is kept, for "save this
  answer to my notes" to keep.
- `split-rock-corerc` switches Ask off: `[Ask] Enabled=false`. Off, it answers
  only that it is off.
- Codex only, the person's choice: `[Ask] CodexOnly=true`, set through
  `SetCodexOnly`. Where Codex is installed, the model is then never asked or
  offered: after the safety check, an ask ends with `codexOnly`, its words say
  it waits for Ask Codex, and the Helper its words name is listed. Nothing
  goes to Codex until the person taps Ask Codex.

## The front desk

The desk decides, not the model alone (`DECISIONS.md`, Ask on this computer,
and A question about this computer is answered, not refused):

1. A plain question about this computer that Ask can read itself is answered
   from what it read, with no model, even before the model is downloaded:
   "what is my IP address" gives this computer's addresses on the networks
   it is on now, read through Qt (IPv4 first, never the computer talking to
   itself, a container's or a VPN's network, or an address only the cable
   uses), says that websites see the router's address instead, and names the
   computer's network name; "what is this computer called" gives that name.
   An ask that tells of someone else wanting the address, names another
   device, or wants it changed is not one of these. The same name and
   addresses, the hardware the notebook reads, and the system, go to the
   model with each answer. `routed` is `computer`.
   Something the person wrote down, such as "where's my golden key?" or
   "what is my email password?", is looked for in Notes first, where Notes
   describes its tools: a note that mentions it is named, with its folder,
   and its words are never said; with none found, the ask goes on. An ask
   the safety check counts as harm is never looked for. `routed` is
   `whereabouts`.
2. A fixed safety check (`src/desk/Gate.cpp`) answers scams, remote access,
   passwords, money, wiping, piracy, spying, blocked updates, scareware and
   switching protection off with set words. It needs no model, so it answers
   even before the model is downloaded. Once the model is here, an ask it
   catches only as a password of the person's own, rather than harm, waits
   for the look in Notes above, and is checked again after it. So does an ask with nothing to answer
   yet, such as "help" or a row of keys, and a plain "how do I" ask whose
   answer is a Helper: "how do I update my computer", "how do I uninstall an
   app", "how do I free up space" open that Helper.
3. The model sorts the question: an answer, a change and the skill or Helper
   that covers it, or an unsafe ask and its kind. Unsafe is only real harm:
   a scam or a code read out to a caller, a stranger taking control, payment
   or bank codes, deleting the system, pirated software, watching someone
   secretly, switching protection off. Its words tell the model that a
   question about this computer or the person's own things, such as its
   address, name, hardware, Wi-Fi or disk, or where they wrote a password or
   a key down, is never unsafe. An unsafe sort that names no kind of harm, or
   only a password of the person's own, is answered in words; the safety
   check above still catches asking Ask to keep a password or switch the
   password prompt off.
4. An answer is written in words, with no tools, and sent as it is written. A
   claim that something was changed, advice to use a terminal, or a claim
   about what it can't see, such as "your computer is up to date", is put
   right beneath it. The Helper its words plainly need goes under it, and the
   desk's own last line names it and what it does. It names only the places Ask describes to the model:
   Shuffle's own where Shuffle is installed, and System Settings' real pages.
   Words that go round in circles are cut where the circling began, and
   never shown. After the first line, a line shows once it is whole, and a
   line that ends on what can't be done, promises a card, or names a taskbar
   or a Search bar at the top is left out; the steps that stay are numbered
   again.
5. For a settings skill (`skills/plasma/`), the desk opens it, runs its
   read-only look-around, and the model fills in the Do it card. The desk
   checks the card against the skill's own commands and the names found on
   this computer, and asks the model once more whether it does what was
   asked, before the card is offered. A look-around line that starts
   `No card:` says there is nothing to change, such as no failed service, and
   the ask is answered in words without a card being asked for. A card that fails either is never
   shown; the ask is answered in words instead.
6. A Helper is named by its id, and goes under the answer.
7. Where the person's apps describe tools (`BROKER.md`, Tools), the model
   may sort an ask as `tools`: something to find, read or keep in one of
   their own apps, such as Notes. An ask that names one of those apps goes
   there too, unless a skill or Helper covers it. The model then fills one
   tool request on a form the engine enforces, which allows only the tools
   the apps' files list, each with the arguments its file allows, or none.
   The desk runs it through the broker, which checks it again. After a read
   the model may ask for one more read, never a write; then it answers from
   what came back, set apart in `<tool_result>` marks a note can't close
   early, and the desk's own last line names each note read and that it was
   read on this computer. A write is a Do it card titled with its first
   words, such as "Save this answer to my notes", with the answer put in
   whole by the desk; nothing is written until the person taps it. A tool
   request that picks none is answered in words.
8. Anything else that changes the computer is a bigger change. Once Codex
   is signed in, the answer says so and `handOff` is set, and Search offers
   Ask Codex; until then, it is answered in words, and nobody is sent to
   sign in.

## The model and its download

- `data/models.json` lists the model: Qwen3 4B, about 2.5 GB, the same for
  every computer, with its exact size and SHA-256. A model without them is
  never downloaded, and a download that doesn't match is deleted. Its licence,
  Apache 2.0, installs beside the list.
- Until it is downloaded, an ask ends with `needsModel`, and its words name
  the model, say it runs on this computer and that nothing asked leaves it,
  and give its size; never a time, which depends on the connection. Where
  `llama-server` isn't found, it also carries `needsEngine`, and its words say
  llama.cpp must be installed first; the download isn't offered until it is.
- It downloads only when a surface asks with `DownloadModel`, after the
  person's tap, into `split-rock/models` in the person's data folder. A
  download that stops carries on where it stopped.
- `RemoveModel` deletes it again.
- The model runs on a graphics chip with room for it, and on the processor
  otherwise. Intel's older built-in graphics (HD, UHD and Iris) are never
  used: llama.cpp's Vulkan on them mixed questions together and doubled
  words, where the processor answered cleanly. On the processor, nothing is
  handed to the chip at all.
- Until it is downloaded, and while it downloads, the Helper an ask's words
  name still goes with the answer, so Search keeps it listed.
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

The adapter is `@agentclientprotocol/codex-acp`, which replaced Zed's
codex-acp; Zed's last release, 0.16.0, carried a Codex too old for ChatGPT
accounts, which turned every question away. Its ChatGPT sign-in is called
`chat-gpt` (Zed's was `chatgpt`); the API-key, gateway and device-code ways
in stay hidden. When Codex fails, the window shows the service's own words
from under the protocol's "Internal error", not the bare code.

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
- **The person's apps:** Codex is given the apps' tools marked for it, for
  the question the person handed it, through a tool server that only
  forwards out of the fence (`BROKER.md`, Codex). The window lists each note
  Codex read, since its words went to OpenAI, and a write it asks for is a
  Do it row.
- **Changes:** Codex runs in read-only mode, and every request it makes to
  act is refused. A change it proposes with `split-rock-change` goes on a
  Do it row; Split Rock makes it outside the fence, as the person and never
  as the administrator, written in the notebook with its way back first, and
  tells Codex how it went. Upkeep goes to the Helpers instead.
- **The fence:** the one `llama-server` runs in. Codex reads the desktop's
  own settings, and writes only its own folder and a working folder of its
  own; it keeps the network to reach OpenAI and to finish its sign-in.

## Split Rock Settings

`split-rock --settings` opens Split Rock Settings, the simplest settings page,
for a computer with or without Shuffle; the app launcher lists it by that
name. Four things, each a row: Ask in Search, on or off (`[Ask] Enabled`); who
answers, the model with Codex for bigger fixes, the default, or Codex only
(`SetCodexOnly`); the model, with Download, Stop or Remove; and Codex's
sign-in, through Codex's own window. Opening it changes nothing. Shuffle's own
Settings has the same choices on its Ask Your Computer page.

## On the bus

Name `io.github.carlsonjm.splitrock.Ask`, object `/Ask`, interface
`io.github.carlsonjm.SplitRock.Ask`, version 1.

| Call | What it does |
| --- | --- |
| `ProtocolVersion() → u` | 1 |
| `Enabled() → b` | Whether Ask is switched on |
| `Ask(s question) → u` | Starts answering and returns the question's id. A new question stops the one before it |
| `Cancel(u id)` | Stops a question |
| `Model() → a{sv}` | `state` (missing, downloading, verifying, ready, failed), `progress` from 0 to 1, the model's `name`, `size` and `credit` in words, `problem`, and `engine`, whether `llama-server` is found |
| `DownloadModel()` | Downloads the model, after the person's tap |
| `CancelDownload()` | Stops the download; it carries on from there next time |
| `RemoveModel()` | Deletes the model from this computer |
| `Codex() → a{sv}` | Codex: `installed`, `signedIn`, `handOff` (the switch in Settings), `only` (Codex only), and `problem` when it can't start |
| `OpenCodex(s question) → b` | Opens Ask Codex with the question; with none, it only signs in |
| `SignOutCodex() → b` | Forgets Codex's sign-in |
| `SetCodexOnly(b only) → b` | The person's choice: Codex only, or the model on this computer. False for Codex only where Codex isn't installed |

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
| `routed` | How the desk handled it: `computer`, `whereabouts`, `answer`, `gate:<kind>`, `unsafe:<kind>`, `card:<helper>`, `skill:<id>`, `tools:<tool>`, `tools: answered from what was read`, `handoff: <why>` |
| `helpers` | The Helpers that fit, by Core's ids (`space`, `install`, …), for `OpenAction` |
| `query` | What Install an app looks for |
| `doIt` | A Do it card, as JSON, or empty: from a settings skill, with its `command`; or from an app's tool, with `tool`, `app`, `args` and the `token` the surface hands to the broker's `Run` after the tap |
| `read` | What the apps' tools read for the answer, by name, such as each note's title |
| `open` | The notes a `whereabouts` answer names, as a JSON array of `id` and `title`, or empty; Search offers each as one tap that opens it in Notes. Never their words |
| `handOff` | A bigger change, which Codex, signed in, can take |
| `handedToCodex` | It went straight on to Codex, as the person switched on |
| `needsModel` | The model isn't downloaded yet; `text` says what it needs |
| `needsEngine` | With `needsModel`: `llama-server` isn't installed either |
| `codexOnly` | Codex only: the question waits for Ask Codex |
| `off` | Ask is switched off |
| `cancelled` | A newer question, or `Cancel`, stopped it |
| `problem` | The model couldn't start, and why |

## The trial

`tools/desk-trial/` asks the front desk the trial's 144 questions against the
real model, on a computer that has it: `split-rock-ask --framing` gives the
words the model sorts with, and `split-rock-ask --trial` answers each
question with the skills' look-around taken from a pretend computer, so
nothing on the computer is read or changed.
