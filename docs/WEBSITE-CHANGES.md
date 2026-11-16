# Website changes for the 3.0.8 release (www.cgterm.se)

The live page's source is not in this repository, so the text below is
written to be pasted into the live page. Package file names stay
`CGTerm-3.0-*` (matching the repo, the branding and the graphics); only
the program version moves to 3.0.8.

Status: the site is updated for 3.0.5 (GitHub issue #13). 3.0.6, 3.0.7
and 3.0.8 are published on GitHub, and the download buttons already
serve 3.0.8. Outstanding on the page: the version strings, the nail
credit (issue #14), the three changelog blocks below, the font count if
the page states one, and a new screenshot of the start screen if the
page shows the old one.

How the site gets its files (since 2026-11-08, per issue #13): the
download buttons point at
`https://github.com/unbreached/CGTerm-3.0/releases/latest/download/CGTerm-3.0-{win32.zip,macos.zip,source.tar.gz}`
and the old `/files/CGTerm-3.0-*` addresses redirect there. Nothing is
hosted on the web server, so a published GitHub release reaches the site
at once, `SHA256SUMS` included. Website work for a release is text only.
That depends on two things staying the same in every release: the three
asset names, and the release being marked "latest".

## 1. Version and credits (everywhere on the page)

- Bump every version string to **3.0.8**: header badge "3.0.8", title
  "CGTerm 3.0.8: Scene Edition", changelog heading
  "CGTERM 3.0.8 / SCENE EDITION", footer
  "CGTerm 3.0.8 · Genesis*Project · 2026".
- Credits: Code **David "m00p" Jacoby (Genesis*Project)**,
  music Mr.Death, QA **Larry, Jucke, SkyHawk, hedning**.
- **New credit (issue #14): ANSI logo by nail / fire.** nail drew the
  CGTerm ANSI logo that is the animated start screen from 3.0.7. Put it
  in the credits list next to the music credit, written exactly
  "ANSI logo: nail / fire".
- Thank-you for the reports stays: **mrdudz** and **THE-MrMicro**
  (GitHub issues), and **Larry** (transfer testing).

## 2. Changelog: add the 3.0.8, 3.0.7 and 3.0.6 blocks, in that order, above the 3.0.5 block

```
─── 3.0.8 · november 2026 ───

TRANSFERS (checked against C*Base 3.1's own Punter and X-Modem code)
[!] XMODEM download: one stray CAN byte no longer aborts the
    transfer. A real cancel from the board still stops it at once.
[!] XMODEM upload: a file that ends in 0x1A arrives complete; the
    board no longer strips its last bytes with the padding.
[!] Punter download: a broken block is reported as a failed
    transfer instead of a "complete" truncated file.
[+] The transfer window shows percentage and time left.

KEYBOARD AND INPUT
[+] Your own keyboard profiles: put *.kbd files in ~/.cgterm-kbd
    (Windows: %APPDATA%\CGTerm\cgterm-kbd) and they show up in the
    keyboard menu.
[+] Ctrl+V / Cmd+V pastes into the Connect, bookmark and file name
    dialogs.

DISPLAY
[+] Screenshots are saved as PNG.

FONTS AND LICENCES
[!] Five header fonts whose licences do not allow redistribution
    are removed: Maesto, Nano Pix, Blackentina 4F, Playful Boxes
    and Sportifity. Six header fonts remain.
[+] The packages carry the licence texts of the fonts and the
    bundled libraries.
```

```
─── 3.0.7 · november 2026 ───

[+] New start screen: the CGTerm ANSI logo by nail / fire, with a
    pulsing glow and a sweep of light. It shows while you are offline
    and gives way as soon as the terminal or a menu draws.
[+] Your own start screen: put any 80-column ANSI in the assets
    folder as startup.ans and it gets the same treatment. Remove the
    file to get the old picture back.
```

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

- **Screenshot**: if the page shows the old "CGTERM 3.0" start picture
  (green computer and modem), replace it with a screenshot of the new
  start screen from 3.0.7.
- **Look and feel / intro box**: add "Animated ANSI start screen, logo
  by nail / fire. Drop in your own startup.ans to replace it."
- **Fonts (3.0.8)**: if the page names or counts the header fonts,
  remove Maesto, Nano Pix, Blackentina 4F, Playful Boxes and
  Sportifity and change the count from 11 to 6. Replace any screenshot
  that shows one of them.
- **Screenshots (3.0.8)**: where the page says screenshots are BMP,
  it is PNG now.
- The 3.0.5 wording (ZMODEM asks before accepting, AltGr, `\;`, the
  macOS line "Apple Silicon, macOS 26 or later", Linux source only) is
  already on the page and stays.

## 4. Still true (leave as is)

- macOS first launch on recent macOS needs System Settings > Privacy &
  Security > "Open Anyway" (the app is signed but not notarized).
- The program uses SDL 1.2 (sdl12-compat on top of SDL2/SDL3); "SDL
  builds" is accurate, "SDL2 builds" is not.
- The site is served over HTTPS with a valid certificate; the READMEs
  link `https://www.cgterm.se`.

## 5. Checklist for the web team

1. Bump the version strings to 3.0.8 (section 1), including the dev
   log, llms.txt and JSON-LD.
2. Add the credit "ANSI logo: nail / fire" (section 1, issue #14).
3. Paste the 3.0.8, 3.0.7 and 3.0.6 changelog blocks (section 2) above
   the 3.0.5 block.
4. Replace the start-screen screenshot if the page has one, and fix
   the font list and the screenshot format (section 3).
5. Reload the page without cache and check that the tag shown under
   Download reads v3.0.8.
