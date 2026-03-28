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
void menu_draw_message(const char *message);
void menu_draw_bookmarks(void);
void menu_fs_draw(const char *title);
void menu_fs_draw_line(int line, const char *text, int selected, int font);
