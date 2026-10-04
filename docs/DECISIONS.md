# Decisions

Each settled ruling: the rule, why it holds, and what was rejected.

## Split Rock is an Agent Client Protocol client

Split Rock talks to the assistant through the Agent Client Protocol, so any
agent that speaks it can be the assistant, and the person chooses.

Rejected: driving one assistant's command-line program directly, which ties the
product to a single assistant; and calling a model's API directly, which bills
per use and would make Split Rock responsible for accounts and payment.

## Each agent owns its sign-in and billing

Split Rock never offers an assistant's sign-in itself, never handles an account,
and never pays for anyone's use. The agent signs the person in through its own
window, and Split Rock shows whatever sign-in methods that agent supports.

Rejected: Split Rock managing accounts or keys for the person. Some providers
also do not allow a third-party product to offer their consumer sign-in.

## The notebook is a plain folder

Memory is an ordinary folder of plain notes, which the assistant reads and
writes as files and Split Rock displays itself.

Rejected: requiring a notes app, which is a setup step the person this is for
will not complete; and database-backed or online note apps, which the assistant
cannot read as files.

## Nothing changes without a card

Every change to the computer is proposed on a card and made only after the
person approves it.

Rejected: acting automatically for convenience. One unwanted change costs more
trust than any number of saved taps returns.

## Every system change is recorded with its undo

Before a change, Split Rock writes what it will change and how to reverse it to
`This computer.md`, and takes a snapshot where the disk supports one.

Rejected: relying on the agent to remember. A record outlives the conversation
and makes the next problem faster.

## Administrator rights only through the system's prompt

Split Rock asks KDE's own password prompt for each privileged action and never
sees the password.

Rejected: storing credentials to avoid repeated prompts.

## Useful before any account

The tour and the troubleshooting guide work with no assistant connected (see
"Two layers: the guide in the box, the butler unlocked").

Rejected: an assistant that does nothing until the person signs up for
something.

## Open component, desktop extras outside

Split Rock is open source and works on any KDE Plasma desktop. Tools specific to
one desktop switch on only when that desktop is installed.

Rejected: a closed, bundle-only assistant. A program that acts on a computer
with administrator rights has to be inspectable to be trusted.

## Window arrangement is requested, never done directly

To arrange windows, Split Rock asks the window manager, which decides and stays
the only owner of window positions.

Rejected: Split Rock moving windows itself, which would give windows two owners.

## A background service holds the conversation; the widget only shows it

A small service in the person's session runs the assistant, keeps the
conversation and the notebook, and makes approved changes. The panel widget is a
view of it.

Rejected: running everything inside the widget. The panel would freeze while an
assistant hangs, a panel restart would lose the conversation, and nothing could
be tested without a desktop.

## The assistant is fenced in; the card is the only way to change the computer

The assistant's program runs in a sandbox: it can read what it needs to explain
the computer, write only to the notebook and its own settings, and look at
personal files only where setup allowed it. Every change goes through Split
Rock's own tools, which show the card.

Rejected: trusting the assistant to ask before it acts. An assistant can be
wrong or misconfigured; the computer, not the assistant's good behavior, has to
enforce the card.

## Personal settings are copied aside before a change

Before changing a settings file, Split Rock copies it into its own storage, out of
the person's sight, and its undo puts the copy back.

Rejected: relying on disk snapshots alone. They usually cover the system and
not the person's own settings, and not every disk supports them.

## Sign-in never needs a terminal

Setup offers only assistants that sign the person in through a browser or a
key. One that signs in only through a terminal is left out of setup.

Rejected: opening a terminal just for sign-in. It is the first thing Pat would
see, and the one thing Split Rock promises Pat never has to see.

## Claude is offered only with an Anthropic key

Split Rock starts Claude's adapter with subscription sign-in hidden. This holds
on the maintainer's machine too, because it is a rule about the product, not
about who uses it.

Rejected: offering sign-in with a Claude subscription. Anthropic does not allow
it in a third-party product without its approval. If Anthropic approves Split
Rock, this ruling changes.

