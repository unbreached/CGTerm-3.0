# Website changes for the 3.0.6 release (www.cgterm.se)

The live page's source is not in this repository, so the text below is
written to be pasted into the live page. Package file names stay
`CGTerm-3.0-*` (matching the repo, the branding and the graphics); only
the program version moves to 3.0.6.

Status: the site is updated for 3.0.5 (GitHub issue #13). 3.0.6 is
published as GitHub release `v3.0.6`, so only the 3.0.6 items below are
outstanding: the version strings and the 3.0.6 changelog block.

How the site gets its files (since 2026-11-08, per issue #13): the
download buttons point at
`https://github.com/unbreached/CGTerm-3.0/releases/latest/download/CGTerm-3.0-{win32.zip,macos.zip,source.tar.gz}`
and the old `/files/CGTerm-3.0-*` addresses redirect there. Nothing is
hosted on the web server, so a published GitHub release reaches the site
at once, `SHA256SUMS` included. Website work for a release is text only.
That depends on two things staying the same in every release: the three
asset names, and the release being marked "latest".

## 1. Version and credits (everywhere on the page)

- Bump every version string to **3.0.6**: header badge "3.0.6", title
  "CGTerm 3.0.6: Scene Edition", changelog heading
  "CGTERM 3.0.6 / SCENE EDITION", footer
  "CGTerm 3.0.6 · Genesis*Project · 2026".
- Credits: Code **David "m00p" Jacoby (Genesis*Project)**,
  music Mr.Death, QA **Larry, Jucke, SkyHawk, hedning**.
- **New credit, add it now (it does not have to wait for 3.0.7): ANSI
  logo by nail / fire.** nail drew the CGTerm ANSI logo that becomes the
  animated start screen in 3.0.7. Put it in the credits list next to
  the music credit, written exactly "ANSI logo: nail / fire".
- Thank-you for the reports stays: **mrdudz** and **THE-MrMicro**
  (GitHub issues), and **Larry** (transfer testing).

## 1b. Coming in 3.0.7 (not released yet, nothing to paste)

3.0.7 replaces the still start picture with nail's ANSI logo, animated
with a glow and a sweep of light. If the page shows a
screenshot of the old "CGTERM 3.0" start picture, it will need a new
one when 3.0.7 is published; a changelog block will follow then.

## 2. Changelog: add this 3.0.6 block above the 3.0.5 block

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

The full list is in `CHANGELOG.md` in the source package and in the
GitHub release notes.

## 3. Feature boxes and page text

Nothing new for 3.0.6. The 3.0.5 wording (ZMODEM asks before accepting,
AltGr, `\;`, the macOS line "Apple Silicon, macOS 26 or later", Linux
source only) is already on the page and stays.

## 4. Still true (leave as is)

- macOS first launch on recent macOS needs System Settings > Privacy &
  Security > "Open Anyway" (the app is signed but not notarized).
- The program uses SDL 1.2 (sdl12-compat on top of SDL2/SDL3); "SDL
  builds" is accurate, "SDL2 builds" is not.
- The site is served over HTTPS with a valid certificate; the READMEs
  link `https://www.cgterm.se`.

## 5. Checklist for the web team

1. Bump the version strings to 3.0.6 (section 1), including the dev
   log, llms.txt and JSON-LD.
2. Paste the 3.0.6 changelog block (section 2) above the 3.0.5 block.
3. Reload the page without cache and check that the tag shown under
   Download reads v3.0.6.
