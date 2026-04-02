int net_connect(const char *host, int port, void (*status)(int, char *));
signed int net_receive(void);
signed int net_receive_raw(void);
void net_send(unsigned char c);
void net_send_string(const unsigned char *s);
void net_disconnect(void);
int net_connected(void);
