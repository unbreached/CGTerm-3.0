#include "paths.h"
extern char *cfg_prefix;
extern char *cfg_homedir;

extern int cfg_read;

extern char *cfg_host;
extern unsigned int cfg_port;
extern char *cfg_keyboard;
extern int cfg_zoom;
extern char *cfg_logfile;
extern int cfg_fullscreen;
extern int cfg_localecho;
extern int cfg_senddelay;
extern int cfg_recvdelay;
extern int cfg_reconnect;
extern unsigned int cfg_nextreconnect;
extern int cfg_columns;
extern int cfg_rows;
extern int cfg_numbookmarks;
extern int cfg_sound;
extern char *cfg_bookmark_alias[];
extern char *cfg_bookmark_host[];
extern int cfg_bookmark_port[];
extern char cfg_xferdir[];
extern char cfg_dldir[];
extern char cfg_seqdir[];
extern char cfg_screendir[];
extern int cfg_editmode;
extern int cfg_debugmode;
extern int cfg_splash;
extern int cfg_modem;
extern int cfg_splashfont;
extern int cfg_menufont;
extern char cfg_bookmarkfile[];
extern int cfg_termmode;
extern int cfg_bookmark_termmode[];
extern char cfg_connect_name[];
extern int cfg_charset;  /* 0=standard, 1=swedish, 2=german */


int cfg_init(char *argv0);
int cfg_file_exists(char *filename);
signed int cfg_readconfig(char *configfile);
void cfg_sethost(char *h);
int cfg_change_dir(char *dirbuffer, const char *newdir);
void cfg_writeconfig(char **data, char *configfile);
void cfg_disable_splash(void);
int cfg_save_setting(const char *key, const char *value);
void cfg_debug(const char *s);
int addhost(int num, char *alias, char *hostname, int port);
void cfg_load_bookmarks(void);
void cfg_save_bookmark(char *alias, char *host, int port);
void cfg_resolve_bookmarkfile(char *resolved, size_t size);
void cfg_log_connection(const char *host, int port);
const char *cfg_get_bookmark_note(const char *host, int port);
void cfg_set_bookmark_note(const char *host, int port, const char *note);