## Written in C++ and Qt, like the rest of the suite

The service and the widget use C++, Qt and KDE's own libraries for the password
prompt, settings and the session bus. The protocol layer is Split Rock's own,
pinned to one protocol version and tested against the pretend assistant and the
real ones.

Rejected: Rust or Python for their official protocol libraries, which would add
a second language and toolchain to a suite maintained by one person and agents;
and copying Qt Creator's client, whose licence does not match Split Rock's.

## Do it offers only changes Split Rock knows how to undo

The assistant may propose a change only from a fixed set, each with a tested
undo. Anything else gets Show me how: the steps, for the person to take. The set
grows one tested change at a time. (The maintainer, 3 October.)

Rejected: letting the assistant propose any change, with a snapshot as the
safety net. Its undo cannot be promised, and a broken promise of undo is the
fastest way to lose the person's trust.

## ChatGPT first, Claude with a key second

Setup offers ChatGPT through Codex first: a browser sign-in to an account many
people already pay for, and the maintainer's preferred assistant. Claude with an
Anthropic key is offered next. (The maintainer, 3 October.)

Rejected: Claude first, because a key means a developer website and billing per
use; and waiting for Anthropic to approve subscription sign-in before building.

## Conversations are not saved; the notebook is the memory

A conversation lasts until the person starts a new one, logs out or restarts,
and is never written to disk. What is worth remembering is written into the
notebook as `MEMORY.md` describes. (The maintainer, 3 October.)

Rejected: saving every conversation, which fills the notebook with what nobody
chose to keep and makes it harder for the assistant to find what matters.

## No change without a card, however small

Even a small change, such as text size, gets a card; a small one needs one tap.
(The maintainer, 3 October.)

Rejected: making small changes at once with an Undo afterwards. Where small ends
would blur, and one simple promise is worth more than the saved tap.

## Kept notes go to Gooseberry where it is installed

Where Gooseberry, the notes component, is installed, what the person keeps and
the memory the assistant needs to call on later become Gooseberry notes, added
and read only through Gooseberry's notes tool. Split Rock keeps its own `Notes/`
until then, and always keeps `This computer.md` and `About me.md`. The fence is
unchanged: the assistant still writes files only in the notebook. (The
maintainer, 3 October.)

Rejected: kept notes only in the notebook, which splits the person's notes in
two and keeps them off Gooseberry's board and planner; and reading Gooseberry's
folder directly, which would make its layout a contract and hide what was read.

## Gooseberry arriving later is a question, asked once

When Gooseberry is installed after answers were kept in `Notes/`, Split Rock
asks once, on a card, whether to move them, and introduces Gooseberry in a few
steps. Like every decision, it is the person's, brought to them as a question.
(The maintainer, 3 October.)

Rejected: moving them without asking, which breaks the card; and leaving them
silently, which leaves two places to look without the person knowing.

## Milestone 0 is built in one pass

The concept and these decisions are the approval for Milestone 0. It is built
whole, then handed over for testing against its proof, rather than brought back
step by step. A question the documents do not answer still comes to the
maintainer before it is guessed, and nothing in it may change the computer.
(The maintainer, 4 October.)

Rejected: options before each step, which the documents already settle.

## The mock-up is the approved visual direction

The five screens in `mockup/` are the approved layout and behaviour: the Just
Type sheet, the expanded coworking card, the Do it card, working on a note, and
Search's doors. An agent builds to them without asking again; a later ruling
wins where they disagree. (The maintainer, 4 October.)

## Jack is the working name

The assistant is called Jack in the interface and in documents until a consumer
name is settled. A web search found other assistants called Jack, so it waits on
a registered trademark search, as Shuffle Ace does. (The maintainer, 4 October.)

## The person is leaving decades of Windows

Split Rock is for someone who has used Windows for decades and has just moved to
Linux: at ease with a computer, new to this one. Age is not the point, and
nothing in Split Rock is framed by it. Scam questions stay first-class, framed
by what decades of Windows taught: that a warning in a window can be real, that
support may phone, and that a fix is a program to download. (The maintainer,
3 October.)

