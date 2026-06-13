/*
 * music.c — XM/MOD/IT tracker music playback.
 * macOS/Linux: native libopenmpt + raw SDL audio.
 * Windows: SDL_mixer + libmikmod (bundled with the win32 zip).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifdef WINDOWS

#include <SDL.h>
#include <SDL_mixer.h>

static Mix_Music *current_music = NULL;
static int music_initok = 0;

#define MAX_SFX 8
static Mix_Chunk *sfx_chunks[MAX_SFX];
static int sfx_is_wav[MAX_SFX];   /* 1 = Mix_LoadWAV (Mix_FreeChunk), 0 = raw (free) */
static int sfx_count = 0;         /* number of slots in use */

int music_init(void) {
  if (music_initok) return 0;
  if (Mix_OpenAudio(22050, AUDIO_S16, 1, 1024) < 0) {
    printf("SDL_mixer init failed: %s\n", Mix_GetError());
    return 1;
  }
  Mix_AllocateChannels(4);
  music_initok = 1;
  printf("[+] SDL_mixer initialized (XM/MOD/IT support via libmikmod)\n");
  return 0;
}

int music_play(const char *filename) {
  if (!music_initok) return 1;
  if (current_music) {
    Mix_HaltMusic();
    Mix_FreeMusic(current_music);
    current_music = NULL;
  }
  current_music = Mix_LoadMUS(filename);
  if (!current_music) {
    printf("Couldn't load music %s: %s\n", filename, Mix_GetError());
    return 1;
  }
  if (Mix_PlayMusic(current_music, -1) < 0) {
    printf("Couldn't play music: %s\n", Mix_GetError());
    Mix_FreeMusic(current_music);
    current_music = NULL;
    return 1;
  }
  return 0;
}

void music_stop(void) {
  if (!music_initok) return;
  Mix_HaltMusic();
  if (current_music) {
    Mix_FreeMusic(current_music);
    current_music = NULL;
  }
}

void music_set_volume(int vol) {
  if (!music_initok) return;
  Mix_VolumeMusic(vol);
}

int music_is_playing(void) {
  if (!music_initok) return 0;
  return Mix_PlayingMusic();
}

/* find a free SFX slot, or -1 if the table is full */
static int sfx_find_free_slot(void) {
  int i;
  for (i = 0; i < MAX_SFX; i++) {
    if (sfx_chunks[i] == NULL) return i;
  }
  return -1;
}

int music_load_sfx(const char *filename) {
  Mix_Chunk *chunk;
  int slot;
  if (!music_initok) return -1;
  if ((slot = sfx_find_free_slot()) < 0) return -1;
  chunk = Mix_LoadWAV(filename);
  if (!chunk) {
    printf("Couldn't load SFX %s: %s\n", filename, Mix_GetError());
    return -1;
  }
  sfx_chunks[slot] = chunk;
  sfx_is_wav[slot] = 1;
  sfx_count++;
  return slot;
}

int music_load_sfx_raw(void *buf, unsigned int len) {
  Mix_Chunk *chunk;
  int slot;
  if (!music_initok) return -1;
  if ((slot = sfx_find_free_slot()) < 0) return -1;
  chunk = (Mix_Chunk *)malloc(sizeof(Mix_Chunk));
  if (!chunk) return -1;
  chunk->allocated = 0;
  chunk->abuf = (Uint8 *)buf;
  chunk->alen = len;
  chunk->volume = MIX_MAX_VOLUME;
  sfx_chunks[slot] = chunk;
  sfx_is_wav[slot] = 0;
  sfx_count++;
  return slot;
}

