/* ZMODEM batch transfer (see zmodem.c). All functions use the byte I/O
 * layer in xfer.c and honour xfer_cancel. */

/* Receive a whole batch. Returns the number of files saved (>0 = success)
 * or 0 on failure / cancel. */
int zmodem_recv(void);

/* Sender side, in this order:
 *   zmodem_send_begin()   ZRQINIT handshake, learns the receiver's options
 *   zmodem_send_file(n)   one file (the already opened xfer_sendfile)
 *   zmodem_send_end()     ZFIN handshake and "OO"
 * Each returns 1 on success, 0 on failure / cancel. */
int zmodem_send_begin(void);
int zmodem_send_file(const char *displayname);
int zmodem_send_end(void);

/* When non-zero (the default) the receiver advertises CANFC32 and the
 * sender uses 32-bit CRCs. Cleared only for testing the 16-bit paths. */
extern int zmodem_recv_crc32;
