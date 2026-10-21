#!/bin/sh
# Turns an app that starts at sign-in off or back on, for the person only, the
# way the desktop's own Autostart page does: a copy of the app's entry in the
# person's autostart folder, marked Hidden, overrides the system's entry
# (XDG Autostart). An entry Split Rock wrote only to hide one is removed again
# when the app is turned back on, so the system's entry applies as before.
#
#   startup-app.sh off <name>     <name> is the entry's file name without .desktop
#   startup-app.sh on <name>
#   startup-app.sh state <name>   prints on, off or missing
set -u

mode=${1:-}
name=${2:-}
case $name in
    ''|*/*|.*) echo "No such app: $name" >&2; exit 2 ;;
esac
case $name in
    *[!A-Za-z0-9._@+-]*) echo "No such app: $name" >&2; exit 2 ;;
esac

own="${XDG_CONFIG_HOME:-$HOME/.config}/autostart"
mine="$own/$name.desktop"
system=
IFS=:
for dir in ${XDG_CONFIG_DIRS:-/etc/xdg}; do
    if [ -f "$dir/autostart/$name.desktop" ]; then
        system="$dir/autostart/$name.desktop"
        break
    fi
done
unset IFS

hidden() {
    grep -qis '^Hidden[[:space:]]*=[[:space:]]*true' "$1"
}

case $mode in
    state)
        if [ -f "$mine" ]; then
            if hidden "$mine"; then echo off; else echo on; fi
        elif [ -n "$system" ]; then
            if hidden "$system"; then echo off; else echo on; fi
        else
            echo missing
        fi ;;
    off)
        if [ -f "$mine" ]; then
            if ! grep -qi '^Hidden[[:space:]]*=' "$mine"; then
                # The key goes in the entry's own group, under its header.
                sed -i '/^\[Desktop Entry\]/a Hidden=true' "$mine"
            else
                sed -i 's/^Hidden[[:space:]]*=.*/Hidden=true/I' "$mine"
            fi
        elif [ -n "$system" ]; then
            mkdir -p "$own"
            printf '[Desktop Entry]\nHidden=true\nX-SplitRock-Hid=true\n' > "$mine"
        else
            echo "No app called $name starts at sign-in." >&2
            exit 1
        fi
        hidden "$mine" ;;
    on)
        if [ ! -f "$mine" ]; then
            [ -n "$system" ] && ! hidden "$system"
            exit $?
        fi
        if grep -qs '^X-SplitRock-Hid=true' "$mine"; then
            rm -f -- "$mine"
        else
            sed -i '/^Hidden[[:space:]]*=/Id' "$mine"
        fi
        [ ! -f "$mine" ] || ! hidden "$mine" ;;
    *)
        echo "usage: startup-app.sh off|on|state <name>" >&2
        exit 2 ;;
esac
