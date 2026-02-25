#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
C_WORKTREE="${ROOT_DIR}/../tui-man"

echo "[parity] C baseline CLI flags"
"${C_WORKTREE}/build/tuiman" --version
"${C_WORKTREE}/build/tuiman" --help >/tmp/tuiman-c-help.txt

echo "[parity] TypeScript launcher CLI flags"
"${ROOT_DIR}/scripts/tuiman" --version
"${ROOT_DIR}/scripts/tuiman" --help >/tmp/tuiman-ts-help.txt

echo "[parity] Frontend typecheck"
bun run --cwd "${ROOT_DIR}/frontend" typecheck

echo "[parity] TypeScript service smoke"
"${ROOT_DIR}/tools/parity/ts-smoke.sh"

echo "[parity] smoke checks complete"
