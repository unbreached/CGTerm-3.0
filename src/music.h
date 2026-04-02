void music_preload_mikmod(void);
int music_init(void);
int music_play(const char *filename);
void music_stop(void);
void music_set_volume(int vol);  /* 0-128 */
int music_is_playing(void);
void music_shutdown(void);
int music_load_sfx(const char *filename);
int music_load_sfx_raw(void *buf, unsigned int len);
void music_free_sfx(int id);
void music_play_sfx(int id);
int music_sfx_playing(void);
