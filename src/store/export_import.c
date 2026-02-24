#include "tuiman/export_import.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

static int has_json_suffix(const char *name) {
  size_t len = strlen(name);
  return len > 5 && strcmp(name + len - 5, ".json") == 0;
}

static int ensure_dir(const char *path) {
  char tmp[PATH_MAX];
  size_t len = strlen(path);
  if (len == 0 || len >= sizeof(tmp)) {
    return -1;
  }

  snprintf(tmp, sizeof(tmp), "%s", path);
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

static void now_iso(char out[40]) {
  time_t now = time(NULL);
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);
  strftime(out, 40, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

int export_requests(const app_paths_t *paths, const request_list_t *requests, const char *destination_dir,
                    export_report_t *report) {
  (void)paths;

  if (report != NULL) {
    report->exported_count = 0;
    report->scrubbed_secret_refs = 0;
  }

  if (ensure_dir(destination_dir) != 0) {
    return -1;
  }

  char req_dir[PATH_MAX];
  if (snprintf(req_dir, sizeof(req_dir), "%s/requests", destination_dir) < 0) {
    return -1;
  }
  if (ensure_dir(req_dir) != 0) {
    return -1;
  }

  for (size_t i = 0; i < requests->len; i++) {
    request_t copy = requests->items[i];
    if (copy.auth_secret_ref[0] != '\0' && report != NULL) {
      report->scrubbed_secret_refs++;
    }
    copy.auth_secret_ref[0] = '\0';

    char file_path[PATH_MAX];
    if (snprintf(file_path, sizeof(file_path), "%s/%s.json", req_dir, copy.id) < 0) {
      return -1;
    }
    if (request_store_write_file(file_path, &copy) != 0) {
      return -1;
    }

    if (report != NULL) {
      report->exported_count++;
    }
  }

  char when[40];
  now_iso(when);
  char manifest_path[PATH_MAX];
  if (snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.json", destination_dir) < 0) {
    return -1;
  }
  FILE *manifest = fopen(manifest_path, "wb");
  if (manifest == NULL) {
    return -1;
  }

  fprintf(manifest,
          "{\n"
          "  \"format\": 1,\n"
          "  \"exported_at\": \"%s\",\n"
          "  \"request_count\": %zu,\n"
          "  \"secrets_included\": false,\n"
          "  \"scrubbed_secret_refs\": %zu\n"
          "}\n",
          when, requests->len, report ? report->scrubbed_secret_refs : 0UL);
  fclose(manifest);

  return 0;
}

int import_requests(const app_paths_t *paths, const char *source_dir, size_t *imported_count) {
  if (imported_count != NULL) {
    *imported_count = 0;
  }

  char req_dir[PATH_MAX];
  if (snprintf(req_dir, sizeof(req_dir), "%s/requests", source_dir) < 0) {
    return -1;
  }

  DIR *dir = opendir(req_dir);
  if (dir == NULL) {
    return -1;
  }

  struct dirent *entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (!has_json_suffix(entry->d_name)) {
      continue;
    }

    char src_path[PATH_MAX];
    if (snprintf(src_path, sizeof(src_path), "%s/%s", req_dir, entry->d_name) < 0) {
      continue;
    }

    request_t req;
    if (request_store_read_file(src_path, &req) != 0) {
      continue;
    }

    if (request_store_save(paths, &req) == 0 && imported_count != NULL) {
      (*imported_count)++;
    }
  }

  closedir(dir);
  return 0;
}
