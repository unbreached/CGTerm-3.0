# Website changes for the 3.0.2 release (www.cgterm.se)

The live page's source is not in this repository, so the text below is
written to be pasted into the live page. Package file names stay
`CGTerm-3.0-*` (matching the repo, the branding and the graphics); only
the program version moves to 3.0.2.

## 1. Version and credits (everywhere on the page)

- Bump every version string to **3.0.2**: header badge "3.0.2", title
  "CGTerm 3.0.2: Scene Edition", changelog heading
  "CGTERM 3.0.2 / SCENE EDITION", footer
  "CGTerm 3.0.2 · Genesis*Project · 2026".
- Credits unchanged: Code **David "m00p" Jacoby (Genesis*Project)**,
  music Mr.Death, QA **Larry, Jucke, SkyHawk, hedning**.
- Add a thank-you line for the bug reporters: **mrdudz** and
  **THE-MrMicro** (GitHub issues). Optional but nice.

## 2. Download files

Upload the packages from the GitHub release `v3.0.2` (draft until
published) under exactly the names the Download section links. The names
do not change from 3.0.1, so existing links keep working once the files
are replaced:

| Link on the site | File | SHA-256 |
|------------------|------|---------|
| `./files/CGTerm-3.0-win32.zip` | CGTerm-3.0-win32.zip | 49d7b71acc3da4af5acab82d9e8c612b0f5e09c83fc23ccf566ee13d323c8be0 |
| `./files/CGTerm-3.0-macos.zip` | CGTerm-3.0-macos.zip | 9e6566659a3c6c3cda695362e3d55d5e43dca6dae356b3f9441c31c76269eede |
| `./files/CGTerm-3.0-source.tar.gz` | CGTerm-3.0-source.tar.gz | 04f2134e28351f744ac4296e7cd25ba8ff2213ebf75da63448729cb1f41dc2ae |

The tarball unpacks to `CGTerm-3.0/`, so the Linux instructions stay
`tar xzf CGTerm-3.0-source.tar.gz` and `cd CGTerm-3.0`.

## 3. Changelog: add this 3.0.2 block above the 3.0.1 block

```
─── 3.0.2 · november 2026 ───

Reported fixes (thanks to mrdudz and THE-MrMicro):
[!] Auto-login ignores the colour/cursor/case codes boards sprinkle
    through their prompts, so the expect text is just the letters you
    see on screen. C*Base logins work now.
[!] RUN/STOP (Tab) sends CHR$(3), so it actually aborts a listing or a
    C*Base pause/more prompt.
[!] A too-large saved zoom can no longer open the window as an unusable
    black rectangle; the zoom is clamped to what fits the display.
[!] The window repaints after alt-tab or being uncovered instead of
    staying blank; dragging it no longer flickers.
[+] Commodore+Shift (Alt+Shift) switches the display case from the
    terminal, like a real C64.
[+] Receive speed can be capped to an old-modem baud rate
    (2400/1200/300 ...) in Options [R], so you can read the scroll.
[+] Window scaling is selectable in Options [W]: smooth (fills any
    window size) or crisp (snaps to whole zoom steps); the menu and the
    offline logo scale with the window.
[+] Ten editable text macros, fired with Cmd/Win+0-9 or from Options
    [M]; stored as macro0..macro9, \r sends RETURN.
[+] Bookmark notes editor has a real cursor (arrows, Home/End, Delete).
[+] Options panel [O] is listed in the menu, local echo shows its
    state, and every menu shares one colour scheme.
[!] Plus a full security and stability pass (overflow, transfer
    freezes, DoS and resource-leak fixes); detail in REVIEW-2026-10-31.md
    in the source package.
```

## 4. Feature boxes: text to adjust

- **UI**: replace the resize sentence with: "Drag the window to any
  size: crisp mode snaps to whole zoom steps, smooth mode fills any
  size. The menu and logo scale with the window."
- **Sessions / Bookmarks**: add "Ten text macros on Cmd/Win+0-9, and a
  cursor-editable notes field for auto-login scripts."
- **Transfers**: add "Cap the receive speed to a retro baud rate so
  message scroll stays readable."

## 5. Still true from 3.0.1 (leave as is, re-check once)

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
