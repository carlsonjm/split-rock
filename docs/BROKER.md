# The broker

The broker hands the models to the Good Input apps that ask for them, starting
with Mail. It is `split-rock-broker`, a program of its own beside Core and Ask
on the session bus. The bus starts it on the first call, and it ends on its own
after ten minutes with nothing asked, which gives the model's memory back.

Apps own what they can do. The broker owns the models and the rules for
using them. An app hands it material, such as one email or one conversation,
and gets back a structured result. The app does every action itself, after the
person's tap wherever one is needed. The one way the broker reaches an app is
through the tools the app describes in its own file (Tools, below), each call
checked against that file.

Ask keeps its own rules on top, because they are written for questions about
the computer. The broker has only the rules that hold for every app.

This is version 0, for testing with Mail and Notes. It may still change. The
tools were added within version 0, since Mail starts only a broker of the
version it knows.

## What holds

- **The model on this computer** sorts and summarises on a form the engine
  enforces. It has no tools and works one request at a time. It is Ask's own
  download, with one copy on the computer for both. Nothing is downloaded
  unless the person taps for it in Ask or Settings, and until then a request
  fails at once with a reason.
- **Codex** drafts and answers only on the person's tap, and only from what
  the app passes it. It runs in the fence Ask's Codex runs in, which holds its
  own folder and an empty working folder and nothing of the person's. Every
  request it makes to act is refused. Each request starts Codex, asks once, and
  stops it, and what Codex wrote about the conversation is removed.
- **What an app passes is material, never instructions.** An email goes to a
  model inside `<email>` marks, and it can't close those marks early. Both
  models are told never to follow instructions inside one.
- **A result is checked before it leaves.** A sorting outside the form's
  values fails. A line is cut to one plain sentence. The clerk's proposals
  are kept only when they are actions the app offers, and only on messages
  the app passed.
- **Every result says who gave it:** `computer` or `codex`.

## On the bus

Name `io.github.carlsonjm.splitrock.Broker`, object `/Broker`, interface
`io.github.carlsonjm.SplitRock.Broker`, version 0.