Rejected: a senior in their seventies, set up by a relative, as the person
tested against. It framed the product by frailty rather than by the switch, and
missed what the person brings: decades of habits from another system.

## Keeping an unusual device working is a core job

Troubleshooting the device Split Rock runs on is one of its main jobs, not a side
case, including unusual hardware such as a tablet like the ASUS ROG Flow Z13,
with its own power firmware and kernel quirks. Milestone 1 proves it on that
kind of device. (The maintainer, 3 October.)

Rejected: treating hardware problems as out of scope for an assistant about the
desktop. They are what a person leaving Windows meets first, and what they can
least fix alone.

## Two layers: the guide in the box, the butler unlocked

In the box, with no account, Split Rock offers a tour and a troubleshooting
guide of known problems and their fixes, written ahead of time, shipped with
Split Rock and updated with each release. A desktop that installs Split Rock's
extras adds entries of its own. The person's own assistant account unlocks the
rest: Jack becomes a butler on the desktop that answers anything and makes
changes through the Do it card, at the person's own cost. Split Rock sells no
account and pays for no use. It is for someone who already pays for a chatbot
in daily life, most often ChatGPT. What the account costs is the person's
choice, not a risk for Split Rock to manage. (The maintainer, 3 October.)

Rejected: Split Rock selling, bundling or paying for assistant use, which would
make it responsible for accounts and billing; and an assistant that is only
useful with an account.

## The guide's entries: plain files in shared folders, one per problem

Split Rock owns the format of the troubleshooting guide and its general Plasma
entries. A desktop adds its own entries without Split Rock depending on it.
(Engineering, 4 October.)

- **An entry** is one Markdown file for one known problem. Its header gives an
  `id`, unique in its folder; a `title`, the problem as a person would say it;
  `also`, other ways people say it; `applies`, which may name a desktop or a
  device by the name its hardware reports, and is left out for an entry that
  holds on every Plasma desktop; `checked`, the release it was last checked
  against; and `format`, the version of this format, now 1. Its body says what
  the person sees, why it happens, and what to do, in numbered steps.
- **Where entries live:** in `split-rock/guide/<source>/` inside the system's
  shared data folders, normally `/usr/share`. Split Rock's own general entries
  are `split-rock/guide/plasma/`. Split Rock reads every source folder it
  finds there, and none from the person's home.
- **How a release updates them:** the entries are part of Split Rock's package.
  Each release checks every entry it ships, raises its `checked`, and removes
  one that no longer holds rather than leaving it. Nothing is downloaded
  between releases; what is fixed on one computer goes in its
  `This computer.md`, never into the guide.
- **How a desktop adds entries:** the desktop's own package installs
  `split-rock/guide/<desktop>/` beside Split Rock's folder, and removing the
  desktop removes them. Split Rock never names a desktop in its code. A
  desktop's entry may say that it `replaces` a general entry, as
  `plasma/<id>`, when the general fix does not hold on that desktop; Split Rock
  then shows the desktop's entry instead. An entry in a format version Split
  Rock does not know is skipped, so an older Split Rock is never misled by a
  newer desktop.

Rejected: a database or an online service, which needs a network, cannot be
read as plain files, and would be a second kind of storage beside the notebook;
Split Rock carrying each desktop's entries, which would make it depend on that
desktop and release with it; and updating entries over the network between
releases, which would ship fixes not checked against the release in hand.

## The tour is written like the guide

The tour's stops are plain Markdown files in `split-rock/tour/<source>/`, read
from the same shared data folders as the guide and kept the same way: the same
header (`id`, `title`, `applies`, `checked`, `format`), and a body of numbered
steps, each one action and what the person will see. In this repository Split
Rock's own entries live in `guide/plasma/` and `tour/plasma/`, installed as they
are; `checked` reads `unreleased` until the first release checks them.
(Engineering, 4 October.)

Rejected: a second format for the tour, which would give a desktop two ways to
add its own and two to keep up to date.
