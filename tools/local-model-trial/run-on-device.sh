#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Runs the local model trial on this computer with llama.cpp.
#
# It installs nothing system-wide and changes no settings. Everything it fetches,
# llama.cpp's own release and the models (about 17 GB), goes into .work/ beside
# this file, so deleting this folder removes all of it. Each model server is
# stopped when its run ends. Results land in results/.
#
#   ./run-on-device.sh               4B, 8B and 14B, then a slow-laptop pass of each
#   ./run-on-device.sh qwen3-8b      one model only
#
# Set LLAMA_SERVER to use a llama-server already on this computer instead.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
work="$here/.work"
port=8080
host="http://127.0.0.1:$port"
mkdir -p "$work" "$here/results"
export LLAMA_CACHE="$work/models"

# Each size, and where its quantized model is published.
all="qwen3-4b=unsloth/Qwen3-4B-Instruct-2507-GGUF:Q4_K_M
qwen3-8b=Qwen/Qwen3-8B-GGUF:Q4_K_M
qwen3-14b=Qwen/Qwen3-14B-GGUF:Q4_K_M"
wanted=${*:-"qwen3-4b qwen3-8b qwen3-14b"}

python3 "$here/selftest.py"

# llama.cpp itself: the newest release built for Linux with Vulkan, which
# reaches the graphics chip through the driver the desktop already uses.
server=${LLAMA_SERVER:-$(find "$work/engine" -name llama-server -type f 2>/dev/null | head -n1)}
if [ -z "$server" ]; then
    echo "Fetching llama.cpp..."
    python3 - "$work/engine" <<'EOF'
import io, json, os, sys, tarfile, urllib.request, zipfile
dest = sys.argv[1]
release = json.load(urllib.request.urlopen("https://api.github.com/repos/ggml-org/llama.cpp/releases/latest"))
asset = next(a for a in release["assets"] if "ubuntu-vulkan-x64" in a["name"])
print(release["tag_name"], asset["name"])
data = urllib.request.urlopen(asset["browser_download_url"]).read()
os.makedirs(dest, exist_ok=True)
if asset["name"].endswith(".zip"):
    zipfile.ZipFile(io.BytesIO(data)).extractall(dest)
else:
    tarfile.open(fileobj=io.BytesIO(data)).extractall(dest, filter="data")
EOF
    server=$(find "$work/engine" -name llama-server -type f | head -n1)
    chmod +x "$(dirname "$server")"/llama-* 2>/dev/null || true
fi
export LD_LIBRARY_PATH="$(dirname "$server")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
"$server" --version 2>&1 | tail -n2

gpu_used() {
    cat /sys/class/drm/card*/device/mem_info_vram_used /sys/class/drm/card*/device/mem_info_gtt_used 2>/dev/null \
        | awk '{s += $1} END {print s + 0}'
}

pid=""
stop_server() {
    [ -n "$pid" ] && kill "$pid" 2>/dev/null && wait "$pid" 2>/dev/null || true
    pid=""
}
trap stop_server EXIT INT TERM

# start_server <repo:quant> <log> [extra flags]: waits until the model is
# loaded, which on the first run includes downloading it.
start_server() {
    repo=$1; log=$2; shift 2
    "$server" -hf "$repo" --jinja -c 8192 --port "$port" --host 127.0.0.1 "$@" >"$log" 2>&1 &
    pid=$!
    for _ in $(seq 3600); do
        curl -fsS "$host/health" >/dev/null 2>&1 && return 0
        kill -0 "$pid" 2>/dev/null || { echo "llama-server stopped; see $log"; tail -n 20 "$log"; return 1; }
        sleep 1
    done
    echo "llama-server did not come up within an hour; see $log"
    return 1
}

for name in $wanted; do
    repo=$(printf '%s\n' "$all" | sed -n "s|^$name=||p")
    [ -n "$repo" ] || { echo "Unknown model $name"; exit 1; }

    # Quality and speed as this computer runs it, graphics chip included.
    before=$(gpu_used)
    start_server "$repo" "$work/$name.log" -ngl 99
    python3 "$here/trial.py" --model "$name" --gpu-before "$before"
    stop_server

    # Speed on a slow laptop: processor only, four threads, the short set.
    start_server "$repo" "$work/$name-cpu4.log" -ngl 0 -t 4
    python3 "$here/trial.py" --model "$name" --cpu --threads 4 --only speed --label "$name-cpu4"
    stop_server
done

python3 "$here/report.py" "$here/results" all > "$here/results/report.md"
python3 "$here/report.py" "$here/results" table
echo "Full report: $here/results/report.md"
