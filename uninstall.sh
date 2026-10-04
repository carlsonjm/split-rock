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
kbuildsycoca6 >/dev/null 2>&1 || true

echo "Split Rock removed. The notebook and your assistants' sign-ins stay where they were."
