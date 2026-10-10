---
id: settings-files-after-an-update
title: An update left new settings files beside mine
also:
  - Updates left new versions of these settings files beside mine
  - What are pacnew files
  - Which pacnew files matter
  - Settings files have a new version
checked: unreleased
format: 1
---

## What you see

After an update, the health check or the Update card says some settings files
have a new version beside yours. Their names end in .pacnew, such as
mirrorlist.pacnew or resolv.conf.pacnew.

## Why

When an update brings a new default version of a settings file you already
have, it never overwrites yours: it puts the new one beside it, ending in
.pacnew. Nothing applies them on their own, and there is no button in System
Settings that does. Your own versions stay in use, so the computer keeps
working as it did. Windows has nothing quite like this; it would simply have
kept your settings.

Most of them can wait. The lists of download servers (the mirrorlist files)
only matter if updates start failing to download. The package manager's own
settings, the internet name settings (resolv.conf) and the
printer settings (cupsd.conf) are best compared line by line before anything
is swapped, because a wrong swap can stop updates, the internet or printing.

## What to do

1. Carry on: nothing needs doing today, and the computer is using your own versions.
2. Don't delete them or swap one for yours by hand.
3. To tidy them up, ask Codex to compare each new file with yours. It shows a card for each change, and nothing changes until you tap Do it.
