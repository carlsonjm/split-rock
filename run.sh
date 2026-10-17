#!/usr/bin/env bash
# Builds Split Rock and opens one Helper from this checkout, as Core opens it:
#
#   ./run.sh space
#   ./run.sh install --query "pdf editor"
#
# The Helper looks first and changes nothing until its button is tapped.
# Nothing is installed on the system.
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${project_root}/build/m0"

if [[ $# -lt 1 ]]; then
    echo "Name a Helper: update, space, install, health, changes, restore or community." >&2
    exit 2
fi

generator=()
if command -v ninja >/dev/null 2>&1; then
    generator=(-G Ninja)
fi
cmake -S "${project_root}" -B "${build_dir}" "${generator[@]}" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "${build_dir}" --parallel --target split-rock >/dev/null

card="$1"
shift
exec "${build_dir}/split-rock" --card "${card}" "$@"
