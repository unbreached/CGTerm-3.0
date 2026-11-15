extern int punter_last_filetype;
int punter_recv(void);
extern int punter_send_filetype;  /* 1 = PRG, 2 = SEQ, 3 = USR */
extern int punter_batch_end_seen; /* punter_recv(): the board ended the batch instead of sending */
int punter_send(void);
int punter_send_no_presignal(void);
