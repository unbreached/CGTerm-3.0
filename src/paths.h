#ifndef PATHS_H
#define PATHS_H

#include <stddef.h>

int path_init(const char *argv0);
const char *path_asset_root(void);
const char *path_system_config_dir(void);
void path_build_asset(char *out, size_t outsz, const char *name);
int path_is_absolute(const char *path);

#endif