| Call | What it does |
| --- | --- |
| `ProtocolVersion() → u` | 0 |
| `Model() → a{sv}` | The model, as Ask's `Model()` |
| `Codex() → a{sv}` | `installed`, `signedIn`, and `problem` when it can't start |
| `Warm()` | Starts the model ahead of the first request, where it is downloaded |
| `SortMail(a{sv} message) → u` | Sorts one email with the model on this computer |
| `StyleNote(av sent) → u` | The model on this computer writes how the person writes email, from up to 30 messages they sent, with nothing private in it |
| `DraftReply(a{sv} conversation, s styleNote, s ask) → u` | Codex drafts a reply to one conversation |
| `Clerk(s question, av found) → u` | Codex answers a question from up to twelve emails the app found itself. Each call stands alone: for a follow-up the app passes the earlier turns at the head of `question`, under "Earlier:", and the emails from earlier turns again in `found` |
| `Cancel(u id)` | Stops a request |
| `Tools() → av` | Every tool the apps' files describe, read again, each an `a{sv}` (Tools, below) |
| `Call(s tool, a{sv} args, s caller) → a{sv}` | Calls a tool for `caller` (`ask`, `codex` or the app's own name), after checking it against the app's file |
| `Run(s cardToken) → a{sv}` | Makes the write a card stands for, once, after the person's tap |

An email is a dictionary with these keys: `id`, `from`, `to`, `cc`,
`subject`, `date`, `listId`, `listUnsubscribe`, `mine` (b), `replyToMine` (b),
`attachments` (as, by name) and `body` (plain text, of which the first 4,000
characters are used). A conversation is `id`, `from` (who replies),
`replyAll` (b) and `messages`.

A list of emails, whether `messages`, `found` or `sent`, goes as `av`, each
item an `a{sv}` in a variant: Qt matches a call on its exact signature, so
`Clerk(s, aa{sv})` reaches no method. Inside a dictionary the broker reads an
`aa{sv}` too, but a client should send `av`. Proposals come back the same way.

A trash proposal's `senders` asks for more than the emails Codex read, so
the broker keeps only what the emails passed vouch for. The app counts what
each sender has before asking, moves it only on the person's yes, and never
sweeps anyone the person has written to: that rule is the app's, since only
it knows the mail.

| Signal | When |
| --- | --- |
| `Sorted(u id, a{sv})` | Carries `id` (the email's), `needsYou` (b), `line` (one plain sentence of up to 120 characters), `kind` (receipt, travel, orders, accounts, notice, newsletter, person or other), `suggest` (none, reply, file or unsubscribe) and `who` |
| `Said(u id, s text)` | The next words of a style note, draft or answer |
| `Drafted(u id, a{sv})` | Carries `text`, the reply's body, and `who` |
| `Answered(u id, a{sv})` | A style note or the clerk's answer. Carries `text` and `who`; from the clerk also `read` (the emails Codex was given) and `proposals`, each with `action` (file, done, needs_you, draft or trash), `ids`, `why`, to file `tab`, and to trash perhaps `senders`: addresses or domains meaning everything from them, each kept only when one of the emails passed came from it (a whole shared domain such as gmail.com never) |
| `Failed(u id, s problem)` | Why it couldn't be done, in plain words, or `cancelled` |
| `ModelChanged(a{sv})` | As `Model()` |

Each signal comes after the call has returned its id.

## Tools

Each app describes its tools in one JSON file, installed beside its bus
service (the capabilities contract, version 0). The broker reads
`good-input/capabilities/*.json` in the person's data folder first, then in
each of the system's data folders; the first file for an app counts. It reads
them afresh for every call, so uninstalling an app, which removes its file,
removes its tools at once, even from a card already shown. A file that doesn't
fit the contract is left out whole. Nothing in the broker, Ask or Codex names
an app: what they offer comes from the files.

Notes' file lists `notes.search`, `notes.read` and `notes.create`.

- **Every call is checked against the file** before the app hears of it: a
  tool no file lists, an argument the file doesn't name, the wrong type, text
  longer than its `maxLength`, a number outside `min` and `max`, or text that
  doesn't fit its `pattern` (whose `$` means the very end) is refused with a
  reason in plain words. Arguments go to the app's method in the file's order,
  with defaults filled in.
- **A read runs** and comes back with `tool`, `app`, `appName`, `risk` and the
  app's own answer as `result`, so the client can name what was read.
- **A write never runs from a call.** It comes back as a card: `card` (true),
  `title` (the first words of what it writes, such as a note's), `what`, `tool`,
  `app`, `appName`, `args` (exactly what will be written, and where) and
  `token`. `Run(token)` makes it, once, after the person's tap, within 30
  minutes; the token is the only way to it.
- **An outward tool**, one that sends or publishes, is refused.
- **Codex** may use only a tool whose file marks it `codex`, and only for a
  question the person handed it (below).
- Every reply carries `ok`, and `problem` when it is false. An app's own
  `problem` in its answer counts as a failure.
- **One log line a call**: the tool, the caller and how it went, never what
  was passed or read.

A tool's result goes to a model inside `<tool_result tool="...">` marks it
can't close early, as material, never instructions.

## Codex

Codex runs in the fence, which keeps the desktop's message bus out. Ask Codex
gives it one tool server in the protocol's `mcpServers` slot:
`split-rock-tools-mcp`, in `lib/split-rock`, which runs beside Codex in the
fence and only forwards, through a socket in a folder of its own that the
fence lets it read. Split Rock answers on the other side, outside the fence: it
lists the tools marked for Codex, spelled `notes_search` as tool servers spell
them, with the arguments' shape from the same file, and hands every call to
the broker with `codex` as the caller. A read goes back to Codex as material,
and the window lists each note it read, since its words go to OpenAI. A write
is a Do it row in the window and runs through `Run` only on the tap. Codex's
own request to use the server's tools is allowed; every other request is still
refused.

The broker's own Codex requests, Mail's drafts and the clerk, are given no
tools.
