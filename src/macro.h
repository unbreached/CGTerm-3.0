extern int macro_rec;
extern int macro_len;
extern int macro_maxlen;
extern int macro_play;
extern int macro_ctr;
extern unsigned char macrobuf_key[];
extern unsigned char macrobuf_shift[];
extern unsigned char macrobuf_ctrl[];
extern unsigned char macrobuf_cbm[];

/* Send text macro slot n (0..9), paced like a paste (issue #8) */
void macro_send_slot(int n);
