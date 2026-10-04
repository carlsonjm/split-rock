#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${project_root}/build/verify"

python3 "${project_root}/tests/verify-public.py"

# Builds Milestone 0 and runs every test. The tests use the pretend assistant
# only, with a home folder of their own inside the build.
generator=()
if command -v ninja >/dev/null 2>&1; then
    generator=(-G Ninja)
fi
cmake -S "${project_root}" -B "${build_dir}" "${generator[@]}" -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "${build_dir}" --parallel
ctest --test-dir "${build_dir}" --output-on-failure

echo "Split Rock verification passed."
