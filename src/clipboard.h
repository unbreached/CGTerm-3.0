void clipboard_paste(void);
int clipboard_paste_byte(void);
int clipboard_paste_pending(void);
void clipboard_paste_clear(void);
/* Queue text as if pasted (used by login scripts); paced like a paste. */
void clipboard_queue_text(const char *text);
/* Put text on the system clipboard. Returns 1 on success. */
int clipboard_copy_text(const char *text);
