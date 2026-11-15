#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Third-party dev files live under build/ (not a shared, predictable /tmp
# path that another local user could pre-populate) and every download is
# checked against a pinned SHA-256 before it is unpacked.
DEPS_DIR="$ROOT_DIR/build/win32-deps"
mkdir -p "$DEPS_DIR"
SDL_DIR="$DEPS_DIR/SDL-1.2.15"
SDL_TGZ_SHA256="d51eedfe7e07893d6c93a2d761c6ccc91d04b5f68a2ecabdbef83b7a1fef9cde"
SDL_MIXER_ZIP_SHA256="25084eb2b3c49fea38d01e24b172109bae07c0ac82b1765d5c179026a164387b"

verify_sha256() {   # verify_sha256 <file> <expected>
    local got
    got="$(shasum -a 256 "$1" | awk '{print $1}')"
    if [ "$got" != "$2" ]; then
        echo "[!] Checksum mismatch for $1"
        echo "    expected $2"
        echo "    got      $got"
        rm -f "$1"
        return 1
    fi
}
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
    cd "$DEPS_DIR"
    curl -fL -o SDL-devel-1.2.15-mingw32.tar.gz \
        "https://www.libsdl.org/release/SDL-devel-1.2.15-mingw32.tar.gz"
    verify_sha256 SDL-devel-1.2.15-mingw32.tar.gz "$SDL_TGZ_SHA256"
    tar xzf SDL-devel-1.2.15-mingw32.tar.gz
    echo "[+] SDL 1.2.15 downloaded"
fi
echo "[+] Found SDL 1.2 at $SDL_DIR"

# Check for SDL_mixer dev files (optional — music support)
# Use the MinGW build which includes mikmod support built-in
SDL_MIXER_DIR="$DEPS_DIR/SDL_mixer-1.2.12"
HAVE_SDL_MIXER=0
if [ ! -f "$SDL_MIXER_DIR/include/SDL_mixer.h" ]; then
    echo "[*] Downloading SDL_mixer 1.2.12 Windows dev files..."
    cd "$DEPS_DIR"
    # optional download — don't let a network failure abort the build (set -e)
    curl -fL --connect-timeout 10 --max-time 30 -o SDL_mixer-devel-1.2.12-VC.zip \
        "https://www.libsdl.org/projects/SDL_mixer/release/SDL_mixer-devel-1.2.12-VC.zip" 2>/dev/null || true
    if [ -f SDL_mixer-devel-1.2.12-VC.zip ] && ! verify_sha256 SDL_mixer-devel-1.2.12-VC.zip "$SDL_MIXER_ZIP_SHA256"; then
        echo "[!] Refusing the SDL_mixer download; building without music"
    fi
    if [ -f SDL_mixer-devel-1.2.12-VC.zip ]; then
        if unzip -o SDL_mixer-devel-1.2.12-VC.zip >/dev/null 2>&1; then
            echo "[+] SDL_mixer 1.2.12 downloaded"
        else
            rm -f SDL_mixer-devel-1.2.12-VC.zip
            echo "[*] SDL_mixer archive unusable — continuing without music"
        fi
    fi
fi
if [ -f "$SDL_MIXER_DIR/include/SDL_mixer.h" ]; then
    HAVE_SDL_MIXER=1
    echo "[+] Found SDL_mixer at $SDL_MIXER_DIR"
else
    echo "[*] SDL_mixer not found — music support disabled (optional)"
fi

# Clean
echo "[*] Cleaning..."
rm -rf "$ROOT_DIR/build/obj-win32"
rm -rf "$DIST_DIR"
mkdir -p "$ROOT_DIR/build/obj-win32"
mkdir -p "$DIST_DIR"

CC="i686-w64-mingw32-gcc"
# Same hardening the Makefile gives the native builds: stack protector and
# FORTIFY in the code, ASLR and DEP in the PE header. libssp is linked
# statically so no extra DLL has to ship.
HARDEN_CFLAGS="-fstack-protector-strong -D_FORTIFY_SOURCE=2"
HARDEN_LDFLAGS="-Wl,--dynamicbase -Wl,--nxcompat"
# Older mingw-w64 keeps the stack-protector runtime in libssp; newer ones
# have it in the CRT and ship no libssp at all.
if [ "$(i686-w64-mingw32-gcc -print-file-name=libssp.a)" != "libssp.a" ]; then
    HARDEN_LDFLAGS="$HARDEN_LDFLAGS -Wl,-Bstatic -lssp -Wl,-Bdynamic"
