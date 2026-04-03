typedef enum direction {
  DIR_NONE = 0,
  DIR_SEND,
  DIR_RECV
} Direction;

typedef enum protocol {
  PROT_NONE = 0,
  PROT_XMODEM,
  PROT_XMODEMCRC,
  PROT_XMODEM1K,
  PROT_PUNTER,
  PROT_RAINBOW,
  PROT_MULTIPUNTER
} Protocol;


extern Direction xfer_direction;
extern Protocol xfer_protocol;
extern char xfer_filename[];
extern int xfer_cancel;
extern int xfer_saved_bytes;
extern int xfer_file_size;
extern unsigned char xfer_buffer[];
extern int xfer_debug;


struct fileselector;

int xfer_recv(void);
void xfer_send(char *filename);
void xfer_send_multipunter(struct fileselector *fs);
void xfer_save_file(char *filename);

void xfer_send_byte(unsigned char c);
signed int xfer_recv_byte(int timeout);
signed int xfer_recv_byte_error(int timeout, int errorcnt);
int xfer_save_data(unsigned char *data, int length);
int xfer_load_data(unsigned char *data, int length);

void xfer_progress_status(const char *message, int current, int total);
void xfer_progress(const char *message);
