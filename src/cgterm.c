#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef WINDOWS
#include "getopt_win.h"
#else
#include <unistd.h>
#endif
#include "SDL.h"
#include "kernal.h"
#include "gfx.h"
#include "keyboard.h"
#include "net.h"
#include "config.h"
#include "paths.h"
#include "timer.h"
#include "sound.h"
#include "crc.h"
#include "menu.h"
#include "modem.h"
#include "music.h"
#include "xfer.h"
#include "ansi.h"
#include "ui.h"
#include "session.h"
#include "login.h"
#include "clipboard.h"


#ifndef CGTERM_VERSION
#define CGTERM_VERSION "3.1.0"
#endif

int sendcrlf = 0;
static int was_connected = 0;
unsigned int lastsend = 0;
unsigned int lastrecv = 0;
unsigned int lastvbl = 0;
char *default_cgterm_cfg[] = {
  "#keyboard = ",
  "#zoom = 2",
  "#fullscreen = no",
  "columns = 40",
  "#localecho = no",
  "#senddelay = 5",
  "#recvdelay = 0",
  "#reconnect = 0",
  "#logfile = ",
  "#host = ",
  "#port = ",
  "#sound = yes",
  "#debug = no",
  "#xferdir = ",
  "#debug = no",
  "bookmark = FRoZEN FLoPPY BBS, bbs.retrohack.se, 64128",
  "bookmark = OPTiCAL iLLUSiON, oi.ath.cx, 64128",
  "bookmark = ANTiDOTE, antidote.triad.se, 64128",
  "bookmark = Boar's Head Tavern, byob.hopto.org, 64128",
  "bookmark = Dark Endless, darkendlessbbs.hopto.org, 6510",
  "bookmark = Dead Zone, dzbbs.hopto.org, 64128",
  "bookmark = Fria Bad BBS, friabad.hopto.org, 64128",
  "bookmark = The Hidden, the-hidden.hopto.org, 64128",
  "bookmark = Rapid Fire BBS, rapidfire.hopto.org, 64128",
  "bookmark = Raveolution, raveolution.hopto.org, 64128",
  "bookmark = The Valley, valley64.com, 6400",
  NULL
};


void print_net_status(int code, char *message) {
  if (code == 2) {
    ffd2(0x96);
    print_ascii((const unsigned char *)message);
    ffd2(0x05);
    ffd2(0x0d);
  }
}


void usage(void) {
    puts("cgterm [-4|-8] [-d delay] [-f] [-k keyboard.kbd] [-o logfile] [-r seconds]");
    puts("       [-s] [-z zoom] [-b(debug)] [-l (local echo on)] [-V (version)]");
    puts("       [host [port]]");
}


/* kbd_getkey() flags bytes coming from a "Load SEQ file" with +256: they are
 * meant to be displayed locally, never sent to the BBS. Storing the result in
 * an unsigned char dropped the flag and blasted the file to the board. */
static unsigned char main_getkey(void) {
  int kk = kbd_getkey();

  if (kk > 255) {
    ffd2((unsigned char)(kk - 256));
    return 0;
  }
  return (unsigned char)kk;
}

