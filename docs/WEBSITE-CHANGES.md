# Website changes for the 3.1.0 release (www.cgterm.se)

Checked against the live site on 2026-10-05. The site already describes
3.1 well; the items below are what still differs from the shipped code
and packages.

## 1. Must fix before announcing

### HTTPS serves the wrong page
`https://cgterm.se` and `https://www.cgterm.se` show the "NO SIGNAL /
THIS CHANNEL IS OFF THE AIR" placeholder with a self-signed certificate.
Only `http://cgterm.se` has the site. Browsers that auto-upgrade to HTTPS
(and every link from GitHub) land on the placeholder. Either point the
TLS virtual host at the site (and get a real certificate, e.g. Let's
Encrypt) or redirect HTTPS to HTTP until then. The READMEs in the repo
link `http://www.cgterm.se` for now and should go back to https once
this works.

### Download files
The Download section links these names; upload the packages from the
GitHub release `v3.1.0` (draft until published) with exactly these names:

| Link on the site | File to upload | SHA-256 |
|------------------|----------------|---------|
| `./files/CGTerm-3.1-win32.zip` | CGTerm-3.1-win32.zip | 18f48e714e4018c3e14bcbcbb7251151a6eab06d8968ae7af3a12417375226fa |
| `./files/CGTerm-3.1-macos.zip` | CGTerm-3.1-macos.zip | cb00f7559952eab463641da698b48853a43302a5070e9e53a47d6ece3c2595f0 |
| `./files/CGTerm-3.1-source.tar.gz` | CGTerm-3.1-source.tar.gz | see the release page (the tarball cannot contain its own hash) |

The source tarball unpacks to `CGTerm-3.1/`, matching the Linux
instructions on the page (`cd CGTerm-3.1`).

## 2. Changelog lines to add

Reported by a caller after months of 3.0 use (fixed 2026-10-15, in the
3.1.0 packages):

- Punter and Multi Punter uploads were stored on the boards as SEQ files
  (file type byte reversed), so nobody could download what you uploaded.
  PRG now arrives as PRG. This one deserves its own line at the top.
- Deleted phonebook entries came back on the next start because the
  defaults lived in the config file; bookmarks now live only in the
  bookmark file and are moved there automatically.
- "Press X to never see the intro again" did nothing when the config
  folder was not writable (Windows, Program Files); settings now live in
  %APPDATA% and a failed save is reported.

From a long-time CGTerm user (fixed 2026-10-15):

- The window can be dragged to any size; it snaps to the nearest whole
  zoom step (no more "small or fullscreen only").
- 40/80 columns is now a key in the Options panel, including PETSCII 80
  columns; ANSI stays 80.
- Oldskool modem mode no longer shows the scrolling phone book, and the
  TCP connection is made when the modem "answers" instead of before the
  dial sequence, so boards no longer time out during the theatre.

The 3.1 changelog on the site is the one from CHANGELOG.md. These fixes
are the ones callers actually hit and are not spelled out there:

- Multi Punter upload: no longer gives up after a 3 second silence while
  the board writes to its drive; files picked from inside a D64 are
  opened before they are announced (a failed open used to leave the
  board waiting inside a Punter receive); a failed file stops the batch
  and is reported instead of being counted as sent.
- Downloads over a Telnet board: 0xFF bytes inside Punter/XMODEM data are
  no longer doubled or corrupted (incoming IAC IAC is collapsed, outgoing
  0xFF only escaped after a real Telnet negotiation).
- ANSI boards: hide-cursor no longer moves the cursor to the top left,
  reverse video no longer draws box characters, and starting with
  `termmode = ansi` in the config now loads the ANSI font.
- Disk images: "New disk image" D81 now has a working directory; D71 no
  longer reports disk full with ~170 blocks free; a disk-full save no
  longer leaves an undeletable splat file in the image.

## 3. Text that is stale or wrong

- QA line: add **hedning** (listed as tester in the README; Larry, Jucke
  and SkyHawk are already there).
- "What is this release" block: "macOS: from source, SDL2 + optional
  libopenmpt" should say the macOS download is a self-contained signed
  app (Apple Silicon); building from source is still possible.
- About text says "cross-platform SDL2 builds". The program uses SDL 1.2
  (sdl12-compat on top of SDL2/SDL3). "SDL builds" or "SDL 1.2 via
  sdl12-compat" is accurate.
- macOS download note: on macOS 15 the first launch needs System
  Settings > Privacy & Security > "Open Anyway" (the app is signed but
  not notarized). Right-click > Open is no longer enough.
- Windows note: settings, bookmarks, notes and logs now live in
  `%APPDATA%\CGTerm` (the site mentions this in Platforms; worth
  repeating next to the download).

## 4. Nice to have

- Link to `REVIEW-2026-10.md` in the repository from the changelog
  footnote ("dozens of bug fixes") so people can read what was fixed.
- Mention `make test` / `make fuzz` and the GitHub Actions badge for the
  curious.
- Add the auto-login script syntax to the Sessions feature box:
  `login=w:Handle;s:name;cr;w:Password;s:secret;cr` in a bookmark's notes.
