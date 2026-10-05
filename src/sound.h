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
/* Mixing-device mode: the music engine owns the SDL audio device and calls
 * sound_mix_into() from its callback; samples are still kept here. */
void sound_attach_to_music_device(void);
void sound_mix_into(Sint16 *stream, int frames);
void sound_start_sample(int sample);          /* play, bypassing the SFX hook */
void sound_unregister_buffer(int sample);     /* forget a buffer without freeing it */
