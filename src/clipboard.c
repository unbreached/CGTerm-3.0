#include <stdio.h>
#include <string.h>
#include "net.h"
#include "clipboard.h"

#ifdef WINDOWS
#include <windows.h>
#endif

void clipboard_paste(void) {
#ifdef WINDOWS
  if (!OpenClipboard(NULL)) return;
  {
    HANDLE h = GetClipboardData(CF_TEXT);
    if (h) {
      char *text = (char *)GlobalLock(h);
      if (text) {
        while (*text) {
          char c = *text++;
          if (c == '\n') c = '\r';
          if (c == '\r' && *text == '\n') text++;
          else if (c >= 'A' && c <= 'Z') c += 32;
          else if (c >= 'a' && c <= 'z') c -= 32;
          net_send((unsigned char)c);
        }
        GlobalUnlock(h);
      }
    }
  }
  CloseClipboard();
#else
  FILE *fp = NULL;
  char buf[4096];
  int len;

#ifdef __APPLE__
  fp = popen("pbpaste", "r");
#else
  fp = popen("xclip -selection clipboard -o 2>/dev/null || xsel --clipboard -o 2>/dev/null", "r");
#endif

  if (!fp) return;

  while ((len = (int)fread(buf, 1, sizeof(buf), fp)) > 0) {
    int i;
    for (i = 0; i < len; i++) {
      char c = buf[i];
      if (c == '\n') c = '\r';
      else if (c >= 'A' && c <= 'Z') c += 32;
      else if (c >= 'a' && c <= 'z') c -= 32;
      net_send((unsigned char)c);
    }
  }
  pclose(fp);
#endif
}
