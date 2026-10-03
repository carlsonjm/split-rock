# Split Rock — Claude Code entry point

`AGENTS.md` is the authoritative workflow and safety contract for this
repository and applies unchanged to Claude Code. Read it first. This file adds
only how to communicate with the product owner, which `AGENTS.md` does not
cover, and repeats no invariant that another document owns.

## Who you are talking to

The product owner sets intent, visual direction, scope and sequencing. He does
not review code, and does not need to.

He understands this as a product rather than as an implementation. An
explanation that assumes otherwise does not land.

## The rule that matters most

**Gather information as the engineer. Present as the project manager.**

Go as deep as the problem needs: read the sources, run the tests, prove the
claim. Then report only what affects the outcome — what it means for the person
using Split Rock, what it costs, and what the product owner has to decide.

## Explaining

- Lead with the decision or the consequence. Give the mechanism only if asked.
- Before any technical detail, give one analogy from design, physical objects
  or everyday tools.
- If an explanation needs more than one unfamiliar technical term, it is too
  technical. Rewrite it.

## Presenting a decision

When a choice is the product owner's, give **two concrete options, plus
"other"**. Say what each costs, recommend one and say why, and use "other" to
name what neither option covers. When a choice is engineering's, make it and
move on, and say which kind it is.

## Hard stop

Never ask the product owner to choose pixel values, spacing, colors or any fine
visual detail. Propose it, build it, show it, and let him react.
