#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_SOURCE="${SCRIPT_DIR}/tuiman"

if [ ! -f "${BIN_SOURCE}" ]; then
  echo "Expected binary at ${BIN_SOURCE}" >&2
  echo "Run this script from an extracted release folder." >&2
  exit 1
fi

PREFIX="${TUIMAN_PREFIX:-$HOME/.local}"
BINDIR="${PREFIX}/bin"
TARGET="${BINDIR}/tuiman"

mkdir -p "${BINDIR}"
install -m 0755 "${BIN_SOURCE}" "${TARGET}"

echo "Installed: ${TARGET}"

case ":${PATH}:" in
  *":${BINDIR}:"*)
    ;;
  *)
    echo
    echo "${BINDIR} is not currently on PATH."
    echo "Add this to your shell profile:"
    echo "  export PATH=\"${BINDIR}:\$PATH\""
    ;;
esac

echo
echo "Verify with:"
echo "  ${TARGET} --version"
