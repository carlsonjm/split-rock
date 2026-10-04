#!/usr/bin/env bash
set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

python3 "${project_root}/tests/verify-public.py"
python3 "${project_root}/tests/verify-guide.py"

echo "Split Rock verification passed."
