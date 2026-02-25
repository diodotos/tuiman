#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN_FRONTEND="${SCRIPT_DIR}/tuiman"
BIN_BACKEND="${SCRIPT_DIR}/tuiman-backend"

if [ ! -f "${BIN_FRONTEND}" ]; then
  echo "Expected frontend launcher at ${BIN_FRONTEND}" >&2
  echo "Run this script from an extracted rewrite release folder." >&2
  exit 1
fi

if [ ! -f "${BIN_BACKEND}" ]; then
  echo "Expected backend binary at ${BIN_BACKEND}" >&2
  echo "Run this script from an extracted rewrite release folder." >&2
  exit 1
fi

PREFIX="${TUIMAN_PREFIX:-$HOME/.local}"
BINDIR="${PREFIX}/bin"
TARGET="${BINDIR}/tuiman"
TARGET_BACKEND="${BINDIR}/tuiman-backend"

mkdir -p "${BINDIR}"
install -m 0755 "${BIN_FRONTEND}" "${TARGET}"
install -m 0755 "${BIN_BACKEND}" "${TARGET_BACKEND}"

echo "Installed: ${TARGET}"
echo "Installed: ${TARGET_BACKEND}"

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
echo "  ${TARGET_BACKEND} --version"
