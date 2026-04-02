extern int sound_bell;
extern int sound_initok;

int sound_init(void);
signed int sound_load_sample(const char *filename);
signed int sound_register_buffer(Uint8 *buf, Uint32 len);
void sound_free_sample(int sample);
void sound_free_buffer(int sample);
void sound_play_sample(int sample);
void sound_set_sfx_hook(void (*hook)(int));
int sound_is_playing(void);
