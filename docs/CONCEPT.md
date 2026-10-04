# Concept

What Split Rock is for, who it is for, and the rules it keeps. The plan is in
`ROADMAP.md`; settled rulings and what they rejected are in `DECISIONS.md`.

## The person

Pat has used Windows for decades, at work and at home, and has just left it for
Linux with a friendly desktop. Pat knows how to use a computer. What Pat does not
know is this one: where the settings are, what the Windows habits turn into
here, and what to do when something breaks. Pat has never needed a terminal and
should not need one now.

Pat already pays for a chatbot in daily life, most often ChatGPT. An assistant
on the desktop that answers anything and fixes things, with that same account,
is a reason to switch.

Pat's first question is "where did everything go?" Split Rock is where Pat asks
it.

Every decision here is tested against Pat first. A feature that only works for
someone who already understands Linux is not a Split Rock feature.

## What it is

A panel widget that opens a small, calm window. Pat types or taps a question in
their own words, and Split Rock does one of three things:

1. **Answers.** It explains in plain words, one step at a time, and can point at
   the thing on screen.
2. **Does it, with permission.** It shows a card saying what will change, and
   changes it only when Pat taps Do it.
3. **Remembers.** What it changes, and what Pat asks it to keep, goes into Pat's
   notebook, so the next problem starts from what is already known.

The gap it fills: Linux already has nearly every capability a person needs.
What it has never had is someone to ask.

## Two layers

Split Rock comes in two layers.

- **In the box, with no account.** A tour of the desktop, and a troubleshooting
  guide: known problems, in the words a person would use, each with its fix.
  The guide is written ahead of time, ships with Split Rock and is updated with
  each release. A desktop that installs Split Rock's extras adds entries of its
  own. Nothing here needs an account or a network.
- **Unlocked by Pat's own assistant account.** Pat signs in to an assistant they
  already pay for, and Jack becomes a butler on the desktop: it answers
  anything, troubleshoots what the guide does not cover, and makes changes
  through the Do it card. The use is billed to Pat's own account, at Pat's
  choice. Split Rock sells no account and pays for no use.

How the guide's entries are kept, and how a desktop adds its own, is in
`DECISIONS.md`.

## Keeping this computer working

Keeping an unusual device working is one of Split Rock's main jobs, not a side
case. People leave Windows on the hardware they own, and some of it is unusual:
a tablet such as the ASUS ROG Flow Z13, with its own power firmware and kernel
quirks, breaks in ways a plain laptop never does, and Windows used to hide that
behind the maker's own software.

So Jack learns the computer it is on. Jack is local: it starts fresh on every
computer, reads what the hardware reports at setup, and keeps what it learns
about the device in the notebook's `Device/` folder, which grows with use. With
an account, the assistant troubleshoots what the guide does not cover and finds
the maker's documentation for that hardware; every fix lands in
`This computer.md` and the device's folder, so the next time starts from what
worked. Nothing learned on one computer reaches another.

## The first five minutes

1. Split Rock opens on its own once, after installation.
2. It says hello and offers a short tour of the desktop. The tour, and the
   troubleshooting guide behind it, work without any account.
3. It offers to unlock the rest with an assistant account Pat already has. Pat
   can choose one, or Not now.
4. Pat's first real question gets an answer, a guide entry, or a Do it card.

Split Rock must be useful at step 2. An assistant that is a blank box until Pat
signs up for something has failed Pat.

## Rules

- **No terminal.** If Split Rock runs a command, Pat sees what it does in plain
  words. The command sits behind a "Show details" fold.
- **Ask before doing.** Nothing changes without a Do it card. Pat can choose
  "Show me how" instead, to learn rather than delegate.
- **Every change can be undone.** Before changing the system, Split Rock records
  what it will change and how to put it back, and takes a snapshot where the
  computer supports one.
- **Never holds a password.** Anything that needs administrator rights uses the
  system's own password prompt.
- **Never asks for money or card details, and warns when something else does.**
  Decades of Windows taught Pat that a warning in a window can be real, that
  support may phone, and that a fix is a program to download. On this computer
  those are how scams arrive. "Is this email real?" and "a window says my
  computer is infected" are first-class questions, answered firmly.
