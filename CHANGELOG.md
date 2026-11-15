══════════════════════════════════════════════════════════════════════
CGTERM 3.0 / SCENE EDITION
FULL CHANGELOG / NFO
Genesis*Project · 2026
══════════════════════════════════════════════════════════════════════
─── 3.0.6 · NOVEMBER 2026 ───

Transfers (aligned with the C*Base board after a protocol audit of both sides)
[!]Files uploaded from a disk image keep their type. The type was read from the file name, which has no extension inside an image, so every SEQ and USR file went out as PRG (Punter, Multi Punter and Rainbow)
[!]Multi Punter announces the file type as the letter the drive expects (,P ,S ,U) and always sends one. The lower-case letter used before is a different PETSCII byte: the board could not create the file and the upload never started. Names are cut to 16 characters and characters the drive treats as syntax are replaced
[!]Punter upload waits for the board's verdict while it writes the block to disk. It used to give up after one quiet 3 s read, never acknowledged the late GOO, and the board aborted
[!]Punter single upload repeats its start signal until the board answers, instead of sending it once
[!]Multi Punter download: a board that refuses a file and ends the batch is recognised at once, instead of sending GOO for a minute
[!]Rainbow upload answers the board's ready signal immediately, so the two sides can no longer stay out of step at the start
[!]XMODEM upload uses the receiver's latest start request to choose CRC or checksum; cancelling while waiting for the receiver now tells it (CAN CAN)

─── 3.0.5 · NOVEMBER 2026 ───

Transfers
[!]Rainbow uploads recover from a retry. The block sender consumed its own length and data pointer, so any resend (a NAK, a timeout, one stray byte from the board) went out with a bogus length and no data and the upload could never finish. Only an upload where every block was accepted first time worked. The reply after each block is now scanned for, so a status byte in front of the board's ACK no longer forces a resend
[!]Rainbow downloads throw away the rest of a block they reject. The leftover payload used to be read as control bytes, and a payload byte equal to EOT ended the download as a "success" with a truncated file
[!]Punter downloads no longer lose sync on a stray byte. The download handshake still read fixed 3-byte groups (3.0.4 fixed only the upload side); it now scans the stream like the upload side does
[!]A raw PETSCII board is no longer mistaken for a Telnet server. PETSCII art containing pi (0xFF) followed by a graphics byte looked like Telnet negotiation, which switched on Telnet escaping for the rest of the session and corrupted every later Punter/XMODEM/Rainbow transfer. In PETSCII mode Telnet negotiation is now only believed at the start of a connection
[!]XMODEM upload: queued start requests are purged before the first block, a block is retried at most 20 times, a single stray CAN no longer aborts, and the upload is only reported complete when the receiver acknowledged the end of file
[!]ZMODEM: a file the receiver skipped is no longer counted as sent; a sender streaming empty packets can no longer hold the transfer open forever
[!]Multi Punter: waiting for the board's file announcement now times out instead of waiting until ESC; the pause between files reacts to ESC

Security
[!]Text still queued for a board (a paste, or the password step of an auto-login) is dropped when the connection ends or a new one starts. It used to be sent to whatever answered next, including an automatic redial
[!]Automatic ZMODEM download asks once per connection before it accepts, and only reacts to a complete, valid ZMODEM start header. Any user who could post the old 6-byte trigger in a message base could start a download on every reader
[!]The notes file (which holds auto-login passwords), capture logs, the connection history and the debug trace are now readable by your user only
[!]Downloads: a name starting with a dot is saved with an underscore instead, "CON .txt"-style reserved names are rejected, and the file is created exclusively so nothing existing can be written through
[!]Windows package: built with stack protection, ASLR and DEP like the other platforms; build downloads are checksum-verified; the unused MPEG and FLAC decoder DLLs are no longer shipped
[!]Telnet option replies are answered once and capped, so a server cannot make the client flood replies

Stability
[!]A bad line in the config file no longer stops CGTerm from starting. The line is skipped and a notice is shown on screen. A long macro or a long download path could previously make the saved config unloadable, and uncommenting "reconnect = 0" was rejected
[!]Hostname lookup runs in the background: the window no longer freezes on a slow DNS server, and ESC cancels it
[!]Bookmarks with "_" or ":" in the host (some hostnames, IPv6 addresses) survive a restart
[!]The history list keeps showing new connections after 4096 entries
[!]A failed fullscreen switch keeps the session instead of exiting; closing the window exits with status 0; Quit asks first while connected
[!]Windows: connection errors show a real reason instead of "No error"

