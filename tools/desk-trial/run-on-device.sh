#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Round three on a real computer: builds split-rock-desk from this checkout and
# runs desk-trial.py against both models, then prints each model file's size
# and SHA-256 for data/models.json.
#
#   run-on-device.sh WORK
#
# WORK is round two's .work folder (tools/local-model-trial/.work on the
# trial's branch), which holds llama.cpp and the models already downloaded;
# nothing is downloaded here. LLAMA_SERVER overrides the llama-server used.
#
# It installs nothing and changes no settings: the build goes to build/trial in
# this checkout, the desk's stand-ins only record, and each model server stops
# when its run ends. Results land in results/ beside this file.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
work=${1:?usage: run-on-device.sh WORK}

server=${LLAMA_SERVER:-$(find -L "$work" -name llama-server -type f | head -n1)}
[ -x "$server" ] || { echo "No llama-server under $work; set LLAMA_SERVER."; exit 1; }
export LD_LIBRARY_PATH="$(dirname "$server")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
small=$(find -L "$work" -iname '*Qwen3-4B-Instruct-2507-Q4_K_M*.gguf' -type f | head -n1)
large=$(find -L "$work" -iname '*Qwen3-8B-Q4_K_M*.gguf' -type f | head -n1)
[ -n "$small" ] && [ -n "$large" ] || { echo "The 4B and 8B models aren't both under $work."; exit 1; }

cmake -S "$root" -B "$root/build/trial" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF >/dev/null
cmake --build "$root/build/trial" --target split-rock-desk --parallel >/dev/null
desk="$root/build/trial/split-rock-desk"

"$server" --version 2>&1 | tail -n2
"$server" --list-devices 2>&1 | tail -n4

run() {
    python3 "$here/desk-trial.py" --desk "$desk" --engine "$server" "$@"
}
run --model "$small" --label qwen3-4b
run --model "$large" --label qwen3-8b
# A computer with no usable graphics chip: the small model on the processor
# alone with four threads, the short set.
run --model "$small" --layers 0 --threads 4 --only speed --label qwen3-4b-cpu4

echo
echo "For data/models.json:"
for model in "$large" "$small"; do
    printf '%s  bytes %s  sha256 %s\n' "$(basename "$model")" "$(stat -c %s "$model")" \
        "$(sha256sum "$model" | cut -d' ' -f1)"
done
