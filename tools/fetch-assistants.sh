#!/usr/bin/env bash
# Downloads the pinned assistant adapters into one folder, each in its own
# subfolder, with npm's cache kept inside that folder too. Nothing is installed
# system-wide and nothing outside the target folder is written.
set -euo pipefail

target="${1:?usage: fetch-assistants.sh TARGET_DIR}"

# The versions Milestone 0 was built and checked against. Change one only
# together with docs/ASSISTANTS.md.
declare -A packages=(
    [codex]="@zed-industries/codex-acp@0.16.0"
)

if ! command -v npm >/dev/null 2>&1; then
    echo "npm was not found. The assistants run on Node.js, which this computer does not have." >&2
    exit 1
fi

mkdir -p "${target}"
for name in codex; do
    spec="${packages[${name}]}"
    dir="${target}/${name}"
    stamp="${dir}/.fetched"
    if [[ -f "${stamp}" && "$(cat "${stamp}")" == "${spec}" ]]; then
        continue
    fi
    echo "Fetching ${spec} ..."
    mkdir -p "${dir}"
    npm install \
        --prefix "${dir}" \
        --cache "${target}/.npm-cache" \
        --no-audit --no-fund --no-update-notifier \
        --ignore-scripts --loglevel=error \
        "${spec}" >/dev/null
    echo "${spec}" > "${stamp}"
done
