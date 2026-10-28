# The broker

The broker hands the models to the Good Input apps that ask for them, starting
with Mail. It is `split-rock-broker`, a program of its own beside Core and Ask
on the session bus. The bus starts it on the first call, and it ends on its own
after ten minutes with nothing asked, which gives the model's memory back.

Apps own what they can do. The broker owns the models and the rules for
using them. An app hands it material, such as one email or one conversation,
and gets back a structured result. The broker reaches no app, no mail server
and no file of the person's. The app does every action itself, after the
person's tap wherever one is needed.

Ask keeps its own rules on top, because they are written for questions about
the computer. The broker has only the rules that hold for every app.

This is version 0, for testing with Mail. It may still change.

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
