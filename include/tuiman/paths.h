#ifndef TUIMAN_PATHS_H
#define TUIMAN_PATHS_H

#include <limits.h>

typedef struct {
  char config_dir[PATH_MAX];
  char state_dir[PATH_MAX];
  char cache_dir[PATH_MAX];
  char requests_dir[PATH_MAX];
  char history_db[PATH_MAX];
} app_paths_t;

int paths_init(app_paths_t *out);

#endif
