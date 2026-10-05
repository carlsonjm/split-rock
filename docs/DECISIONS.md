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

## Do it offers known changes, and any other change with a snapshot first

Changes Split Rock knows get a tested, one-tap Undo. Any other change the
assistant proposes can also be made from the Do it card, which says plainly how
it would be undone: from the system snapshot taken first, from the copies of the
person's files made first, or by the way back the assistant gave. A change with
none of these says it can't be undone automatically before the person taps.
Nothing runs without the tap. (The maintainer, 5 October, replacing the ruling
of 3 October that offered only a fixed set.)

Why: a fixed set that grows one change at a time leaves most everyday problems
going elsewhere for a fix, which is the opposite of the product, and real use is
the only test that finds which changes matter.

Rejected: a fixed set only, which was the earlier ruling; and making any change
with no snapshot or copy first, because the undo record is the safety net the
person is promised.

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
Type answer, the expanded coworking card, the Do it card, working on a note, and
Search's doors. An agent builds to them without asking again; a later ruling
wins where they disagree. (The maintainer, 4 October.)

## The window takes Search's shape

The assistant's own window is one card the size of Shuffle's Search: 64% of the
work area's width and height, centred over the work, with 8 px corners, a 1 px
outline and a 22 px inner margin. Its 44 px header holds the lamp in its tile
and the name on the left, the question in the middle and an Expand pill on the
right; the answer, its numbered steps and Do it, Show me how and Keep this sit
below, as `mockup/JustType.dc.html` draws them. With the on-screen keys up the
card rises only as far as it must to stay 10 px above them, then gets shorter,
never below 160 px, reading where the keys lie from the input method as
Gooseberry's quick-note card does. Expand grows the card to the whole work area
less a 10 px gutter, and the full window, with the full view of
`mockup/Workspace.dc.html`, takes over from it (below).
There is no Back, at either size: Back belongs to Search, and this is the
assistant's own window. Esc, or a tap beside the card, closes it.
(The maintainer, on the tablet, 4 October.)

## No panel widget in Milestone 1; the rest of Milestone 1 as proposed

For the week of use there is no panel widget of Split Rock's own: its icon,
pinned to the panel, opens the full window, and Ask in its menu opens the card.
Kept answers are listed in the full window's Kept tab, in the rows the record of
this computer uses, newest first, with Forget where those rows have Undo.
"Remember that?" is a line under the answer with Remember and Not now: the
assistant offers what to remember, Split Rock asks, and Split Rock writes it to
`About me.md` only after Remember. That fixed it, a button under the answer
beside Keep this, records the problem and what fixed it in
`This computer.md`. (The maintainer, 4 October.)

Rejected: a panel widget of its own now, which is a Plasma add-on to build and
only the tablet can test; Forget only by saying "forget that"; the person
typing yes to the assistant, which leaves the writing to it; and Split Rock
asking at the end of a troubleshooting conversation whether it was fixed.

## The fence is bubblewrap

The assistant's program runs inside bubblewrap. It sees the system read-only
and the desktop's own settings read-only; the person's home folder is hidden
except the notebook and the assistant's own folder, which it may write; the
desktop's message buses, the display and the person's other programs are out of
its reach; what it writes anywhere else is lost when it stops. It keeps the
network, to reach its provider. A request to write is allowed only inside the
notebook, never to `This computer.md`, `ASSISTANT.md` or `About me.md`, which
Split Rock writes, and never with anything in it that is never written down; every other
request is refused and shown on the Do it card, as in Milestone 0. Where the
computer has no bubblewrap, a real assistant is not started, and `install.sh`
says so before installing. (Engineering, 4 October, from the ruling above.)

Rejected: refusing every write, which leaves the device's folder to Split Rock
alone; and trusting an assistant's own sandbox, which differs between
assistants and is the assistant's to switch off.

## Kept answers are filed by topic from their words, in Milestone 1

Keep this files the answer in `Notes/`, in a topic folder Split Rock chooses
from the question's words, and every write is checked for what is never written
down. Having the assistant choose the topic, and filing into Gooseberry where it
is installed (`MEMORY.md`), come later: Gooseberry offers no way yet for another
program to add a note, and its quick note is Search's own session. (Engineering,
4 October.)

Rejected: a turn in the conversation asking the assistant for a topic, which
the person would see and pay for.

## The icon opens the full window; Search hosts the conversation

