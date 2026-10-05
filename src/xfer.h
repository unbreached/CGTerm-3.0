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
  PROT_MULTIPUNTER,
  PROT_ZMODEM
} Protocol;


extern Direction xfer_direction;
extern Protocol xfer_protocol;
extern char xfer_filename[];
extern int xfer_cancel;
extern int xfer_saved_bytes;
extern int xfer_file_size;
#define XFER_BUFFER_SIZE 4096
/* upper bound on a single download; far above any legitimate C64-era
   transfer (a D81 image is 800 KB), so a hostile server cannot stream an
   unbounded file to fill the disk. */
#define XFER_MAX_DOWNLOAD (64 * 1024 * 1024)
extern unsigned char xfer_buffer[];
extern int xfer_debug;


struct fileselector;

int xfer_recv(void);
/* rawname: exact 16-byte PETSCII name when sending from inside a disk
 * image (may be NULL). Returns 1 on success. */
int xfer_send(const char *filename, const unsigned char *rawname);
void xfer_send_multipunter(struct fileselector *fs);
void xfer_send_zmodem(struct fileselector *fs);
/* Returns 1 saved, 0 failed, -1 re-prompting the user for another name. */
int xfer_save_file(char *filename);
/* Drop the completed-but-unsaved download (user abandoned the name prompt). */
void xfer_discard_download(void);

/* Batch-protocol file hooks (ZMODEM): open a temp file, stream data with
 * xfer_save_data(), then save it under the (sanitised) remote name. */
int xfer_begin_file(void);
int xfer_end_file(const char *remote_name);
void xfer_abort_file(void);
void xfer_log_result(const char *direction, const char *name, long bytes, unsigned int ms, const char *result);

void xfer_send_byte(unsigned char c);
signed int xfer_recv_byte(int timeout);
signed int xfer_recv_byte_error(int timeout, int errorcnt);
int xfer_save_data(unsigned char *data, int length);
int xfer_load_data(unsigned char *data, int length);

void xfer_progress_status(const char *message, int current, int total);
void xfer_progress(const char *message);
