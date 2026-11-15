int net_connect(const char *host, int port, void (*status)(int, char *));
signed int net_receive(void);
signed int net_receive_raw(void);
void net_send(unsigned char c);
void net_send_string(const unsigned char *s);
void net_disconnect(void);
void net_send_naws(void);   /* after a 40/80 column switch */
int net_connected(void);

/* Set by the application; called when a connection starts or ends so
 * nothing queued for the previous peer (paste, login script) reaches the
 * next one. May be NULL. */
extern void (*net_session_reset_hook)(void);
