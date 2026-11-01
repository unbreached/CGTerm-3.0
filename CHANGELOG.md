══════════════════════════════════════════════════════════════════════
CGTERM 3.0 / SCENE EDITION
FULL CHANGELOG / NFO
Genesis*Project · 2026
══════════════════════════════════════════════════════════════════════
─── REPORTED FIXES · NOVEMBER 2026 ───

[!]Window no longer opens as an unusable black rectangle when a saved zoom is too large for the display: the zoom is clamped to what actually fits and the stored value is corrected (#12)
[!]Window is repainted after alt-tab or being uncovered, instead of staying blank until the next change (#11)
[!]Auto-login now ignores colour, cursor and case codes in a board's prompts, so the expect text just needs the letters you see on screen; C*Base logins work (#2)
[!]RUN/STOP (Tab) now sends CHR$(3), so it actually aborts a listing or a C*Base pause prompt (it used to send a dead byte) (#4)
[+]Commodore+Shift (Alt+Shift) now switches the display charset case straight from the terminal, like a real C64, no trip through the menu (#9)
[+]Receive speed can be capped to an old-modem baud rate (2400/1200/300 ...) in Options [R], so text does not scroll past faster than you can read (#3)
[+]Bookmark notes editor now has a real cursor: arrow keys, Home/End, and Insert/Delete, so you can fix a typo without retyping the line (#5)
[+]Text macros: 10 editable macros fired with Cmd/Win+0-9 (or from Options [M]), stored as macro0..macro9, with \r for RETURN, for canned logins and commands (#8)
[+]Menu now scales with the window instead of staying tiny on big screens: it is drawn at a fixed design size and scaled to fit, so the text grows with the zoom (#6)
[+]The offline / startup CGTerm logo now scales to fill the window instead of sitting in black bars once you zoom past 2x
[!]Menu keys [O] Options, [X] Copy screen and [Y] History now work (they were listed but the menu swallowed the keypress), dragging the window no longer flickers, and the Options panel is colour-coded
[+]Options panel [O] is now listed in the main menu, and local echo shows its ON/OFF state there (#7, #10)
[+]Help screen documents the special keys (RUN/STOP, cursor, DEL, f-keys, Alt+Shift) and a working C*Base auto-login example (#4)

Thanks to mrdudz for the careful, detailed bug reports that drove these fixes.

─── SECURITY & STABILITY · NOVEMBER 2026 ───

[!]Fixed a heap overflow when switching 40/80 columns at an odd zoom (the font surface and the dirty-line array were sized with a stale character height)
[!]File transfers can no longer hang the client: ESC cancels even while a board keeps sending, and the ZMODEM, XMODEM and Rainbow receive loops are all bounded against a byte flood
[!]Disk images: fixed a file-descriptor leak on every image load; the D71 free-block count no longer trusts a crafted BAM
[!]Bookmarks: a write error while saving notes can no longer wipe the notes file, and a comma in a bookmark name no longer corrupts the entry on reload
[!]Network: a flood of Telnet option negotiation or ANSI status queries can no longer freeze the window or be amplified back at the board
[!]Downloads: added an aggregate batch size cap, always close the output file on a write error, and reject ':' and more Windows reserved device names in received filenames
[!]Windows: when %APPDATA% is unset the per-user files fall back to the user profile instead of the launch directory
[!]Hardening: temp files honor TMPDIR, login pause values are clamped, and several NULL and signed-char edge cases fixed; full detail in REVIEW-2026-10-31.md

─── 3.0.1 · OCTOBER 2026 ───

[+]ZMODEM: batch send/receive, file names and sizes, crash recovery, CRC-32, auto-start when the board sends
[+]Transfers: last protocol preselected (T + Return), transfer log with bytes/time/cps, on-screen summary, cancel prompt no longer stalls the peer
[+]XMODEM: CRC to checksum fallback, bounded handshakes, cancel during send, 0-byte files
[+]Punter: one-byte last blocks no longer lost, duplicate block guard, patient S/B wait, empty files
[!]Punter / Multi Punter upload: the file type byte was reversed (3.0 announced every upload as SEQ, so other callers could not download them); PRG is now sent as PRG, SEQ as SEQ
[!]Bookmarks: boards listed in the config file came back after being deleted; they now live only in the bookmark file (moved over automatically on first start) and the defaults are seeded there once
[+]Window can be dragged to any size and snaps to the nearest zoom step (1x-8x); 40/80 columns is a key in the Options panel, PETSCII 80 columns included
[!]Oldskool modem: the phone-book animation is gone and the real connection is made when the modem "answers", so boards no longer start their login timers twenty seconds before you see the prompt; ESC still skips to CONNECT
[!]Splash: when "X" cannot be saved (config folder not writable) CGTerm now says so instead of showing the intro forever; Windows settings live in %APPDATA% so this no longer happens in Program Files
[+]Disk tools: validate (rebuild BAM), lock/unlock, rename disk/ID, extract to host, insert into image, CBM listing with splat/lock markers, error-table images, D81/D71 fixes
[+]Terminal: status line, capture toggle, copy screen text, paste transliterates UTF-8, macros record RETURN/cursor/F-keys
[+]ANSI: scroll regions, pending wrap, DECSC/DECRC, ICH/DCH/ECH, iCE colours, truecolour, line drawing, xterm keys, Alt hotkeys
[+]Network: IPv6 with IPv4 first, Telnet negotiation offered in ANSI mode, raw 0xFF from PETSCII boards shown, no SIGPIPE/EAGAIN losses
[+]Session: connection history with redial, per-bookmark auto-login scripts, reconnect after any drop, options panel, keyboard layouts scanned from assets
[+]Audio: bell and modem sounds on macOS/Linux, runtime mute and volume, gapless XM loop
[+]Build: native MinGW build, make test / make fuzz, GitHub Actions CI, make uninstall/distclean, %APPDATA% on Windows
[!]Dozens of bug fixes listed in REVIEW-2026-10.md

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

Respect forever to MagerValp for the original CGTerm and the BBS culture it stands for. Scene Edition: code David "m00p" Jacoby (Genesis*Project), music Mr.Death, support mermaid, QA Larry / Jucke / SkyHawk, and every sysop still running a C64 (or bridge) board.
══════════════════════════════════════════════════════════════════════
  --- end of changelog ---
══════════════════════════════════════════════════════════════════════
