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
  systemd, not yet with a real printer. Game controllers and second screens
  have been run against a stand-in of the kernel's folders, not yet with a
  real controller or monitor.
- Workspace Memory has been run against a stand-in for Cards on the bus. It
  needs Kadunce's Workspace Memory calls, which ship after 1.0, and sign-out
  rounds on a tablet before anyone relies on it.
- Automations have been run against a stand-in for Cards' record of apps
  opened together. The offer's wording is a draft until it is approved.
- Saved layouts have been run against a stand-in for Cards on the bus. They
  need Kadunce's layout calls and Search's Layouts island, which ship after
  the 6 November launch.
- The Health check's Battery row has been run against a stand-in power supply
  folder and a stand-in UPower, not yet on a computer whose battery has worn.
- Ask's front desk has been tested against a pretend model here. Its last
  trial against the real model was before it became a program of its own;
  `tools/desk-trial/` runs it again on a computer that has the model.
- Ask needs `llama-server` installed. Split Rock never installs it on its own; Ask's first card offers it through Install an app, after the person's yes.
- Codex has been tested here against a pretend assistant. Its ChatGPT sign-in
  through the fence, and what it costs, are still to be checked live.
- Codex needs its adapter, `codex-acp`, installed; nothing in Split Rock
  installs it.

- The broker is version 0. It has been tested against a pretend model and the
  pretend assistant, not yet against the real model or Codex, and Mail is the
  only app that calls it for the models.
- The apps' tools (`BROKER.md`, Tools) have been tested against a stand-in
  Notes, the pretend model and the pretend assistant only. Still to do: the
  trial of about 40 note questions against the real model on the tablet, a
  pass mark for it agreed before it runs, and a check with the real Notes once
  it ships `Read`, `Create` and its `notes.json`.
- Codex with the apps' tools is still to be checked live. The codex-acp
  adapter Shuffle ships (2.5.0) reads stdio tool servers from the session's
  `mcpServers` and asks before each call with the title `mcp.apps.<tool>`, as
  read from its source, but no real Codex has yet started
  `split-rock-tools-mcp` in the fence, listed its tools or had a call allowed.
- When Ask finds where something was written down, it names the note but
  can't open it with one tap: Search has no way yet to show a tap that opens
  a note from Ask's answer.
- Search shows no Do it card from Ask yet, so a card from an app's tool, such
  as saving an answer to Notes, waits for Search to show it and hand its token
  to the broker's `Run`.

## Not planned

- Handling anyone's account, payment or password.
- Changing the computer without the person's yes on a Helper.