void music_free_sfx(int id) {
  if (!music_initok || id < 0 || id >= MAX_SFX) return;
  if (sfx_chunks[id]) {
    Mix_HaltChannel(-1);
    if (sfx_is_wav[id]) {
      Mix_FreeChunk(sfx_chunks[id]);   /* Mix owns abuf — must use Mix_FreeChunk */
    } else {
      free(sfx_chunks[id]);            /* raw chunk: abuf is owned by the caller */
    }
    sfx_chunks[id] = NULL;
    sfx_is_wav[id] = 0;
    if (sfx_count > 0) sfx_count--;    /* reclaim the slot so the table can't exhaust */
  }
}

void music_play_sfx(int id) {
  if (!music_initok || id < 0 || id >= MAX_SFX || !sfx_chunks[id]) return;
  Mix_PlayChannel(-1, sfx_chunks[id], 0);
}

int music_sfx_playing(void) {
  if (!music_initok) return 0;
  return Mix_Playing(-1);
}

void music_shutdown(void) {
  int i;
  music_stop();
  for (i = 0; i < MAX_SFX; i++) {
    if (sfx_chunks[i]) {
      if (sfx_is_wav[i]) Mix_FreeChunk(sfx_chunks[i]);
      else free(sfx_chunks[i]);
      sfx_chunks[i] = NULL;
      sfx_is_wav[i] = 0;
    }
  }
  sfx_count = 0;
  if (music_initok) {
    Mix_CloseAudio();
    music_initok = 0;
  }
}

#else /* !WINDOWS — macOS/Linux: libopenmpt + raw SDL audio */

#define USE_SIMPLE_AUDIO

#ifdef USE_SIMPLE_AUDIO

#include <SDL.h>
#include <libopenmpt/libopenmpt.h>
#define HAVE_OPENMPT

static int music_initok = 0;
static int music_playing = 0;
static int current_volume = 64; /* 0-128 */

/* Simple music sequencer for C64-style melodies */
typedef struct {
    double frequency;
    int duration;  /* in samples */
} Note;

/* Simple melody inspired by retro computer music */
static Note demo_melody[] = {
    {523.25, 11025},  /* C5 - 0.5 seconds */
    {659.25, 11025},  /* E5 */
    {783.99, 11025},  /* G5 */
    {1046.5, 11025},  /* C6 */
    {783.99, 11025},  /* G5 */
    {659.25, 11025},  /* E5 */
    {523.25, 22050},  /* C5 - 1 second */
    {493.88, 11025},  /* B4 */
    {523.25, 11025},  /* C5 */
    {587.33, 11025},  /* D5 */
    {659.25, 22050},  /* E5 - 1 second */
    {0, 0}            /* End marker */
};

static int current_note = 0;
static int note_samples_played = 0;

/* Native XM module playback support */
#ifdef HAVE_OPENMPT
static openmpt_module *xm_module = NULL;
static int xm_loaded = 0;
#endif

#ifdef HAVE_OPENMPT
/* Native XM module loader using libopenmpt */
static int load_xm_file(const char *filename) {
  FILE *file;
  long file_size;
  unsigned char *file_data;

  /* Clean up any existing module */
  if (xm_module) {
    openmpt_module_destroy(xm_module);
    xm_module = NULL;
    xm_loaded = 0;
  }

  /* Open and read the XM file */
  file = fopen(filename, "rb");
  if (!file) {
    printf("[!] Cannot open XM file: %s\n", filename);
    return 0;
  }

  /* Get file size */
  fseek(file, 0, SEEK_END);
  file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (file_size <= 0) {
    printf("[!] Invalid or empty XM file: %s\n", filename);
    fclose(file);
    return 0;
  }

  /* Allocate buffer and read file */
  file_data = malloc(file_size);
  if (!file_data) {
    printf("[!] Cannot allocate XM file buffer\n");
    fclose(file);
    return 0;
  }

  if (fread(file_data, 1, file_size, file) != file_size) {
    printf("[!] Error reading XM file\n");
    free(file_data);
    fclose(file);
    return 0;
  }
  fclose(file);

  /* Create OpenMPT module */
  xm_module = openmpt_module_create_from_memory2(file_data, file_size,
                                                 NULL, NULL, NULL, NULL,
                                                 NULL, NULL, NULL);
  free(file_data);

  if (!xm_module) {
    printf("[!] Failed to load XM module: %s\n", filename);
    return 0;
  }

  xm_loaded = 1;
  printf("[+] Loaded XM module: %s\n", filename);
  printf("[+] Title: %s\n", openmpt_module_get_metadata(xm_module, "title"));
  return 1;
}
#else
static int load_xm_file(const char *filename) {
  printf("[!] XM module support requires libopenmpt: %s\n", filename);
  return 0;
}
#endif

