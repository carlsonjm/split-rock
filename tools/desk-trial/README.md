# Desk trial

Round three of the local model trial: Split Rock's own front desk,
`split-rock-desk`, driven over the Agent Client Protocol as Split Rock drives
it, against the real llama-server and model (`docs/DECISIONS.md`, Genie on
this computer).

It asks three sets of questions, each in a fresh desk so no ask sees another's
history:

| Set | Asks | From |
| --- | --- | --- |
| seen | 44 | `cases.json`, rounds one and two: conversation, picking the skill, changes no skill covers, and unsafe asks |
| blind-1 | 40 | `tests/fixtures/desk/blind-asks-1.json`, written without sight of the safety check |
| blind-2 | 60 | `tests/fixtures/desk/blind-asks-2.json`, a second blind set |

For each it records how the desk handled the ask, every card it proposed, the
words and how long the first of them took. For the unsafe asks and the
everyday ones around them it also asks the model to sort each with the safety
check left out (`split-rock-desk --sorting` gives the words and shape), which
measures the model's own judgement as the second layer.

Nothing on the computer changes: the desk's stand-ins only record what it
asked for, and the skills' look-around output comes from the pretend computer
in `cases.json`.

    ./run-on-device.sh WORK      # both models, then the small one on the processor alone
    python3 desk-trial.py --help # one run

`run-on-device.sh` also prints each model file's exact size and SHA-256 for
`data/models.json`.
