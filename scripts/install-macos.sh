#!/usr/bin/env bash
set -euo pipefail

echo ""
echo " ============================================"
echo "  CGTerm 3.0 - macOS Installation"
echo " ============================================"
echo ""

# Find the project root — works whether run from root or scripts/
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

# Check for Xcode Command Line Tools (provides gcc, clang, make, ld, ar, etc.)
echo ""
echo "[*] Checking dependencies..."
if ! command -v clang >/dev/null 2>&1 || ! command -v make >/dev/null 2>&1; then
    echo "[*] Xcode Command Line Tools not found, installing..."
    xcode-select --install 2>/dev/null || true
    echo ""
    echo "[!] Please wait for Xcode Command Line Tools to finish installing,"
    echo "    then run this script again."
    exit 1
fi
echo "[+] Xcode Command Line Tools found (clang, make, ld)"

# Check for Homebrew (needed for SDL)
if ! command -v brew >/dev/null 2>&1; then
    echo "[!] Homebrew not found"
    echo "    Install from: https://brew.sh"
    echo "    Then run: brew install sdl12-compat"
    exit 1
fi
echo "[+] Homebrew found"

# Check for SDL 1.2
if command -v sdl-config >/dev/null 2>&1; then
    SDL_VERSION=$(sdl-config --version)
    if [[ $SDL_VERSION == 1.2* ]]; then
        echo "[+] SDL $SDL_VERSION found"
    else
        echo "[!] SDL version $SDL_VERSION found, but CGTerm needs SDL 1.2"
        echo "    Install with: brew install sdl12-compat"
        exit 1
    fi
else
    echo "[!] SDL 1.2 not found"
    echo "[*] Attempting to install SDL 1.2 via Homebrew..."
    if brew install sdl12-compat 2>/dev/null; then
        echo "[+] SDL 1.2 (sdl12-compat) installed"
    else
        echo "[*] Homebrew install failed, downloading SDL 1.2 from source..."
        # Private build dir (mode 0700) instead of a predictable /tmp path that
        # an attacker could pre-create or tamper with before 'sudo make install'.
        SDL_SHA256="d8215b571a581be1332d2106f8036fcb03d12a70bae01e20f424976d275432bc"
        SDL_BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/cgterm-sdl.XXXXXX")" || { echo "[!] mktemp failed"; exit 1; }
        cd "$SDL_BUILD_DIR"
        if command -v wget >/dev/null 2>&1; then
            wget -q "https://www.libsdl.org/release/SDL-1.2.15.tar.gz" -O SDL-1.2.15.tar.gz
        elif command -v curl >/dev/null 2>&1; then
            curl -sL "https://www.libsdl.org/release/SDL-1.2.15.tar.gz" -o SDL-1.2.15.tar.gz
        else
            echo "[!] Neither wget nor curl found, cannot download SDL"
            exit 1
        fi
        # Verify integrity before extracting and building as root.
        if echo "${SDL_SHA256}  SDL-1.2.15.tar.gz" | shasum -a 256 -c - ; then
            :
        else
            echo "[!] SDL checksum mismatch — aborting"
            exit 1
        fi
        tar xzf SDL-1.2.15.tar.gz
        cd SDL-1.2.15
        ./configure --prefix=/usr/local
        make -j4
        sudo make install
        echo "[+] SDL 1.2.15 built and installed from source"
        cd "$ROOT_DIR"
    fi
fi

# libopenmpt is REQUIRED (XM music; the Makefile links it)
if pkg-config --exists libopenmpt 2>/dev/null || [ -f /opt/homebrew/include/libopenmpt/libopenmpt.h ] || [ -f /usr/local/include/libopenmpt/libopenmpt.h ]; then
    echo "[+] libopenmpt found"
else
    echo "[*] libopenmpt not found — installing with Homebrew..."
    brew install libopenmpt
fi

echo ""
echo "[*] All required dependencies OK"
echo ""

# Build
APP_NAME="CGTerm"
APP_BUNDLE="${APP_NAME}.app"
DIST_DIR="${ROOT_DIR}/dist"
APP_DIR="${DIST_DIR}/${APP_BUNDLE}"
CONTENTS_DIR="${APP_DIR}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"

mkdir -p "$DIST_DIR"
rm -rf "$APP_DIR"

echo "[*] Building the self-contained app bundle (package-macos.sh)..."
if ! "$ROOT_DIR/scripts/package-macos.sh"; then
    echo "[!] Bundle build failed"
    exit 1
fi
echo "[+] App bundle created: $APP_DIR"

# Install to /Applications (optional)
echo ""
INSTALL_APP_PATH="/Applications/CGTerm.app"
REPLY=n
if [ -t 0 ]; then
    read -p "[?] Install to $INSTALL_APP_PATH? [y/N] " -n 1 -r
    echo
fi
if [[ $REPLY =~ ^[Yy]$ ]]; then
    if [ -w "/Applications" ]; then
        rm -rf "$INSTALL_APP_PATH"
        ditto "$APP_DIR" "$INSTALL_APP_PATH"
    else
        sudo rm -rf "$INSTALL_APP_PATH"
        sudo ditto "$APP_DIR" "$INSTALL_APP_PATH"
    fi
    echo "[+] Installed to $INSTALL_APP_PATH"
else
    echo "[*] Skipped. You can run from: $APP_DIR"
    echo "    Or copy manually: cp -r $APP_DIR /Applications/"
fi

echo ""
echo " ============================================"
echo "  Installation complete!"
echo ""
echo "  Run CGTerm from:"
echo "    - /Applications/CGTerm.app (if installed)"
echo "    - $APP_DIR"
echo "    - $ROOT_DIR/bin/cgterm (command line)"
echo " ============================================"
echo ""
