#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
REFERENCE_ROOT="${REFERENCE_REPO_ROOT:?Set REFERENCE_REPO_ROOT to the reference backend root}"

"$ROOT/build-debug/tightrope-streaming-contract-capture" \
  --reference-root "$REFERENCE_ROOT" \
  --fixture-root "$ROOT/native/tests/contracts/fixtures/streaming"
