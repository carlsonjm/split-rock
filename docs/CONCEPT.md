# Concept

What Split Rock is for, who it is for, and the rules it keeps. What it does not
do yet is in `LIMITATIONS.md`; settled rulings and what they rejected are in
`DECISIONS.md`.

## The person

Pat has used Windows for decades, at work and at home, and has just moved to a
KDE Plasma desktop on Linux. Pat knows how to use a computer. What Pat does not
know is this one: where the settings are, what the Windows habits turn into
here, and how to keep it running. Pat has never needed a terminal and should
not need one now.

Every decision here is tested against Pat first. A feature that only works for
someone who already understands Linux is not a Split Rock feature.

## What it is

Split Rock is two things:

- **The Helpers.** Small windows that each do one upkeep job: Check for
  updates, Free up space, Tidy up Downloads, Install an app (or remove one),
  Your apps, Health check, What changed, Restore points and Community apps.
  A Helper looks first, says in plain words what it found and what it would
  change, and changes nothing until Pat taps its button. Its window closes
  when it is done.
- **Core.** A background program with no window. It notices when space runs
  low, updates have waited too long, the Health check finds a problem, a
  Windows installer lands in Downloads, or a new printer, drive, Bluetooth
  device, game controller or screen appears, and says so once, without
  nagging. Tapping what it says opens the Helper that fits. How Core decides when to speak is in `CORE.md`.

A Helper opens from Search, by its name or by the Windows words Pat already
knows (Add or remove programs, Disk Cleanup, Windows Update, System Restore),
or from one of Core's notices. How a Helper's window meets the desktop is in
`DESKTOP.md`.

Linux already has nearly every tool a person needs to keep a computer healthy.
What it lacks is a way in for someone who has never used them.

## Rules

- **No terminal.** When a Helper runs a command, Pat sees what it does in plain
  words. The command itself sits behind a "Show details" fold.
- **Ask before doing.** Nothing changes until Pat taps the Helper's button.
  Not now leaves everything as it was.
- **Every change can be undone.** Before a change, Split Rock writes what it
  will change and how to put it back. A system change takes a restore point
  first, where the disk supports one, and a personal settings file is copied
  aside. What changed lists the last two weeks, with Undo beside each change.
- **Never holds a password.** Anything that needs administrator rights uses
  the system's own password prompt, one action at a time.
- **Never asks for money or card details.** On Windows, a window asking to be
  paid to fix the computer was often how a scam arrived. Nothing Split Rock
  shows ever asks.
- **Plain words by default.** Jargon only where there is no plainer word, and
  explained when used.
- **Big and touchable.** Large text and touch targets, one job at a time, so a
  Helper works on a tablet as well as with a mouse.

## Keeping this computer working

People leave Windows on the hardware they own, and some of it is unusual: a
tablet such as the ASUS ROG Flow Z13, with its own power firmware and kernel
quirks, breaks in ways a plain laptop never does, and Windows used to hide
that behind the maker's own software. Split Rock reads what the hardware
reports into the notebook at its first start, and the Health check's "Copy for
help" hands that description, with the problem, to whoever Pat asks for help.
Nothing is sent anywhere by Split Rock itself.

## What Split Rock relies on

| Need | Approach |
| --- | --- |
| Updates and apps | pacman and Flatpak; the AUR for Community apps |
| A way to undo system changes | snapper restore points, where the disk is Btrfs and set up for them |
| Administrator rights | The system's own password prompt, through `split-rock-admin`, which runs only Split Rock's named jobs |
| A record of what changed | `This computer.md` in the notebook, and each change's own folder (`MEMORY.md`) |
| Notices | The desktop's ordinary notifications, and Search's island where Tettegouche's launcher is installed |

## Risks

- **Rolling-release systems break things.** Restore points before every
  update let Pat go back to the computer as it was.
- **Unusual hardware breaks in new ways.** A kernel or firmware update can
  change how a device such as the Z13 behaves overnight.
- **Trust.** One bad change loses Pat for good. Looking first, asking first
  and Undo are what Split Rock is for.
