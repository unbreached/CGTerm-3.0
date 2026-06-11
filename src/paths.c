#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include "paths.h"

/* snprintf truncation is intentional and safe here — paths are best-effort */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

#ifdef WINDOWS
#include <windows.h>
#define DIRCHAR '\\'
#else
#include <unistd.h>
#define DIRCHAR '/'
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#endif

#ifndef PATH_MAX
#define PATH_MAX 1024
#endif
#define PATH_BUF (PATH_MAX + 256)

static char g_asset_root[PATH_BUF] = ".";
static char g_system_config_dir[PATH_BUF] = "/etc";

static void dirname_inplace(char *path) {
  char *s1 = strrchr(path, '/');
  char *s2 = strrchr(path, '\\');
  char *s = s1 > s2 ? s1 : s2;
  if (s) {
    *s = 0;
  } else {
    strcpy(path, ".");
  }
}

int path_is_absolute(const char *path) {
  if (!path || !*path) return 0;
#ifdef WINDOWS
  if ((path[0] && path[1] == ':') || path[0] == '\\' || path[0] == '/') return 1;
#else
  if (path[0] == '/' || path[0] == '~') return 1;
#endif
  return 0;
}

static int file_exists(const char *path) {
  FILE *fh = fopen(path, "rb");
  if (fh) {
    fclose(fh);
    return 1;
  }
  return 0;
}

static int dir_has_asset(const char *dir, const char *testfile) {
  char tmp[PATH_BUF];
#ifdef WINDOWS
  snprintf(tmp, sizeof(tmp), "%s\\%s", dir, testfile);
#else
  snprintf(tmp, sizeof(tmp), "%s/%s", dir, testfile);
#endif
  return file_exists(tmp);
}

static int get_exe_dir(char *out, size_t outsz, const char *argv0) {
#ifdef WINDOWS
  DWORD len = GetModuleFileNameA(NULL, out, (DWORD)outsz);
  if (len == 0 || len >= outsz) return 0;
  out[len] = 0;
  dirname_inplace(out);
  return 1;
#elif defined(__APPLE__)
  uint32_t size = (uint32_t)outsz;
  if (_NSGetExecutablePath(out, &size) == 0) {
    dirname_inplace(out);
    return 1;
  }
  if (argv0 && *argv0) {
    snprintf(out, outsz, "%s", argv0);
    dirname_inplace(out);
    return 1;
  }
  return 0;
#else
  ssize_t len = readlink("/proc/self/exe", out, outsz - 1);
  if (len > 0) {
    out[len] = 0;
    dirname_inplace(out);
    return 1;
  }
  if (argv0 && *argv0) {
    snprintf(out, outsz, "%s", argv0);
    dirname_inplace(out);
    return 1;
  }
  return 0;
#endif
}

const char *path_asset_root(void) {
  return g_asset_root;
}

const char *path_system_config_dir(void) {
  return g_system_config_dir;
}

void path_build_asset(char *out, size_t outsz, const char *name) {
#ifdef WINDOWS
  snprintf(out, outsz, "%s\\%s", g_asset_root, name);
#else
  snprintf(out, outsz, "%s/%s", g_asset_root, name);
#endif
}

