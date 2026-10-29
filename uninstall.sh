#!/usr/bin/env bash
# Removes what install.sh installed, and nothing else.
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
manifest="${project_root}/build/install/install_manifest.txt"

# Every file install.sh puts in place, as CMakeLists.txt installs them, so the
# removal is whole even when the build folder's list is gone or out of date. A
# copy left behind would stop Shuffle's own package from installing over it.
# Shuffle's own guide and tour, beside Split Rock's, are not touched.
installed=(
    /usr/bin/split-rock
    /usr/share/applications/io.github.carlsonjm.SplitRock.desktop
    /usr/share/icons/hicolor/scalable/apps/io.github.carlsonjm.SplitRock.svg
    /usr/share/dbus-1/services/io.github.carlsonjm.splitrock.service
    /usr/share/dbus-1/services/io.github.carlsonjm.splitrock.Core.service
    /usr/share/dbus-1/services/io.github.carlsonjm.splitrock.Ask.service
    /usr/share/dbus-1/services/io.github.carlsonjm.splitrock.Broker.service
    /usr/lib/systemd/user/split-rock-core.service
    /etc/xdg/autostart/io.github.carlsonjm.splitrock.Core.desktop
)
folders=(
    /usr/lib/split-rock
    /usr/share/split-rock/guide/plasma
    /usr/share/split-rock/tour/plasma
    /usr/share/split-rock/skills/plasma
)
# And whatever the build folder's list adds, from a build newer than this list.
if [[ -f $manifest ]]; then
    while IFS= read -r listed; do
        case $listed in /usr/* | /etc/*) installed+=("$listed") ;; esac
    done < "$manifest"
fi

# Only the program, its desktop entries and the guide go; the notebook stays in
# the person's own folder.
install_key=/usr/local/libexec/shuffle/install-step
if [[ -x "${install_key}" ]] \
        && sudo -n -l "${install_key}" split-rock remove >/dev/null 2>&1; then
    # Shuffle's install key removes Split Rock's own files without a password,
    # and leaves another desktop's guide entries beside them alone.
    sudo -n "${install_key}" split-rock remove
    removed_by_key=true
fi
# A file a package owns is the package's to remove, never this script's.
packaged() {
    command -v pacman >/dev/null 2>&1 && pacman -Qqo -- "$1" >/dev/null 2>&1
}
present=()
for path in "${installed[@]}"; do
    if [[ -f $path || -L $path ]] && ! packaged "$path"; then
        present+=("$path")
    fi
done
for folder in "${folders[@]}"; do
    if [[ -d $folder ]] && ! packaged "$folder"; then
        present+=("$folder")
    fi
done
if ((${#present[@]} == 0)) && [[ -z ${removed_by_key:-} ]]; then
    echo "Nothing to remove: Split Rock is not installed." >&2
    exit 1
fi
if ((${#present[@]} > 0)); then
    sudo rm -rf -- "${present[@]}"
fi
# The folders install.sh made for Split Rock's own files, once empty.
for folder in /usr/share/split-rock/guide /usr/share/split-rock/tour \
        /usr/share/split-rock/skills /usr/share/split-rock; do
    if [[ -d $folder ]]; then
        sudo rmdir --ignore-fail-on-non-empty -- "$folder"
    fi
done
# A Core still running for this account ends with its program, and Ask too.
systemctl --user stop split-rock-core.service >/dev/null 2>&1 || true
pkill -x -u "$(id -u)" split-rock-ask >/dev/null 2>&1 || true
pkill -x -u "$(id -u)" split-rock-broker >/dev/null 2>&1 || true
pkill -x -u "$(id -u)" split-rock-core >/dev/null 2>&1 || true
systemctl --user daemon-reload >/dev/null 2>&1 || true
kbuildsycoca6 >/dev/null 2>&1 || true

echo "Split Rock removed. The notebook stays where it was."
