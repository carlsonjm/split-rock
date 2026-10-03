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
