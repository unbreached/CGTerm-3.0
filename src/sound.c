#include <stdlib.h>
#include <string.h>
#include <SDL.h>
#include "sound.h"


typedef struct snd {
  int playing;
  int current_sample;
  int current_position;
} SoundControl;

#define MAXSAMPLES 10

int sound_bell;

int sound_initok = 0;
SDL_AudioSpec sound_audiospec;
int sound_numsamples;
Uint32 sound_length[MAXSAMPLES];
Uint8 *sound_buffer[MAXSAMPLES];
SoundControl sound_control;


void fillbuffer(void *userdata, Uint8 *stream, int len) {
  if (len) {
    if (sound_control.playing) {
      if (sound_control.current_position + len >= sound_length[sound_control.current_sample]) {
	memcpy(stream, sound_buffer[sound_control.current_sample] + sound_control.current_position,
	       sound_length[sound_control.current_sample] - sound_control.current_position);
	memset(stream + sound_length[sound_control.current_sample] - sound_control.current_position,
	       sound_audiospec.silence,
	       len - (sound_length[sound_control.current_sample] - sound_control.current_position));
	sound_control.playing = 0;
      } else {
	memcpy(stream, sound_buffer[sound_control.current_sample] + sound_control.current_position, len);
	sound_control.current_position += len;
      }
    } else {
      memset(stream, sound_audiospec.silence, len);
    }
  }
}


/* The music engine (music.c, Unix build) opens the only SDL 1.2 audio
 * device; it calls sound_mix_into() from its callback so bell/modem effects
 * play through it. Mark the sample table usable without opening a device. */
void sound_attach_to_music_device(void) {
  sound_numsamples = 0;
  memset(sound_buffer, 0, sizeof(sound_buffer));
  sound_control.playing = SDL_FALSE;
  sound_audiospec.freq = 22050;
  sound_audiospec.format = AUDIO_S16SYS;
  sound_audiospec.channels = 1;
  sound_audiospec.silence = 0;
  sound_initok = 1;
}


/* Additively mix the playing sample (16-bit mono 22050) into 'frames'
 * samples of 'stream'. Called from the audio callback thread; state changes
 * from the main thread happen under SDL_LockAudio(). */
void sound_mix_into(Sint16 *stream, int frames) {
  const Sint16 *src;
  Uint32 avail;
  int i, n;

  if (!sound_control.playing || sound_control.current_sample < 0 ||
      sound_control.current_sample >= MAXSAMPLES ||
      sound_buffer[sound_control.current_sample] == NULL) {
    sound_control.playing = 0;
    return;
  }
  avail = (sound_length[sound_control.current_sample] - sound_control.current_position) / 2;
  n = (int)avail < frames ? (int)avail : frames;
  src = (const Sint16 *)(sound_buffer[sound_control.current_sample] + sound_control.current_position);
  for (i = 0; i < n; i++) {
    int v = stream[i] + src[i];
    if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
    stream[i] = (Sint16)v;
  }
  sound_control.current_position += n * 2;
  if (sound_control.current_position >= sound_length[sound_control.current_sample]) {
    sound_control.playing = 0;
  }
}


int sound_init(void) {
  sound_initok = 0;

  sound_numsamples = 0;
  memset(sound_buffer, 0, sizeof(sound_buffer));
  sound_control.playing = SDL_FALSE;

  if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    printf("Audio init failed: %s\n", SDL_GetError());
    return(1);
  }
  sound_audiospec.freq = 22050;
  sound_audiospec.format = AUDIO_S16SYS;
  sound_audiospec.channels = 1;
  sound_audiospec.samples = 1024;
  sound_audiospec.callback = fillbuffer;
  sound_audiospec.userdata = NULL;
  if (SDL_OpenAudio(&sound_audiospec, NULL) < 0) {
    printf("Audio init failed: %s\n", SDL_GetError());
    return(1);
  }
  SDL_PauseAudio(0);
  sound_initok = 1;
  return(0);
}


