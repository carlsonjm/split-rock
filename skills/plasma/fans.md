---
id: fans
title: The fans and the power profile - quieter, cooler or faster
also:
  - The fan is too loud
  - Make it quieter
  - The computer gets hot
  - Turn on performance mode
  - Save battery
checked: unreleased
format: 1
---

## Look around

```sh
if command -v asusctl >/dev/null; then
  echo "This computer has asusctl."
  echo "Profile now: $(asusctl profile -p 2>&1)"
  echo "Profiles: $(asusctl profile -l 2>&1)"
fi
if command -v powerprofilesctl >/dev/null; then
  echo "Power profile now: $(powerprofilesctl get 2>&1)"
  echo "Power profiles:"
  powerprofilesctl list 2>&1
fi
```

## Change

On a computer with asusctl, its profile sets the fans: `asusctl profile -P
<name>`, with a name from its list, such as Quiet, Balanced or Performance.
Otherwise `powerprofilesctl set <name>`, with power-saver, balanced or
performance. Neither needs administrator rights for the person at the
computer.

Quieter fans mean a little less speed; say so on the card.

## Undo

The same command with the profile in use now, from the look above.

## Check

`asusctl profile -p`, or `powerprofilesctl get`.
