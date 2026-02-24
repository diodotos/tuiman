#include "tuiman/history_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *SCHEMA_SQL =
    "CREATE TABLE IF NOT EXISTS runs ("
    "id INTEGER PRIMARY KEY AUTOINCREMENT,"
    "request_id TEXT NOT NULL,"
    "request_name TEXT NOT NULL,"
    "method TEXT NOT NULL,"
    "url TEXT NOT NULL,"
    "status_code INTEGER,"
    "duration_ms INTEGER,"
    "error TEXT,"
    "created_at TEXT NOT NULL"
    ");";

int history_store_open(const char *db_path, sqlite3 **out_db) {
  if (sqlite3_open(db_path, out_db) != SQLITE_OK) {
    return -1;
  }

  char *errmsg = NULL;
  if (sqlite3_exec(*out_db, SCHEMA_SQL, NULL, NULL, &errmsg) != SQLITE_OK) {
    sqlite3_free(errmsg);
    sqlite3_close(*out_db);
    *out_db = NULL;
    return -1;
  }

  return 0;
}

void history_store_close(sqlite3 *db) {
  if (db != NULL) {
    sqlite3_close(db);
  }
}

int history_store_add_run(sqlite3 *db, const run_entry_t *run) {
  static const char *SQL = "INSERT INTO runs "
                           "(request_id, request_name, method, url, status_code, duration_ms, error, created_at)"
                           " VALUES (?, ?, ?, ?, ?, ?, ?, ?);";

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, SQL, -1, &stmt, NULL) != SQLITE_OK) {
    return -1;
  }

  sqlite3_bind_text(stmt, 1, run->request_id, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, run->request_name, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, run->method, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, run->url, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 5, run->status_code);
  sqlite3_bind_int64(stmt, 6, run->duration_ms);
  sqlite3_bind_text(stmt, 7, run->error, -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, run->created_at, -1, SQLITE_TRANSIENT);

  int rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  return rc == SQLITE_DONE ? 0 : -1;
}

int history_store_list_runs(sqlite3 *db, int limit, run_list_t *out) {
  out->items = NULL;
  out->len = 0;

  static const char *SQL = "SELECT id, request_id, request_name, method, url, status_code, duration_ms, error, "
                           "created_at FROM runs ORDER BY id DESC LIMIT ?;";

  sqlite3_stmt *stmt = NULL;
  if (sqlite3_prepare_v2(db, SQL, -1, &stmt, NULL) != SQLITE_OK) {
    return -1;
  }

  sqlite3_bind_int(stmt, 1, limit);

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    run_entry_t row;
    memset(&row, 0, sizeof(row));

    const unsigned char *request_id = sqlite3_column_text(stmt, 1);
    const unsigned char *request_name = sqlite3_column_text(stmt, 2);
    const unsigned char *method = sqlite3_column_text(stmt, 3);
    const unsigned char *url = sqlite3_column_text(stmt, 4);

    row.id = sqlite3_column_int(stmt, 0);
    snprintf(row.request_id, sizeof(row.request_id), "%s", request_id ? (const char *)request_id : "");
    snprintf(row.request_name, sizeof(row.request_name), "%s", request_name ? (const char *)request_name : "");
    snprintf(row.method, sizeof(row.method), "%s", method ? (const char *)method : "");
    snprintf(row.url, sizeof(row.url), "%s", url ? (const char *)url : "");
    row.status_code = sqlite3_column_int(stmt, 5);
    row.duration_ms = sqlite3_column_int64(stmt, 6);
    const unsigned char *err = sqlite3_column_text(stmt, 7);
    const unsigned char *created = sqlite3_column_text(stmt, 8);
    snprintf(row.error, sizeof(row.error), "%s", err ? (const char *)err : "");
    snprintf(row.created_at, sizeof(row.created_at), "%s", created ? (const char *)created : "");

    run_entry_t *next = realloc(out->items, (out->len + 1) * sizeof(run_entry_t));
    if (next == NULL) {
      sqlite3_finalize(stmt);
      run_list_free(out);
      return -1;
    }
    out->items = next;
    out->items[out->len] = row;
    out->len++;
  }

  sqlite3_finalize(stmt);
  return 0;
}

void run_list_free(run_list_t *list) {
  if (list == NULL) {
    return;
  }
  free(list->items);
  list->items = NULL;
  list->len = 0;
}
