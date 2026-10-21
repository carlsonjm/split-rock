# Repository context and startup

For every task, read only this startup set, in order:

1. `AGENTS.md`
2. `docs/CONCEPT.md`
3. `docs/LIMITATIONS.md`
4. The suite record, kept privately in the Shuffle repository and read from its
   checkout beside this one: `../shuffle/docs/suite/CURRENT_STATE.md`,
   `../shuffle/docs/suite/ROADMAP-CC.md` and `../shuffle/docs/suite/SWARM.md`.
   `ROADMAP-CC.md` is a frozen record; its first line points to the suite's
   maintained roadmap. When the record is not checked out, say that the suite
   plan was unavailable rather than inventing an order.

Then open only the documents the work touches; `docs/README.md` routes each
subject to its document. Before proposing an approach, check
`docs/DECISIONS.md` for one that was already rejected.

Claude Code loads `CLAUDE.md` automatically. It routes into this same startup set
and adds no separate protocol.

## Holds

These hold in every Shuffle repository.

- The product owner approves product behavior and visual direction before
  implementation begins. Engineering may present evidence, constraints and
  alternatives; an unapproved proposal does not become a candidate.
- One implementation owner per repository. A second worker is read-only review or
  a disjoint file set.
- Reproduce a defect and measure the property controlling it before changing it.
- Components never depend on private Shuffle features. Split Rock works on any
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

- The plan for this repository is kept privately; suite order lives in the
  suite's maintained roadmap. What Split Rock does not do yet, and will not do,
  is in `docs/LIMITATIONS.md`.
- A ruling goes in `docs/DECISIONS.md` as its rule, why it holds, and what was
  rejected.
- Public text stays fit to publish: no attribution by initial, and no paths into
  a real home folder. `tests/verify-public.py` checks it.
- Code comments explain code behavior and reasoning only.

## Git history

Work freely on task branches and commit as needed during implementation. Before
opening or merging a pull request, clean the branch so its history reflects
meaningful product or engineering changes, not the debugging process.

- Keep commits that represent distinct behavior, architecture, migration or
  design decisions. Squash failed attempts, test-only repair churn, typo fixes,
  CI retries, forgotten-file follow-ups and other mechanical corrections into
  the commit they belong to.
- Prefer one to three meaningful commits per pull request. Do not combine
  unrelated changes merely to reduce the count.
- Clean the branch before its head is handed over for an install or a hands-on
  check, so the head tested there is the head that merges. Rewriting a head
  after hand-over breaks the hand-over.
- Never rewrite history already merged to `main`.

## Verification

Run `./verify.sh` for every change.
