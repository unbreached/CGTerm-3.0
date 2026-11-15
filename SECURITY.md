# Security

CGTerm connects to bulletin boards you do not control, so everything the
board sends (terminal output, file transfers, file names) is treated as
untrusted input.

## Reporting a problem

Please do not open a public issue for a security problem. Use GitHub's
private reporting instead: the "Report a vulnerability" button under the
Security tab of this repository. Include the CGTerm version (`cgterm -V`),
your platform, and the steps or data needed to reproduce it. A debug trace
(`cgterm -b`, written to the CGTerm user directory) helps, but read it
first: it records everything typed, passwords included.

## Supported versions

Fixes go into the latest release only.

## Things worth knowing

- Connections are plain Telnet/TCP. Nothing is encrypted, including
  logins. Auto-login scripts are stored in plaintext in the notes file
  (readable by your user only).
- Downloads are saved under the name the board announces, inside the
  download directory. Path components, control characters and reserved
  names are rejected, existing files are never overwritten, and a leading
  dot is replaced.
- An automatic ZMODEM download asks for confirmation the first time on
  each connection.
