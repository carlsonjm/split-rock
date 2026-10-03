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

A built-in guide to the desktop works with no assistant connected.

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
