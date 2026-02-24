#ifndef TUIMAN_EXPORT_IMPORT_H
#define TUIMAN_EXPORT_IMPORT_H

#include <stddef.h>

#include "tuiman/paths.h"
#include "tuiman/request_store.h"

typedef struct {
  size_t exported_count;
  size_t scrubbed_secret_refs;
} export_report_t;

int export_requests(const app_paths_t *paths, const request_list_t *requests, const char *destination_dir,
                    export_report_t *report);
int import_requests(const app_paths_t *paths, const char *source_dir, size_t *imported_count);

#endif
