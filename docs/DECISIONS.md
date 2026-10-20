# Decisions

Each settled ruling: the rule, why it holds, and what was rejected.

Many rulings below concern the conversation with an assistant, its windows,
the troubleshooting guide's part in it and the mock-up drawn for it. They
record what Split Rock built before "Genie's chat is gone" removed it, and stay
as history; the mock-up itself is in git history. Where an earlier ruling
disagrees with a later one, the later one holds.

## Split Rock is an Agent Client Protocol client

Split Rock talks to the assistant through the Agent Client Protocol, so any
agent that speaks it can be the assistant, and the person chooses.

Rejected: driving one assistant's command-line program directly, which ties Split
Rock to a single assistant; and calling a model's API directly, which bills
per use and would make Split Rock responsible for accounts and payment.

## Each agent owns its sign-in and billing

Split Rock never offers an assistant's sign-in itself, never handles an account,
and never pays for anyone's use. The agent signs the person in through its own
window, and Split Rock shows whatever sign-in methods that agent supports.

Rejected: Split Rock managing accounts or keys for the person. Some providers
also do not allow a third-party program to offer their personal-account
sign-in.

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

Split Rock is useful with no account: Split Rock on this computer answers from
the guide and makes everyday changes, and where it can't run, the tour and the
troubleshooting guide work with no assistant connected (see "Two layers: the
guide in the box, the butler unlocked", and "Genie on this computer").

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

Claude is not offered (Two assistants: on this computer, and Codex); this holds
for when it is offered again. Split Rock starts Claude's adapter with
subscription sign-in hidden. This holds
on the maintainer's machine too, because it is a rule about Split Rock, not
about who uses it.

Rejected: offering sign-in with a Claude subscription. Anthropic does not allow
it in a third-party program without its approval. If Anthropic approves Split
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
Nothing runs without the tap.

Why: a fixed set that grows one change at a time leaves most everyday problems
going elsewhere for a fix, which is the opposite of what Split Rock is for, and real use is
the only test that finds which changes matter.

Rejected: a fixed set only, which was the earlier ruling; and making any change
with no snapshot or copy first, because the undo record is the safety net the
person is promised.

## ChatGPT first, Claude with a key second

Setup offers ChatGPT through Codex first: a browser sign-in to an account many
people already pay for. Claude with an Anthropic key is offered next. Claude is
not offered (Two assistants: on this computer, and Codex); this holds for when
it is offered again.

Rejected: Claude first, because a key means a developer website and billing per
use; and waiting for Anthropic to approve subscription sign-in before building.

## Conversations are not saved; the notebook is the memory

A conversation lasts until the person starts a new one, logs out or restarts,
and is never written to disk. What is worth remembering is written into the
notebook as `MEMORY.md` then described.

Rejected: saving every conversation, which fills the notebook with what nobody
chose to keep and makes it harder for the assistant to find what matters.

## No change without a card, however small

Even a small change, such as text size, gets a card; a small one needs one tap.

Rejected: making small changes at once with an Undo afterwards. Where small ends
would blur, and one simple promise is worth more than the saved tap.

## Kept notes go to Gooseberry where it is installed

Where Gooseberry, the notes component, is installed, what the person keeps and
the memory the assistant needs to call on later become Gooseberry notes, added
and read only through Gooseberry's notes tool. Split Rock keeps its own `Notes/`
until then, and always keeps `This computer.md` and `About me.md`. The fence is
unchanged: the assistant still writes files only in the notebook. This is
not built yet.

Rejected: kept notes only in the notebook, which splits the person's notes in
two and keeps them off Gooseberry's board and planner; and reading Gooseberry's
folder directly, which would make its layout a contract and hide what was read.

## Gooseberry arriving later is a question, asked once

When Gooseberry is installed after answers were kept in `Notes/`, Split Rock
asks once, on a card, whether to move them, and introduces Gooseberry in a few
steps. Like every decision, it is the person's, brought to them as a question.

Rejected: moving them without asking, which breaks the card; and leaving them
silently, which leaves two places to look without the person knowing.

## Milestone 0 is built in one pass

The concept and these decisions are the approval for Milestone 0. It is built
whole, then handed over for testing against its proof, rather than brought back
step by step. A question the documents do not answer still comes to the
maintainer before it is guessed, and nothing in it may change the computer.

Rejected: options before each step, which the documents already settle.

## The mock-up is the approved visual direction

The five screens in `mockup/` are the approved layout and behaviour: the Just
Type answer, the expanded coworking card, the Do it card, working on a note, and
Search's doors. An agent builds to them without asking again; a later ruling
wins where they disagree.

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

## No panel widget in Milestone 1; the rest of Milestone 1 as proposed

For the week of use there is no panel widget of Split Rock's own: its icon,
pinned to the panel, opens the full window, and Ask in its menu opens the card.
Kept answers are listed in the full window's Kept tab, in the rows the record of
this computer uses, newest first, with Forget where those rows have Undo.
"Remember that?" is a line under the answer with Remember and Not now: the
assistant offers what to remember, Split Rock asks, and Split Rock writes it to
`About me.md` only after Remember. That fixed it, a button under the answer
beside Keep this, records the problem and what fixed it in
`This computer.md`. The Kept tab, and Forget with it, gave way to This Device
(This Device: one drawer, and the side follows the card).

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
says so before installing.

Rejected: refusing every write, which leaves the device's folder to Split Rock
alone; and trusting an assistant's own sandbox, which differs between
assistants and is the assistant's to switch off.

## Kept answers are filed by topic from their words, in Milestone 1

Keep this files the answer in `Notes/`, in a topic folder Split Rock chooses
from the question's words, and every write is checked for what is never written
down. Having the assistant choose the topic, and filing into Gooseberry where it
is installed (as `MEMORY.md` then described), come later: Gooseberry offers no
way yet for another program to add a note, and its quick note is Search's own
session.

Rejected: a turn in the conversation asking the assistant for a topic, which
the person would see and pay for.

## The icon opens the full window; Search hosts the conversation

Split Rock's icon, in a launcher, in Search or on a dock, opens the full window,
the view in `mockup/Workspace.dc.html`, as an ordinary window: it can be
resized, and a card workspace can place it beside another window. The card in
Search's shape stays, as Ask in the icon's menu, and in Spread it holds the
centre as before. Inside Shuffle, Search hosts the conversation as one of its
modes, drawn in Search's own window, as it hosts Gooseberry's quick note:
Split Rock keeps the conversation and offers it on the session bus (as
`DESKTOP.md` described until the conversation was removed), and Search keeps
nothing. So that the conversation is there when Search asks, Split Rock runs
once, keeps running after its windows close, and is started by Search's first
call. Gooseberry
works the same way.

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
title bar.

It follows that signing in happens in the full window. The card sits above
every window, so a browser an assistant opens for its sign-in could not come in
front of it; a way to sign in chosen in the card opens the full window first,
and the sign-in starts once it has the conversation.

Rejected: an ordinary window for the card, which Kadunce makes a card of its
own; and the full view inside the grown card, which would leave the
conversation on a surface above every window.

Search hosting the assistant's conversation inside its own window is a
different thing (The icon opens the full window; Search hosts the
conversation).

Rejected: the Just Type sheet rising from the bottom, which the on-screen keys
covered.

## Shuffle Genie inside Shuffle, Split Rock on its own

Inside Shuffle it was called Shuffle Genie, and Search's pill
and the window's header say Genie. On its own, on any Plasma desktop, the
application is Split Rock. The repository stays Split Rock. Its mark, first a
genie lamp and now a lighthouse, is kept as one file, `assets/mark.svg`, which
every surface that draws the assistant uses, so that it is swapped in one
place. Genie waits on a registered trademark search, as Shuffle Ace does: other AI
assistants are already called Genie.

Rejected: the earlier working name.

Since 2026-10-16 a Helper's window inside Shuffle is titled by the Helper
alone ("Free up space"), centred in its title bar, and Split Rock's own window
says Shuffle: Genie's chat is retired, and these windows are Shuffle's Helpers.
The rest of Genie's name goes with the chat.

## The person is leaving decades of Windows

Split Rock is for someone who has used Windows for decades and has just moved to
Linux: at ease with a computer, new to this one. Age is not the point, and
nothing in Split Rock is framed by it. Scam questions stay first-class, framed
by what decades of Windows taught: that a warning in a window can be real, that
support may phone, and that a fix is a program to download.

Rejected: a senior in their seventies, set up by a relative, as the person
tested against. It framed Split Rock by frailty rather than by the switch, and
missed what the person brings: decades of habits from another system.

## Keeping an unusual device working is a core job

Troubleshooting the device Split Rock runs on is one of its main jobs, not a side
case, including unusual hardware such as a tablet like the ASUS ROG Flow Z13,
with its own power firmware and kernel quirks. Milestone 1 proves it on that
kind of device.

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
choice, not a risk for Split Rock to manage.

Rejected: Split Rock selling, bundling or paying for assistant use, which would
make it responsible for accounts and billing; and an assistant that is only
useful with an account.

The butler also comes without an account, on the computer itself (Genie on
this computer, below). An account still unlocks the bigger
fixes.

## The guide's entries: plain files in shared folders, one per problem

Split Rock owns the format of the troubleshooting guide and its general Plasma
entries. A desktop adds its own entries without Split Rock depending on it.

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
copy behaves.

Rejected: sharing the person's existing sign-ins, which would let Split Rock's
copy replace them, and could carry over settings that let an assistant act
without asking.

## A sign-in the agent runs in its own program is tried without a terminal

Some agents advertise sign-in as running their own program again. Split Rock
runs it hidden, with nothing typed into it, and offers the page it opens; if it
needs anything typed, it fails, and that assistant's sign-in counts as needing
a terminal. Milestone 0's live sign-in check confirms it.

Rejected: leaving such methods out without trying, which would drop Claude
before knowing whether its Console sign-in finishes in the browser.

## An assistant's own record of a conversation is removed when it ends

Assistants write their own transcripts and logs into their folders. Split Rock
removes those whenever the assistant stops, and again before it starts, keeping
only the sign-in. It carries out "Conversations are not saved".

Rejected: letting each assistant keep its history on disk, which would save the
conversation the person was promised is not saved.

## The tour is written like the guide

The tour's stops are plain Markdown files in `split-rock/tour/<source>/`, read
from the same shared data folders as the guide and kept the same way: the same
header (`id`, `title`, `applies`, `checked`, `format`), and a body of numbered
steps, each one action and what the person will see. In this repository Split
Rock's own entries live in `guide/plasma/` and `tour/plasma/`, installed as they
are; `checked` reads `unreleased` until the first release checks them.

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

Rejected: shipping device folders learned on one machine, which gives every
person someone else's setup; and collecting what each copy learns for future
releases, which needs each person's permission and carries a security risk.

## A device folder describes the device

`Device/DEVICE.md` has a header like a guide entry's, with `id`, `name`,
`maker`, `matches` (the text the hardware's product name contains), `checked`
and `format`, and says how the device was recognised, what it is, what is known
about it, and the maker's documentation found so far. The device's entries sit
beside it in the guide's format.

Rejected: folding device knowledge into `This computer.md`, which is the undo
record and has to stay short enough to read.

## The assistant proposes a change through a stand-in, and Split Rock makes it

Inside its fence the assistant finds one program, `split-rock-change`, which
takes the proposal as one JSON argument (`title`, `what`, `why`, `command`,
`undo`, `check`, `files`) and hands it to Split Rock; the stand-in changes
nothing. An assistant that instead asks leave to run the stand-in is refused,
and its proposal goes on the card all the same. When the person taps Do it,
Split Rock runs the command itself, outside the fence, as the person and never
as the administrator (see "No card runs a command as the administrator"); the
person's own files named in `files` are copied aside first. Each change is kept
with its outcome in Split Rock's own storage and written under Changes in
`This computer.md`, and the assistant is told how it went. A change still
running after two minutes is stopped and recorded as not done. The assistant is told
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

Rejected: letting the assistant run changes itself after asking, which puts the
fence's safety in the assistant's hands; and a helper that holds administrator
rights of its own, which Split Rock never holds.

## No card runs a command as the administrator

A proposal the assistant writes runs as the person, or not at all. One that
asks for the administrator (`admin`), or names a program that changes who a
command runs as (`sudo`, `pkexec`, `su`, `doas`, `run0`, `systemd-run`,
`machinectl`) anywhere in its command, undo or check, is never shown on a card,
and the assistant is told that system changes are made only by Split Rock's own
Helpers: Update, Free up space, Install an app, Restore points and Community
apps. What it does is fixed in Split Rock and shown in Split Rock's words. The
change, its undo and its check run in bubblewrap with the whole computer as the
person sees it, but with no_new_privs set, so a set-user-ID program such as
`sudo` or `pkexec` gains nothing however the command spells it, and with the
system bus hidden, or, where `xdg-dbus-proxy` is installed, replaced by a
filtered one that reaches only the fan and power-profile services a person may
set. Without bubblewrap nothing is made. A system change recorded before this
rule can still be undone, only from its snapshot pair, by `snapper undochange`
with the two numbers; its own undo command is never run.

Why: the card's heading and reasons are the assistant's own words, and the
person cannot judge a shell command, so a card that could reach the
administrator let a web page or a file the assistant read ask for root behind
a friendly title, with a password prompt that named only `/bin/sh`. Keeping
the word list as well as the guard tells the assistant why, so it stops
proposing what would fail.

Rejected: showing the command and letting the person decide, since the person
can't read it; a word list alone, which a command can spell around; and hiding
the whole system bus, which would stop the fan and power-profile settings a
person makes without a password.

## The administrator runs only Split Rock's own named jobs

What needs the administrator goes through the system's password prompt
(`pkexec`) to one program, `split-rock-admin`, installed beside Split Rock's
other programs where only the administrator can change it. It holds every job
the Helpers need, by name: `update`, `install` and `remove` one package,
`free-space`, `restore-list`, `restore-undo` and `community`. It takes only
the job's name and arguments it checks itself: a package name as pacman
allows it, 0 or 1 for restore points, two restore point numbers, a commit's
full hash, a recipe folder the person owns; anything else is refused before a
thing runs. Run by the prompt it keeps nothing of the caller's environment,
uses a PATH of its own and builds a community recipe as the person the prompt
asked. Split Rock hands it to the prompt only when it, and every folder above
it, belongs to the administrator and no one else can write to it. The prompt
then names `split-rock-admin` and the job, not `/bin/sh`.

Why: before, each Helper passed its own script to `pkexec /bin/sh`, so the
prompt approved a shell and whatever text Split Rock gave it. With the jobs in
one checked, installed program, nothing running as the person can change what
the administrator runs, and the prompt says what it is for.

Rejected: a service that holds administrator rights between jobs, which Split
Rock never holds; a polkit action of its own for each job, which would give
each prompt its own words but needs a file under `/usr/share/polkit-1`, which
Shuffle's install key doesn't place yet; and a job that takes a command.

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
the look up front as before.

Rejected: telling the assistant everything about every setting at the start,
which grows with each skill and slows every conversation; Split Rock choosing
the skill alone, which misses questions worded in ways it can't foresee; and
letting the assistant run its own look-around outside the fence, which would
put an unreviewed command outside the fence without a card.

## The Update card updates the computer

Updating the whole computer is Split Rock's own routine, on a card of its own,
never the Do it card. A proposal whose command updates the whole system
(`pacman -Syu` and its spellings, or `paru` or `yay` on their own), or fetches
the package lists without updating (`pacman -Sy`), opens the Update card
instead of a Do it card, and the assistant is told so. The Update tile on the
upkeep shelf opens it too. Installing or removing one package is still a change like
any other.

The card looks first, as the person: what is waiting and what of it a person
notices, the notices Arch posted since the last update, the power, the free
space, whether something else is installing, and whether restore points are
taken. On battery under half, under 2 GB free, or while something else
installs, Update is unavailable and the card says why. Arch's notices, a Plasma
release Shuffle may not have caught up with, and a computer without restore
points are said before Update, not after.

Before Update, the card lists everything the update installs: each package with
its version now and after, what it costs to download, and the packages it
brings new; the Flatpak apps it updates follow. The big pieces of the desktop,
the kernel, KDE Plasma, KWin, Qt and the graphics drivers among them, sit at
the top, marked when they move a version step and not only a fix. Where Shuffle
is installed, as a package or from its own folder, and Plasma or KWin moves a
step, Update asks once more before anything changes. Tapping Update looks
again first: the update installs what is newest when it runs, so a list that
changed in between goes back to the person, said so, and nothing is installed.

Update runs through the system's own password prompt with a snapper pair around
it, unless pacman's own snap-pac hook takes one. It keeps the computer awake,
is never stopped by a timer, and writes to its own log, which Split Rock follows
for the progress the card shows, so the update finishes even if Split Rock
stops partway. Flatpak apps update after it, as the person. Community (AUR)
packages are never part of it: their recipes are written by anyone, so each is
offered on its own. Afterwards the card says what needs the person: a restart,
settings files the update left beside theirs, and services that failed. The
update is written under Changes in `This computer.md` with the restore point to
go back to. Going back is starting the computer from that restore point, not
Undo: putting system files back under a running kernel can leave a computer
that won't start.

Rejected: the Do it card with a longer time limit, which leaves the questions
unanswered and Undo unsafe; a terminal with the assistant narrating beside it,
which breaks No terminal for anyone but the maintainer; and holding Plasma back
while the rest updates, which is the half-update Arch doesn't support.

## The conversation reads as a page, with one colour

A later ruling than the mock-up's look, which stays the layout (The mock-up is
the approved visual direction). An answer is set as text beside the
assistant's mark, with no box around it, at a reading size; a question is a
soft bubble at the right; cards and the composer float a step above the sheet
with a hairline edge and larger corners. The mark's blue is the conversation's
one colour, deepened on a light sheet: primary buttons, the send button,
progress, focus and step numbers take it, where the suite's visual language keeps
a shell monochrome. The card window keeps Search's shape.

The blue runs into violet across the question bubble, the send
button and progress, and each upkeep card has a hue of its own, washed across
its top and carried by its shelf tile and its icon: Update blue, Free up space
teal, Install an app violet, Restore points amber, Health check green and
Community apps orange. Warning and success keep their own colours inside every
card.

Rejected: grey boxes around every answer and a black or white primary button,
which read as dated; and colour spread over the answers themselves, which
competes with what they say.

## Upkeep has cards of its own

Five more of the computer's routines follow the Update card's pattern: look as
the person, show what was found on a card, and make the change after one tap,
through the system's own password prompt once, written in `This computer.md`
before it starts and reported to the assistant after. The cards are fixed in
Split Rock; the assistant opens one by proposing it (`"card"` in a proposal,
with an optional query) and never writes their commands. A shelf on the welcome
page and in the notebook opens each.

- **Free up space** offers the package cache, the system journal, old crash
  reports, the person's cache and leftover app builds, Flatpak runtimes nothing
  uses, the Trash and packages nothing needs, each with its size. Packages
  nothing needs and the Trash start unticked, and removing packages takes a
  restore point first.
- **Install an app** searches the official shelf and Flathub, the shelf first,
  and installs with a restore point. Community (AUR) packages are never offered
  there.
- **Restore points** lists the snapper pairs in words and says how to start
  from one, through the boot menu, and how to keep it with Btrfs Assistant
  where that is installed. It never rolls back itself: that replaces the system
  under a running session, and starting from the point first shows it works.
- **Health check** only reads: services, space, the drive, the battery, the last
  update, the newest restore point, settings files beside the person's own and
  crashes this week, each with the card or question that helps. It looks once a
  day by itself, quietly, and the notebook and the tiles show what it found.
- **Community apps** compares the installed community packages with the AUR,
  shows what changed in each recipe since the installed version, flagging new
  download addresses, install scripts and commands that reach the network, and
  builds an update only after the person has seen it: as the person, at the
  exact commit shown, with a restore point.

Rejected: running `snapper rollback` from the card; mixing community packages
into Install an app or the update; building a community update from the latest
recipe instead of the one the person reviewed; and a Shuffle catch-up card on
the day Plasma releases, which Shuffle's own preparation covers.

## The app icon keeps an icon theme's margin

The icon's tile sits inside the margin icon themes leave around application
artwork, 42 of 48 in Colloid, so it is the size of the apps beside it in a dock,
a launcher or Search.

Rejected: the tile drawn to the icon's edges, chosen first, which reads
a size larger than every themed app next to it.

## Genie on this computer

Split Rock offers an assistant of its own that runs on the computer, named
"Split Rock on this computer" (Genie on this computer inside Shuffle). It
answers questions and makes everyday changes through the same Do it cards, with
no account, and nothing the person asks leaves the computer. A change bigger
than it can make by itself ends with an Ask button that passes the question to
the assistant the person brought, usually Codex. In trials on a tablet the
model did far better behind the front desk below than on its own.

- **The model is Split Rock's own download, made once.** It is fetched the
  first time the person chooses it, after they tap Download: Qwen3 4B, about
  2.5 GB, the same for every computer, and faster where a graphics chip can
  run it. The list in
  `data/models.json` gives each model's exact size and SHA-256; a model without
  one is never downloaded, and a download that does not match is deleted.
  The download card credits the model and names its licence (Apache 2.0),
  whose text installs beside the list.
- **llama.cpp runs it**, as `llama-server` on a random local port with a key
  of its own, inside the same fence as every assistant, with the graphics chip
  let through.
- **The front desk decides, not the model alone.** A fixed safety check answers
  scams, remote access, passwords, money, wiping, piracy, spying, blocked
  updates, scareware and switching protection off with set words before any
  model sees the ask. The model then sorts the ask; a question is answered as
  it is written, word by word; a change opens a skill or an upkeep card, and the
  card is checked against the skill's own commands and against the ask before
  it is shown; anything else is handed off. When the model only talks, the
  desk finds the skill itself.
- **Nothing changes without a card**, as for every assistant: the desk has
  only the stand-ins, and a claim that something was done, or advice to use a
  terminal, is corrected under the answer.

Rejected: Ollama, a second service to install and keep running; letting the
model run commands in an agent loop, which a small model does not do safely;
the larger 8B model on computers with 16 GB of memory and a graphics chip,
which scored lower than the 4B in the third trial (115 of 144 against 125) for
twice the download; and
a local model with no hand-off, which leaves bigger fixes with nowhere to go.

## The tiles say what Genie noticed

The three tiles above the conversation say what Genie noticed on this
computer, in a few words with a dot in the colour of how much it matters:
updates waiting first, from the Update card's own look, then what the daily
health check found. A tap opens the card that helps, or asks about it. When
nothing needs the person, the tiles offer what Genie can do here instead,
from its upkeep cards taking turns with quick tiles such as Take a screenshot,
which only open an app the computer already has and so need no card. Both
looks only read, run quietly while Split Rock runs, and open no card by
themselves. "Find an app for a job" read as a job search, so the tile says
Install a new app.

Rejected: the same three questions for everyone, which read as a help page;
and offers only, which say nothing about the computer itself.


## Recovery starts from what changed

"Undo that", "put it back", "something changed", "it worked yesterday" and "why
did this stop working?" open the What changed card, whichever assistant is in
use. It looks as the person over the last two weeks and lists, a day at a time,
newest first: changes made from Do it cards, each with Undo, the newest of them
on a button of its own; what the package manager updated, installed and
removed, naming only what a person would notice ("the Plasma desktop 6.7.4",
"the Linux kernel"); Flathub apps; the desktop's settings files that were
written, by what they hold ("Windows and effects") and said to be by the
person or the desktop itself, since the desktop writes some of them on its
own; background services that stopped working; and a new kernel waiting for a
restart. An update, install or removal goes back through the Restore points
card where the computer takes restore points, and is asked about where it
doesn't; settings and services are asked about, with the day and the names in
the question; "Ask what caused it" hands the whole list to the assistant. The
card itself changes nothing. On Genie on this computer the front desk opens it
for these words without the model's help, unless the model found a settings
skill that makes the change asked for. On the upkeep shelf it takes Restore
points' tile, which opens from the card's Go back and from the health check,
so the shelf keeps six tiles.

Maintenance gains two settings skills and three health checks:

- **Startup**: the apps and the person's own services that start at sign-in,
  and turning one off or back on, for the person only. An app is turned off
  the way the desktop's Autostart page does it, with a hidden copy of its entry
  in the person's autostart folder, by a helper that ships beside the skill;
  turning it back on removes the copy Split Rock wrote, or the mark, so the
  entry is as it was. Parts of the desktop itself are left off the list.
- **Services**: services that failed, with what each said last, and starting
  one again, as the person or through the system's prompt. Only a service
  listed as failed can be on the card.
- **The health check** also says when a restart finishes the last update,
  when an update or install was cut off or the list of installed packages has
  a problem, which it hands to the assistant, and when downloads left
  unopened for three months take a gigabyte or more, which only opens the
  Downloads folder.

A skill may name its own undo and check, with the same names left open as its
change. Genie then fills them in from the command on the card, and the model's
own are never run; a skill that says there is nothing to put back has its
undo left empty.

Rejected: answering "what changed?" in words, which leaves Undo a tab away and
asks a small model to summarise a long list correctly; moving old downloads to
the Trash from Free up space, which frees nothing until the Trash is emptied,
and deleting them, which can't be undone; restarting any service the model
names; and a seventh tile on the shelf, which leaves one alone on its row.

## Two assistants: on this computer, and Codex

Split Rock offers two assistants: Split Rock on this computer, which needs no
account, and Codex, for bigger fixes with a ChatGPT account. They are the only
two tested to ship. Where Split Rock on this computer runs, it answers from the
guide itself, as written, whenever an entry fits the question, so the guide is
no longer a choice of its own there; it stays one only where Split Rock on this
computer can't run. Split Rock on this computer never offers to remember
something about the person, since what a small model would keep can't be
trusted to be right. Other assistants may be looked at later.

Rejected: keeping Claude Agent and Gemini CLI, never tested to ship; the guide
as a separate choice beside an assistant that already knows it; and the small
model retelling a guide entry in its own words, which on the first real day
turned "these settings files wait for you" into "the system applies them".
The rulings "Claude is offered only with an Anthropic key" and "ChatGPT first,
Claude with a key second" stand only for when Claude is offered again.

## A change the person approved runs to the end

The person says yes on the card, so a change never stops to ask again: the
package manager is given its yes (`--noconfirm`), and Flatpak its own
(`-y --noninteractive`), for installs, removals and updates, whichever
assistant wrote the command. The card shows the command as written; the record
keeps it so. Without it, pacman stopped at "Do you want to remove these
packages? [Y/n]" with no one to answer, and a removal failed.

Rejected: answering the question by feeding "y" to the change, which answers
every other question it might ask too.

## Apps are removed from the app card

"Uninstall", "remove" or "get rid of" an app opens the app card to remove one,
for Genie on this computer whatever card the model picked. It lists the
installed apps the words fit, by their names in the app menu and by the
package's own name, so an app from outside the shelf, such as
main-keyboard-git, is found too. A tap asks first; yes removes it with what it
alone needed, after a restore point, and a removal another app depends on stops
with nothing removed. What the system runs on (the kernel, the desktop, the
package manager, Shuffle's own parts and the like) is never listed.

Search's Uninstall names the app by its menu entry; the card finds the package
that owns it and opens at the yes. Before any yes, the administrator's helper
says, read only, whether the system runs on it: by its name, by Plasma's own
groups (`plasma` and `kde-system`, which hold System Settings, Konsole and
Dolphin), or because another installed package needs it. Any of those says
why and stays, before a password is asked, and the helper's remove job
refuses them too.

Rejected: removing from the Do it card alone, which the local model can't
write safely; and a separate Remove an app tile, which would make the shelf
seven.

## This Device: one drawer, and the side follows the card

The drawer beside the conversation is one page, This Device, in place of the
three tabs This computer, About me and Kept. The upkeep tiles come first, as
a bento: as many columns as fit, chosen so the rows come out even, every tile
the same height. Under them each part is a closet, folded until tapped:
System, About me, Troubleshooting (the device's open and solved problems,
open while one is still open) and Changes. An upkeep card open in the
conversation brings the closet that goes with it to the top, open: What
changed its Changes, the health check Troubleshooting, Restore points the
System.

Kept goes from the drawer: kept answers are plain notes, read and searched in
the notes app where Gooseberry is installed. Fixed problems goes too: the
device's Open and Solved, under Troubleshooting, say the same and more.

Rejected: every part on show at once, which made the drawer a long list of
everything; and keeping a Kept list beside the notes app that already holds
the notes.

## Restore points say whose they are

Each restore point says whether Split Rock took it, before a change it made,
or the system did by itself, as CachyOS's snap-pac does before every install,
removal and update; a helper's command line is said in words ("Before
installing a community app"). The card says the computer clears old ones by
itself, with how many it keeps, read from its own settings, so no one needs
to delete them.

Rejected: deleting restore points from the card, which the system's own
cleanup already does on a schedule the person never has to think about.

## Core lives in Split Rock, as a program of its own

Core, the front desk, is a second program in this repository beside the
conversation (`CORE.md`). The upkeep cards are its hands and open on their own;
the conversation is optional, and Core never needs it. It runs as the person,
never calls a language model, never changes the computer, and keeps only plain
files the person can read. A fact speaks only when it gets worse, and each topic
has its own daily allowance for the island, so one never silences another.

Why: the checks and cards Core relies on are already written and tested here,
in the open, and one repository proves that what Core says matches what the
card then shows.

Rejected: a repository of its own, which would copy working code and keep two
copies in step; Core inside Shuffle's own repository, which is private and
downstream, so the components could not rely on it; and letting the model
interpret facts in the background, where its known weak spots would act
unasked.

## A package is removed only by name, after the person saw it

Every row of Free up space opens into exactly what it holds: each old copy
with its version, the log files trimming takes, each crash report by app and
day, cache folders, community build folders and each thing in the trash. It
lists every package it would remove, by name, on the card before its button, and removes those and nothing with them (`pacman -Rn`, not
`-Rns`). Removing an app from the app card takes only that app the same way;
what came with it stays, for Free up space to list. The parts nothing uses
are `split-rock-admin`'s own list (its read-only `unused` job), the same one
it removes from, and it never counts what building software needs: compilers
and build tools, and any package that ships headers, CMake or pkg-config
files, Qt's build files or protocol descriptions.

Why: on the tablet the card offered build-only packages (extra-cmake-modules,
the Vulkan and Wayland headers and protocols, opencv) as unused, in a folded
list, and `-s` then took cmake and four more no list had shown. A checkout
under `~/Projects` builds against exactly these, so the next install failed.

Rejected: reading what each checkout builds against, which needs its build
run; and dropping parts nothing uses from the card, which is the one place
the person can see them.

## Genie's chat is gone

Split Rock is the Helpers and Core. The conversation with an assistant, its
card and full window, Search's Ask on the session bus, the model on this
computer and its front desk, Codex's adapter, the fence they ran in and the
settings skills they opened are removed. Each Helper opens on its own, from
Core or Search, and writes what it changes in the notebook first. What changed
still puts back a change made earlier from a card: Undo never loses a file. A
file changed since the change is left as it is, with nothing put back; any file
Undo replaces or removes is kept aside in the change's folder first; and a
change is never made unless its record, with its way back, was written whole.

Why: it was ruled that Genie is not a chat. Core makes faceless, temporary
Helpers, and an assistant that could be asked anything was the part with the
open problems: reading beyond the home folder, an undo that could
lose a file, and tokens in the environment passing to the assistant. With the
conversation gone, the first and last have nothing left to reach; the undo is
fixed where Undo still runs.

Rejected: keeping the conversation behind a switch, which keeps every finding
open for a feature Split Rock no longer offers; and dropping Undo with it, which
would leave the changes already made from cards without their way back.

## The Helpers follow the Visual sheet

A later ruling than the conversation's colours (The conversation reads as a
page, with one colour), which went with the chat. Every Helper draws to the
suite's Visual sheet: the window is the card, with nothing boxed inside it but
its rows, on the 10 px paper corner; the card is inset 16 px, its rows stand
8 px apart and its groups 20; the heading is 16 px demi-bold over a 13 px line
in the secondary colour; buttons are 40 to 44 px pills; and its chrome is the
text colour. Colour is kept for state: amber for worth a look, warm red for a
problem and teal for good. A Helper's colour name still says which it is, and
draws nothing.

Why: the Helpers' move onto the Visual sheet was approved, so a Helper reads
like every other Shuffle surface, and the Helpers still to come match the seven
already here.

Rejected: keeping each Helper's own hue and larger corners, which made the
Helpers the one place in Shuffle that did not match; and redrawing only the
Helpers new work touched, which would have left two looks side by side.

## Your apps lists only what the person added, and removes one at a time

Your apps lists the apps the person installed on purpose, from the system's
shelf or Flathub, by the room each takes, and removes one with Install an
app's own yes and restore point. Shelf apps ask for the password once each.

Why: the simple version was chosen for 1.1: what the person added and how big
it is can be read plainly, and removing reuses a path already proved.

Rejected for now: when each app was last used, which would mean reading
Plasma's private record of app use; and removing several at once with one
password, which would change the administrator's helper. Both are kept for
later.

## Health is Core's third topic, and a problem is copied for help

Core looks over the computer once a week with the Health check's own look and
speaks of a new problem: the ticker for any, the island only for a drive that
reports it is failing, the one problem that can lose files. Space and updates
are left to their own topics. A problem no Helper fixes, which once offered
"Ask about it" in the conversation, now offers "Copy for help": the problem as a
question, with what the computer is, for the person to paste where they ask.
The Health check says it runs by itself once a week only while Core's Health
topic is on.
What changed does the same for a change with no way back, such as settings
saved, a service that stopped or Flathub's apps: its row offers "Copy for help"
with the change as a question.

Why: the card said it ran by itself once a week, and nothing ran it once the
conversation was gone; its questions had nowhere to go. Copying was
chosen over a web search, because it works without a network and sends
nothing on its own.

Rejected: a web search for the problem, whose results are often old threads
that don't fit this computer; leaving those rows with nothing to do; and the
island for every problem, which would make a failed service as loud as a
failing drive.

## A Windows installer gets one island, and the Helpers know Windows's words

When a `.exe` or `.msi` arrives in the Downloads folder, Core says once, on the
island, that Windows programs don't run here, and offers to find the Linux
version by the program's name. Each installer is news once; `never` stops it
for good. The Helpers also answer to Windows's own words: Add or remove
programs, Disk Cleanup, Windows Update, System Restore.

Why: Split Rock is for someone who has used Windows for decades, and a
downloaded installer that does nothing is where they get stuck. The island at
that moment was chosen over a line in Search they would have
to think to look for.

Rejected: a line in Search and the Health check only, which waits for the
person to look; and offering to run Windows programs through a compatibility
layer, which fails often enough to look like the desktop's fault.

## Tidy up Downloads moves to the Trash, and ticks only the plainly safe

A Helper of its own gathers what has piled up in the Downloads folder and on the
Desktop and nowhere else, in five groups: installers of apps already
installed and Windows programs, copies the same as their original ("report
(1).pdf"), files not opened in 30 days, screenshots, and the folders there,
each whole. Only the first two are ticked; what is in a folder can't be told
from its name. What the person ticks goes to the
Trash, file by file, with no password and no restore point. Core says once on
the ticker when the two folders pass 5 GB, and again only 5 GB later; Free up
space ends with a row for it, never ticked.

Why: Downloads is where a Windows user's leftovers pile up, and the person
can't tell an installer they used from a document they need. The approved
mock-up ticks only what is plainly safe and keeps the
rest for the person to look at.

Rejected: deleting for good, which can't be undone; ticking old files, which
may be the one thing the person kept; looking in any other folder; and an
island, which would make a full Downloads folder as loud as a full drive. An
AppImage the person opened in the last month is run from where it is, so it is
never ticked.

## A new device gets one ticker line that opens its settings

Core's Devices topic starts with printers, drives and Bluetooth, the three
that people moving from Windows ask about most. The first time one is seen,
one ticker line names it and opens KDE's own settings page for its kind. What
is already plugged in when Core first looks is known without a word. Core
reads only what UDisks2, BlueZ and systemd already publish on the system bus.

Why: on Windows a new device announces itself and offers its setup; here the
setup page exists but has to be found. A ticker line is enough for news that
asks for nothing, and these three kinds were chosen to come first.

Rejected: every kind of device at once, with more to test on hardware nobody
here has; the island, which is for what can't wait; and a note in the
notebook's `Device/` folder for each one, which describes the computer
itself, not what was plugged into it. The logbook keeps each device instead.

## When Do Not Disturb ends, Focus says once what came in

Core's Focus topic speaks once, on the island, when Do Not Disturb ends: how
many notifications came in and which of Core's own facts it held back. The
desktop's notification surface counts the notifications, since only it sees
them, and tells Core over the bus; a tap asks that surface to open its
history.

Why: Do Not Disturb already holds Core's islands back, so the moment it ends
is when the person is ready to hear what waited. One moment and one island
build on a switch people already use.

Rejected: Core reading every notification on the bus to count them, which
would put other apps' messages in front of a program that needs only their
number; focus hours that switch Do Not Disturb on by themselves, which need
a setting of their own; and a summary at the end of every day, a reminder
about nothing being wrong.

Since then, Do Not Disturb turned off inside the notifications raises no
island: the notifications mark what came in, where the person is looking, and
opening them takes down an island already up. On the tablet the island came
up over the open notifications, and its See them asked to open what was
already open, so the tap did nothing anyone could see.

## What changed says each change, and puts it back right there

What changed says what changed, old to new, and offers the way back on the
row. A settings change is said as it was made, from Core's own copies of the
settings files (CORE.md, Settings copies), and Put back sets the values from
before as a change of Split Rock's own, after a yes, with Undo; only where that
is proven to take effect at once, and the rest open KDE's own page for it. A
background service that stopped working says what it said last and offers
Start it again; a Flathub app updated from a commit Flathub still keeps offers
Go back; a system update with no restore point copies every version, old to
new, where the old ones still are, and the errors since. The administrator's
part of starting a service or taking a system app back is two jobs of
`split-rock-admin`, each taking only a checked name: `start-service` starts
only a service that stopped working, and `flatpak-back` takes only a full
commit.

Why: on the tablet, What changed named settings files and copied only their
names, which nobody can act on. The product owner asked what a person needs to
solve the problem in front of them, and chose putting back right there over
copying the exact change for help alone.

Rejected: copying the exact change only, which still leaves the person to do
it by hand; putting back the look and panels live before it is proven, since
writing Plasma's panel file while Plasma runs is overwritten or breaks the
panel; and reading the settings' history from a backup tool, which most
computers don't have.

## Ask comes back on its own, beside Core

Ask answers plain questions on the computer (`ASK.md`): the front desk and the
model from "Genie on this computer" above, unchanged in how they decide, as a
program of their own, `split-rock-ask`, on the session bus beside Core. There
is no chat. Search asks one question at a time, each afresh, and shows the
answer with the Helpers that fit under it; a Do it card from a settings skill
waits for the person's tap. Every answer says it was given on this computer,
so the person always knows who is speaking. The safety check answers before
any model is downloaded. Only `llama-server` runs in the fence, since the
desk is Split Rock's own code and reaches nothing but the model and the
skills' read-only looks. The model downloads after a tap, and the program
ends after ten minutes with nothing to answer, which gives its memory back.

Why: the product owner put Ask and the small model in 1.1 after the third
trial (125 of 144). Core never calls a model, so Ask can't live inside it,
and a program the bus starts only when asked costs nothing to someone who
never asks. The chat's open findings stay closed: nothing here reads beyond
the model and the skills, and no account's token is anywhere near it.

Rejected: Ask inside Core, which would break Core's rule and keep 2.5 GB of
model in memory all day; bringing back the conversation and the Agent Client
Protocol for the desk, which was only needed while the desk was one assistant
among several; and keeping earlier questions in each answer, which Search's
one-question box has no way to show.
