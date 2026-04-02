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
    echo "    Then run: brew install sdl"
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
        echo "    Install with: brew install sdl"
        exit 1
    fi
else
    echo "[!] SDL 1.2 not found"
    echo "[*] Attempting to install SDL 1.2 via Homebrew..."
    if brew install sdl 2>/dev/null; then
        echo "[+] SDL 1.2 installed"
    else
        echo "[*] Homebrew install failed, downloading SDL 1.2 from source..."
        SDL_BUILD_DIR="/tmp/SDL-1.2-build"
        mkdir -p "$SDL_BUILD_DIR"
        cd "$SDL_BUILD_DIR"
        if command -v wget >/dev/null 2>&1; then
            wget -q "https://www.libsdl.org/release/SDL-1.2.15.tar.gz" -O SDL-1.2.15.tar.gz
        elif command -v curl >/dev/null 2>&1; then
            curl -sL "https://www.libsdl.org/release/SDL-1.2.15.tar.gz" -o SDL-1.2.15.tar.gz
        else
            echo "[!] Neither wget nor curl found, cannot download SDL"
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

# Check for SDL_mixer (optional)
if [ -f /opt/homebrew/include/SDL/SDL_mixer.h ] || [ -f /usr/local/include/SDL/SDL_mixer.h ]; then
    echo "[+] SDL_mixer found (music support enabled)"
else
    echo "[*] SDL_mixer not found — music support disabled (optional)"
    echo "    For XM/MOD music, build SDL_mixer 1.2.12 from source:"
    echo "    https://www.libsdl.org/projects/SDL_mixer/release/SDL_mixer-1.2.12.tar.gz"
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

echo "[*] Building CGTerm..."
if ! make -C "$ROOT_DIR" clean all; then
    echo "[!] Build failed"
    exit 1
fi

echo "[*] Creating app bundle..."
mkdir -p "$MACOS_DIR" "$RESOURCES_DIR"
cp "$ROOT_DIR/bin/cgterm" "$MACOS_DIR/CGTerm"
chmod +x "$MACOS_DIR/CGTerm"
cp -R "$ROOT_DIR/assets/." "$RESOURCES_DIR/"

# Bundle libmikmod if available (needed by SDL_mixer for XM/MOD music)
for lib in /opt/homebrew/lib/libmikmod.3.dylib /usr/local/lib/libmikmod.3.dylib; do
    if [ -f "$lib" ]; then
        cp "$lib" "$MACOS_DIR/libmikmod.dylib"
        echo "[+] Bundled libmikmod.dylib for XM music support"
        break
    fi
done

# Info.plist
cat > "${CONTENTS_DIR}/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>CGTerm</string>
<key>CFBundleDisplayName</key><string>CGTerm</string>
<key>CFBundleExecutable</key><string>CGTerm</string>
<key>CFBundleIdentifier</key><string>com.cgterm.app</string>
<key>CFBundleVersion</key><string>3.0</string>
<key>CFBundleShortVersionString</key><string>3.0</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
EOF

echo "APPLCGTR" > "${CONTENTS_DIR}/PkgInfo"
echo "[+] App bundle created: $APP_DIR"

# Install to /Applications (optional)
echo ""
INSTALL_APP_PATH="/Applications/CGTerm.app"
read -p "[?] Install to $INSTALL_APP_PATH? [y/N] " -n 1 -r
echo
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
