#ifndef TUIMAN_HISTORY_STORE_H
#define TUIMAN_HISTORY_STORE_H

#include <stddef.h>

#include <sqlite3.h>

#define TUIMAN_HISTORY_TIME_LEN 40
#define TUIMAN_HISTORY_ERR_LEN 256

typedef struct {
  int id;
  char request_id[37];
  char request_name[128];
  char method[16];
  char url[512];
  int status_code;
  long duration_ms;
  char error[TUIMAN_HISTORY_ERR_LEN];
  char created_at[TUIMAN_HISTORY_TIME_LEN];
} run_entry_t;

typedef struct {
  run_entry_t *items;
  size_t len;
} run_list_t;

int history_store_open(const char *db_path, sqlite3 **out_db);
void history_store_close(sqlite3 *db);

int history_store_add_run(sqlite3 *db, const run_entry_t *run);
int history_store_list_runs(sqlite3 *db, int limit, run_list_t *out);
void run_list_free(run_list_t *list);

#endif