int path_init(const char *argv0) {
  char exe_dir[PATH_BUF] = ".";
  char candidate[PATH_BUF];
  const char *env = getenv("CGTERM_ASSET_DIR");

  if (env && *env) {
    snprintf(g_asset_root, sizeof(g_asset_root), "%s", env);
#ifdef WINDOWS
    snprintf(g_system_config_dir, sizeof(g_system_config_dir), ".");
#else
    snprintf(g_system_config_dir, sizeof(g_system_config_dir), "/etc");
#endif
    return 1;
  }

  if (!get_exe_dir(exe_dir, sizeof(exe_dir), argv0)) {
    snprintf(exe_dir, sizeof(exe_dir), ".");
  }

#ifdef WINDOWS
  /* Check exe_dir\assets first, then exe_dir\..\assets, then exe_dir itself */
  snprintf(candidate, sizeof(candidate), "%s\\assets", exe_dir);
  if (dir_has_asset(candidate, "default.kbd")) {
    snprintf(g_asset_root, sizeof(g_asset_root), "%s", candidate);
    snprintf(g_system_config_dir, sizeof(g_system_config_dir), "%s", exe_dir);
    return 1;
  }
  {
    char parent[PATH_BUF];
    snprintf(parent, sizeof(parent), "%s", exe_dir);
    dirname_inplace(parent);
    snprintf(candidate, sizeof(candidate), "%s\\assets", parent);
    if (dir_has_asset(candidate, "default.kbd")) {
      snprintf(g_asset_root, sizeof(g_asset_root), "%s", candidate);
      snprintf(g_system_config_dir, sizeof(g_system_config_dir), "%s", parent);
      return 1;
    }
  }
  snprintf(g_asset_root, sizeof(g_asset_root), "%s", exe_dir);
  snprintf(g_system_config_dir, sizeof(g_system_config_dir), "%s", exe_dir);
  return 1;
#elif defined(__APPLE__)
  if (strstr(exe_dir, ".app/Contents/MacOS")) {
    char contents_dir[PATH_BUF];
    snprintf(contents_dir, sizeof(contents_dir), "%s", exe_dir);
    dirname_inplace(contents_dir);
    snprintf(g_asset_root, sizeof(g_asset_root), "%s/Resources", contents_dir);
    snprintf(g_system_config_dir, sizeof(g_system_config_dir), "%s/Resources", contents_dir);
    return 1;
  }
  snprintf(candidate, sizeof(candidate), "%s/assets", exe_dir);
  if (dir_has_asset(candidate, "default.kbd")) {
    snprintf(g_asset_root, sizeof(g_asset_root), "%s", candidate);
    snprintf(g_system_config_dir, sizeof(g_system_config_dir), "%s", exe_dir);
    return 1;
  }
  /* check parent directory (e.g. bin/ -> project root) */
  {
    char parent[PATH_BUF];
    snprintf(parent, sizeof(parent), "%s", exe_dir);
    dirname_inplace(parent);
    snprintf(candidate, sizeof(candidate), "%s/assets", parent);
    if (dir_has_asset(candidate, "default.kbd")) {
      snprintf(g_asset_root, sizeof(g_asset_root), "%s", candidate);
      snprintf(g_system_config_dir, sizeof(g_system_config_dir), "%s", parent);
      return 1;
    }
  }
  snprintf(g_asset_root, sizeof(g_asset_root), "%s", exe_dir);
  snprintf(g_system_config_dir, sizeof(g_system_config_dir), "%s", exe_dir);
  return 1;
#else
  snprintf(candidate, sizeof(candidate), "%s/assets", exe_dir);
  if (dir_has_asset(candidate, "default.kbd")) {
    snprintf(g_asset_root, sizeof(g_asset_root), "%s", candidate);
    snprintf(g_system_config_dir, sizeof(g_system_config_dir), "%s", exe_dir);
    return 1;
  }
  /* check parent directory (e.g. bin/ -> project root) */
  {
    char parent[PATH_BUF];
    snprintf(parent, sizeof(parent), "%s", exe_dir);
    dirname_inplace(parent);
    snprintf(candidate, sizeof(candidate), "%s/assets", parent);
    if (dir_has_asset(candidate, "default.kbd")) {
      snprintf(g_asset_root, sizeof(g_asset_root), "%s", candidate);
      snprintf(g_system_config_dir, sizeof(g_system_config_dir), "%s", parent);
      return 1;
    }
  }
#ifdef PREFIX
  if (dir_has_asset(PREFIX "/share/cgterm/assets", "default.kbd")) {
    snprintf(g_asset_root, sizeof(g_asset_root), PREFIX "/share/cgterm/assets");
    snprintf(g_system_config_dir, sizeof(g_system_config_dir), "/etc");
    return 1;
  }
#endif
  if (dir_has_asset("/usr/local/share/cgterm/assets", "default.kbd")) {
    snprintf(g_asset_root, sizeof(g_asset_root), "/usr/local/share/cgterm/assets");
    snprintf(g_system_config_dir, sizeof(g_system_config_dir), "/etc");
    return 1;
  }
  if (dir_has_asset("/usr/share/cgterm/assets", "default.kbd")) {
    snprintf(g_asset_root, sizeof(g_asset_root), "/usr/share/cgterm/assets");
    snprintf(g_system_config_dir, sizeof(g_system_config_dir), "/etc");
    return 1;
  }
  snprintf(g_asset_root, sizeof(g_asset_root), "%s", exe_dir);
  snprintf(g_system_config_dir, sizeof(g_system_config_dir), "/etc");
  return 1;
#endif
}
