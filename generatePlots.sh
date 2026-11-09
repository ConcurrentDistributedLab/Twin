#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

# Prefer project .venv over system python
if [ -x "${ROOT_DIR}/.venv/bin/python" ]; then
    PYTHON="${ROOT_DIR}/.venv/bin/python"
else
    PYTHON="python3"
    echo "WARNING: .venv not found; using system python3. Run ./install.sh for a local env."
fi

# Root directory containing result folders (override with first arg)
root_dir="${1:-results}"

if [ ! -d "$root_dir" ]; then
    echo "Directory $root_dir does not exist!"
    exit 1
fi

# All subdirectories that contain a plot_config.json
mapfile -t directories < <(find "$root_dir" -type f -name 'plot_config.json' -printf '%h\n' | sort -u)

if [ ${#directories[@]} -eq 0 ]; then
    echo "No plot_config.json found under $root_dir"
    exit 0
fi

for dir in "${directories[@]}"; do
    echo "Processing: $dir"
    "$PYTHON" "${ROOT_DIR}/plotGen/main.py" --root-dir "$dir" --config-file "${ROOT_DIR}/config.json"
    echo "Completed: $dir"
    echo "----------------------------------------"
done

echo "All processing completed!"
