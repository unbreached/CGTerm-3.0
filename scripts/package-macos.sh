#!/usr/bin/env bash
#
# Build a self-contained CGTerm.app (no Homebrew required at runtime) and a
# distributable tarball. Bundles libSDL-1.2 (sdl12-compat), libopenmpt and its
# codec deps via dylibbundler, then copies in libSDL2 — which sdl12-compat
# dlopens at runtime and dylibbundler therefore cannot see.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="$ROOT_DIR/dist"
APP="$DIST/CGTerm.app"
MACOS="$APP/Contents/MacOS"
RES="$APP/Contents/Resources"
LIBS="$APP/Contents/libs"

echo "[*] Building binaries..."
make -C "$ROOT_DIR" >/dev/null

echo "[*] Creating bundle layout..."
rm -rf "$APP"
mkdir -p "$MACOS" "$RES" "$LIBS"

cp "$ROOT_DIR/bin/cgterm" "$MACOS/CGTerm"
cp "$ROOT_DIR/bin/cgchat" "$MACOS/cgchat"
cp "$ROOT_DIR/bin/cgedit" "$MACOS/cgedit"

# assets -> Resources (paths.c maps .app/Contents/MacOS to ../Resources).
# Exclude demo font sources / archives; keep the .bmp grids the app loads.
rsync -a \
  --exclude='*.ttf' --exclude='*.otf' --exclude='*.zip' \
  --exclude='._*' --exclude='.DS_Store' \
  "$ROOT_DIR/assets/" "$RES/"

if [ -f "$ROOT_DIR/assets/AppIcon.icns" ]; then
  cp "$ROOT_DIR/assets/AppIcon.icns" "$RES/AppIcon.icns"
fi

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>CGTerm</string>
  <key>CFBundleDisplayName</key><string>CGTerm</string>
  <key>CFBundleExecutable</key><string>CGTerm</string>
  <key>CFBundleIdentifier</key><string>com.cgterm.app</string>
  <key>CFBundleVersion</key><string>3.1.0</string>
  <key>CFBundleShortVersionString</key><string>3.1.0</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
</dict>
</plist>
PLIST

command -v dylibbundler >/dev/null 2>&1 || { echo "[!] dylibbundler not found: brew install dylibbundler"; exit 1; }
echo "[*] Bundling linked libraries with dylibbundler..."
dylibbundler -of -b \
  -x "$MACOS/CGTerm" -x "$MACOS/cgchat" -x "$MACOS/cgedit" \
  -d "$LIBS" -p "@executable_path/../libs/" >/dev/null

echo "[*] Adding libSDL2 (dlopen'd by sdl12-compat, missed by dylibbundler)..."
SDL2_SRC="$(readlink -f /opt/homebrew/lib/libSDL2-2.0.0.dylib 2>/dev/null || echo /opt/homebrew/lib/libSDL2-2.0.0.dylib)"
cp "$SDL2_SRC" "$LIBS/libSDL2-2.0.0.dylib"
chmod u+w "$LIBS/libSDL2-2.0.0.dylib"
# sdl12-compat dlopens "@loader_path/libSDL2-2.0.0.dylib" (relative to the
# bundled libSDL-1.2.0.dylib in Contents/libs), so this id makes it resolve.
install_name_tool -id "@loader_path/libSDL2-2.0.0.dylib" "$LIBS/libSDL2-2.0.0.dylib"

# Since late 2025 Homebrew's "sdl2" is sdl2-compat: an SDL2 API on top of
# SDL3 that dlopens "@loader_path/libSDL3.dylib" at load time and shows a
# modal "could not load SDL3" dialog (the app never starts) when it is
# missing. Bundle SDL3 next to it whenever the copied libSDL2 refers to it.
if strings "$LIBS/libSDL2-2.0.0.dylib" | grep -q "libSDL3.dylib"; then
  SDL3_SRC="$(readlink -f /opt/homebrew/lib/libSDL3.0.dylib 2>/dev/null || echo /opt/homebrew/lib/libSDL3.0.dylib)"
  if [ ! -f "$SDL3_SRC" ]; then
    echo "[!] libSDL2 is sdl2-compat but libSDL3 was not found (brew install sdl3)"
    exit 1
  fi
  echo "[*] Adding libSDL3 (dlopen'd by sdl2-compat)..."
  cp "$SDL3_SRC" "$LIBS/libSDL3.dylib"
  chmod u+w "$LIBS/libSDL3.dylib"
  install_name_tool -id "@loader_path/libSDL3.dylib" "$LIBS/libSDL3.dylib"
fi

echo "[*] Ad-hoc code signing (inside-out)..."
# Sign each bundled dylib explicitly. They live in Contents/libs (non-standard),
# which `codesign --deep` does NOT descend into — and install_name_tool above
# invalidated their signatures, so dyld would SIGKILL the app on load if we
# relied on --deep alone.
for dylib in "$LIBS"/*.dylib; do
  codesign --force -s - "$dylib"
done
# then the executables, then the bundle as a whole
codesign --force -s - "$MACOS/cgchat"
codesign --force -s - "$MACOS/cgedit"
codesign --force -s - "$MACOS/CGTerm"
codesign --force -s - "$APP"

echo "[*] Creating zip..."
cd "$DIST"
rm -f CGTerm-3.1-macos.zip
# ditto (not zip/tar) preserves the code signature, symlinks and resource forks
# of the signed .app — a plain archiver corrupts the signature and the app gets
# SIGKILLed on launch.
ditto -c -k --keepParent CGTerm.app CGTerm-3.1-macos.zip

echo ""
echo "[+] Built: $DIST/CGTerm-3.1-macos.zip"
ls -lh "$DIST/CGTerm-3.1-macos.zip"
