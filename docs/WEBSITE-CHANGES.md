# Website changes for the 3.0.6 release (www.cgterm.se)

The live page's source is not in this repository, so the text below is
written to be pasted into the live page. Package file names stay
`CGTerm-3.0-*` (matching the repo, the branding and the graphics); only
the program version moves to 3.0.6.

Status: 3.0.6 follows 3.0.5 by a few hours. If the page was not yet
updated for 3.0.5, do everything below in one go; sections 3 to 5 cover
both versions. 3.0.6 passes the build and the test suite for all three
packages and is published as GitHub release `v3.0.6`.

## 1. Version and credits (everywhere on the page)

- Bump every version string to **3.0.6**: header badge "3.0.6", title
  "CGTerm 3.0.6: Scene Edition", changelog heading
  "CGTERM 3.0.6 / SCENE EDITION", footer
  "CGTerm 3.0.6 · Genesis*Project · 2026".
- Credits unchanged: Code **David "m00p" Jacoby (Genesis*Project)**,
  music Mr.Death, QA **Larry, Jucke, SkyHawk, hedning**.
- Thank-you for the reports stays: **mrdudz** and **THE-MrMicro**
  (GitHub issues), and **Larry** (transfer testing).

## 2. Download files

Upload the packages from the GitHub release `v3.0.6` under exactly the
names the Download section links. The names do not change, so existing
links keep working once the files are replaced:

| Link on the site | File | SHA-256 |
|------------------|------|---------|
| `./files/CGTerm-3.0-win32.zip` | CGTerm-3.0-win32.zip | 00cba976c6cef73d3d0dd7eff2713c0d463caf59186c2c733925a7c9a00a4d27 |
| `./files/CGTerm-3.0-macos.zip` | CGTerm-3.0-macos.zip | 9e9273739af03e55d5b3775406f0504fb69dd8edbd60745c9fab1c150478162f |
| `./files/CGTerm-3.0-source.tar.gz` | CGTerm-3.0-source.tar.gz | 5e84c59f5edb07ef5aecb75a57457ed30810bc8b4654e1da13be320f8c4b2019 |

The release also carries a `SHA256SUMS` file with the same three sums;
linking it next to the downloads is optional.

Because the file names are the same as for 3.0.4 and 3.0.5, a browser or proxy may
serve the old file from cache. After uploading, download each file once
and compare its SHA-256 with the table.

The tarball unpacks to `CGTerm-3.0/`, so the Linux instructions stay
`tar xzf CGTerm-3.0-source.tar.gz` and `cd CGTerm-3.0`.

## 3. Changelog: add the 3.0.6 block, then the 3.0.5 block, above the 3.0.4 block

```
─── 3.0.6 · november 2026 ───

TRANSFERS (aligned with the C*Base board)
[!] Files uploaded from a disk image keep their type. Every SEQ and
    USR file used to go out as PRG (Punter, Multi Punter, Rainbow).
[!] Multi Punter uploads start on C*Base. The file type letter was
    sent as the wrong PETSCII byte, so the board could not create the
    file and the upload never began.
[!] Punter upload waits for the board while it writes a block to
    disk, instead of giving up after 3 seconds.
[!] Punter single upload repeats its start signal until the board
    answers.
[!] Multi Punter download notices at once when the board refuses a
    file and ends the batch.
[!] Rainbow upload answers the board's ready signal immediately.
[!] XMODEM upload picks CRC or checksum from the receiver's latest
    request; cancelling during the start tells the receiver.
```

```
─── 3.0.5 · november 2026 ───

TRANSFERS
[!] Rainbow uploads recover from a retry. Any resend (a NAK, a
    timeout, one stray byte from the board) used to go out empty, so
    the upload could never finish.
[!] Rainbow downloads no longer end early with a truncated file after
    a rejected block.
[!] Punter downloads no longer lose sync on a stray byte (3.0.4 fixed
    the upload side, this is the download side).
[!] A raw PETSCII board is no longer mistaken for a Telnet server
    because of PETSCII art. That mistake corrupted every later
    transfer in the session.
[!] XMODEM upload start, retry and end-of-file handling tightened;
    ZMODEM no longer counts a skipped file as sent; Multi Punter waits
    time out instead of hanging until ESC.

SECURITY
[!] Text still queued for a board (a paste, or the password step of
    an auto-login) is dropped when the connection ends. It used to be
    sent to whatever answered next.
[!] Automatic ZMODEM download asks once per connection before it
    accepts, and only reacts to a real ZMODEM start header.
[!] Notes (auto-login passwords), capture logs and history are
    readable by your user only.
[!] Downloads cannot overwrite an existing file or create hidden or
    reserved file names.
[!] Windows package built with stack protection, ASLR and DEP.

STABILITY
[!] A bad line in the config file no longer stops CGTerm from
    starting. The line is skipped and a notice is shown.
[!] Hostname lookup no longer freezes the window; ESC cancels it.
[!] Bookmarks with "_" or ":" in the host survive a restart.
[!] Quit asks first while connected.

KEYBOARD
[!] AltGr works on non-US layouts: AltGr+2 types "@" on Swedish and
    German keyboards instead of sending a colour code.
[!] Caps Lock only affects letters (it used to turn HOME into CLR).
[!] Paste and auto-login text keep their case on ANSI boards.
[+] Shift+Backspace sends INST.

DISPLAY
[!] ANSI text fills the whole character cell at every zoom, and ANSI
    mode has a proper underline cursor.
[!] Zoom 8 in 40 columns no longer gives a blank window.
[!] Scrollback: PageUp/PageDown move half a screen and typing returns
    to the live screen.
[!] Lower idle CPU use.
```

The full list is in `CHANGELOG.md` in the source package and in the
GitHub release notes.

## 4. Feature boxes and page text to adjust

- **Transfers**: where the page lists ZMODEM auto-download, add
  "CGTerm asks before it accepts a download a board offers."
- **Keyboard**: add "AltGr types the characters printed on your
  keyboard (@, [, ] and so on) on Swedish, German and other non-US
  layouts. Left Alt is the Commodore key."
- **Auto-login**: add "A semicolon inside a step is written `\;`."
- **macOS download line**: must say **Apple Silicon, macOS 26 or
  later**. The package now declares the macOS version it really needs
  (it used to claim macOS 11 and then fail to start on older systems).
  On an older macOS the user gets a clear "requires macOS 26" message;
  building from source is the route for them. Do not advertise Intel
  Macs.
- **Linux**: source only. There is no .deb or other Linux package, so
  remove any wording that suggests one.
- **Windows**: 32-bit, portable, unzip and run. Unchanged.

## 5. Still true from 3.0.2 (leave as is, re-check once)

- HTTPS still serves the "NO SIGNAL" placeholder with a self-signed
  cert; only `http://cgterm.se` has the site. Point the TLS vhost at the
  site (Let's Encrypt) or redirect HTTPS to HTTP. The READMEs link
  `http://www.cgterm.se`.
- macOS first launch on recent macOS needs System Settings > Privacy &
  Security > "Open Anyway" (the app is signed but not notarized).
- The program uses SDL 1.2 (sdl12-compat on top of SDL2/SDL3); "SDL
  builds" is accurate, "SDL2 builds" is not.

## 6. Checklist for the web team

1. Take the files from the GitHub release `v3.0.6`.
2. Replace the three files under `./files/` and verify the SHA-256 sums.
3. Bump the version strings (section 1).
4. Paste the changelog blocks (section 3) above the 3.0.4 block.
5. Apply the text changes in section 4, the macOS line in particular.
6. Reload the page without cache and test all three download links.
