#include "tuiman/paths.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static int ensure_dir(const char *path) {
  char tmp[PATH_MAX];
  size_t len = strlen(path);
  if (len == 0 || len >= sizeof(tmp)) {
    return -1;
  }

  strncpy(tmp, path, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';

  if (tmp[len - 1] == '/') {
    tmp[len - 1] = '\0';
  }

  for (char *p = tmp + 1; *p != '\0'; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
        return -1;
      }
      *p = '/';
    }
  }

  if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
    return -1;
  }

  return 0;
}

int paths_init(app_paths_t *out) {
  const char *home = getenv("HOME");
  if (home == NULL || out == NULL) {
    return -1;
  }

  if (snprintf(out->config_dir, sizeof(out->config_dir), "%s/.config/tuiman", home) < 0) {
    return -1;
  }
  if (snprintf(out->state_dir, sizeof(out->state_dir), "%s/.local/state/tuiman", home) < 0) {
    return -1;
  }
  if (snprintf(out->cache_dir, sizeof(out->cache_dir), "%s/.cache/tuiman", home) < 0) {
    return -1;
  }
  if (snprintf(out->requests_dir, sizeof(out->requests_dir), "%s/requests", out->config_dir) < 0) {
    return -1;
  }
  if (snprintf(out->history_db, sizeof(out->history_db), "%s/history.db", out->state_dir) < 0) {
    return -1;
  }

  if (ensure_dir(out->config_dir) != 0) {
    return -1;
  }
  if (ensure_dir(out->state_dir) != 0) {
    return -1;
  }
  if (ensure_dir(out->cache_dir) != 0) {
    return -1;
  }
  if (ensure_dir(out->requests_dir) != 0) {
    return -1;
  }

  return 0;
}
