#!/usr/bin/env bash
# Activate .venv, run benchmarks, then generate plots.
# Usage: ./run.sh [config_file]
#   config_file  Experiment config (default: bash/config_main.json)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

CONFIG_FILE="${1:-bash/config_main.json}"

# Ensure bash and plot scripts are executable
for f in "${ROOT_DIR}/bash/"*.sh "${ROOT_DIR}/generatePlots.sh" "${ROOT_DIR}/run.sh"; do
    if [ -f "$f" ] && [ ! -x "$f" ]; then
        chmod +x "$f"
        echo "Made executable: $f"
    fi
done

# Enter project virtual environment
if [ -f "${ROOT_DIR}/.venv/bin/activate" ]; then
    # shellcheck disable=SC1091
    source "${ROOT_DIR}/.venv/bin/activate"
    echo "Activated virtual environment: ${ROOT_DIR}/.venv"
else
    echo "ERROR: .venv not found. Run ./install.sh first."
    exit 1
fi

if [ ! -f "$CONFIG_FILE" ]; then
    echo "ERROR: config file not found: $CONFIG_FILE"
    exit 1
fi

echo "############################################"
echo "Running benchmarks: ./bash/main.sh -c $CONFIG_FILE"
echo "############################################"
./bash/main.sh -c "$CONFIG_FILE"

echo ""
echo "############################################"
echo "Generating plots: ./generatePlots.sh"
echo "############################################"
./generatePlots.sh

echo ""
echo "Done."
