#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAUNCHER_SOURCE="${SCRIPT_DIR}/tuiman"
FRONTEND_SOURCE="${SCRIPT_DIR}/frontend"

if [ ! -f "${LAUNCHER_SOURCE}" ]; then
  echo "Expected frontend launcher at ${LAUNCHER_SOURCE}" >&2
  echo "Run this script from an extracted rewrite release folder." >&2
  exit 1
fi

if [ ! -d "${FRONTEND_SOURCE}" ]; then
  echo "Expected frontend directory at ${FRONTEND_SOURCE}" >&2
  echo "Run this script from an extracted rewrite release folder." >&2
  exit 1
fi

PREFIX="${TUIMAN_PREFIX:-$HOME/.local}"
BINDIR="${PREFIX}/bin"
LIBEXECDIR="${PREFIX}/libexec/tuiman"
TARGET="${BINDIR}/tuiman"

mkdir -p "${BINDIR}" "${LIBEXECDIR}"

rm -rf "${LIBEXECDIR}/frontend"
cp -R "${FRONTEND_SOURCE}" "${LIBEXECDIR}/frontend"
install -m 0755 "${LAUNCHER_SOURCE}" "${LIBEXECDIR}/tuiman"
ln -sf "${LIBEXECDIR}/tuiman" "${TARGET}"

echo "Installed: ${TARGET}"
echo "Installed assets: ${LIBEXECDIR}"

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
