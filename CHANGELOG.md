══════════════════════════════════════════════════════════════════════
CGTERM 3.0 / SCENE EDITION
FULL CHANGELOG / NFO
Genesis*Project · 2026
══════════════════════════════════════════════════════════════════════
─── WHAT IS THIS RELEASE ───

Major update to the C64 BBS terminal MagerValp gave the scene. Same heart: real boards, PETSCII on glass, files in motion. The codebase is cleaned and lifted onto SDL2; the polish nods to demoscene intros, modem-era mood, batch transfers, disk tools, and UI work for people who still enjoy watching bytes.
─── FILE TRANSFERS & DISK IMAGES ───

[+]Punter: classic C64 BBS protocol (send + receive)
[+]Multi Punter: batch download + upload, filename announce, C*BASE-style batch end, drain between files
[+]Rainbow: upload and download where the BBS supports it
[+]XMODEM: checksum, CRC, and 1K block variants
[+]Interactive transfer UI: progress, live hex feed, cancel with confirm, PC ↔ 1541 style cues
[+]D64 / D71 / D81: open as directories, pick image as download target, extract / inject PRG/SEQ/USR, PETSCII-aware names, block-free checks
[+]Filename conversion: e.g. .prg ↔ ,p for C64 naming
[+]Temp files: mkstemp / Windows temp APIs for uploads & downloads
─── TERMINAL, ANSI & KEYBOARD ───

[+]PETSCII + ANSI (80 cols in ANSI), toggle from menu
[+]Telnet IAC for chat mode; raw bytes for protocols so 0xFF isn’t eaten
[+]Clipboard paste, macro record/playback
[+]Clipboard paste now paced to baud (no flooding the board); Cmd-V works on macOS
[+]Keyboard profiles (US / SE / DE) + keyboard test view
[+]PETSCII modifiers: Commodore key, control colours
─── UI / SCENE POLISH ───

[+]Demo-style splash: vectors, plasma, rotozoom, wireframe morph, scroller, greetz; tracker music on supported builds; CRT power-off on quit
[+]Modem theatre: phosphor, fake AT dial, phone book, V.34-ish audio (menu toggle)
[+]Menus by category; pink / cyan / neon theme
[+]File selector: colour hints for dirs, disk images, files; path line in menu
[+]Font system: header + body pixel fonts, in-app picker, TTF/OTF pipeline
─── BOOKMARKS & SESSION ───

[+]40 bookmark slots: host, port, alias, term mode
[+]Per-bookmark notes (separate file)
[+]Bookmark edit/delete now honour your configured bookmark file
[+]Quick connect: number keys 0–9
[+]Connection history logging
[+]Session capture appends instead of overwriting yesterday’s log
─── SECURITY & STABILITY ───

[+]Downloads: filename validation; block .., slashes, controls, Windows reserved devices (CON, COM1–9, …)
[+]Punter: bounded handshake; block length clamped to internal buffer; honours ESC cancel
[+]Multi-Punter: bounded remote filename; temp file reclaimed on a rejected name
[+]XMODEM: padding-aware flush so a final/all-pad block can’t drop real data
[+]Disk images: bounded BAM allocator (a crafted free-count can’t hang the client); atomic temp+rename write so a failed save can’t corrupt your .d64/.d71/.d81; chain and track/sector validation; disk-full handled instead of trampling block 0
[+]PETSCII render: bounds-checked cursor / delete so remote control bytes can’t write before the screen buffer
[+]ANSI: clamp CSI params against pathological repeat loops; scroll region respected; parser state reset on connect (no carryover between boards)
[+]Telnet: IAC parser survives sequences split across packets; outgoing 0xFF doubled on telnet links
[+]Macros: playback bounds-checked so a re-armed recording can’t run off the buffer
[+]Config: hostname/alias/port validation; safer path joins; no FILE-handle leak on a failed save
[+]net_connect: reject empty host; close socket on failed resolve (no FD leak); literal-IP parse first (single-label & digit-suffix hosts work)
[+]Bookmarks: rollback host alloc if alias malloc fails
[+]Resource hygiene: menu/surface/font re-init frees, double-frees removed, use-after-free in the file selector closed
[+]Installers: SDL source-build fallback uses a private mktemp dir with SHA-256 verification before sudo
[+]Hardened native build: stack protector, _FORTIFY_SOURCE, PIE (Linux); prefer bounded string APIs; clean build on GCC/Clang
─── NETWORKING ───

[+]Non-blocking TCP connect: ESC can cancel
[+]Unix: getaddrinfo + AI_ADDRCONFIG → IPv4
[+]Windows zips may use legacy resolver; still IPv4-first for BBS interop
─── AUDIO / MUSIC ───

[+]Windows: SDL_mixer + libmikmod path (XM/MOD/IT) where enabled
[+]macOS/Linux: libopenmpt + SDL when built with those deps
[+]Volume control; graceful fallback if audio init fails
─── PLATFORMS & BUILD ───

[+]Windows: portable ZIP
[+]Linux: make + optional sudo make install
[+]macOS: self-contained, signed CGTerm.app (bundles SDL 1.2 / SDL2 / libopenmpt) — runs without Homebrew
[+]Cross-compile: MinGW from Unix documented
[+]cgterm -V prints the version
─── DOCS & HELP ───

[+]In-app help: shortcuts, transfers, ANSI, C*BASE user cheatsheet, sysop hints
─── KNOWN LIMITATIONS ───

[!]BBS = classic Telnet, cleartext on the wire. Trust your network; don’t reuse critical passwords.
[!]Roadmap (not promised): TLS where boards support it, IPv6, file digests, sandboxing; see GitHub issues/PRs.
─── GREETZ & CREDITS ───

Respect forever to MagerValp for the original CGTerm and the BBS culture it stands for. Scene Edition: code m00p (Genesis*Project), music Mr.Death, support mermaid, QA Larry / Jucke / SkyHawk, and every sysop still running a C64 (or bridge) board.
══════════════════════════════════════════════════════════════════════
  --- end of changelog ---
══════════════════════════════════════════════════════════════════════
