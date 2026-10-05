/* Per-bookmark auto-login scripts.
 *
 * A bookmark note may contain one line
 *   login=w:Handle;s:m00p;cr;w:Password;s:secret;cr
 * with these steps, separated by ';':
 *   w:TEXT   wait until TEXT (case-insensitive) arrives from the board
 *   s:TEXT   send TEXT, paced like a paste (letters are case-swapped for PETSCII)
 *   cr       send RETURN
 *   p:N      pause N milliseconds
 * A wait step gives up after 30 s and aborts the script. ESC (any key that
 * opens the menu) also aborts. */
void login_start(const char *host, int port);
/* Feed one received byte (c >= 0) or -1 for "nothing received"; call every
 * main-loop pass. Returns 1 while a script is running. */
int login_tick(int c);
void login_abort(void);
int login_active(void);