int main(int argc, char *argv[]) {
  int c = 0;
  unsigned char k;
  int opt;
  char fname[1024];
    
  cfg_init(argv[0]);

  /* System-wide config first (if any: /etc/cgterm.cfg, or cgterm.cfg next
   * to the executable), then the per-user file on top. Reading only one of
   * them meant the first saved user setting hid every bookmark and default
   * from the system file for good. */
#ifdef WINDOWS
  snprintf(fname, sizeof(fname), "%s\\cgterm.cfg", path_system_config_dir());
#else
  snprintf(fname, sizeof(fname), "%s/cgterm.cfg", path_system_config_dir());
#endif
  if (cfg_file_exists(fname) && cfg_readconfig(fname) < 0) {
    return(1);
  }
#ifdef WINDOWS
  cfg_user_file(fname, sizeof(fname), "cgterm.cfg");
#else
  snprintf(fname, sizeof(fname), "%s/.cgtermrc", cfg_homedir);
#endif
  if (cfg_file_exists(fname)) {
    if (cfg_readconfig(fname) < 0) {
      return(1);
    }
  }
  if (cfg_read == 0) {
    cfg_writeconfig(default_cgterm_cfg, fname);
    cfg_readconfig(fname);
  }

  cfg_load_bookmarks();

  while ((opt = getopt(argc, argv, "r:d:z:k:o:fs48lbV")) != -1) {
      
    switch (opt) {

    case '4':
      cfg_columns = 40;
      break;

    case '8':
      cfg_columns = 80;
      break;

    case 'd':
      cfg_senddelay = strtol(optarg, (char **)NULL, 10);
      if (cfg_senddelay < 0 || cfg_senddelay > 10000) {
	usage();
	return(13);
      }
      break;

    case 'f':
      cfg_fullscreen = 1;
      break;

    case 'k':
      cfg_keyboard = optarg;
      break;

    case 'o':
      cfg_logfile = optarg;
      break;

    case 'r':
      cfg_reconnect = strtol(optarg, (char **)NULL, 10);
      if (cfg_reconnect < 1 || cfg_reconnect > 10000) {
	usage();
	return(12);
      }
      break;

    case 's':
      cfg_sound = 0;
      break;

    case 'z':
      cfg_zoom = strtol(optarg, (char **)NULL, 10);
      if (cfg_zoom < 1 || cfg_zoom > 8) {   /* same bounds as the config file */
	usage();
	return(11);
      }
      break;

    case 'b':
            cfg_debugmode = 1;
            break;

    case 'V':
            puts("CGTerm " CGTERM_VERSION " - Genesis Project C64 Scene Edition");
            return(0);
            
    case 'l':
            cfg_localecho = 1;
            break;
            
    case '?':
      usage();
      return(100);
    default:
      usage();
      return(100);

    }
  }
  argc -= optind;
  argv += optind;

  if (crc_init()) {
    return(14);
  }
  gfx_status_enable(cfg_statusline);
  if (gfx_init(cfg_fullscreen, "CGTerm")) {
      printf("Killing because of GFX");
      return(15);
  }
  if (kbd_init(cfg_keyboard)) {
    return(16);
  }
  if (kernal_init()) {
    return(17);
  }
  if (cfg_termmode == 1) {
    /* termmode = ansi in the config: set up the CP437 font, 80 columns and
     * the colour tables now; enter_ansi_mode() only ran from the menu. */
    gfx_set_columns(80);
    ansi_init();
  }
  /* Initialize audio: try SDL_mixer first (supports XM music + WAV),
   * fall back to raw SDL audio if SDL_mixer not available */
  if (cfg_sound) {
    music_preload_mikmod();
    if (music_init() == 0) {
      /* SDL_mixer handles audio — load bell as SFX chunk */
      path_build_asset(fname, sizeof(fname), "bell.wav");
      sound_bell = music_load_sfx(fname);
      if (sound_bell < 0) {
        printf("Couldn't load %s\n", fname);
      }
      /* Redirect sound_play_sample to use SDL_mixer */
      sound_set_sfx_hook(&music_play_sfx);
    } else if (sound_init() == 0) {
      path_build_asset(fname, sizeof(fname), "bell.wav");
      if ((sound_bell = sound_load_sample(fname)) < 0) {
        printf("Couldn't load %s\n", fname);
        return(19);
      }
    } else {
      printf("Sound init failed, sound disabled\n");
    }
  }

  /* Splash screen — covers entire CGTerm window */
  if (cfg_splash) {
    int splash_frame = 0;
    int splash_done = 0;
    SDL_Event splash_event;

    /* Black out the PETSCII screen behind the overlay */
    gfx_bgcolor(0);
    ffd2(147);  /* clear screen */
    gfx_cursor_show(0);  /* hide blinking cursor */

    /* Play splash music */
    {
      path_build_asset(fname, sizeof(fname), "cgterm.xm");
      music_play(fname);
    }

    menu_show();
    while (!splash_done) {
      while (SDL_PollEvent(&splash_event)) {
        switch (splash_event.type) {
        case SDL_QUIT:
          exit(0);
          break;
        case SDL_KEYDOWN:
          if (splash_event.key.keysym.sym == SDLK_ESCAPE) {
            splash_done = 1;
          } else if (splash_event.key.keysym.sym == SDLK_x) {
            cfg_disable_splash();
            splash_done = 1;
          }
          break;
        }
      }
      menu_draw_splash_frame(splash_frame, cfg_dldir, cfg_xferdir);
      gfx_vbl();
      timer_delay(20);
      splash_frame++;
    }
    music_stop();
    menu_hide();
    menu_cls();
    gfx_setcursxy(0, 0);
    ffd2(147);  /* clear screen for normal use */
  }

  /* Show startup background:
   * PETSCII: background.seq → background_40.bmp → built-in banner
   * ANSI:    background.ans → background_80.bmp → black */
  gfx_show_startup_bg();

  if (argc == 0) {

    if (!cfg_host) {
      if (cfg_columns == 80) {
	print("                    ");
      }
      /* print("           \x96pRESS\x9e eSC\x96 FOR MENU\x05\x0d\x0d"); */
    }

  } else if (argc == 1 || argc == 2) {

    /* Let the resolver decide what is valid — the old strchr('.') check
     * rejected legitimate single-label names (localhost, /etc/hosts aliases,
     * SSH/stunnel tunnels) and was inconsistent with the connect dialog. */
    cfg_host = argv[0];
    if (argc == 2) {
      long p = strtol(argv[1], (char **)NULL, 10);
      if (p <= 0 || p > 65535) {
        printf("Invalid port: %s\n", argv[1]);
        return(1);
      }
      cfg_port = (int)p;
    }

  } else {

    usage();
    return(1);

  }

  if (cfg_host) {
    if (modem_connect(cfg_host, cfg_port, &print_net_status)) {
      print("\x96" "cONNECT FAILED.\x05\x0d");
      gfx_vbl();
    }
    cfg_nextreconnect = timer_get_ticks() + cfg_reconnect * 1000;
  }


  if (cfg_logfile) {
    if (!session_capture_open(cfg_logfile)) {
      return(1);
    }
  }
  atexit(session_capture_close);


  for (;;) {

    if (timer_get_ticks() > lastvbl + 20) {
      if (timer_get_ticks() > lastvbl + 40) {
	lastvbl = timer_get_ticks();
      } else {
	lastvbl += 20;
      }
      session_status_tick();
      gfx_vbl();
    }
    if (kbd_focus == FOCUS_TERM) {
      login_tick(-1);   /* advance a running auto-login script (timeouts, sends) */
    }

    if (!net_connected() && cfg_host && cfg_reconnect && cfg_nextreconnect) {
      if (timer_get_ticks() > cfg_nextreconnect) {
	if (net_connect(cfg_host, cfg_port, &print_net_status)) {
	  print("\x96" "rECONNECT FAILED.\x05\x0d");
	  gfx_vbl();
	}
	cfg_nextreconnect = timer_get_ticks() + cfg_reconnect * 1000;
      }
    }

    if (cfg_senddelay) {
      if (timer_get_ticks() > lastsend + cfg_senddelay) {
          k = main_getkey();
      } else {
	k = 0;
      }
    } else {
        k = main_getkey();
    }

    /* Drain queued clipboard paste, paced at >= 8ms/byte even when senddelay
     * is 0, so a paste doesn't overrun the BBS. Only while connected and the
     * terminal has focus; bytes flow through the same send path as typing. */
    if (!k && net_connected() && kbd_focus == FOCUS_TERM && clipboard_paste_pending()) {
      unsigned int pace = cfg_senddelay > 8 ? (unsigned int)cfg_senddelay : 8;
      if (timer_get_ticks() > lastsend + pace) {
        int pb = clipboard_paste_byte();
        if (pb > 0) k = (unsigned char)pb;
      }
    }

    c = -1;
    if (net_connected() && kbd_focus == FOCUS_TERM) {
      /* Only consume network bytes in terminal mode.
       * When any menu/dialog is active, let bytes accumulate in the TCP buffer.
       * Transfer protocols (Punter, etc.) need to read these bytes directly.
       * If we consume them here via ffd2(), protocol handshake data is lost. */
      if (cfg_recvdelay) {
	if (timer_get_ticks() > lastrecv + cfg_recvdelay) {
	  lastrecv = timer_get_ticks();
	  c = net_receive();
	}
      } else {
	c = net_receive();
      }
    }

    /* a failed send (net_write) disconnects without a -2 from net_receive():
     * notice the transition so redial and the login script see the drop */
    if (c != -2 && was_connected && !net_connected()) {
      c = -2;
    }
    was_connected = net_connected();

    if (c == -2) {
      print("\x0d\x96" "dISCONNECTED!\x05\x0d");
      login_abort();
      /* -r N / reconnect = N: redial N seconds after ANY drop, not only when
       * the drop happened within N seconds of the last attempt */
      if (cfg_reconnect > 0) {
	cfg_nextreconnect = timer_get_ticks() + cfg_reconnect * 1000;
	print("\x9e" "rEDIALING...\x05\x0d");
      } else {
	cfg_nextreconnect = 0;
      }
    }

    if (k || c >= 0) {

      if (k) {
	net_send(k);
	lastsend = timer_get_ticks();
	if (k == 13 && sendcrlf) {
	  net_send(10);
	}
	if (cfg_localecho) {
	  ffd2(k);
	  session_capture_byte(k);
	}
      }

      if (c >= 0) {
	if (cfg_autozmodem && session_zmodem_sentinel(c)) {
	  /* the board started a ZMODEM send: answer it without a menu trip */
	  print("\x0d\x9e" "zMODEM DOWNLOAD...\x05\x0d");
	  ui_autostart_zmodem();
	  continue;
	}
	if (cfg_termmode == 1) {
	  ansi_out(c);
	} else {
	  ffd2(c);
	}
	session_capture_byte(c);
	login_tick(c);
      }

    } else {

      timer_delay(1);

    }


  }

}