fi
CFLAGS="-O2 -Wall $HARDEN_CFLAGS -DWINDOWS -I$SDL_DIR/include/SDL -I$ROOT_DIR/src"
LDFLAGS="-static-libgcc -L$SDL_DIR/lib -lmingw32 -lSDL -lws2_32 -lm -mwindows $HARDEN_LDFLAGS"

# Add SDL_mixer if available (VC package: lib/x86/SDL_mixer.lib, include/SDL_mixer.h)
if [ "$HAVE_SDL_MIXER" = "1" ]; then
    CFLAGS="$CFLAGS -DHAVE_SDL_MIXER -I$SDL_MIXER_DIR/include"
    LDFLAGS="-static-libgcc -L$SDL_DIR/lib -L$SDL_MIXER_DIR/lib/x86 -lmingw32 -lSDL -lSDL_mixer -lws2_32 -lm -mwindows $HARDEN_LDFLAGS"
fi
OBJDIR="$ROOT_DIR/build/obj-win32"
SRCDIR="$ROOT_DIR/src"

COMMON="kernal gfx net config paths keyboard menu font timer crc sound macro ui clipboard modem music music_preload ansi cp437font login"
TERM="xfer xmodem punter rainbow zmodem diskimage dir fileselector session ui_term"

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
if [ "$HAVE_SDL_MIXER" = "1" ]; then
    # Copy SDL_mixer.dll and any dependency DLLs
    for p in "$SDL_MIXER_DIR/lib/x86/SDL_mixer.dll" \
             "$SDL_MIXER_DIR/lib/x86/"*.dll; do
        # SDL_mixer loads its codecs on demand. CGTerm only plays tracker
        # modules (libmikmod), so the MPEG and FLAC decoders are dead weight:
        # 2012 builds without ASLR/DEP that nothing ever calls.
        case "$(basename "$p")" in smpeg.dll|libFLAC-8.dll) continue ;; esac
        if [ -f "$p" ]; then
            cp "$p" "$DIST_DIR/"
            echo "[+] $(basename $p) copied"
        fi
    done
fi
rsync -a --exclude='._*' --exclude='.DS_Store' --exclude='*.ttf' --exclude='*.otf' --exclude='*.zip' "$ROOT_DIR/assets/" "$DIST_DIR/assets/"
cp "$ROOT_DIR/README" "$DIST_DIR/"
cp "$ROOT_DIR/INSTALL" "$DIST_DIR/"
cp "$ROOT_DIR/scripts/cgterm-default.cfg" "$DIST_DIR/cgterm.cfg"

# Strip binaries
echo "[*] Stripping binaries..."
i686-w64-mingw32-strip "$DIST_DIR/cgterm.exe"
i686-w64-mingw32-strip "$DIST_DIR/cgchat.exe"
i686-w64-mingw32-strip "$DIST_DIR/cgedit.exe"

# Clean macOS cruft from dist before zipping
find "$DIST_DIR" -name "._*" -delete 2>/dev/null
find "$DIST_DIR" -name ".DS_Store" -delete 2>/dev/null

# Create portable zip (primary distribution — no SmartScreen warnings)
echo "[*] Creating distribution zip..."
cd "$ROOT_DIR/dist"
rm -f CGTerm-3.0-win32.zip
zip -r CGTerm-3.0-win32.zip win32/ -x "*/._*" -x "*/.DS_Store"

# Show result
echo ""
echo " ============================================"
echo "  Cross-compilation successful!"
echo ""
echo "  Output:"
ls -lh "$ROOT_DIR/dist/CGTerm-3.0-win32.zip"
echo ""
echo "  dist/win32/              — portable (unzip and run)"
echo "  dist/CGTerm-3.0-win32.zip — zip for distribution"
echo " ============================================"
