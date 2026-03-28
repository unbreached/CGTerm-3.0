#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="${PREFIX:-/usr/local}"
BINDIR="${BINDIR:-$PREFIX/bin}"
DATADIR="${DATADIR:-$PREFIX/share/cgterm/assets}"
DESTDIR="${DESTDIR:-}"
NEED_ROOT=0
SUDO=""

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || { echo "[!] Missing command: $1" >&2; exit 1; }
}

check_sdl() {
  need_cmd sdl-config
  ver="$(sdl-config --version)"
  case "$ver" in
    1.2*) echo "[*] Found SDL $ver" ;;
    *) echo "[!] CGTerm expects SDL 1.2, found $ver" >&2; exit 1 ;;
  esac
}

if [[ ! -w "$(dirname "$BINDIR")" || ! -w "$(dirname "$DATADIR")" ]]; then
  NEED_ROOT=1
fi
if [[ -n "$DESTDIR" ]]; then
  NEED_ROOT=0
fi

check_sdl

echo "[*] Building CGTerm 3.0"
make -C "$ROOT_DIR" clean all

if [[ "$NEED_ROOT" -eq 1 && "${EUID}" -ne 0 ]]; then
  if command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
  else
    echo "[!] Root privileges required for install into $PREFIX" >&2
    echo "    Re-run as root or set PREFIX=$HOME/.local" >&2
    exit 1
  fi
fi

echo "[*] Installing binaries into ${DESTDIR}${BINDIR}"
sudo make install 
echo "[*] Installing assets into ${DESTDIR}${DATADIR}"
$SUDO make -C "$ROOT_DIR" install DESTDIR="$DESTDIR" PREFIX="$PREFIX" BINDIR="$BINDIR" DATADIR="$DATADIR"

echo "[+] Done"
