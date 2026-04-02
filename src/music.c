/*
 * music.c — XM/MOD/IT tracker music playback via SDL_mixer.
 * Compiles to stubs when SDL_mixer is not available.
 */

#include <stdio.h>

#ifdef HAVE_SDL_MIXER

#include <SDL.h>
#include <SDL_mixer.h>

static Mix_Music *current_music = NULL;
static int music_initok = 0;

#define MAX_SFX 8
static Mix_Chunk *sfx_chunks[MAX_SFX];
static int sfx_count = 0;

int music_init(void) {
  if (music_initok) return 0;

  /* Open audio: 22050 Hz, signed 16-bit, mono, 1024-sample buffer.
   * SDL_mixer takes over the audio device from our custom callback,
   * so sound.c's fillbuffer won't be used anymore — we'll use
   * Mix_PlayChannel for sound effects instead. */
  if (Mix_OpenAudio(22050, AUDIO_S16, 1, 1024) < 0) {
    printf("SDL_mixer init failed: %s\n", Mix_GetError());
    return 1;
  }

  /* Allocate channels for sound effects */
  Mix_AllocateChannels(4);
  music_initok = 1;
  printf("[+] SDL_mixer initialized (XM/MOD/IT support)\n");
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
    /* Also log to file for Windows where console isn't visible */
    {
      FILE *mlog = fopen("/tmp/cgterm-music.log", "a");
      if (!mlog) mlog = fopen("cgterm-music.log", "a");
      if (mlog) {
        fprintf(mlog, "Couldn't load music %s: %s\n", filename, Mix_GetError());
        fclose(mlog);
      }
    }
    return 1;
  }

  if (Mix_PlayMusic(current_music, -1) < 0) {  /* -1 = loop forever */
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
  Mix_VolumeMusic(vol);  /* 0-128 */
}


int music_is_playing(void) {
  if (!music_initok) return 0;
  return Mix_PlayingMusic();
}


int music_load_sfx(const char *filename) {
  Mix_Chunk *chunk;
  if (!music_initok || sfx_count >= MAX_SFX) return -1;
  chunk = Mix_LoadWAV(filename);
  if (!chunk) {
    printf("Couldn't load SFX %s: %s\n", filename, Mix_GetError());
    return -1;
  }
  sfx_chunks[sfx_count] = chunk;
  return sfx_count++;
}


int music_load_sfx_raw(void *buf, unsigned int len) {
  Mix_Chunk *chunk;
  if (!music_initok || sfx_count >= MAX_SFX) return -1;

  /* Create a Mix_Chunk from raw audio data.
   * The buffer is Sint16 mono 22050Hz — same as our Mix_OpenAudio format. */
  chunk = (Mix_Chunk *)malloc(sizeof(Mix_Chunk));
  if (!chunk) return -1;
  chunk->allocated = 0;  /* we manage the buffer ourselves */
  chunk->abuf = (Uint8 *)buf;
  chunk->alen = len;
  chunk->volume = MIX_MAX_VOLUME;

  sfx_chunks[sfx_count] = chunk;
  return sfx_count++;
}


void music_free_sfx(int id) {
  if (!music_initok || id < 0 || id >= sfx_count) return;
  if (sfx_chunks[id]) {
    Mix_HaltChannel(-1);  /* stop any playing sounds */
    /* Don't free abuf — caller manages it for raw buffers */
    free(sfx_chunks[id]);
    sfx_chunks[id] = NULL;
  }
}


void music_play_sfx(int id) {
  if (!music_initok || id < 0 || id >= sfx_count) return;
  Mix_PlayChannel(-1, sfx_chunks[id], 0);
}


int music_sfx_playing(void) {
  if (!music_initok) return 0;
  return Mix_Playing(-1);
}


void music_shutdown(void) {
  int i;
  music_stop();
  for (i = 0; i < sfx_count; i++) {
    if (sfx_chunks[i]) Mix_FreeChunk(sfx_chunks[i]);
  }
  sfx_count = 0;
  if (music_initok) {
    Mix_CloseAudio();
    music_initok = 0;
  }
}

#else  /* !HAVE_SDL_MIXER — stubs */

int music_init(void) { return 1; }
int music_play(const char *filename) { (void)filename; return 1; }
void music_stop(void) {}
void music_set_volume(int vol) { (void)vol; }
int music_is_playing(void) { return 0; }
void music_shutdown(void) {}
int music_load_sfx(const char *filename) { (void)filename; return -1; }
int music_load_sfx_raw(void *buf, unsigned int len) { (void)buf; (void)len; return -1; }
void music_free_sfx(int id) { (void)id; }
void music_play_sfx(int id) { (void)id; }
int music_sfx_playing(void) { return 0; }

#endif
