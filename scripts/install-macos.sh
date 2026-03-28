#!/usr/bin/env bash
set -euo pipefail

echo "Checking CGTerm dependencies..."

# Check SDL
if command -v sdl-config >/dev/null 2>&1; then
    SDL_VERSION=$(sdl-config --version)
    echo "Found SDL version: $SDL_VERSION"

    if [[ $SDL_VERSION == 1.2* ]]; then
        echo "YES!!!  SDL 1.2 is installed"
    else
        echo "SORRY!! :/  SDL version is not 1.2 (CGTerm expects SDL 1.2)"
        exit 1
    fi
else
    echo "✗ SDL not found"
    echo ""
    echo "Install SDL 1.2:"
    echo "macOS:   brew install sdl"
    echo "Ubuntu:  sudo apt install libsdl1.2-dev"
    echo "Arch:    sudo pacman -S sdl"
    exit 1
fi

echo ""
echo "All dependencies look OK."

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_NAME="CGTerm"
APP_BUNDLE="${APP_NAME}.app"
DIST_DIR="${ROOT_DIR}/dist"
APP_DIR="${DIST_DIR}/${APP_BUNDLE}"
CONTENTS_DIR="${APP_DIR}/Contents"
MACOS_DIR="${CONTENTS_DIR}/MacOS"
RESOURCES_DIR="${CONTENTS_DIR}/Resources"
INSTALL_APP_PATH="${INSTALL_APP_PATH:-/Applications/${APP_BUNDLE}}"
COPY_TO_APPLICATIONS="${COPY_TO_APPLICATIONS:-0}"

mkdir -p "$DIST_DIR"
rm -rf "$APP_DIR"

echo "[*] Building CGTerm"
make -C "$ROOT_DIR" clean all

mkdir -p "$MACOS_DIR" "$RESOURCES_DIR"
cp "$ROOT_DIR/bin/cgterm" "$MACOS_DIR/CGTerm"
chmod +x "$MACOS_DIR/CGTerm"
cp -R "$ROOT_DIR/assets/." "$RESOURCES_DIR/"

cat > "${CONTENTS_DIR}/Info.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleName</key><string>CGTerm</string>
<key>CFBundleDisplayName</key><string>CGTerm</string>
<key>CFBundleExecutable</key><string>CGTerm</string>
<key>CFBundleIdentifier</key><string>com.cgterm.app</string>
<key>CFBundleVersion</key><string>2.3</string>
<key>CFBundleShortVersionString</key><string>2.3</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
EOF

echo "APPLCGTR" > "${CONTENTS_DIR}/PkgInfo"


echo "[+] App bundle created: $APP_DIR"
INSTALL_APP_PATH="/Applications/CGTerm.app"

#  if [[ "$EUID" -ne 0 ]]; then
#    echo "[+] Root privileges required to install to /Applications"
#    exec sudo env \
#      COPY_TO_APPLICATIONS=1 \
#      APP_DIR="$APP_DIR" \
#      INSTALL_APP_PATH="$INSTALL_APP_PATH" \
#      "$0" "$@"
#  fi

 echo "[+] Installing to $INSTALL_APP_PATH"
 sudo  rm -rf  $INSTALL_APP_PATH
 sudo ditto $APP_DIR $INSTALL_APP_PATH

 echo "[+] Installed to $INSTALL_APP_PATH"

