/*
 * Preload libmikmod before SDL_mixer needs it.
 * SDL_mixer uses dlopen("libmikmod.dylib") which doesn't search
 * common paths on macOS with SIP. We preload it ourselves using
 * the full path so it's already in memory when SDL_mixer asks for it.
 */

#ifdef HAVE_SDL_MIXER
#ifndef WINDOWS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

static void *mikmod_handle = NULL;

void music_preload_mikmod(void) {
  if (mikmod_handle) return;

  /* Set DYLD_FALLBACK_LIBRARY_PATH so dlopen("libmikmod.dylib") finds it.
   * This must be set before SDL_mixer's first dlopen call. */
#ifdef __APPLE__
  {
    const char *current = getenv("DYLD_FALLBACK_LIBRARY_PATH");
    char buf[2048];
    if (current && strlen(current) > 0) {
      snprintf(buf, sizeof(buf), "/opt/homebrew/lib:/usr/local/lib:%s", current);
    } else {
      snprintf(buf, sizeof(buf), "/opt/homebrew/lib:/usr/local/lib:/usr/lib");
    }
    setenv("DYLD_FALLBACK_LIBRARY_PATH", buf, 1);
  }
#endif

  /* Try common locations */
  /* Full paths to try — SDL_mixer will dlopen("libmikmod.dylib") later,
   * but by preloading with RTLD_GLOBAL the symbols are already available.
   * We also dlopen the exact name SDL_mixer will use, with the full path,
   * so it resolves correctly. */
  const char *paths[] = {
    "/opt/homebrew/lib/libmikmod.dylib",
    "/opt/homebrew/lib/libmikmod.3.dylib",
    "/usr/local/lib/libmikmod.dylib",
    "/usr/local/lib/libmikmod.3.dylib",
    "/usr/lib/libmikmod.so",
    "/usr/lib/x86_64-linux-gnu/libmikmod.so.3",
    "/usr/lib/aarch64-linux-gnu/libmikmod.so.3",
    NULL
  };
  const char *names[] = {
    "libmikmod.dylib",
    "libmikmod.so",
    "libmikmod.so.3",
    NULL
  };
  int i;

  /* First, load with full path and RTLD_GLOBAL */
  for (i = 0; paths[i]; i++) {
    mikmod_handle = dlopen(paths[i], RTLD_NOW | RTLD_GLOBAL);
    if (mikmod_handle) {
      printf("[+] Preloaded mikmod from %s\n", paths[i]);
      break;
    }
  }

  /* Now also try to make the bare name resolve.
   * Some dlopen implementations cache by name, so if we also dlopen
   * "libmikmod.dylib" with the full path, subsequent dlopen("libmikmod.dylib")
   * by SDL_mixer may find it. This is a workaround for macOS SIP. */
  if (mikmod_handle) {
    for (i = 0; names[i]; i++) {
      /* This may or may not help depending on the OS */
      dlopen(paths[0], RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
    }
  } else {
    /* Try just the name — might work on Linux */
    for (i = 0; names[i]; i++) {
      mikmod_handle = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
      if (mikmod_handle) break;
    }
  }
}

#else /* WINDOWS */
void music_preload_mikmod(void) { /* not needed on Windows */ }
#endif

#else /* !HAVE_SDL_MIXER */
void music_preload_mikmod(void) {}
#endif
