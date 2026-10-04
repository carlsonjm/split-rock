#!/usr/bin/env bash
# Builds Milestone 0 and opens its window: Genie inside Shuffle, Split Rock
# elsewhere.
#
# The first run downloads the three assistants' adapters into this checkout's
# build folder. Everything this program fetches, and every assistant's sign-in,
# stays in build/m0; delete that folder to remove all of it. Nothing is
# installed on the system.
#
# Extra arguments go to the program, for example:
#   ./run.sh --pretend tests/pretend-scripts/signed-in.json
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${project_root}/build/m0"

for tool in cmake node npm; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "Split Rock needs ${tool}, which this computer does not have. Nothing was changed." >&2
        exit 1
    fi
done

generator=()
if command -v ninja >/dev/null 2>&1; then
    generator=(-G Ninja)
fi
cmake -S "${project_root}" -B "${build_dir}" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "${build_dir}" --parallel --target split-rock-m0 split-rock-pretend-assistant >/dev/null

"${project_root}/tools/fetch-assistants.sh" "${build_dir}/assistants"

args=()
for arg in "$@"; do
    # A script named relative to the checkout still works from anywhere.
    if [[ -f "${arg}" ]]; then
        arg="$(cd "$(dirname "${arg}")" && pwd)/$(basename "${arg}")"
    fi
    args+=("${arg}")
done

# The guide in the box: this checkout's own entries, and Shuffle's when its
# checkout sits beside this one.
guides=(--guide "${project_root}")
if [[ -d "${project_root}/../shuffle/guide" ]]; then
    guides+=(--guide "$(cd "${project_root}/../shuffle" && pwd)")
fi

exec "${build_dir}/split-rock-m0" \
    --assistants "${build_dir}/assistants" \
    --state "${build_dir}/sign-ins" \
    "${guides[@]}" \
    "${args[@]}"