signed int sound_load_sample(const char *filename) {
  int sample;

  if (sound_initok) {
    if (sound_numsamples >= MAXSAMPLES) {
      return(-1);
    }
    /* bound the scan and re-check, like sound_register_buffer — without this
     * (and the missing ++sound_numsamples) a full table would index past the
     * end of sound_buffer[] */
    for (sample = 0; sample < MAXSAMPLES && sound_buffer[sample]; ++sample);
    if (sample >= MAXSAMPLES) {
      return(-1);
    }
    {
      /* Load into a scratch spec and convert to the device format: passing
       * the live spec let a stereo/44.1 kHz/8-bit bell.wav overwrite it and
       * then play as noise at the wrong pitch. */
      SDL_AudioSpec wavspec;
      SDL_AudioCVT cvt;
      Uint8 *wavbuf;
      Uint32 wavlen;
      Uint8 *out;

      if (SDL_LoadWAV(filename, &wavspec, &wavbuf, &wavlen) == NULL) {
        return(-1);
      }
      if (SDL_BuildAudioCVT(&cvt, wavspec.format, wavspec.channels, wavspec.freq,
                            AUDIO_S16SYS, 1, 22050) < 0) {
        SDL_FreeWAV(wavbuf);
        return(-1);
      }
      if ((out = malloc((size_t)wavlen * (cvt.len_mult > 0 ? cvt.len_mult : 1) + 16)) == NULL) {
        SDL_FreeWAV(wavbuf);
        return(-1);
      }
      memcpy(out, wavbuf, wavlen);
      SDL_FreeWAV(wavbuf);
      cvt.buf = out;
      cvt.len = (int)wavlen;
      if (cvt.needed && SDL_ConvertAudio(&cvt) < 0) {
        free(out);
        return(-1);
      }
      sound_buffer[sample] = out;
      sound_length[sample] = cvt.needed ? (Uint32)cvt.len_cvt : wavlen;
    }
    ++sound_numsamples;
    return(sample);
  } else {
    return(0);
  }
}


void sound_free_sample(int sample) {
  /* every table entry is a malloc'd buffer (converted WAV or registered) */
  sound_free_buffer(sample);
}


/* Optional hook for SDL_mixer-based SFX playback */
static void (*sfx_play_hook)(int) = NULL;

void sound_set_sfx_hook(void (*hook)(int)) {
  sfx_play_hook = hook;
}

void sound_start_sample(int sample) {
  if (sample < 0 || sample >= MAXSAMPLES) {
    return;   /* guard sound_buffer[]/sound_length[] indexing */
  }
  if (sound_initok) {
    if (sound_buffer[sample] && !sound_control.playing) {
      SDL_LockAudio();
      sound_control.current_sample = sample;
      sound_control.current_position = 0;
      sound_control.playing = SDL_TRUE;
      SDL_UnlockAudio();
    }
  }
}

extern int cfg_sound;

void sound_play_sample(int sample) {
  if (!cfg_sound) {
    return;   /* muted from the options panel */
  }
  if (sfx_play_hook) {
    sfx_play_hook(sample);
    return;
  }
  sound_start_sample(sample);
}


signed int sound_register_buffer(Uint8 *buf, Uint32 len) {
  int sample;
  if (!sound_initok || sound_numsamples >= MAXSAMPLES) return -1;
  for (sample = 0; sample < MAXSAMPLES && sound_buffer[sample]; ++sample);
  if (sample >= MAXSAMPLES) return -1;
  sound_buffer[sample] = buf;
  sound_length[sample] = len;
  ++sound_numsamples;
  return sample;
}


void sound_unregister_buffer(int sample) {
  if (sample >= 0 && sample < MAXSAMPLES && sound_buffer[sample]) {
    SDL_LockAudio();
    if (sound_control.current_sample == sample) {
      sound_control.playing = 0;
    }
    sound_buffer[sample] = NULL;   /* caller still owns the memory */
    SDL_UnlockAudio();
    --sound_numsamples;
  }
}


void sound_free_buffer(int sample) {
  if (sample >= 0 && sample < MAXSAMPLES && sound_buffer[sample]) {
    Uint8 *buf = sound_buffer[sample];
    sound_unregister_buffer(sample);
    free(buf);
  }
}


int sound_is_playing(void) {
  return sound_control.playing;
}
