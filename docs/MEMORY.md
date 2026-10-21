# The notebook

How Split Rock remembers what it did. Think of the binder that comes with a
house: what the place is, and a log of every repair with how it was done. The
notebook is that binder, kept as a plain folder of Markdown files,
`~/Documents/Notebook` by default (`--notebook` names another).

## What is in it

Split Rock makes the folder and these files the first time a Helper opens,
where they are missing. It never overwrites one that is already there.

| File | What it holds | Who writes it |
| --- | --- | --- |
| `This computer.md` | The hardware, and every change a Helper made with how to undo it | Split Rock itself |
| `Device/DEVICE.md` | What this computer is, read from what the hardware reports | Split Rock, at the first start |
| `ASSISTANT.md` | Rules for an assistant working in this folder | Split Rock, at the first start |

`ASSISTANT.md`, the "Fixed problems" section and `Device/DEVICE.md`'s "Still
open" list come from when Split Rock held a conversation with an assistant
(`DECISIONS.md`, Genie's chat is gone). Split Rock still writes them at the
first start, and no Helper reads them now.

## The record of this computer

Split Rock writes `This computer.md` itself, because it is the undo record and
has to be right.

- **Hardware:** the maker, model, firmware, processor, memory, system and
  kernel, filled in at the first start.
- **Changes:** newest first. Each has the date, what changed in plain words,
  why, how to undo it, and how it went. Undo on What changed marks its line as
  undone.

Each change also has a folder of its own under `split-rock/changes` in the
person's data folder, with what is needed to put it back. Before Undo replaces
or removes a file, it keeps a copy there.

Changes older than a year are meant to move to `Earlier changes.md`, to keep
the page short enough to read. That move is not built yet.

## The device

At the first start Split Rock reads what the hardware reports, such as the
maker, the model and the firmware, into `Device/DEVICE.md`, with a header in
the guide's format. Nothing in it comes from, or goes to, any other computer.

## Never written down

Passwords, card numbers, bank details and codes sent by text or email. Split
Rock checks every write to the notebook and refuses these.

## The notebook belongs to the person

It is a plain folder on their computer. Any notes app that reads plain
Markdown, such as KDE's Marknote or Obsidian, or the file manager, can open
it. Uninstalling Split Rock leaves it in place.
