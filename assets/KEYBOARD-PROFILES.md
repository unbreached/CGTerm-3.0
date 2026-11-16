# CGTerm keyboard profiles

These profiles are for the use case where a modern host keyboard should feel
like a C64 keyboard inside CGTerm.

## Profiles

- `linux-us-c64.kbd`
- `linux-se-c64.kbd`
- `win-us-c64.kbd`
- `win-se-c64.kbd`
- `mac-us-c64.kbd`
- `mac-se-c64.kbd`

## Recommended usage

Select the profile explicitly instead of relying on a generic default. Example:

```bash
./cgterm -k assets/mac-se-c64.kbd
./cgterm -k assets/mac-us-c64.kbd
./cgterm -k assets/linux-se-c64.kbd
./cgterm -k assets/win-us-c64.kbd
```

## Your own profiles

The shipped profiles live inside the program folder (the .app bundle on
macOS, the install folder on Windows), which is not a good place to edit.
Copy a profile to your own folder, change it there, and it shows up in the
keyboard menu with "(mine)" after its name:

- macOS / Linux: `~/.cgterm-kbd/`
- Windows: `%APPDATA%\CGTerm\cgterm-kbd\`

## Notes

- The Linux and Windows profiles are based on the existing `us.kbd`, `swedish.kbd` and `windows.kbd`.
- The macOS profiles are based on the more complete `Mac5.kbd` table.
- `mac-se-c64.kbd` is a best-effort Swedish profile and is the one most likely to need small follow-up adjustments after live testing.

## What a profile controls

In PETSCII mode ordinary typing does not go through the profile: letters,
digits and punctuation are taken from the character your keyboard layout
produces, so they follow the host layout. A profile supplies:

- the Commodore column (left Alt + key: colours and graphics),
- the Ctrl column (Ctrl + key: colours, reverse on/off),
- the fallback for keys that produce no character of their own.

RETURN, cursor keys, HOME/CLR, DEL/INST, RUN/STOP (Tab) and F1-F8 are fixed
and cannot be remapped from a profile. Right Alt (AltGr) types its normal
character on layouts that use it; left Alt is the Commodore key.
