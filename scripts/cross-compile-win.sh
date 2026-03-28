#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SDL_DIR="/tmp/SDL-1.2.15"
DIST_DIR="$ROOT_DIR/dist/win32"

echo ""
echo " ============================================"
echo "  CGTerm 3.0 - Windows Cross-Compile"
echo "  Building from macOS using MinGW-w64"
echo " ============================================"
echo ""

# Check for cross-compiler
if ! command -v i686-w64-mingw32-gcc >/dev/null 2>&1; then
    echo "[!] MinGW-w64 cross-compiler not found."
    echo "    Install with: brew install mingw-w64"
    exit 1
fi
echo "[+] Found i686-w64-mingw32-gcc (32-bit, matches SDL 1.2)"

# Check for SDL dev files
if [ ! -f "$SDL_DIR/include/SDL/SDL.h" ]; then
    echo "[*] Downloading SDL 1.2.15 Windows development files..."
    cd /tmp
    curl -L -o SDL-devel-1.2.15-mingw32.tar.gz \
        "https://www.libsdl.org/release/SDL-devel-1.2.15-mingw32.tar.gz"
    tar xzf SDL-devel-1.2.15-mingw32.tar.gz
    echo "[+] SDL 1.2.15 downloaded"
fi
echo "[+] Found SDL 1.2 at $SDL_DIR"

# Clean
echo "[*] Cleaning..."
rm -rf "$ROOT_DIR/build/obj-win32"
rm -rf "$DIST_DIR"
mkdir -p "$ROOT_DIR/build/obj-win32"
mkdir -p "$DIST_DIR"

CC="i686-w64-mingw32-gcc"
CFLAGS="-O2 -Wall -DWINDOWS -I$SDL_DIR/include/SDL -I$ROOT_DIR/src"
# Statically link compiler runtime so no extra DLLs are needed.
# -mwindows hides the console window (GUI app).
LDFLAGS="-static-libgcc -L$SDL_DIR/lib -lmingw32 -lSDL -lws2_32 -mwindows"
OBJDIR="$ROOT_DIR/build/obj-win32"
SRCDIR="$ROOT_DIR/src"

COMMON="kernal gfx net config paths keyboard menu font timer crc sound macro ui"
TERM="xfer xmodem punter rainbow diskimage dir fileselector ui_term"

# Compile common objects
echo "[*] Compiling common sources..."
for src in $COMMON; do
    $CC $CFLAGS -c "$SRCDIR/$src.c" -o "$OBJDIR/$src.o"
done

# Compile terminal objects
echo "[*] Compiling terminal sources..."
for src in $TERM; do
    $CC $CFLAGS -c "$SRCDIR/$src.c" -o "$OBJDIR/$src.o"
done

# Compile WinMain shim
echo "[*] Compiling WinMain shim..."
$CC $CFLAGS -c "$SRCDIR/winmain.c" -o "$OBJDIR/winmain.o"

# Compile and link cgterm
echo "[*] Linking cgterm.exe..."
$CC $CFLAGS -c "$SRCDIR/cgterm.c" -o "$OBJDIR/cgterm.o"
OBJS=""
for src in $COMMON $TERM; do
    OBJS="$OBJS $OBJDIR/$src.o"
done
$CC -o "$DIST_DIR/cgterm.exe" "$OBJDIR/winmain.o" "$OBJDIR/cgterm.o" $OBJS $LDFLAGS

# Compile and link cgchat
echo "[*] Linking cgchat.exe..."
$CC $CFLAGS -c "$SRCDIR/cgchat.c" -o "$OBJDIR/cgchat.o"
$CC $CFLAGS -c "$SRCDIR/chat.c" -o "$OBJDIR/chat.o"
$CC $CFLAGS -c "$SRCDIR/status.c" -o "$OBJDIR/status.o"
$CC $CFLAGS -c "$SRCDIR/ui_chat.c" -o "$OBJDIR/ui_chat.o"
CHAT_OBJS=""
for src in $COMMON; do
    CHAT_OBJS="$CHAT_OBJS $OBJDIR/$src.o"
done
$CC -o "$DIST_DIR/cgchat.exe" "$OBJDIR/winmain.o" "$OBJDIR/cgchat.o" "$OBJDIR/chat.o" "$OBJDIR/status.o" "$OBJDIR/ui_chat.o" $CHAT_OBJS $LDFLAGS

# Compile and link cgedit
echo "[*] Linking cgedit.exe..."
$CC $CFLAGS -c "$SRCDIR/cgedit.c" -o "$OBJDIR/cgedit.o"
$CC $CFLAGS -c "$SRCDIR/ui_edit.c" -o "$OBJDIR/ui_edit.o"
EDIT_OBJS=""
for src in $COMMON; do
    EDIT_OBJS="$EDIT_OBJS $OBJDIR/$src.o"
done
$CC -o "$DIST_DIR/cgedit.exe" "$OBJDIR/winmain.o" "$OBJDIR/cgedit.o" "$OBJDIR/ui_edit.o" "$OBJDIR/fileselector.o" "$OBJDIR/dir.o" "$OBJDIR/diskimage.o" $EDIT_OBJS $LDFLAGS

echo "[+] Binaries compiled"

# Copy SDL.dll and assets
echo "[*] Copying runtime files..."
cp "$SDL_DIR/bin/SDL.dll" "$DIST_DIR/"
cp -r "$ROOT_DIR/assets" "$DIST_DIR/assets"
cp "$ROOT_DIR/README.txt" "$DIST_DIR/"
cp "$ROOT_DIR/scripts/cgterm-default.cfg" "$DIST_DIR/cgterm.cfg"

# Strip binaries
echo "[*] Stripping binaries..."
i686-w64-mingw32-strip "$DIST_DIR/cgterm.exe"
i686-w64-mingw32-strip "$DIST_DIR/cgchat.exe"
i686-w64-mingw32-strip "$DIST_DIR/cgedit.exe"

# Build NSIS installer if makensis is available
if command -v makensis >/dev/null 2>&1; then
    echo "[*] Building NSIS installer..."
    makensis "$ROOT_DIR/scripts/cgterm-installer.nsi"
    if [ $? -eq 0 ]; then
        echo "[+] Installer built: $ROOT_DIR/dist/CGTerm-3.0-Setup.exe"
    else
        echo "[!] NSIS failed — installer not created"
    fi
else
    echo "[*] makensis not found — skipping installer"
    echo "    Install with: brew install makensis"
fi

# Create zip
echo "[*] Creating distribution zip..."
cd "$ROOT_DIR/dist"
rm -f CGTerm-3.0-win32.zip
if [ -f CGTerm-3.0-Setup.exe ]; then
    zip -r CGTerm-3.0-win32.zip win32/ CGTerm-3.0-Setup.exe
else
    zip -r CGTerm-3.0-win32.zip win32/
fi

# Show result
echo ""
echo " ============================================"
echo "  Cross-compilation successful!"
echo ""
echo "  Output:"
ls -lh "$ROOT_DIR/dist/"CGTerm-3.0-*
echo ""
echo "  dist/win32/          — portable (unzip and run)"
echo "  dist/CGTerm-3.0-Setup.exe — installer"
echo "  dist/CGTerm-3.0-win32.zip — both in one zip"
echo " ============================================"
