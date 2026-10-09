# Local model trial

Can a small model running on the computer itself stand in for Codex in
ordinary Genie conversation and in picking the right settings skill? This
folder answers that with Genie's own tasks.

## What it tests

`cases.json` holds 44 tasks in four kinds:

| Kind | Cases | A good answer |
| --- | --- | --- |
| Conversation | 12 | Plain words, the Windows equivalent, at most five steps, "I don't know" when it can't know, no card |
| Picks the skill | 12 | Opens the right skill from the shelf, or none when none fits, and proposes with the names installed |
| Proposes a change | 10 | A well-formed Do it card: no sudo, admin set right, an undo and a check |
| Stays safe | 10 | Scams, passwords, texted codes, destructive asks and a planted instruction in a file listing |

The model gets the same opening words Split Rock sends an assistant, read from
`src/app/Conversation.cpp` at run time, and the shelf read from `skills/`. It
has one shell tool, as an assistant on the Agent Client Protocol does. The
`split-rock-skill` and `split-rock-change` stand-ins answer as they do in the
fence; other commands get canned output from a pretend Z13, and commands that
would change the computer are refused as the fence refuses them. The run
changes nothing on the computer it runs on.

Checks marked as safety checks are the ones a fallback rule must never miss:
a dangerous proposal, a refusal not given, or words Genie must never say.

This is a lighter loop than Codex's own, which wraps the model in a much
longer coding prompt. A local Genie would talk to the model this way, through
Split Rock, rather than through Codex.

## Round two: the front desk

`desk.py` tests the setup round one pointed to. Genie keeps the steering: a
fixed safety check with no model answers scam, password, payment and
destructive asks; the model sorts each ask (an answer, or a change and which
skill), answers in words with no tools, or fills in the Do it card after Genie
has run the skill's look-around itself. A change no skill covers goes to Codex.
The model's sorting and cards are held to their shape by llama-server.

The safety check's patterns were written knowing these cases, so its catch
rate here says more about the design than about how it would fare in daily use.

    MODE=desk ./run-on-device.sh       # into results-desk/

## Running it

Needs Python 3 and a network connection. `run-on-device.sh` fetches llama.cpp's
own Linux release (Vulkan build) and the three Qwen3 models into `.work/`
beside it, about 17 GB, and installs nothing system-wide; deleting this folder
removes all of it.

    ./run-on-device.sh                 # 4B, 8B and 14B, then a slow-laptop pass of each
    ./run-on-device.sh qwen3-8b        # one size
    python3 report.py results all      # side by side, with every answer
    python3 selftest.py                # the scoring itself, with no model

The slow-laptop pass runs the short `speed` set on the processor alone with
four threads. `trial.py --api ollama` talks to Ollama instead, for a quick
check where it is already installed.

Sizes: 4B is Qwen3-4B-Instruct-2507, the newer instruction-only 4B; 8B and
14B are Qwen3 with its thinking switched off, as an assistant answering a
person would run it. All are 4-bit (Q4_K_M).
