# Repository context and startup

For every task, read only this startup set, in order:

1. `AGENTS.md`
2. `docs/CONCEPT.md`
3. `docs/ROADMAP.md`
4. The suite record, kept privately in the Shuffle repository and read from its
   checkout beside this one: `../shuffle/docs/suite/CURRENT_STATE.md`,
   `../shuffle/docs/suite/ROADMAP-CC.md` and `../shuffle/docs/suite/SWARM.md`.
   When it is not checked out, say that the suite plan was unavailable rather
   than inventing an order.

Then open only the documents the work touches; `docs/README.md` routes each
subject to its document. Before proposing an approach, check
`docs/DECISIONS.md` for one that was already rejected.

Claude Code loads `CLAUDE.md` automatically. It routes into this same startup set
and adds no separate protocol.

## Holds

These hold in every Shuffle repository.

- The maintainer approves product behavior and visual direction before
  implementation begins. Engineering may present evidence, constraints and
  alternatives; an unapproved proposal does not become a candidate.
- One implementation owner per repository. A second worker is read-only review or
  a disjoint file set.
- Reproduce a defect and measure the property controlling it before changing it.
- Components never depend on private product features. Split Rock works on any
  KDE Plasma desktop; what Shuffle adds switches on only when Shuffle is present.

These hold here, because this program acts on a person's computer.

- Nothing on the computer changes without a card the person approved.
- Every change to the system is recorded with how to undo it before it is made.
- Split Rock never holds, stores or asks for a password. Administrator rights come
  only through the system's own prompt, one action at a time.
- Split Rock never offers an assistant's sign-in itself. Each agent signs the
  person in through its own window and owns its own billing.
- The person never sees a terminal unless they ask to.
- Tests never act on a real person's settings, files or session.

## Writing here

- `docs/ROADMAP.md` is the plan for this repository; suite order lives in the
  suite record.
- A ruling goes in `docs/DECISIONS.md` as its rule, why it holds, and what was
  rejected.
- Public text stays fit to publish: no attribution by initial, and no paths into
  a real home folder. `tests/verify-public.py` checks it.
- Code comments explain code behavior and reasoning only.

## Verification

Run `./verify.sh` for every change.
