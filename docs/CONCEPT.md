# Concept

What Split Rock is for, who it is for, and the rules it keeps. The plan is in
`ROADMAP.md`; settled rulings and what they rejected are in `DECISIONS.md`.

## The person

Pat is in their seventies. Their nephew wiped the old Windows laptop and
installed Linux with a friendly desktop. Pat has never opened a terminal and
never will, does not know what a desktop environment is, and should not need to.

Pat's first question is "how do I use this?" Split Rock is where Pat asks it.

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

## The first five minutes

1. Split Rock opens on its own once, after installation.
2. It says hello and offers a short tour of the desktop. The tour works without
   any account: it is a built-in guide.
3. It offers to answer anything about the computer if Pat connects an assistant
   account. Pat can choose one, or Not now.
4. Pat's first real question gets an answer, or a Do it card.

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
  Older people are the main target of computer scams. "Is this email real?" and
  "a window says my computer is infected" are first-class questions.
- **Says "I don't know."** When unsure, it says so and offers to write the
  problem up for a person who can help.
- **Plain words by default.** Jargon only on request, and defined when used.
- **Big and touchable.** 44 px touch targets, large readable text, one question
  at a time.

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
| An assistant account | Pat signs in with an account they already have, through that assistant's own sign-in | Or skips it and keeps the built-in guide |
| The assistant program | Any agent that speaks the Agent Client Protocol | Each agent owns its sign-in and billing |
| What the assistant needs to run | Installed silently with Split Rock | Many agents ship as Node.js programs; Pat never sees that |
| A place to remember | A plain folder of notes that Split Rock shows itself | Any plain-notes app can open it too |
| A way to undo system changes | A snapshot before every system change, where the disk supports it, and a copy of any personal settings file it changes | Btrfs supports snapshots; snapshots usually leave personal settings out |
| Administrator rights | KDE's own password prompt, per action | Split Rock never holds the password |
| Acting on the desktop | Split Rock's own tools, offered to the agent over MCP | Desktop-specific tools switch on only where that desktop is installed |
| Help from a person | "Send this to someone": a plain summary of the problem and what was tried | For the nephew |

### The account

Setup offers only assistants Pat can sign in to without a terminal, and the
first one offered is the easiest sign-in to an account Pat may already pay for.
Free tiers have mostly ended, so Split Rock says plainly that an assistant
costs money, reads what each one offers at setup rather than promising it in
advance, and never signs Pat up for anything. Not now keeps the built-in guide.
What each assistant offers is in `ASSISTANTS.md`.

### The notebook

No notes app is required. The notebook is a plain folder, chosen at setup and
`~/Documents/Notebook` by default, that Split Rock shows in its own Notebook
tab. KDE's Marknote or Obsidian can open the same folder for anyone who wants
more; apps that keep notes in a database or online are not supported, because
the assistant cannot read them as plain files.

Inside it:

- `This computer.md`: the hardware, what has been fixed, and every change made,
  with how to undo it. Split Rock writes it itself.
- `About me.md`: how Pat likes to be helped and who helps Pat, written only
  after Pat says yes.
- `Notes/`: every answer Pat kept, filed by topic without Pat filing anything.
- `ASSISTANT.md`: the rules the assistant follows in this notebook, written at
  setup.

Conversations are not saved. How memory works is in `MEMORY.md`.

## Risks

- **The account step may stop Pat.** It is likely a paid account, and a
  provider's rules decide which sign-ins Split Rock may offer. The built-in
  guide works without one.
- **The Agent Client Protocol is young.** Agents and their sign-in methods will
  change under Split Rock.
- **Rolling-release systems break things.** Snapshots before updates are the
  safety net, and an update Split Rock runs is one Pat can undo.
- **Trust.** One bad change, or one confident wrong answer about a scam, loses
  Pat for good. Asking first, undo and "I don't know" are the product, not
  polish.
