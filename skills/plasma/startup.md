---
id: startup
title: Apps and services that start at sign-in - see them, turn one off or back on
also:
  - What starts when I sign in?
  - Stop this app opening at startup
  - Signing in is slow
  - Too many apps open when I log in
  - Turn off a startup app
checked: unreleased
format: 1
---

## Look around

```sh
config="${XDG_CONFIG_HOME:-$HOME/.config}"
echo "Apps that start at sign-in, as name: what it is (on or off):"
seen=
for f in "$config"/autostart/*.desktop /etc/xdg/autostart/*.desktop; do
  [ -e "$f" ] || continue
  file=$(basename "$f" .desktop)
  case " $seen " in *" $file "*) continue ;; esac
  case $file in org.kde.*|*plasma*|*kwallet*|polkit*|*xembed*|gmenudbusmenuproxy|baloo*|kaccess|at-spi*|geoclue*|xdg-*|pipewire*|wireplumber*|ibus*|fcitx*|im-launch|klipper|nvidia*|spice*|vmware*) continue ;; esac
  grep -qs '^X-KDE-autostart-phase=' "$f" && continue
  seen="$seen $file"
  label=$(grep -m1 '^Name=' "$f" | cut -d= -f2-)
  [ -z "$label" ] && label=$(grep -m1 -s '^Name=' "/etc/xdg/autostart/$file.desktop" | cut -d= -f2-)
  state=on
  grep -qis '^Hidden *= *true' "$f" && state=off
  grep -qs '^NotShowIn=.*KDE' "$f" && continue
  grep -qs '^OnlyShowIn=' "$f" && ! grep -qs '^OnlyShowIn=.*KDE' "$f" && continue
  echo "$file: ${label:-$file} ($state)"
done
echo "Background services of this account that start at sign-in:"
systemctl --user list-unit-files --type=service --state=enabled --no-legend --plain 2>/dev/null | cut -d' ' -f1 \
  | grep -v -E '^(plasma-|pipewire|wireplumber|xdg-|dbus|at-spi|kwallet|gnome-keyring|p11-kit|gpg-agent|systemd-|ssh-agent|obex|app-org.kde)'
```

## Change

An app from the list above, by the name before the colon:
`sh {here}/startup-app.sh off <app>` stops it starting at sign-in, for this
person only. It changes nothing else, and the app stays installed.

One of the person's own background services from the list above:
`systemctl --user disable --now <service>`.

Neither needs administrator rights. Say on the card
that the app still opens normally when the person opens it, and that it takes
effect at the next sign-in. Parts of the desktop itself, such as sound, the
password prompt and the on-screen keyboard, are left out of the list above on
purpose, and are never turned off: explain what they do instead.

To turn one back on, the same commands with `on` and `enable --now`.

## Undo

`sh {here}/startup-app.sh on <app>`, or `systemctl --user enable --now <service>`,
with the same name as the change.

## Check

`sh {here}/startup-app.sh state <app>`, or `systemctl --user is-enabled <service>`.