/* Audio callback for XM module playback */
static void music_audio_callback(void *userdata, Uint8 *stream, int len) {
  /* Clear the audio buffer first */
  memset(stream, 0, len);

  if (music_playing) {
    Sint16 *samples = (Sint16*)stream;
    int sample_count = len / 2; /* 16-bit samples */
    int xm_rendered = 0;

    /* Priority 1: Render XM module if loaded */
#ifdef HAVE_OPENMPT
    if (xm_loaded && xm_module) {
      size_t frames_rendered = openmpt_module_read_mono(
        xm_module,
        22050,  /* sample rate */
        sample_count,
        samples
      );

      /* Apply volume scaling */
      double volume_scale = current_volume / 128.0;
      for (int i = 0; i < frames_rendered; i++) {
        samples[i] = (Sint16)(samples[i] * volume_scale);
      }

      /* If module finished, restart it */
      if (frames_rendered < sample_count) {
        openmpt_module_set_position_seconds(xm_module, 0.0);
      }
      xm_rendered = 1;
    }
#endif

    /* Priority 2: Fall back to melody if no XM */
    if (!xm_rendered) {
      static double phase = 0.0;
      double sample_rate = 22050.0;
      double amplitude = 12000.0 * (current_volume / 128.0);

      for (int i = 0; i < sample_count; i++) {
        /* Check if current note is finished */
        if (demo_melody[current_note].frequency == 0) {
          current_note = 0;
          note_samples_played = 0;
          phase = 0.0;
        }

        /* Check if we need to advance to next note */
        if (note_samples_played >= demo_melody[current_note].duration) {
          current_note++;
          note_samples_played = 0;
          phase = 0.0;
          if (demo_melody[current_note].frequency == 0) {
            current_note = 0;
          }
        }

        /* Generate current note */
        double frequency = demo_melody[current_note].frequency;
        double sample = 0.0;

        if (frequency > 0) {
          sample = sin(phase) * amplitude;
          phase += (2.0 * M_PI * frequency) / sample_rate;
          if (phase >= 2.0 * M_PI) {
            phase -= 2.0 * M_PI;
          }
        }

        samples[i] = (Sint16)sample;
        note_samples_played++;
      }
    }
  }
}

int music_init(void) {
  SDL_AudioSpec want, have;

  if (music_initok) return 0;

  /* Configure audio specifications */
  memset(&want, 0, sizeof(want));
  want.freq = 22050;
  want.format = AUDIO_S16SYS;
  want.channels = 1;
  want.samples = 512;
  want.callback = music_audio_callback;
  want.userdata = NULL;

  if (SDL_OpenAudio(&want, &have) < 0) {
    printf("SDL audio init failed: %s\n", SDL_GetError());
    return 1;
  }

  /* Print audio configuration for debugging */
  printf("[+] Audio opened: %dHz, format=0x%x, %d channel(s), %d samples\n",
         have.freq, have.format, have.channels, have.samples);

  music_initok = 1;
  printf("[+] Simple audio initialized (C64-style sounds)\n");
  return 0;
}


