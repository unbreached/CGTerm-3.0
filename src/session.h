/* Session-level odds and ends for cgterm: capture file, status row text,
 * ZMODEM auto-start detection. */
int session_capture_open(const char *path);   /* NULL = default dated file in the download dir */
void session_capture_close(void);
int session_capture_active(void);
const char *session_capture_path(void);
void session_capture_byte(int byte);          /* PETSCII/ANSI byte as received or echoed */

/* Rebuild the status row text if anything changed (cheap; call every frame). */
void session_status_tick(void);
void session_status_message(const char *text, unsigned int ms);  /* transient override */

/* Feed received bytes; returns 1 once the ZMODEM ZRQINIT sentinel has been seen. */
int session_zmodem_sentinel(int c);
