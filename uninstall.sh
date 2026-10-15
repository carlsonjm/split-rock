#!/usr/bin/env bash
# Removes what install.sh installed from this checkout, and nothing else.
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
manifest="${project_root}/build/install/install_manifest.txt"

if [[ ! -f $manifest ]]; then
    echo "Nothing to remove: Split Rock was not installed from this folder." >&2
    exit 1
fi

# Only the program, its desktop entries, the guide and the adapters go; the
# notebook and every assistant's sign-in stay in the person's own folders.
install_key=/usr/local/libexec/shuffle/install-step
if [[ -x "${install_key}" ]] \
        && sudo -n -l "${install_key}" split-rock remove >/dev/null 2>&1; then
    # Shuffle's install key removes Split Rock's own files without a password,
    # and leaves another desktop's guide entries beside them alone.
    sudo -n "${install_key}" split-rock remove
else
    folders=()
    while IFS= read -r installed; do
        [[ -z $installed ]] && continue
        if [[ -f $installed || -L $installed ]]; then
            sudo rm -- "$installed"
        fi
        case $installed in
            */split-rock/*) folders+=("${installed%%/split-rock/*}/split-rock") ;;
        esac
    done < "$manifest"
    # The folders install.sh made for Split Rock's own files, once empty.
    for folder in $(printf '%s\n' "${folders[@]}" | sort -u); do
        if [[ -d $folder ]]; then
            sudo find "$folder" -depth -type d -empty -delete
        fi
    done
fi
# A Core still running for this account ends with its program.
systemctl --user stop split-rock-core.service >/dev/null 2>&1 || true
pkill -x -u "$(id -u)" split-rock-core >/dev/null 2>&1 || true
systemctl --user daemon-reload >/dev/null 2>&1 || true
kbuildsycoca6 >/dev/null 2>&1 || true

echo "Split Rock removed. The notebook and your assistants' sign-ins stay where they were."
