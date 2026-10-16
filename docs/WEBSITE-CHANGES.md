# Website changes for the 3.0.1 release (www.cgterm.se)

Checked against the live site on 2026-10-16. The live page's source is
not in this repository (the copies under ~/code/AI/website are older
designs), so the text below is written to be pasted into the live page.

## 1. Version and credits (everywhere on the page)

- The release is **3.0.1** (the branding, URLs and graphics stay
  "CGTerm 3.0"): header badge "3.0.1", title "CGTerm 3.0.1: Scene
  Edition", changelog heading "CGTERM 3.0.1 / SCENE EDITION", footer
  "CGTerm 3.0.1 · Genesis*Project · 2026". Everything that currently says
  "3.1" on the page changes to "3.0.1".
- Replace **m00p** with **David "m00p" Jacoby** in the About text
  ("this scene edition is a deep refresh by David "m00p" Jacoby and
  Genesis*Project"), in the Code credit ("Code: David "m00p" Jacoby
  (Genesis*Project)") and in the changelog footer.
- QA line: **Larry, Jucke, SkyHawk, hedning**.

## 2. Must fix before announcing

### HTTPS serves the wrong page
`https://cgterm.se` and `https://www.cgterm.se` show the "NO SIGNAL /
THIS CHANNEL IS OFF THE AIR" placeholder with a self-signed certificate.
Only `http://cgterm.se` has the site. Browsers that auto-upgrade to HTTPS
(and every link from GitHub) land on the placeholder. Point the TLS
virtual host at the site (Let's Encrypt), or redirect HTTPS to HTTP until
then. The READMEs link `http://www.cgterm.se` for now.

### Download files
Upload the packages from the GitHub release `v3.0.1` (draft until
published) under exactly the names the Download section links:

| Link on the site | File | SHA-256 |
|------------------|------|---------|
| `./files/CGTerm-3.0-win32.zip` | CGTerm-3.0-win32.zip | a756803768c3f579f5678b00f0b6b3cd4023cc2579e2c30ba77f511045c2153d |
| `./files/CGTerm-3.0-macos.zip` | CGTerm-3.0-macos.zip | 953236cb2330945a378a2e0840648483c3c60bc5964f429b7345e47b5a27c874 |
| `./files/CGTerm-3.0-source.tar.gz` | CGTerm-3.0-source.tar.gz | see the release page |

The file names stay `CGTerm-3.0-*` like the repository and the graphics;
the tarball unpacks to `CGTerm-3.0/`, so the Linux instructions on the
page must say `tar xzf CGTerm-3.0-source.tar.gz` and `cd CGTerm-3.0`. The
Download links currently point at `CGTerm-3.1-*` names and need to change
to `CGTerm-3.0-*`.

## 3. Changelog: replace the "3.1 · october 2026" block with this

```
─── 3.0.1 · october 2026 ───

[!] Punter / Multi Punter upload: the file type byte was reversed, so
    every 3.0 upload landed on the boards as a SEQ file nobody could
    download. PRG is now sent as PRG, SEQ as SEQ.
[!] Phonebook: boards listed in the config file came back after being
    deleted. Bookmarks now live only in the bookmark file (moved there
    automatically on first start).
[!] Splash: "X" to never show the intro again is checked and reported
    when the setting cannot be saved; Windows settings live in
    %APPDATA%\CGTerm so this no longer happens in Program Files.
[+] Window can be dragged to any size and snaps to the nearest zoom
    step (1x-8x); 40/80 columns is a key in the Options panel, PETSCII
    80 columns included.
[!] Oldskool modem: the phone-book animation is gone and the real
    connection is made when the modem "answers", so boards no longer
    start their login timers during the dial sequence. ESC skips to
    CONNECT.
[+] ZMODEM: batch send/receive, file names and sizes, crash recovery,
    CRC-32, auto-start when the board sends.
[+] Transfers: last protocol preselected (T + Return), transfer log with
    bytes/time/cps, on-screen summary, cancel prompt no longer stalls
    the peer; debug mode (-b) traces every transfer byte.
[+] Multi Punter upload: waits for a slow 1541 board instead of giving
    up after 3 seconds, opens D64 files before announcing them, stops
    the batch on a failed file and reports it.
[+] XMODEM: CRC to checksum fallback, bounded handshakes, cancel during
    send, 0-byte files.
[+] Punter: one-byte last blocks no longer lost, duplicate block guard,
    empty files.
[+] Disk tools: validate (rebuild BAM), lock/unlock, rename disk/ID,
    extract to host, insert into image, CBM listing with splat/lock
    markers, error-table images, D81 directory and D71 allocation fixes.
[+] Terminal: status line, capture toggle, copy screen text, paste
    transliterates UTF-8, macros record RETURN/cursor/F-keys.
[+] ANSI: scroll regions, pending wrap, DECSC/DECRC, ICH/DCH/ECH, iCE
    colours, truecolour, line drawing, xterm keys, Alt hotkeys; hide
    cursor and reverse video fixed; termmode = ansi loads the right font.
[+] Network: IPv6 with IPv4 first, Telnet negotiation offered in ANSI
    mode, 0xFF handled correctly on Telnet and raw boards, no SIGPIPE /
    EAGAIN losses.
[+] Session: connection history with redial, per-bookmark auto-login
    scripts, reconnect after any drop, options panel, keyboard layouts
    scanned from assets.
[+] Audio: bell and modem sounds on macOS/Linux, runtime mute and
    volume, gapless XM loop.
[+] Build: native MinGW build, make test / make fuzz, GitHub Actions CI,
    make uninstall / distclean, %APPDATA% on Windows.
[!] Dozens of further bug fixes, listed in REVIEW-2026-10.md in the
    source package.
```

## 4. Feature boxes: text to adjust

- **Modem sim & audio**: replace the last sentence with: "The dial
  sequence runs first and the board is called when the modem answers,
  so no board times out while you listen; ESC skips to CONNECT."
- **Transfers**: append "Uploads are stored as PRG/SEQ according to the
  file you send; debug mode (-b) records every transferred byte."
- **UI**: append "Drag the window to any size (it snaps to whole zoom
  steps) and switch 40/80 columns from the Options panel."
- **Bookmarks**: replace "per-board notes file" with "per-board notes and
  auto-login scripts; bookmarks live in their own file, so deleting one
  sticks".
- **Platforms**: "Windows portable archive (settings, bookmarks and logs
  in %APPDATA%\CGTerm)".

## 5. Text that is stale or wrong

- "What is this release" block: "macOS: from source, SDL2 + optional
  libopenmpt" should say the macOS download is a self-contained signed
  app (Apple Silicon); building from source is still possible.
- About text says "cross-platform SDL2 builds". The program uses SDL 1.2
  (sdl12-compat on top of SDL2/SDL3). "SDL builds" is accurate.
- macOS download note: on macOS 15 the first launch needs System
  Settings > Privacy & Security > "Open Anyway" (the app is signed but
  not notarized). Right-click > Open is no longer enough.

## 6. Nice to have

- Link REVIEW-2026-10.md in the repository from the changelog footnote.
- Mention `make test` / `make fuzz` and the GitHub Actions badge.
- Add the auto-login syntax to the Sessions box:
  `login=w:Handle;s:name;cr;w:Password;s:secret;cr` in a bookmark's notes.
