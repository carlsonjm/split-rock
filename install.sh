#!/usr/bin/env bash
# Installs Split Rock on this computer, for every account, from this checkout:
# the program, its desktop file and icon, the session bus's service file that
# lets Search start it, the guide and the tour, and the assistants' adapters.
# The pretend assistant is not installed.
#
# This is a developer install for the maintainer's own computer, so Milestone 1
# can be used every day; it is not Milestone 3's package. uninstall.sh removes
# it. Sign-ins and the notebook stay in the person's own folders either way.
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${project_root}/build/install"

for tool in cmake node npm; do
    if ! command -v "${tool}" >/dev/null 2>&1; then
        echo "Split Rock needs ${tool}, which this computer does not have. Nothing was changed." >&2
        exit 1
    fi
done
# The sandbox the assistant runs in, so that it can write only to the notebook.
if ! command -v bwrap >/dev/null 2>&1; then
    echo "Split Rock keeps the assistant in a sandbox, bubblewrap, which this computer does not have. Nothing was changed." >&2
    exit 1
fi

# Every check first: nothing is installed from a checkout that fails one.
"${project_root}/verify.sh"

"${project_root}/tools/fetch-assistants.sh" "${build_dir}/assistants"

generator=()
if command -v ninja >/dev/null 2>&1; then
    generator=(-G Ninja)
fi
cmake -S "${project_root}" -B "${build_dir}" "${generator[@]}" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DSPLIT_ROCK_ADAPTERS_DIR="${build_dir}/assistants" >/dev/null
cmake --build "${build_dir}" --parallel --target split-rock
sudo cmake --install "${build_dir}"

# Plasma's list of applications gains Split Rock.
kbuildsycoca6 >/dev/null 2>&1 || true

echo "Split Rock installed. Find it in the launcher; inside Shuffle it is Genie."
echo "A Split Rock already running keeps the old program until you log out or end it."
