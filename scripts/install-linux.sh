#!/usr/bin/env bash
set -euo pipefail

echo ""
echo " ============================================"
echo "  CGTerm 3.0 - Linux Installation"
echo " ============================================"
echo ""

# Find the project root — works from root or scripts/
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/Makefile" ]; then
    ROOT_DIR="$SCRIPT_DIR"
elif [ -f "$SCRIPT_DIR/../Makefile" ]; then
    ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
else
    echo "[!] Cannot find project root (Makefile not found)"
    echo "    Run this script from the CGTerm directory or scripts/ subdirectory"
    exit 1
fi
echo "[*] Project root: $ROOT_DIR"

PREFIX="${PREFIX:-/usr/local}"
BINDIR="${BINDIR:-$PREFIX/bin}"
DATADIR="${DATADIR:-$PREFIX/share/cgterm/assets}"
DESTDIR="${DESTDIR:-}"

# Detect package manager
PKG_MGR=""
if command -v apt >/dev/null 2>&1; then
    PKG_MGR="apt"
elif command -v dnf >/dev/null 2>&1; then
    PKG_MGR="dnf"
elif command -v pacman >/dev/null 2>&1; then
    PKG_MGR="pacman"
fi

echo ""
echo "[*] Checking dependencies..."

# Check for C compiler
if ! command -v gcc >/dev/null 2>&1 && ! command -v cc >/dev/null 2>&1; then
    echo "[!] No C compiler found"
    case "$PKG_MGR" in
        apt)    echo "    Install with: sudo apt install build-essential" ;;
        dnf)    echo "    Install with: sudo dnf install gcc make" ;;
        pacman) echo "    Install with: sudo pacman -S base-devel" ;;
        *)      echo "    Install gcc and make for your distribution" ;;
    esac
    exit 1
fi
echo "[+] C compiler found"

# Check for make
if ! command -v make >/dev/null 2>&1; then
    echo "[!] 'make' not found"
    case "$PKG_MGR" in
        apt)    echo "    Install with: sudo apt install build-essential" ;;
        dnf)    echo "    Install with: sudo dnf install make" ;;
        pacman) echo "    Install with: sudo pacman -S base-devel" ;;
        *)      echo "    Install make for your distribution" ;;
    esac
    exit 1
fi
echo "[+] make found"

# Check for SDL 1.2
if command -v sdl-config >/dev/null 2>&1; then
    SDL_VER="$(sdl-config --version)"
    case "$SDL_VER" in
        1.2*) echo "[+] SDL $SDL_VER found" ;;
        *)
            echo "[!] SDL $SDL_VER found, but CGTerm needs SDL 1.2"
            exit 1
            ;;
    esac
else
    echo "[!] SDL 1.2 not found"
    echo "[*] Attempting to install SDL 1.2..."
    case "$PKG_MGR" in
        apt)
            sudo apt install -y libsdl1.2-dev
            ;;
        dnf)
            sudo dnf install -y SDL-devel
            ;;
        pacman)
            sudo pacman -S --noconfirm sdl
            ;;
        *)
            echo "[*] Unknown package manager, downloading SDL from source..."
            # Private build dir (mode 0700) — never a predictable /tmp path an
            # attacker could pre-create or tamper with before 'sudo make install'.
            SDL_SHA256="d8215b571a581be1332d2106f8036fcb03d12a70bae01e20f424976d275432bc"
            SDL_BUILD="$(mktemp -d "${TMPDIR:-/tmp}/cgterm-sdl.XXXXXX")" || { echo "[!] mktemp failed"; exit 1; }
            cd "$SDL_BUILD"
            if command -v wget >/dev/null 2>&1; then
                wget -q "https://www.libsdl.org/release/SDL-1.2.15.tar.gz" -O SDL-1.2.15.tar.gz
            elif command -v curl >/dev/null 2>&1; then
                curl -sL "https://www.libsdl.org/release/SDL-1.2.15.tar.gz" -o SDL-1.2.15.tar.gz
            else
                echo "[!] Neither wget nor curl found, cannot download SDL"
                exit 1
            fi
            # Verify integrity before extracting and building as root.
            if command -v sha256sum >/dev/null 2>&1; then
                echo "${SDL_SHA256}  SDL-1.2.15.tar.gz" | sha256sum -c - \
                    || { echo "[!] SDL checksum mismatch — aborting"; exit 1; }
            elif command -v shasum >/dev/null 2>&1; then
                echo "${SDL_SHA256}  SDL-1.2.15.tar.gz" | shasum -a 256 -c - \
                    || { echo "[!] SDL checksum mismatch — aborting"; exit 1; }
            else
                echo "[!] No sha256 tool available to verify the SDL download — aborting"
                exit 1
            fi
            tar xzf SDL-1.2.15.tar.gz && cd SDL-1.2.15
            ./configure --prefix=/usr/local
            make -j"$(nproc)"
            sudo make install
            sudo ldconfig
            echo "[+] SDL 1.2.15 built and installed from source"
            cd "$ROOT_DIR"
            ;;
    esac
    # Verify it worked
    if ! command -v sdl-config >/dev/null 2>&1; then
        echo "[!] SDL installation failed"
        exit 1
    fi
    echo "[+] SDL installed successfully"
