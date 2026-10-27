# Known limits

What Split Rock does not do yet, and what it will not do.

## Not yet

- Split Rock has no package of its own; Shuffle builds one inside its own
  package source. On its own, the only install is the developer install
  (`install.sh`), which is not for handing to anyone else.
- The troubleshooting guide and the tour are installed, but no Helper shows
  them since Split Rock stopped holding a conversation (`DECISIONS.md`,
  Genie's chat is gone).
- The Helpers have been run in test sessions and on the maintainer's computer,
  not yet on a freshly installed one.
- English only.
- Core's Devices topic has been run against stand-ins for UDisks2, BlueZ and
  systemd, not yet with a real printer.
- Ask's front desk has been tested against a pretend model here. Its last
  trial against the real model was before it became a program of its own;
  `tools/desk-trial/` runs it again on a computer that has the model.
- Ask needs `llama-server` installed; nothing in Split Rock installs it.
- Codex has been tested here against a pretend assistant. Its ChatGPT sign-in
  through the fence, and what it costs, are still to be checked live.
- Codex needs its adapter, `codex-acp`, installed; nothing in Split Rock
  installs it.

- The broker is version 0. It has been tested against a pretend model and the
  pretend assistant, not yet against the real model or Codex, and Mail is the
  only app that calls it.

## Not planned

- Handling anyone's account, payment or password.
- Changing the computer without the person's yes on a Helper.