- **Says "I don't know."** When unsure, it says so and offers to write the
  problem up for a person who can help.
- **Plain words by default.** Jargon only on request, and defined when used.
- **Big and touchable.** 44 px touch targets, large readable text, one question
  at a time, so it works on a tablet as well as with a mouse.

## The Do it card

The card is the product. Split Rock offers Do it only for changes it
knows how to undo; anything else gets Show me how. Each card says what will
change in one sentence, why
Split Rock suggests it, whether and how it can be undone, and whether it needs
Pat's password.

Buttons: **Do it**, **Show me how instead**, **Not now**. A "Show details" fold
holds the exact command or setting.

## What Split Rock relies on

| Need | Approach | Notes |
| --- | --- | --- |
| An assistant account | Pat signs in with an account they already pay for, through that assistant's own sign-in | Or skips it and keeps the tour and the guide |
| The assistant program | Any agent that speaks the Agent Client Protocol | Each agent owns its sign-in and billing |
| What the assistant needs to run | Installed silently with Split Rock | Many agents ship as Node.js programs; Pat never sees that |
| Known problems and their fixes | The troubleshooting guide, shipped with Split Rock and updated with each release | A desktop adds its own entries |
| A place to remember | A plain folder of notes that Split Rock shows itself; kept answers go to Gooseberry where it is installed | Any plain-notes app can open it too |
| A way to undo system changes | A snapshot before every system change, where the disk supports it, and a copy of any personal settings file it changes | Btrfs supports snapshots; snapshots usually leave personal settings out |
| Administrator rights | KDE's own password prompt, per action | Split Rock never holds the password |
| Acting on the desktop | Split Rock's own tools, offered to the agent over MCP | Desktop-specific tools switch on only where that desktop is installed |
| Help from a person | "Send this to someone": a plain summary of the problem and what was tried | For whoever Pat turns to |

### The account

Setup offers only assistants Pat can sign in to without a terminal, and the
first one offered is the easiest sign-in to an account Pat most likely already
pays for: ChatGPT, through Codex. Split Rock says plainly that the assistant's
use is billed to Pat's account, reads what each one offers at setup rather than
promising it in advance, and never signs Pat up for anything. Not now keeps the
tour and the guide. What each assistant offers is in `ASSISTANTS.md`.

### The notebook

No notes app is required. The notebook is a plain folder, chosen at setup and
`~/Documents/Notebook` by default, that Split Rock shows in its own Notebook
tab. KDE's Marknote or Obsidian can open the same folder for anyone who wants
more; apps that keep notes in a database or online are not supported, because
the assistant cannot read them as plain files.

Inside it:

- `This computer.md`: the hardware, what has been fixed, and every change made,
  with how to undo it. Split Rock writes it itself.
- `Device/`: what this computer is and what has been learned about it, starting
  from what Jack reads on the computer and growing with use.
- `About me.md`: how Pat likes to be helped and who helps Pat, written only
  after Pat says yes.
- `Notes/`: every answer Pat kept, filed by topic without Pat filing anything,
  unless Gooseberry is installed, where kept answers become Gooseberry notes.
- `ASSISTANT.md`: the rules the assistant follows in this notebook, written at
  setup.

Conversations are not saved. How memory works is in `MEMORY.md`.

## Risks

- **The box has to stand on its own.** Without an account, Pat has only the tour
  and the guide. A thin guide, or one that falls behind a release, leaves Pat
  with nothing; it is written and checked with every release for that reason.
  Whether to unlock the rest, and what it costs, is Pat's choice, not a risk.
- **The Agent Client Protocol is young.** Agents and their sign-in methods will
  change under Split Rock.
- **Unusual hardware breaks in new ways.** A kernel or firmware update can
  change how a device such as the Z13 behaves overnight. Snapshots before
  updates are the safety net, and the device's folder has to keep up.
- **Rolling-release systems break things.** Snapshots before updates are the
  safety net, and an update Split Rock runs is one Pat can undo.
- **Trust.** One bad change, or one confident wrong answer about a scam, loses
  Pat for good. Asking first, undo and "I don't know" are the product, not
  polish.
