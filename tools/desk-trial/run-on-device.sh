#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# The trial on a real computer: builds split-rock-ask from this checkout and
# runs desk-trial.py against the model, then prints the model file's size and
# SHA-256 for data/models.json.
#
#   run-on-device.sh MODEL
#
# MODEL is the downloaded model file, such as the one Ask keeps in
# split-rock/models in the person's data folder; nothing is downloaded here.
# LLAMA_SERVER overrides the llama-server used.
#
# It installs nothing and changes no settings: the build goes to build/trial in
# this checkout, proposals are only recorded, and the model server stops when
# its run ends. Results land in results/ beside this file.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
model=${1:?usage: run-on-device.sh MODEL}
[ -f "$model" ] || { echo "No model at $model."; exit 1; }

server=${LLAMA_SERVER:-$(command -v llama-server || true)}
[ -x "$server" ] || { echo "No llama-server found; set LLAMA_SERVER."; exit 1; }

cmake -S "$root" -B "$root/build/trial" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF >/dev/null
cmake --build "$root/build/trial" --target split-rock-ask --parallel >/dev/null
ask="$root/build/trial/split-rock-ask"

"$server" --version 2>&1 | tail -n2
"$server" --list-devices 2>&1 | tail -n4

run() {
    python3 "$here/desk-trial.py" --ask "$ask" --engine "$server" --model "$model" "$@"
}
run --label qwen3-4b
# A computer with no usable graphics chip: the model on the processor alone
# with four threads, the short set.
run --layers 0 --threads 4 --only speed --label qwen3-4b-cpu4

echo
echo "For data/models.json:"
printf '%s  bytes %s  sha256 %s\n' "$(basename "$model")" "$(stat -c %s "$model")" \
    "$(sha256sum "$model" | cut -d' ' -f1)"
