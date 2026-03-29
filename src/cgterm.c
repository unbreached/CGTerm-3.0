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


int sendcrlf = 0;
unsigned int lastsend = 0;
unsigned int lastrecv = 0;
unsigned int lastvbl = 0;
FILE *logh;
static int log_newline = 1;


static void log_write_byte(int byte, FILE *f) {
  if (f == NULL) return;
  if (log_newline) {
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    fprintf(f, "[%02d:%02d:%02d] ",
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec);
    log_newline = 0;
  }
  if (fputc(byte, f) == EOF) {
    return;
  }
  if (byte == 0x0d) {
    fputc('\n', f);
    log_newline = 1;
  }
}


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
  "bookmark = ANTiDOTE, antidote.triad.se, 64128",
  "bookmark = Boar's Head Tavern, byob.hopto.org, 64128",
  "bookmark = Dark Endless,darkendlessbbs.hopto.org, 6510",
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


void log_close(void) {
  if (logh) {
    fclose(logh);
  }
}


void usage(void) {
    puts("cgterm [-4|-8] [-d delay] [-f] [-k keyboard.kbd] [-o logfile] [-r seconds]");
    puts("       [-s] [-z zoom] [-b(debug)] [-l(ocal echo) on|off (default=off)]");
    puts("       [host [port]]");
}


int main(int argc, char *argv[]) {
  int c = 0;
  unsigned char k;
  int opt;
  char fname[1024];
    
  cfg_init(argv[0]);

#ifdef WINDOWS
  if (cfg_readconfig("cgterm.cfg") < 0) {
    return(1);
  }
  if (cfg_read == 0) {
    cfg_writeconfig(default_cgterm_cfg, "cgterm.cfg");
    if (cfg_readconfig("cgterm.cfg") < 0) {
      return(1);
    }
  }
#else
  snprintf(fname, sizeof(fname), "%s/.cgtermrc", cfg_homedir);
  if (cfg_file_exists(fname)) {
    if (cfg_readconfig(fname) < 0) {
      return(1);
    }
  } else {
    snprintf(fname, sizeof(fname), "%s/cgterm.cfg", path_system_config_dir());
    if (cfg_readconfig(fname) < 0) {
      return(1);
    }
  }
  if (cfg_read == 0) {
    snprintf(fname, sizeof(fname), "%s/.cgtermrc", cfg_homedir);
    cfg_writeconfig(default_cgterm_cfg, fname);
    cfg_readconfig(fname);
  }
#endif

  cfg_load_bookmarks();

  while ((opt = getopt(argc, argv, "r:d:z:k:o:fs48lb")) != -1) {
      
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
      if ((cfg_zoom = strtol(optarg, (char **)NULL, 10)) == 0) {
	usage();
	return(11);
      }
      break;

    case 'b':
            cfg_debugmode = 1;
            break;
            
    case 'l':
            cfg_localecho = 1;
            break;
            
    case '?': usage(); break;
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
  if (cfg_sound && sound_init()) {
    printf("Sound init failed, sound disabled\n");
  } else {
    path_build_asset(fname, sizeof(fname), "bell.wav");
    if ((sound_bell = sound_load_sample(fname)) < 0) {
      printf("Couldn't load %s\n", fname);
      return(19);
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
    gfx_setcursxy(-1, -1);  /* hide blinking cursor */

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
    menu_hide();
    menu_cls();
    gfx_setcursxy(0, 0);
    ffd2(147);  /* clear screen for normal use */
  }

  if (cfg_columns == 40) {
    print("\x12\x1f            \x9a\xac\x9f\xa2\xa2\x99\xa2\xa2\x9e\xa2\xa2\x05\xa2\xa2\x9e\xa2\xa2\x99\xa2\xa2\x9f\xa2\xa2\x9a\xbb\x1f            ");
    print("\x12\x1f            \x92                \x12            ");
    print("\x12\x1f            \x92\x9e  cg\x96tERM \x05" "3.0    \x12\x1f            ");
    print("\x12\x1f            \x92                \x12            ");
    print("\x12\x1f            \x9a\xbc\x92\x9f\xa2\xa2\x99\xa2\xa2\x9e\xa2\xa2\x05\xa2\xa2\x9e\xa2\xa2\x99\xa2\xa2\x9f\xa2\xa2\x12\x9a\xbe\x1f            ");
    print("\x92\x05\x0d");
  } else {
    print("                    \x12\x1f            \x9a\xac\x9f\xa2\xa2\x99\xa2\xa2\x9e\xa2\xa2\x05\xa2\xa2\x9e\xa2\xa2\x99\xa2\xa2\x9f\xa2\xa2\x9a\xbb\x1f            \x0d");
    print("                    \x12\x1f            \x92                \x12            \x0d");
    print("                    \x12\x1f            \x92\x9e  cg\x96tERM \x05" "3.0    \x12\x1f            \x0d");
    print("                    \x12\x1f            \x92                \x12            \x0d");
    print("                    \x12\x1f            \x9a\xbc\x92\x9f\xa2\xa2\x99\xa2\xa2\x9e\xa2\xa2\x05\xa2\xa2\x9e\xa2\xa2\x99\xa2\xa2\x9f\xa2\xa2\x12\x9a\xbe\x1f            \x0d");
    print("\x92\x05\x0d");
  }
  gfx_vbl();

  if (argc == 0) {

    if (!cfg_host) {
      if (cfg_columns == 80) {
	print("                    ");
      }
      print("           \x96pRESS\x9e eSC\x96 FOR MENU\x05\x0d\x0d");
    }

  } else if (argc == 1 || argc == 2) {

    if (strchr(argv[0], '.') == NULL) {
      printf("Invalid hostname: %s\n", argv[0]);
      return(1);
    }

    cfg_host = argv[0];
    if (argc == 2) {
      cfg_port = (int)strtol(argv[1], (char **)NULL, 10);
    }

  } else {

    usage();
    return(1);

  }

  if (cfg_host) {
    if (net_connect(cfg_host, cfg_port, &print_net_status)) {
      print("\x96" "cONNECT FAILED.\x05\x0d");
      gfx_vbl();
    }
    cfg_nextreconnect = timer_get_ticks() + cfg_reconnect * 1000;
  }


  if (cfg_logfile) {
    if ((logh = fopen(cfg_logfile, "w")) == NULL) {
      printf("Couldn't open %s for writing\n", cfg_logfile);
      return(1);
    }
  }
  atexit(log_close);


  for (;;) {

    if (timer_get_ticks() > lastvbl + 20) {
      if (timer_get_ticks() > lastvbl + 40) {
	lastvbl = timer_get_ticks();
      } else {
	lastvbl += 20;
      }
      gfx_vbl();
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
          k = kbd_getkey();
      } else {
	k = 0;
      }
    } else {
        k = kbd_getkey();
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

    if (c == -2) {
      print("\x0d\x96" "dISCONNECTED!\x05\x0d");
      if (timer_get_ticks() < cfg_nextreconnect) {
	cfg_nextreconnect = timer_get_ticks() + cfg_reconnect * 1000;
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
	  if (logh) {
	    log_write_byte(k, logh);
	  }
	}
      }

      if (c >= 0) {
	ffd2(c);
	if (logh) {
	  log_write_byte(c, logh);
	}
      }

    } else {

      timer_delay(1);

    }

  }

}
