#!/usr/bin/env bash
# ShelterOps Desk Console — test runner
# Builds the shelterops_lib + test binaries inside Docker and runs the CTest suite.
# The Win32/DX11 GUI executable is not built or run here; see README.md.
#
# Usage:
#   ./run_tests.sh              # build + run all tests (unit + api)
#   ./run_tests.sh --build-only # compile without running tests
#   ./run_tests.sh --unit-only  # build + run unit tests only  (CTest label: unit)
#   ./run_tests.sh --api-only   # build + run api tests only   (CTest label: api)
#   ./run_tests.sh --verbose    # build + run all tests with full CTest output
#
# Options can be combined:
#   ./run_tests.sh --unit-only --verbose

set -euo pipefail
cd "$(dirname "$0")"

# ── Dependency check ──────────────────────────────────────────────────────
if ! command -v docker &>/dev/null; then
    echo "[run_tests.sh] ERROR: docker is not installed or not on PATH." >&2
    echo "[run_tests.sh] Install Docker Desktop and ensure it is running, then retry." >&2
    exit 1
fi

# ── Argument parsing ──────────────────────────────────────────────────────
BUILD_ONLY=0
UNIT_ONLY=0
API_ONLY=0
VERBOSE=0

for arg in "$@"; do
    case "$arg" in
        --build-only) BUILD_ONLY=1 ;;
        --unit-only)  UNIT_ONLY=1  ;;
        --api-only)   API_ONLY=1   ;;
        --verbose)    VERBOSE=1    ;;
        *)
            echo "[run_tests.sh] Unknown argument: $arg" >&2
            echo "Usage: $0 [--build-only] [--unit-only] [--api-only] [--verbose]" >&2
            exit 1
            ;;
    esac
done

if [[ "$UNIT_ONLY" -eq 1 && "$API_ONLY" -eq 1 ]]; then
    echo "[run_tests.sh] ERROR: --unit-only and --api-only are mutually exclusive." >&2
    exit 1
fi

# ── Build-only path ───────────────────────────────────────────────────────
if [[ "$BUILD_ONLY" -eq 1 ]]; then
    echo "[run_tests.sh] Building shelterops_lib and test binaries (no test execution)..."
    docker compose build build
    docker compose run --rm build
    echo "[run_tests.sh] Build complete."
    exit 0
fi

# ── Assemble ctest arguments ──────────────────────────────────────────────
# These are forwarded to the ctest invocation inside the test container.
# The container's CMD runs: ctest --test-dir build --output-on-failure --timeout 60
# We extend that via CTEST_EXTRA_ARGS environment variable read by docker-compose.
CTEST_ARGS="--output-on-failure --timeout 60"

if [[ "$VERBOSE" -eq 1 ]]; then
    CTEST_ARGS="$CTEST_ARGS --verbose"
fi

if [[ "$UNIT_ONLY" -eq 1 ]]; then
    CTEST_ARGS="$CTEST_ARGS -L unit"
    LABEL_DESC="unit"
elif [[ "$API_ONLY" -eq 1 ]]; then
    CTEST_ARGS="$CTEST_ARGS -L api"
    LABEL_DESC="api"
else
    LABEL_DESC="all"
fi

echo "[run_tests.sh] Building and running ${LABEL_DESC} tests..."
docker compose build test
CTEST_EXTRA_ARGS="$CTEST_ARGS" docker compose run --rm test
echo "[run_tests.sh] Tests passed (${LABEL_DESC})."
