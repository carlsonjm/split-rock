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
cmake --build "${build_dir}" --parallel --target split-rock split-rock-desk split-rock-core
# Shuffle's install key, where it is set up, installs with no password: the
# build is laid out here with this account's own rights, and a root-owned helper
# takes the files as a stream and puts only Split Rock's own in place.
install_key=/usr/local/libexec/shuffle/install-step
if [[ -x "${install_key}" ]] \
        && sudo -n -l "${install_key}" split-rock install >/dev/null 2>&1; then
    stage="$(mktemp -d "${TMPDIR:-/tmp}/split-rock-stage.XXXXXX")"
    trap 'rm -rf -- "${stage}"' EXIT
    # An earlier install with sudo left this list owned by root.
    rm -f -- "${build_dir}/install_manifest.txt"
    DESTDIR="${stage}" cmake --install "${build_dir}" >/dev/null
    tar -C "${stage}" -cf - . | sudo -n "${install_key}" split-rock install
    # The list then names where each file landed, as an install with sudo does.
    sed -i "s|^${stage}||" "${build_dir}/install_manifest.txt"
else
    sudo cmake --install "${build_dir}"
fi

# Plasma's list of applications gains Split Rock.
kbuildsycoca6 >/dev/null 2>&1 || true

# A Core already running for this account is replaced by the one just
# installed: ended, then started again through the session bus, as Search
# starts it. However it was started, the program it runs is the new one.
systemctl --user daemon-reload >/dev/null 2>&1 || true
if pgrep -x -u "$(id -u)" split-rock-core >/dev/null 2>&1; then
    systemctl --user stop split-rock-core.service >/dev/null 2>&1 || true
    pkill -x -u "$(id -u)" split-rock-core >/dev/null 2>&1 || true
    for _ in 1 2 3 4 5 6 7 8 9 10; do
        pgrep -x -u "$(id -u)" split-rock-core >/dev/null 2>&1 || break
        sleep 0.2
    done
    if dbus-send --session --print-reply --dest=io.github.carlsonjm.splitrock.Core /Core \
            io.github.carlsonjm.SplitRock.Core.ProtocolVersion >/dev/null 2>&1; then
        echo "Core restarted with the new program."
    fi
fi

echo "Split Rock installed. Find it in the launcher; inside Shuffle it is Genie."
echo "A Split Rock window already open keeps the old program until you close it or log out."