fi

# Check for SDL_mixer (optional)
if pkg-config --exists SDL_mixer 2>/dev/null; then
    echo "[+] SDL_mixer found (music support enabled)"
elif [ -f /usr/include/SDL/SDL_mixer.h ] || [ -f /usr/local/include/SDL/SDL_mixer.h ]; then
    echo "[+] SDL_mixer found (music support enabled)"
else
    echo "[*] SDL_mixer not found — music support disabled (optional)"
    echo "[*] Attempting to install SDL_mixer..."
    case "$PKG_MGR" in
        apt)    sudo apt install -y libsdl-mixer1.2-dev 2>/dev/null && echo "[+] SDL_mixer installed" || echo "[*] SDL_mixer install failed, continuing without music" ;;
        dnf)    sudo dnf install -y SDL_mixer-devel 2>/dev/null && echo "[+] SDL_mixer installed" || echo "[*] SDL_mixer install failed, continuing without music" ;;
        pacman) sudo pacman -S --noconfirm sdl_mixer 2>/dev/null && echo "[+] SDL_mixer installed" || echo "[*] SDL_mixer install failed, continuing without music" ;;
        *)      echo "[*] Install SDL_mixer manually for XM/MOD music support" ;;
    esac
fi

echo ""
echo "[*] All required dependencies OK"
echo ""

# Build
echo "[*] Building CGTerm 3.0..."
if ! make -C "$ROOT_DIR" clean all; then
    echo "[!] Build failed"
    exit 1
fi

# Determine if we need sudo. Test the nearest EXISTING ancestor's writability
# (dirname alone fails for not-yet-created trees like $HOME/.local, which would
# otherwise force sudo for an unprivileged --prefix=$HOME/.local install).
NEED_ROOT=0
SUDO=""
ancestor_writable() {
    local d="$1"
    while [[ ! -e "$d" && "$d" != "/" ]]; do d="$(dirname "$d")"; done
    [[ -w "$d" ]]
}
if [[ -n "$DESTDIR" ]]; then
    NEED_ROOT=0
elif ancestor_writable "$BINDIR" && ancestor_writable "$DATADIR"; then
    NEED_ROOT=0
else
    NEED_ROOT=1
fi

if [[ "$NEED_ROOT" -eq 1 && "${EUID:-$(id -u)}" -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    else
        echo "[!] Root privileges required for install into $PREFIX"
        echo "    Re-run as root or set PREFIX=\$HOME/.local"
        exit 1
    fi
fi

echo "[*] Installing into ${DESTDIR}${PREFIX}"
$SUDO make -C "$ROOT_DIR" install DESTDIR="$DESTDIR" PREFIX="$PREFIX" BINDIR="$BINDIR" DATADIR="$DATADIR"

echo ""
echo " ============================================"
echo "  Installation complete!"
echo ""
echo "  Binaries:  ${DESTDIR}${BINDIR}/cgterm"
echo "  Assets:    ${DESTDIR}${DATADIR}/"
echo ""
echo "  Run with:  cgterm"
echo " ============================================"
echo ""
