/*
 * Minimal getopt() for Windows/MinGW when <unistd.h> is not available.
 * Public domain.
 */
#ifndef GETOPT_WIN_H
#define GETOPT_WIN_H

#ifdef WINDOWS

#include <string.h>
#include <stdio.h>

static char *optarg = NULL;
static int optind = 1;
static int optopt = 0;

static int getopt(int argc, char *const argv[], const char *optstring) {
  static int sp = 1;
  int c;
  const char *cp;

  if (optind >= argc || argv[optind][0] != '-' || argv[optind][1] == '\0') {
    return -1;
  }
  if (strcmp(argv[optind], "--") == 0) {
    optind++;
    return -1;
  }

  c = argv[optind][sp];
  optopt = c;

  cp = strchr(optstring, c);
  if (cp == NULL || c == ':') {
    fprintf(stderr, "Unknown option: -%c\n", c);
    if (argv[optind][++sp] == '\0') {
      optind++;
      sp = 1;
    }
    return '?';
  }

  if (cp[1] == ':') {
    if (argv[optind][sp + 1] != '\0') {
      optarg = &argv[optind][sp + 1];
    } else if (optind + 1 < argc) {
      optarg = argv[++optind];
    } else {
      fprintf(stderr, "Option -%c requires an argument\n", c);
      optind++;
      sp = 1;
      return '?';
    }
    optind++;
    sp = 1;
  } else {
    if (argv[optind][++sp] == '\0') {
      optind++;
      sp = 1;
    }
    optarg = NULL;
  }

  return c;
}

#endif /* WINDOWS */
#endif /* GETOPT_WIN_H */
