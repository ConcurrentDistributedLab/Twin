#!/usr/bin/env bash
# Install system packages and a project-local Python .venv for Twin.
# Intended for a fresh Ubuntu/Debian machine (tested pattern: Ubuntu 22.04/24.04).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

LIST_OF_APPS="build-essential g++ make libnuma-dev python3 python3-pip python3-venv jq bc numactl"

echo "############################################"
echo "Installing required system packages:"
echo "  $LIST_OF_APPS"
echo "############################################"

if command -v apt-get >/dev/null 2>&1; then
    if [ "$(id -u)" -eq 0 ]; then
        apt-get update
        apt-get install -y $LIST_OF_APPS
    else
        sudo apt-get update
        sudo apt-get install -y $LIST_OF_APPS
    fi
else
    echo "ERROR: apt-get not found. Install the packages above with your package manager, then re-run."
    exit 1
fi

VENV_DIR="${ROOT_DIR}/.venv"
PYTHON_BIN="${PYTHON_BIN:-python3}"

echo "############################################"
echo "Creating Python virtual environment at .venv"
echo "############################################"

if [ ! -d "$VENV_DIR" ]; then
    "$PYTHON_BIN" -m venv "$VENV_DIR"
else
    echo ".venv already exists; reusing it."
fi

# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"

echo "############################################"
echo "Installing Python packages into .venv"
echo "############################################"

python -m pip install --upgrade pip
python -m pip install -r "${ROOT_DIR}/requirements.txt"

echo ""
echo "Installation complete."
echo ""
echo "Next steps:"
echo "  1) Activate the venv:  source .venv/bin/activate"
echo "  2) Sanity-check setup: ./run_quicktest.sh"
echo "  3) Build everything:   make all"
