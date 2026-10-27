#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# The capability trial on a real computer, with the model Ask downloaded.
#
#   run-on-device.sh [MODEL]
#
# MODEL defaults to the one Ask keeps in split-rock/models in the person's
# data folder; nothing is downloaded. LLAMA_SERVER overrides the llama-server
# used; Shuffle's own is tried first. On a computer whose only graphics chip is
# Intel's built-in one, the model runs on the processor, as Ask does there.
#
# It installs nothing and changes no settings: notes and mail are the made-up
# ones beside this file, a proposed note is only recorded, and the model
# server stops when its run ends. Results land in results/ beside this file.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
model=${1:-$data/split-rock/models/Qwen3-4B-Instruct-2507-Q4_K_M.gguf}
[ -f "$model" ] || { echo "No model at $model. Download it from Ask first, or give its path."; exit 1; }

server=${LLAMA_SERVER:-}
for candidate in /usr/lib/split-rock/engine/llama-server "$(command -v llama-server || true)"; do
    [ -n "$server" ] && break
    [ -x "$candidate" ] && server=$candidate
done
[ -n "$server" ] && [ -x "$server" ] || { echo "No llama-server found; set LLAMA_SERVER."; exit 1; }

layers=99
if command -v lspci >/dev/null && ! lspci | grep -Ei 'vga|3d|display' | grep -viq intel; then
    layers=0
fi
host=$(uname -n)
echo "Model $model"
echo "Engine $server"
echo "Layers on the graphics chip: $layers"

python3 -I "$here/capability-trial.py" --engine "$server" --model "$model" --layers "$layers" --label "qwen3-4b-$host"
# The processor alone with four threads, mail only: how long sorting takes on
# a computer with no usable graphics chip.
if [ "$layers" != 0 ]; then
    python3 -I "$here/capability-trial.py" --engine "$server" --model "$model" --layers 0 --threads 4 \
        --only mail --label "qwen3-4b-$host-cpu4"
fi
echo
echo "Results: $here/results"
