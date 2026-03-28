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

## Notes

- The Linux and Windows profiles are based on the existing `us.kbd`, `swedish.kbd` and `windows.kbd`.
- The macOS profiles are based on the more complete `Mac5.kbd` table.
- `mac-se-c64.kbd` is a best-effort Swedish profile and is the one most likely to need small follow-up adjustments after live testing.
