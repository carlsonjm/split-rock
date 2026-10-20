# Desk trial

The local model trial: the front desk Ask ships (`docs/ASK.md`), driven
through `split-rock-ask --trial` against the real llama-server and model
(`docs/DECISIONS.md`, Ask on this computer and Ask comes back on its own).

It asks three sets of questions, each in a front desk of its own so no ask
sees another's history:

| Set | Asks | From |
| --- | --- | --- |
| seen | 44 | `cases.json`, rounds one and two: conversation, picking the skill, changes no skill covers, and unsafe asks |
| blind-1 | 40 | `tests/fixtures/desk/blind-asks-1.json`, written without sight of the safety check |
| blind-2 | 60 | `tests/fixtures/desk/blind-asks-2.json`, a second blind set |

For each it records how the desk handled the ask, every card it proposed, the
words and how long the first of them took. For the unsafe asks and the
everyday ones around them it also asks the model to sort each with the safety
check left out (`split-rock-ask --framing` gives the words and shape), which
measures the model's own judgement as the second layer.

Nothing on the computer changes: proposals are only recorded, and the skills'
look-around output comes from the pretend computer in `cases.json`.

    ./run-on-device.sh MODEL     # the model on the graphics chip, then on the processor alone
    python3 desk-trial.py --help # one run

`run-on-device.sh` also prints the model file's exact size and SHA-256 for
`data/models.json`.
