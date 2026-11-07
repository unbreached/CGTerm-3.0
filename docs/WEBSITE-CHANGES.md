# Website changes for the 3.0.3 release (www.cgterm.se)

The live page's source is not in this repository, so the text below is
written to be pasted into the live page. Package file names stay
`CGTerm-3.0-*` (matching the repo, the branding and the graphics); only
the program version moves to 3.0.3.

## 1. Version and credits (everywhere on the page)

- Bump every version string to **3.0.3**: header badge "3.0.3", title
  "CGTerm 3.0.3: Scene Edition", changelog heading
  "CGTERM 3.0.3 / SCENE EDITION", footer
  "CGTerm 3.0.3 · Genesis*Project · 2026".
- Credits unchanged: Code **David "m00p" Jacoby (Genesis*Project)**,
  music Mr.Death, QA **Larry, Jucke, SkyHawk, hedning**.
- Thank-you for the reports stays: **mrdudz** and **THE-MrMicro**
  (GitHub issues), and **Larry** (transfer testing).

## 2. Download files

Upload the packages from the GitHub release `v3.0.3` under exactly the
names the Download section links. The names do not change from 3.0.2, so
existing links keep working once the files are replaced:

| Link on the site | File | SHA-256 |
|------------------|------|---------|
| `./files/CGTerm-3.0-win32.zip` | CGTerm-3.0-win32.zip | WIN32_SHA |
| `./files/CGTerm-3.0-macos.zip` | CGTerm-3.0-macos.zip | MACOS_SHA |
| `./files/CGTerm-3.0-source.tar.gz` | CGTerm-3.0-source.tar.gz | SOURCE_SHA |

The tarball unpacks to `CGTerm-3.0/`, so the Linux instructions stay
`tar xzf CGTerm-3.0-source.tar.gz` and `cd CGTerm-3.0`.

## 3. Changelog: add this 3.0.3 block above the 3.0.2 block

```
─── 3.0.3 · november 2026 ───

[!] PETSCII starts in upper case / graphics like a real C64 again, so
    boards that do not send a case code first keep their graphics
    instead of showing letters where graphics should be.
[!] The "case = upper" / "case = lower" config setting now actually
    sticks (it was being overwritten at startup), and Commodore+Shift
    (Alt+Shift) still flips the case live from the terminal.
[!] The keyboard profiles (mac-se-c64.kbd and friends) now ship in
    assets/, so "cgterm -k assets/mac-se-c64.kbd" works as the README
    says instead of failing to open the file.
[!] Fixed an upload regression from 3.0.2: a Punter, Multi Punter or
    Rainbow upload could send an empty file because the upload size was
    clamped to the download limit. XMODEM was never affected.
[!] The transfer window no longer clips a long announced filename.
```

## 4. Feature boxes: text to adjust

- **Terminal**: add "Starts in C64 upper/graphics case; set `case = lower`
  in the config, or flip it live with Commodore+Shift."
- **Keyboard**: "Pick a layout at startup, e.g.
  `cgterm -k assets/mac-se-c64.kbd` (US/SE for mac, linux and windows)."

## 5. Still true from 3.0.2 (leave as is, re-check once)

- HTTPS still serves the "NO SIGNAL" placeholder with a self-signed
  cert; only `http://cgterm.se` has the site. Point the TLS vhost at the
  site (Let's Encrypt) or redirect HTTPS to HTTP. The READMEs link
  `http://www.cgterm.se`.
- macOS first launch on recent macOS needs System Settings > Privacy &
  Security > "Open Anyway" (the app is signed but not notarized).
- The program uses SDL 1.2 (sdl12-compat on top of SDL2/SDL3); "SDL
  builds" is accurate, "SDL2 builds" is not.

## 6. Nice to have

- Link REVIEW-2026-10-31.md (the 3.0.2 security pass) from the changelog
  footnote, alongside REVIEW-2026-10.md.
- Auto-login example for the Sessions box:
  `login=w:handle;s:M00P;cr;w:password;s:secret;cr` in a bookmark's
  notes (case and control codes are ignored when matching).
