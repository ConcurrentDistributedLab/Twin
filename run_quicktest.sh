#!/usr/bin/env bash
# Quick environment sanity check for Twin.
# Verifies tools, .venv Python deps, and a smoke compile/run.
# This does NOT run full paper/benchmark suites.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

PASS=0
FAIL=0

ok()  { echo "  [OK] $1"; PASS=$((PASS + 1)); }
bad() { echo "  [FAIL] $1"; FAIL=$((FAIL + 1)); }

echo "############################################"
echo "Twin quick test - environment readiness"
echo "############################################"
echo ""

echo "[1/4] Checking required tools..."
for cmd in g++ make jq bc python3; do
    if command -v "$cmd" >/dev/null 2>&1; then
        ok "$cmd found ($(command -v "$cmd"))"
    else
        bad "$cmd missing - run ./install.sh"
    fi
done
echo ""

echo "[2/4] Checking Python .venv..."
VENV_PY="${ROOT_DIR}/.venv/bin/python"
if [ -x "$VENV_PY" ]; then
    ok ".venv python found"
    if "$VENV_PY" -c "import matplotlib, numpy, pandas" 2>/dev/null; then
        ok "matplotlib, numpy, pandas importable in .venv"
    else
        bad "Python packages missing in .venv - run ./install.sh"
    fi
else
    bad ".venv not found - run ./install.sh"
fi
echo ""

echo "[3/4] Compiling Twin_List.out (smoke build)..."
if make Twin_List.out >/tmp/twin_quicktest_build.log 2>&1; then
    ok "Twin_List.out built"
else
    bad "build failed - see /tmp/twin_quicktest_build.log"
    echo "---- build log (tail) ----"
    tail -n 40 /tmp/twin_quicktest_build.log || true
fi
echo ""

echo "[4/4] Running binary help smoke check..."
if [ -x "${ROOT_DIR}/Twin_List.out" ]; then
    if ./Twin_List.out -h >/tmp/twin_quicktest_help.log 2>&1; then
        ok "./Twin_List.out -h succeeded"
    elif grep -qiE 'usage|options|-t' /tmp/twin_quicktest_help.log; then
        ok "./Twin_List.out -h printed usage"
    else
        bad "./Twin_List.out -h failed"
    fi
else
    bad "Twin_List.out not executable"
fi
echo ""

echo "############################################"
echo "Quick test summary: ${PASS} passed, ${FAIL} failed"
echo "############################################"

if [ "$FAIL" -eq 0 ]; then
    echo "[PASS] Environment is ready for work."
    echo "   Activate Python: source .venv/bin/activate"
    echo "   Full build:      make all"
    echo "   Benchmarks:      cd bash && ./main.sh help"
    exit 0
else
    echo "[FAIL] Environment is not ready. Fix the failures above (usually ./install.sh)."
    exit 1
fi
