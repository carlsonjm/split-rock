# Memory

How Split Rock remembers. Think of a household binder kept by the computer: a
scratch pad for today, a log of every repair, the computer's own manual, a page
about the person, and a folder of things worth keeping. Conversations themselves are not saved; what is
worth remembering is written into the binder, which is the notebook folder.

## The five kinds of memory

| What | Where | Who writes it | When |
| --- | --- | --- | --- |
| The conversation | Held by Split Rock's service only | — | While it lasts |
| The record of this computer | `This computer.md` | Split Rock itself | Automatically, at every change and every fix |
| The device | `Device/`: `DEVICE.md` and the device's problems and fixes | Split Rock at setup, from what it reads on the computer; the assistant after | At setup, then whenever something about the device is learned |
| About the person | `About me.md` | The assistant, after the person says yes | When something worth knowing comes up |
| Kept notes | Gooseberry, where it is installed; otherwise `Notes/`, one folder per topic | The assistant | When the person taps Keep this |

`ASSISTANT.md` holds the rules the assistant follows in this notebook. Split Rock
writes it at setup.

## The conversation

The scratch pad. It lasts until the person taps New question, logs out or
restarts the computer. Closing the window does not end it, so an accidental tap
loses nothing. It is never written to disk.

The assistant's provider may keep its own history under its own policy; setup
says so once, in plain words.

## The record of this computer

Split Rock writes this itself, never the assistant, because it is the undo
record and has to be right.

- **Hardware:** what the computer is, filled in at setup.
- **Changes:** newest first. Each has the date, what changed in plain words,
  why, how to undo it, and whether it was undone.
- **Fixed problems:** the problem in the person's words, what caused it, and
  what fixed it. This is what makes the next problem faster. That fixed it,
  under an answer, writes one: the question the conversation began with, and
  the answer's lead.

Changes older than a year move to `Earlier changes.md`, so the page stays short
enough to read.

## The device

The computer's own manual, written on this computer and nowhere else. At setup
Split Rock reads what the hardware reports, such as the maker, the model and the
firmware, into `Device/DEVICE.md`. Each problem met on this device, and its fix,
becomes an entry beside it in the guide's format, and the guide offers those
entries like its own. With an account, the assistant also adds the maker's
documentation it finds for that hardware. Without one, `Device/` holds only what
Split Rock can read on the computer. It starts fresh on every computer: nothing in it
comes from, or goes to, anyone else's.

## About the person

How they like to be helped (bigger text, one step at a time), the apps they use,
and who helps them, so Send this to someone knows where to send it. When
something worth knowing comes up, the assistant offers it, and Split Rock asks
"Remember that?" under the answer, with Remember and Not now. Split Rock writes
it, dated, only after Remember; the assistant never writes this page itself.

## Kept notes

Keep this sits under every answer. A kept answer becomes a note with a plain
title, the date and the question it answered. Nobody has to file anything.

- **Where Gooseberry is installed,** the note is added through Gooseberry's
  notes tool, under a place of its own. It is then on the person's board and
  planner and found by Search, like any note they wrote. Memory the assistant
  needs to call on later goes there too.
- **Otherwise,** it is filed in a topic folder the assistant chooses, such as
  `Notes/Printing/`.

When Gooseberry arrives after answers were already kept in `Notes/`, Split Rock
asks once, on a card, whether to move them into Gooseberry, with a short
introduction to it. Nothing moves until the person says yes.

The record of this computer and the page about the person stay in the notebook
either way. Split Rock looks for Gooseberry when it starts, and never needs it.

## Remembering in a conversation

When a conversation starts, the assistant gets `About me.md`, the top of
`This computer.md`, `Device/DEVICE.md` and the list of kept note titles, from Gooseberry where it is
installed. It opens a note only when the question touches it, and says when it does: "Last time, the printer needed its
driver reinstalled." Memory that is used is memory that is shown.

## Never written down

Passwords, card numbers, bank details and codes sent by text or email. Split
Rock checks every write and refuses these even when asked, and says why.

## Forgetting

Forget on any note, or "forget that" in a conversation, moves the note to the
desktop's trash, where it can be restored. A kept note in Gooseberry is removed
the same way, through Gooseberry. A change in `This computer.md` stays
while its undo can still be used.

## The notebook belongs to the person

It is a plain folder on their computer. Switching assistants keeps all of it,
and any notes app that reads plain notes can open it. While a conversation runs,
what the assistant reads goes to its provider, like anything the person types;
setup says this once, in plain words.
