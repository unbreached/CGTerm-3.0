struct menu {
  signed int row;
  char *key;
  char *text;
};


extern SDL_bool menu_visible;
extern SDL_bool menu_dirty;
extern SDL_Surface *menu_surface;
extern int menu_width, menu_height;


void menu_cls(void);
int menu_init(int width, int height);
void menu_show(void);
void menu_hide(void);
void menu_print_menu(struct menu *menu);
void menu_draw_input(const char *title);
void menu_update_input(const char *text, int cursorpos);
void menu_draw_xfer(void);
void menu_update_xfer(int direction, int protocol);
void menu_draw_xfer_progress(const char *filename, int direction, int protocol);
void menu_update_xfer_progress(const char *message, int bytes, int total);
void menu_update_xfer_block_progress(const char *status, const char *protocol, int current_blocks, int total_blocks);
void menu_draw_rectangle(void);
void menu_draw_splash_frame(int frame, const char *dlpath, const char *ulpath);
void menu_draw_message(const char *message);
void menu_xfer_feed_byte(unsigned char b);
void menu_xfer_set_seq_preview(int enabled);
void menu_draw_message_timed(const char *message, int timeout_ms);
void menu_draw_bookmarks(void);
void menu_draw_bookmarks_sel(int selected);
void menu_fs_draw(const char *title);
void menu_fs_draw_path(const char *path);
void menu_fs_draw_line(int line, const char *text, int selected, int entrytype, unsigned int filesize);
void menu_fs_draw_blocks_free(const char *text);
int menu_select_disk_format(void);
void menu_show_bookmark_info(const char *alias, const char *host, int port);
int menu_select_splash_font(void);
int menu_select_menu_font(void);
const char *menu_get_splash_font_name(int idx);
void menu_keyboard_test(void);
int menu_set_paths(void);
int menu_select_post_speed(void);
int menu_edit_bookmark(char *name, int namesz, char *host, int hostsz, char *port, int portsz, int *mode);
void menu_show_help(const char *filename);
