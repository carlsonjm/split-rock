---
id: services
title: Background services that failed - what they do, why they stopped, starting one again
also:
  - A service failed
  - Something didn't start
  - Why did this stop working?
  - Bluetooth stopped working
  - Printing stopped working
  - Restart the service
checked: unreleased
format: 1
---

## Look around

```sh
echo "System services that failed:"
systemctl --failed --type=service --no-legend --plain 2>/dev/null | cut -d' ' -f1
echo "Services of this account that failed:"
systemctl --user --failed --type=service --no-legend --plain 2>/dev/null | cut -d' ' -f1
for unit in $(systemctl --failed --type=service --no-legend --plain 2>/dev/null | cut -d' ' -f1); do
  echo "What $unit said last, since the computer started:"
  journalctl -b -u "$unit" -n 8 -o cat --no-pager 2>&1
done
for unit in $(systemctl --user --failed --type=service --no-legend --plain 2>/dev/null | cut -d' ' -f1); do
  echo "What $unit said last, since the person signed in:"
  journalctl --user -b -u "$unit" -n 8 -o cat --no-pager 2>&1
done
```

## Change

Only for a service listed above as failed. Say first, in plain words, what the
service does and what its last words mean; a service that fails at once again
will not be fixed by starting it, so then explain and offer no card.

To start one of the person's own again: `systemctl --user restart <service>`.

A system service can't be started again from a card: a card never runs a
command as the administrator. Say what the service does, and that restarting
the computer starts it again; offer no card.

## Undo

Starting a service again changes no setting, so there is nothing to put back:
leave "undo" empty.

## Check

`systemctl --user is-active <service>`, or `systemctl is-active <service>`.