int music_play(const char *filename) {
  if (!music_initok) {
    printf("[!] Audio not initialized - cannot play music\n");
    return 1;
  }

  /* Try to load XM module first */
  if (load_xm_file(filename)) {
    printf("[+] Playing native XM module: %s\n", filename);
  } else {
    printf("[+] XM load failed, using fallback melody for: %s\n", filename);
    /* Reset music sequencer for melody fallback */
    current_note = 0;
    note_samples_played = 0;
  }

  /* Start audio playback */
  music_playing = 1;
  SDL_PauseAudio(0); /* Unpause audio - start playback */

  printf("[+] Audio playback started (volume: %d/128)\n", current_volume);

  /* Log to file for debugging */
  {
    FILE *mlog = fopen("/tmp/cgterm-music.log", "a");
    if (!mlog) mlog = fopen("cgterm-music.log", "a");
    if (mlog) {
#ifdef HAVE_OPENMPT
      if (xm_loaded) {
        fprintf(mlog, "Started native XM playback: %s (vol=%d)\n", filename, current_volume);
      } else {
        fprintf(mlog, "Started melody fallback: %s (vol=%d)\n", filename, current_volume);
      }
#else
      fprintf(mlog, "Started melody fallback: %s (vol=%d)\n", filename, current_volume);
#endif
      fclose(mlog);
    }
  }

  return 0;
}


void music_stop(void) {
  if (!music_initok) return;

  music_playing = 0;
  SDL_PauseAudio(1); /* Pause audio */
  printf("[+] Music stopped\n");
}

void music_set_volume(int vol) {
  if (!music_initok) return;

  current_volume = (vol > 128) ? 128 : ((vol < 0) ? 0 : vol);
  printf("[+] Volume set to %d\n", current_volume);
}

int music_is_playing(void) {
  if (!music_initok) return 0;
  return music_playing;
}


/* Sound effects - simplified implementation */
int music_load_sfx(const char *filename) {
  if (!music_initok) return -1;

  printf("[+] SFX load requested: %s (simplified)\n", filename);
  return 0; /* Return dummy ID */
}

int music_load_sfx_raw(void *buf, unsigned int len) {
  if (!music_initok) return -1;

  printf("[+] Raw SFX load requested (%u bytes)\n", len);
  return 0; /* Return dummy ID */
}

void music_free_sfx(int id) {
  if (!music_initok) return;
  printf("[+] SFX free requested: %d\n", id);
}

void music_play_sfx(int id) {
  if (!music_initok) return;
  printf("[+] SFX play requested: %d\n", id);

  /* Could implement simple beep here */
}

int music_sfx_playing(void) {
  if (!music_initok) return 0;
  return 0; /* No SFX currently playing */
}

void music_shutdown(void) {
  music_stop();

  /* Clean up XM module */
#ifdef HAVE_OPENMPT
  if (xm_module) {
    openmpt_module_destroy(xm_module);
    xm_module = NULL;
    xm_loaded = 0;
  }
#endif

  if (music_initok) {
    SDL_CloseAudio();
    music_initok = 0;
    printf("[+] Audio system shutdown\n");
  }
}

#else  /* !USE_SIMPLE_AUDIO — stubs */

int music_init(void) {
  printf("[*] Music system disabled (no audio support compiled)\n");
  return 1;
}

int music_play(const char *filename) {
  printf("[*] Music play requested but disabled: %s\n", filename);
  (void)filename;
  return 1;
}

void music_stop(void) {
  printf("[*] Music stop requested but disabled\n");
}

void music_set_volume(int vol) {
  printf("[*] Volume change requested but disabled: %d\n", vol);
  (void)vol;
}

int music_is_playing(void) { return 0; }
void music_shutdown(void) {}
int music_load_sfx(const char *filename) { (void)filename; return -1; }
int music_load_sfx_raw(void *buf, unsigned int len) { (void)buf; (void)len; return -1; }
void music_free_sfx(int id) { (void)id; }
void music_play_sfx(int id) { (void)id; }
int music_sfx_playing(void) { return 0; }

#endif /* USE_SIMPLE_AUDIO */

#endif /* WINDOWS / !WINDOWS */