Split Rock's icon, in a launcher, in Search or on a dock, opens the full window,
the view in `mockup/Workspace.dc.html`, as an ordinary window: it can be
resized, and a card workspace can place it beside another window. The card in
Search's shape stays, as Ask in the icon's menu, and in Spread it holds the
centre as before. Inside Shuffle, Search hosts the conversation as one of its
modes, drawn in Search's own window, as it hosts Gooseberry's quick note:
Split Rock keeps the conversation and offers it on the session bus
(`DESKTOP.md` § The conversation in Search), and Search keeps nothing. So that
the conversation is there when Search asks, Split Rock runs once, keeps running
after its windows close, and is started by Search's first call. The maintainer
passed the same model for Gooseberry on the tablet, and wants Genie the same
way. (The maintainer, 4 October.)

Rejected: the card as what the icon opens, which cannot be resized or placed
beside another window; and Search drawing the conversation from a copy of its
own, which would leave two conversations.

## The card holds Spread's centre; the full window takes over from it

Genie's card holds Spread's centre the same way Gooseberry's quick note does: as
a companion guest of Kadunce (Kadunce's `docs/TETTEGOUCHE-CONTEXT.md`
§ Companion guests). With Spread shown, the card asks for the centre and is
drawn at the rectangle Kadunce gives; a tap beside it is Kadunce's, which
closes it. Over an Active card there is no guest: the card stands over the
work by itself. On a desktop without Kadunce it stands centred over the work,
as before. So the card can hold the centre at all, it is a surface of the
desktop's own, as Search's is: Kadunce makes an ordinary window a card, and a
card cannot hold the centre.

Expand grows the card, then opens the full window, an ordinary window that
Kadunce makes the Active card: in Spread Kadunce grows the centre and hands it
to that window when it arrives, and over an Active card the card stays up until
Kadunce shows the full window as the selected card, then fades. The full view
of `mockup/Workspace.dc.html` lives in that window, under the desktop's own
title bar. (The maintainer, 4 October.)

