# The mock-up

The approved visual direction (`../DECISIONS.md`), drawn inside Shuffle, where
the assistant is Genie and its mark is the lamp in `../../assets/mark.svg`. Five
screens, each a 1280×800 tablet frame, kept as design source: open a
file to read its exact layout, sizes, colours and copy. They were drawn as live
artboards and need the canvas that drew them to render; they are not part of the
program.

| Screen | What it shows | Where it applies |
| --- | --- | --- |
| `JustType.dc.html` | One field, already focused; answers in short steps; Do it, Show me how, Keep this; Expand | The answer and its buttons; the sheet from the bottom is superseded by a card the size of the search (`../DECISIONS.md`) |
| `DoIt.dc.html` | The Do it card: what changes, why, undo, password, Show details | Milestone 0 shows it for a refused request, with Do it unavailable; working in Milestone 2 |
| `Workspace.dc.html` | Expanded into a card: the conversation beside the record of this computer, with what was read marked | The full view Expand opens, from Milestone 0; the record in Milestone 1, and Undo from it in Milestone 2 |
| `Notes.dc.html` | Working on a Gooseberry note together, proposed lines marked on the note | With Gooseberry's notes tool, later |
| `Search.dc.html` | Search's doors, Notes and Genie among them, each behind a checkbox | Tettegouche's to build |

The screens are drawn inside Shuffle, in Shuffle's appearance, with Shuffle's
dock button. On its own, Split Rock follows the desktop's theme; the layout,
sizes, touch targets and behaviour are what the mock-up fixes. Sample answers
and records are illustrative.

The sheet that rose from the bottom in `JustType.dc.html` is superseded: the
on-screen keys covered it. The assistant's window is now one card the size of
Shuffle's Search, centred over the work, with the name, the question and Expand
in its header; the answer, its steps and its buttons below are as
`JustType.dc.html` draws them, and Expand grows the card into the full view of
`Workspace.dc.html`, without its title bar. What the card is, and why, is in
`../DECISIONS.md`.
