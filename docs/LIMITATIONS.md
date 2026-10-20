# Known limits

What Split Rock does not do yet, and what it will not do.

## Not yet

- Split Rock's package is built only inside Shuffle's own package source. On
  its own, the only install is the developer install for the maintainer's own
  computer (`install.sh`), which is not for handing to anyone else.
- The Helpers have been run in test sessions and on the maintainer's computer,
  not yet on a freshly installed one.
- English only.
- Core's Devices topic has been run against stand-ins for UDisks2, BlueZ and
  systemd, not yet with a real printer.
- Ask's front desk has been tested against a pretend model here. Its last
  trial against the real model was before it became a program of its own;
  `tools/desk-trial/` runs it again on a computer that has the model.
- Ask needs `llama-server` installed; nothing in Split Rock installs it.

## Not planned

- Handling anyone's account, payment or password.
- Acting without a card.