It follows that signing in happens in the full window. The card sits above
every window, so a browser an assistant opens for its sign-in could not come in
front of it; a way to sign in chosen in the card opens the full window first,
and the sign-in starts once it has the conversation. (Engineering, following
from the card's surface.)

Rejected: an ordinary window for the card, which Kadunce makes a card of its
own; and the full view inside the grown card, which would leave the
conversation on a surface above every window.

Search hosting the assistant's conversation inside its own window is a
different thing, and waits on the background service of Milestone 1
(`ROADMAP.md`).

Rejected: the Just Type sheet rising from the bottom, which the on-screen keys
covered.

## Shuffle Genie inside Shuffle, Split Rock on its own

Jack is retired. Inside Shuffle the product is Shuffle Genie, and Search's pill
and the window's header say Genie. On its own, on any Plasma desktop, the
application is Split Rock. The repository stays Split Rock. Its mark is a genie
lamp in the suite's line style, kept as one file, `assets/mark.svg`, which every
surface that draws the assistant uses, so that it is swapped in one place. Genie
waits on a registered trademark search, as Shuffle Ace does: other AI
assistants are already called Genie. (The maintainer, 4 October.)

Rejected: Jack, the working name until now.

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
rest: Split Rock becomes a butler on the desktop that answers anything and makes
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
  finds there and, from the person's home, only the notebook's `Device/`
  folder, which is this computer's own (see "Split Rock is local").
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

## Each assistant keeps its own sign-in, apart from the person's own copies

Split Rock starts each assistant with a folder of its own for its sign-in and
settings, and without any key, token or assistant setting from the
person's environment. A copy of the same assistant the person uses elsewhere is
never read or changed, and none of its settings can loosen how Split Rock's
copy behaves. (Engineering, 4 October; Milestone 0.)

Rejected: sharing the person's existing sign-ins, which would let Split Rock's
copy replace them, and could carry over settings that let an assistant act
without asking.

## A sign-in the agent runs in its own program is tried without a terminal

Some agents advertise sign-in as running their own program again. Split Rock
runs it hidden, with nothing typed into it, and offers the page it opens; if it
needs anything typed, it fails, and that assistant's sign-in counts as needing
a terminal. (Engineering, 4 October; Milestone 0 checks it live.)

Rejected: leaving such methods out without trying, which would drop Claude
before knowing whether its Console sign-in finishes in the browser.

## An assistant's own record of a conversation is removed when it ends

Assistants write their own transcripts and logs into their folders. Split Rock
removes those whenever the assistant stops, and again before it starts, keeping
only the sign-in. (Engineering, 4 October; it carries out "Conversations are
not saved".)

Rejected: letting each assistant keep its history on disk, which would save the
conversation the person was promised is not saved.

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

## Split Rock is local: each computer learns its own device

Split Rock is local by default and starts fresh on every computer. Its device folder
lives on that computer, in the notebook as `Device/`, and grows with use: at
setup, what Split Rock can read on the computer itself, such as the maker, model and
firmware the hardware reports; then the device's problems and their fixes as
they are met, in the guide's format; and, with an assistant account, the maker's
documentation the assistant finds for that hardware. Without an account, Split
Rock knows only what it can read on the computer. Nothing learned on one computer
reaches another: releases update the general Plasma entries and a desktop's own
entries, never device knowledge gathered from people's computers. The ASUS ROG
Flow Z13's lessons are the test machine's own `Device/`, not shipped content.
(The maintainer, 4 October.)

Rejected: shipping device folders learned on one machine, which gives every
person someone else's setup; and collecting what each copy learns for future
releases, which needs each person's permission and carries a security risk.

## A device folder describes the device

`Device/DEVICE.md` has a header like a guide entry's, with `id`, `name`,
`maker`, `matches` (the text the hardware's product name contains), `checked`
and `format`, and says how the device was recognised, what it is, what is known
about it, and the maker's documentation found so far. The device's entries sit
beside it in the guide's format. (Engineering, 4 October.)

Rejected: folding device knowledge into `This computer.md`, which is the undo
record and has to stay short enough to read.

## The assistant proposes a change through a stand-in, and Split Rock makes it

Inside its fence the assistant finds one program, `split-rock-change`, which
takes the proposal as one JSON argument (`title`, `what`, `why`, `command`,
`undo`, `check`, `admin`, `files`) and hands it to Split Rock; the stand-in changes
nothing. An assistant that instead asks leave to run the stand-in is refused,
and its proposal goes on the card all the same. When the person taps Do it,
Split Rock runs the command itself, outside the fence: as the person, or, with
`admin`, through the system's own password prompt (`pkexec`). A `sudo` the
assistant writes becomes that prompt. Before a system change Split Rock takes a
snapper pre and post pair where the disk has one set up, and Undo puts back the
system files between them; the person's own files named in `files` are copied
aside first. Each change is kept with its outcome in Split Rock's own storage
and written under Changes in `This computer.md`, and the assistant is told how
it went. A change still running after two minutes (six for a system change, for
the password prompt) is stopped and recorded as not done. The assistant is told
that its sandbox cannot see the person's windows or running programs, so it asks
the person whether something they can only see worked. Its sandbox cannot reach
the running desktop either, so Split Rock runs the proposal's `check` as the
person after the change and tells the assistant what it read; without a check
the assistant is told not to claim success. A setting written to `kwinrc` is
followed by KWin's reload, after the change and after Undo. Undo runs the
assistant's way back first, because it applies the old setting live, and puts
back the copied files only if that fails. Each change and undo is logged to the
system journal. A change to the desktop's look is a known change: Split Rock
reads the global theme and colour scheme in use before it, and Undo puts that
look back as a whole with `plasma-apply-lookandfeel` and
`plasma-apply-colorscheme`, then checks the colour scheme is back, whatever way
back the assistant gave. The assistant reads the look in use and the installed
names in the look skill (below), and after an Undo it is told them with the
next question.
The pointer theme is part of the look. A theme named loosely, such as "Altas"
for "Altas-Dark", is taken as the one installed theme it fits, and the card
names that one. A proposal naming a theme that fits none, or several, is not
shown to the person; the assistant is asked again with the installed names.
(Engineering, 5 October, after the first tests on the tablet.)

Rejected: letting the assistant run changes itself after asking, which puts the
fence's safety in the assistant's hands; and a helper that holds administrator
rights of its own, which Split Rock never holds.

## The assistant opens settings skills from a shelf

A settings skill tells the assistant how to look at, change, undo and check one
kind of setting: the desktop's look, the pointer, the fans. Skills are plain
files under `skills/<source>/`, in the guide's header format, installed to the
same shared folders, and written for the assistant rather than the person, so
they may hold commands. The assistant starts with only the shelf, one line a
skill, which keeps the opening words short however many skills there are. It
opens a skill by running `split-rock-skill` with the skill's name inside its
fence; Split Rock answers with the skill and what its "Look around" commands
read outside the fence, such as the value in use and the names installed, which
the fence hides. Only those shipped commands run, never the assistant's: the
request names a skill, nothing more, and `tests/verify-skills.py` keeps every
look-around to programs that only read. An assistant whose own program asks
leave to run the stand-in is given it for that command alone, with nothing
else on the line, as it still runs inside the fence. A look-around still running after ten
seconds is stopped. With each question Split Rock adds a hint at the skill that
fits it best, when one clearly does; the assistant makes the pick and may open
another skill, or none. Where the assistant runs without the fence, it is told
the look up front as before. (The maintainer, 5 October: the
assistant picks from the shelf, and Split Rock may hint.)

Rejected: telling the assistant everything about every setting at the start,
which grows with each skill and slows every conversation; Split Rock choosing
the skill alone, which misses questions worded in ways it can't foresee; and
letting the assistant run its own look-around outside the fence, which would
put an unreviewed command outside the fence without a card.