Keyboard and input
[!]AltGr works on non-US layouts: AltGr+2 types "@" and AltGr+8 types "[" on Swedish and German keyboards instead of sending colour codes (left Alt is still the Commodore key)
[!]Caps Lock only affects letters. It used to turn RETURN into shift-RETURN, HOME into CLR (clearing the screen) and F1 into F2
[!]Paste, macros and auto-login text are no longer case-swapped on ANSI boards ("Secret" was sent as "sECRET")
[+]Shift+Backspace sends INST
[+]A ";" inside an auto-login step can be written as "\;"

Display
[!]ANSI text fills the whole character cell at every zoom (at zoom 4 it was drawn at half height and box drawing did not join)
[!]ANSI mode has a proper underline cursor instead of blinking a wrong character
[!]Zoom 8 in 40 columns no longer gives a blank window (the limit is 7)
[!]Cursor blink is timed, so it no longer speeds up during transfers
[!]Scrollback: PageUp/PageDown move half a screen, stop at the oldest stored line, and typing returns to the live screen
[!]A screenshot taken from the menu no longer contains the menu; "copy screen" keeps ANSI line drawing as ASCII instead of turning it into letters
[!]Lower idle CPU use

Build and release
[+]The version lives in one place (src/version.h); the modem screen showed "3.1"
[+]make test-asan runs the tests under AddressSanitizer/UBSan; CI also builds the real 32-bit Windows package and runs the sanitizer and fuzz jobs
[+]New tests: Rainbow upload and download, Punter download with a noisy board, XMODEM upload start
[+]make dist-src / make checksums build the source tarball and a SHA256SUMS file; internal files are left out of the tarball
[!]The macOS package now declares the macOS version it really needs instead of claiming 11.0

─── 3.0.4 · NOVEMBER 2026 ───

[!]Punter and Multi Punter uploads work on real boards again. The handshake that waits for the board's GOO/ACK/SYN read the stream in rigid 3-byte groups, so the moment a board printed any status byte around the transfer the groups misaligned and the whole upload failed. It now scans the byte stream for the handshake word and skips stray bytes, the way the download path, the Rainbow block start and the block S/B wait already do (XMODEM waits on single bytes, which is why XMODEM upload and all downloads were never affected). Added a regression test with a noisy scripted C*Base receiver that reproduced the failure

Thanks to Larry for the upload report.

─── 3.0.3 · NOVEMBER 2026 ───

[!]The "case = upper" / "case = lower" setting in the config file now actually takes effect: it was read at startup and then overwritten by a hardcoded case switch, so the setting did nothing. The startup default is unchanged (lower/upper case, which is what PETSCII boards expect); you can now select upper/graphics in the config if you want it, or flip the case live from the terminal with Commodore+Shift (Alt+Shift)
[!]The keyboard layout profiles (mac-se-c64.kbd, mac-us-c64.kbd, linux/win variants) now live in assets/ where the README and in-app help point, so "cgterm -k assets/mac-se-c64.kbd" works out of the box instead of failing to open
[!]Uploads are no longer measured against the download size cap. A regression in the 3.0.2 hardening pass clamped the upload size to the 64 MB download limit and returned zero past it, which could make a Punter, Multi Punter or Rainbow upload send an empty file (XMODEM was not affected). Upload size is now bounded only by the integer range
[!]The transfer progress window no longer clips a long announced filename (the stored name buffer was too small)

Thanks to Larry for the detailed transfer reports.

─── 3.0.2 · NOVEMBER 2026 ───

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
[+]Window scaling can be set to smooth (stretch to fill any window size) or crisp (snap to whole zoom steps) from Options [W] (#1)
[+]One colour palette across all the menus, so [D], labels and values look the same on every screen
[!]An empty macro slot no longer breaks config loading (the parser used to reject any key with an empty value)

Thanks to mrdudz and THE-MrMicro for the careful, detailed reports that drove these fixes.

─── 3.0.2 · SECURITY & STABILITY ───

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
