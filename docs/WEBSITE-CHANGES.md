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
| `./files/CGTerm-3.1-win32.zip` | CGTerm-3.1-win32.zip | 9c6bf0b503da6419bcb60a5078ef968468f452c7c25e2214a20e7ac1c4ba3f8e |
| `./files/CGTerm-3.1-macos.zip` | CGTerm-3.1-macos.zip | cb695203a873c054450f20ae8ba531d329ac3e95c040a6aa20e81e3b8c6ba8c3 |
| `./files/CGTerm-3.1-source.tar.gz` | CGTerm-3.1-source.tar.gz | see the release page (the tarball cannot contain its own hash) |

The source tarball unpacks to `CGTerm-3.1/`, matching the Linux
instructions on the page (`cd CGTerm-3.1`).

## 2. Changelog lines to add

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
