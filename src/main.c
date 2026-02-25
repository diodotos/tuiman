#include <ctype.h>
#include <ncurses.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "tuiman/editor.h"
#include "tuiman/export_import.h"
#include "tuiman/history_store.h"
#include "tuiman/http_client.h"
#include "tuiman/json_body.h"
#include "tuiman/keychain_macos.h"
#include "tuiman/paths.h"
#include "tuiman/request_store.h"

#define CMDLINE_MAX 256
#define STATUS_MAX 512
#define DEFAULT_MAIN_STATUS \
  "j/k move | / search | : command | Enter actions | E edit | d delete | ZZ/ZQ quit | { } req body | [ ] resp body | drag"

#define MAIN_MIN_LEFT_W 24
#define MAIN_MIN_RIGHT_W 20
#define MAIN_MIN_TOP_H 4
#define MAIN_MIN_RESPONSE_H 4

#define EDITOR_MIN_LEFT_W 42
#define EDITOR_MIN_RIGHT_W 30

typedef enum {
  SCREEN_MAIN = 0,
  SCREEN_NEW = 1,
  SCREEN_HISTORY = 2,
  SCREEN_HELP = 3,
} screen_t;

typedef enum {
  MAIN_MODE_NORMAL = 0,
  MAIN_MODE_ACTION = 1,
  MAIN_MODE_SEARCH = 2,
  MAIN_MODE_REVERSE = 3,
  MAIN_MODE_COMMAND = 4,
  MAIN_MODE_DELETE_CONFIRM = 5,
} main_mode_t;

typedef enum {
  NEW_MODE_NORMAL = 0,
  NEW_MODE_INSERT = 1,
  NEW_MODE_COMMAND = 2,
} new_mode_t;

typedef enum {
  DRAG_NONE = 0,
  DRAG_VERTICAL = 1,
  DRAG_HORIZONTAL = 2,
} drag_mode_t;

typedef struct {
  int valid;
  int term_w;
  int term_h;
  int status_h;
  int available_h;

  int top_h;
  int response_h;
  int response_y;
  int horizontal_sep_y;

  int show_right;
  int left_w;
  int separator_x;
  int right_x;
  int right_w;
} main_layout_t;

typedef struct {
  int valid;
  int term_w;
  int term_h;
  int status_h;
  int content_h;

  int show_right;
  int left_w;
  int separator_x;
  int right_x;
  int right_w;
} editor_layout_t;

typedef struct {
  int valid;
  int term_w;
  int term_h;
  int status_h;
  int content_h;

  int show_right;
  int left_w;
  int separator_x;
  int right_x;
  int right_w;
} history_layout_t;

typedef struct {
  app_paths_t paths;
  sqlite3 *db;

  request_list_t requests;
  size_t *visible_indices;
  size_t visible_len;
  size_t selected_visible;
  size_t scroll;
  char filter[CMDLINE_MAX];

  run_list_t runs;
  size_t history_selected;
  size_t history_scroll;
  size_t history_detail_scroll;

  screen_t screen;
  main_mode_t main_mode;
  new_mode_t new_mode;
  drag_mode_t drag_mode;
  bool pending_g;
  bool pending_Z;

  double split_ratio;
  double response_ratio;

  char cmdline[CMDLINE_MAX];
  size_t cmdline_len;

  char status[STATUS_MAX];
  bool status_is_error;

  size_t request_body_scroll;
  size_t response_body_scroll;
  size_t editor_body_scroll;

  char delete_confirm_id[TUIMAN_ID_LEN];
  char delete_confirm_name[TUIMAN_NAME_LEN];

  request_t draft;
  bool draft_existing;
  int draft_field;
  char draft_input[TUIMAN_BODY_LEN];
  size_t draft_input_len;
  char draft_cmdline[CMDLINE_MAX];
  size_t draft_cmdline_len;

  char last_response_request_id[TUIMAN_ID_LEN];
  char last_response_request_name[TUIMAN_NAME_LEN];
  char last_response_method[TUIMAN_METHOD_LEN];
  char last_response_url[TUIMAN_URL_LEN];
  char last_response_at[40];
  long last_response_status;
  long last_response_ms;
  char last_response_error[256];
  char *last_response_body;
  size_t last_response_body_len;
} app_t;

enum {
  COLOR_GET = 1,
  COLOR_POST = 2,
  COLOR_PUT = 3,
  COLOR_PATCH = 4,
  COLOR_DELETE = 5,
  COLOR_STATUS_2XX = 6,
  COLOR_STATUS_3XX = 7,
  COLOR_STATUS_4XX = 8,
  COLOR_STATUS_5XX = 9,
  COLOR_LABEL = 10,
  COLOR_SECTION = 11,
};

enum {
  DRAFT_FIELD_NAME = 0,
  DRAFT_FIELD_METHOD = 1,
  DRAFT_FIELD_URL = 2,
  DRAFT_FIELD_HEADER_KEY = 3,
  DRAFT_FIELD_HEADER_VALUE = 4,
  DRAFT_FIELD_AUTH_TYPE = 5,
  DRAFT_FIELD_AUTH_SECRET_REF = 6,
  DRAFT_FIELD_AUTH_KEY_NAME = 7,
  DRAFT_FIELD_AUTH_LOCATION = 8,
  DRAFT_FIELD_AUTH_USERNAME = 9,
  DRAFT_FIELD_COUNT = 10,
};

static int method_color_pair(const char *method);

static void set_status(app_t *app, const char *message) {
  snprintf(app->status, sizeof(app->status), "%s", message);
  app->status_is_error = false;
}

static void set_status_error(app_t *app, const char *message) {
  snprintf(app->status, sizeof(app->status), "%s", message);
  app->status_is_error = true;
}

static void set_default_main_status(app_t *app) {
  set_status(app, DEFAULT_MAIN_STATUS);
}

static int clamp_int(int value, int min_value, int max_value) {
  if (value < min_value) {
    return min_value;
  }
  if (value > max_value) {
    return max_value;
  }
  return value;
}

static void compute_main_layout(const app_t *app, int term_h, int term_w, main_layout_t *out) {
  memset(out, 0, sizeof(*out));
  out->term_w = term_w;
  out->term_h = term_h;
  out->status_h = 1;
  out->available_h = term_h - out->status_h;

  if (out->available_h < 3 || term_w < 24) {
    out->valid = 0;
    return;
  }

  out->response_h = 0;
  out->horizontal_sep_y = -1;
  if (out->available_h >= (MAIN_MIN_TOP_H + MAIN_MIN_RESPONSE_H + 1)) {
    int response_h = (int)(app->response_ratio * (double)out->available_h + 0.5);
    int max_response_h = out->available_h - MAIN_MIN_TOP_H - 1;
    response_h = clamp_int(response_h, MAIN_MIN_RESPONSE_H, max_response_h);
    out->response_h = response_h;
    out->horizontal_sep_y = out->available_h - out->response_h - 1;
  }

  out->top_h = out->response_h > 0 ? out->horizontal_sep_y : out->available_h;
  out->response_y = out->response_h > 0 ? out->horizontal_sep_y + 1 : -1;
  if (out->top_h < 2) {
    out->valid = 0;
    return;
  }

  out->show_right = 0;
  out->left_w = term_w;
  out->separator_x = -1;
  out->right_x = 0;
  out->right_w = 0;

  if (term_w >= (MAIN_MIN_LEFT_W + MAIN_MIN_RIGHT_W + 1)) {
    int left_w = (int)(app->split_ratio * (double)term_w + 0.5);
    int max_left_w = term_w - MAIN_MIN_RIGHT_W - 1;
    left_w = clamp_int(left_w, MAIN_MIN_LEFT_W, max_left_w);

    out->show_right = 1;
    out->left_w = left_w;
    out->separator_x = left_w;
    out->right_x = out->separator_x + 1;
    out->right_w = term_w - out->right_x;
  }

  out->valid = 1;
}

static void compute_editor_layout(const app_t *app, int term_h, int term_w, editor_layout_t *out) {
  memset(out, 0, sizeof(*out));
  out->term_w = term_w;
  out->term_h = term_h;
  out->status_h = 1;
  out->content_h = term_h - out->status_h;

  if (out->content_h < 3 || term_w < 24) {
    out->valid = 0;
    return;
  }

  out->show_right = 0;
  out->left_w = term_w;
  out->separator_x = -1;
  out->right_x = 0;
  out->right_w = 0;

  if (term_w >= (EDITOR_MIN_LEFT_W + EDITOR_MIN_RIGHT_W + 1)) {
    int left_w = (int)(app->split_ratio * (double)term_w + 0.5);
    int max_left_w = term_w - EDITOR_MIN_RIGHT_W - 1;
    left_w = clamp_int(left_w, EDITOR_MIN_LEFT_W, max_left_w);

    out->show_right = 1;
    out->left_w = left_w;
    out->separator_x = left_w;
    out->right_x = out->separator_x + 1;
    out->right_w = term_w - out->right_x;
  }

  out->valid = 1;
}

static void compute_history_layout(const app_t *app, int term_h, int term_w, history_layout_t *out) {
  memset(out, 0, sizeof(*out));
  out->term_w = term_w;
  out->term_h = term_h;
  out->status_h = 1;
  out->content_h = term_h - out->status_h;

  if (out->content_h < 3 || term_w < 24) {
    out->valid = 0;
    return;
  }

  out->show_right = 0;
  out->left_w = term_w;
  out->separator_x = -1;
  out->right_x = 0;
  out->right_w = 0;

  if (term_w >= (MAIN_MIN_LEFT_W + MAIN_MIN_RIGHT_W + 1)) {
    int left_w = (int)(app->split_ratio * (double)term_w + 0.5);
    int max_left_w = term_w - MAIN_MIN_RIGHT_W - 1;
    left_w = clamp_int(left_w, MAIN_MIN_LEFT_W, max_left_w);

    out->show_right = 1;
    out->left_w = left_w;
    out->separator_x = left_w;
    out->right_x = out->separator_x + 1;
    out->right_w = term_w - out->right_x;
  }

  out->valid = 1;
}

static void set_resize_status(app_t *app, const main_layout_t *layout) {
  int left_pct = (int)(app->split_ratio * 100.0 + 0.5);
  int response_pct = (int)(app->response_ratio * 100.0 + 0.5);
  int response_lines = layout->response_h;
  char msg[STATUS_MAX];
  snprintf(msg, sizeof(msg), "Resize: left=%d%% response=%d%% (%d lines)", left_pct, response_pct, response_lines);
  set_status(app, msg);
}

static void enable_extended_mouse_tracking(void) {
  /*
   * ncurses often enables click-only tracking (1000). Force drag-capable
   * tracking so divider click+hold+drag behaves closer to tmux.
   */
  fputs("\033[?1002h\033[?1006h", stdout);
  fflush(stdout);
}

static void disable_extended_mouse_tracking(void) {
  fputs("\033[?1002l\033[?1006l", stdout);
  fflush(stdout);
}

static void nudge_split_ratio(app_t *app, double delta) {
  app->split_ratio += delta;
  if (app->split_ratio < 0.20) {
    app->split_ratio = 0.20;
  }
  if (app->split_ratio > 0.80) {
    app->split_ratio = 0.80;
  }
}

static void nudge_response_ratio(app_t *app, double delta) {
  app->response_ratio += delta;
  if (app->response_ratio < 0.15) {
    app->response_ratio = 0.15;
  }
  if (app->response_ratio > 0.70) {
    app->response_ratio = 0.70;
  }
}

static void refresh_resize_status(app_t *app) {
  int h = 0;
  int w = 0;
  getmaxyx(stdscr, h, w);
  main_layout_t layout;
  compute_main_layout(app, h, w, &layout);
  if (layout.valid) {
    set_resize_status(app, &layout);
  }
}

static void now_iso(char out[40]) {
  time_t now = time(NULL);
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);
  strftime(out, 40, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

static void clear_last_response(app_t *app) {
  app->last_response_request_id[0] = '\0';
  app->last_response_request_name[0] = '\0';
  app->last_response_method[0] = '\0';
  app->last_response_url[0] = '\0';
  app->last_response_at[0] = '\0';
  app->last_response_status = 0;
  app->last_response_ms = 0;
  app->last_response_error[0] = '\0';
  app->response_body_scroll = 0;
  free(app->last_response_body);
  app->last_response_body = NULL;
  app->last_response_body_len = 0;
}

static char *dup_text_n(const char *text, size_t len) {
  const char *src = text != NULL ? text : "";
  char *out = malloc(len + 1);
  if (out == NULL) {
    return NULL;
  }
  memcpy(out, src, len);
  out[len] = '\0';
  return out;
}

static char *build_request_snapshot(const request_t *req) {
  if (req == NULL) {
    return NULL;
  }

  int has_header = req->header_key[0] != '\0' || req->header_value[0] != '\0';
  const char *name = req->name[0] != '\0' ? req->name : "(unnamed)";
  const char *auth_type = req->auth_type[0] != '\0' ? req->auth_type : "none";
  const char *secret_ref = req->auth_secret_ref[0] != '\0' ? req->auth_secret_ref : "(none)";
  const char *auth_key_name = req->auth_key_name[0] != '\0' ? req->auth_key_name : "(none)";
  const char *auth_location = req->auth_location[0] != '\0' ? req->auth_location : "(none)";
  const char *auth_username = req->auth_username[0] != '\0' ? req->auth_username : "(none)";
  const char *body = req->body[0] != '\0' ? req->body : "(empty)";

  size_t needed = strlen(name) + strlen(req->method) + strlen(req->url) + strlen(auth_type) + strlen(secret_ref) +
                  strlen(auth_key_name) + strlen(auth_location) + strlen(auth_username) + strlen(body) +
                  strlen(req->header_key) + strlen(req->header_value) + 512;

  char *snapshot = malloc(needed);
  if (snapshot == NULL) {
    return NULL;
  }

  if (has_header) {
    snprintf(snapshot, needed,
             "name: %s\n"
             "method: %s\n"
             "url: %s\n"
             "auth: %s\n"
             "secret_ref: %s\n"
             "auth_key_name: %s\n"
             "auth_location: %s\n"
             "auth_username: %s\n"
             "header: %s: %s\n"
             "body:\n"
             "%s",
             name, req->method, req->url, auth_type, secret_ref, auth_key_name, auth_location, auth_username,
             req->header_key, req->header_value, body);
  } else {
    snprintf(snapshot, needed,
             "name: %s\n"
             "method: %s\n"
             "url: %s\n"
             "auth: %s\n"
             "secret_ref: %s\n"
             "auth_key_name: %s\n"
             "auth_location: %s\n"
             "auth_username: %s\n"
             "header: none\n"
             "body:\n"
             "%s",
             name, req->method, req->url, auth_type, secret_ref, auth_key_name, auth_location, auth_username, body);
  }

  return snapshot;
}

static void append_fmt(char *buf, size_t cap, size_t *offset, const char *fmt, ...) {
  if (buf == NULL || offset == NULL || fmt == NULL || *offset >= cap) {
    return;
  }

  va_list args;
  va_start(args, fmt);
  int wrote = vsnprintf(buf + *offset, cap - *offset, fmt, args);
  va_end(args);

  if (wrote < 0) {
    return;
  }

  size_t n = (size_t)wrote;
  if (n >= cap - *offset) {
    *offset = cap - 1;
  } else {
    *offset += n;
  }
}

static int snapshot_extract_line_value(const char *snapshot, const char *prefix, char *out, size_t out_len) {
  if (out == NULL || out_len == 0) {
    return 0;
  }
  out[0] = '\0';

  if (snapshot == NULL || snapshot[0] == '\0' || prefix == NULL) {
    return 0;
  }

  size_t prefix_len = strlen(prefix);
  const char *p = snapshot;
  while (*p != '\0') {
    const char *line_end = p;
    while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') {
      line_end++;
    }

    size_t line_len = (size_t)(line_end - p);
    if (line_len == 5 && strncmp(p, "body:", 5) == 0) {
      break;
    }

    if (line_len >= prefix_len && strncmp(p, prefix, prefix_len) == 0) {
      size_t value_len = line_len - prefix_len;
      if (value_len >= out_len) {
        value_len = out_len - 1;
      }
      memcpy(out, p + prefix_len, value_len);
      out[value_len] = '\0';
      return 1;
    }

    p = line_end;
    while (*p == '\n' || *p == '\r') {
      p++;
    }
  }

  return 0;
}

static const char *snapshot_body_start(const char *snapshot) {
  if (snapshot == NULL || snapshot[0] == '\0') {
    return NULL;
  }

  const char *p = snapshot;
  while (*p != '\0') {
    const char *line_end = p;
    while (*line_end != '\0' && *line_end != '\n' && *line_end != '\r') {
      line_end++;
    }

    size_t line_len = (size_t)(line_end - p);
    if (line_len == 5 && strncmp(p, "body:", 5) == 0) {
      p = line_end;
      while (*p == '\n' || *p == '\r') {
        p++;
      }
      return p;
    }

    p = line_end;
    while (*p == '\n' || *p == '\r') {
      p++;
    }
  }

  return NULL;
}

static int has_meaningful_value(const char *value) {
  if (value == NULL || value[0] == '\0') {
    return 0;
  }
  if (strcmp(value, "none") == 0 || strcmp(value, "(none)") == 0) {
    return 0;
  }
  return 1;
}

static char *build_history_detail_text(const run_entry_t *run) {
  if (run == NULL) {
    return NULL;
  }

  char method[64];
  char url[768];
  char auth[128];
  char secret_ref[160];
  char auth_key_name[160];
  char auth_location[96];
  char auth_username[160];
  char header[512];

  snprintf(method, sizeof(method), "%s", run->method);
  snprintf(url, sizeof(url), "%s", run->url);
  auth[0] = '\0';
  secret_ref[0] = '\0';
  auth_key_name[0] = '\0';
  auth_location[0] = '\0';
  auth_username[0] = '\0';
  header[0] = '\0';

  const char *request_body = "(request snapshot unavailable for this run)";
  if (run->request_snapshot != NULL && run->request_snapshot[0] != '\0') {
    (void)snapshot_extract_line_value(run->request_snapshot, "method: ", method, sizeof(method));
    (void)snapshot_extract_line_value(run->request_snapshot, "url: ", url, sizeof(url));
    (void)snapshot_extract_line_value(run->request_snapshot, "auth: ", auth, sizeof(auth));
    (void)snapshot_extract_line_value(run->request_snapshot, "secret_ref: ", secret_ref, sizeof(secret_ref));
    (void)snapshot_extract_line_value(run->request_snapshot, "auth_key_name: ", auth_key_name, sizeof(auth_key_name));
    (void)snapshot_extract_line_value(run->request_snapshot, "auth_location: ", auth_location, sizeof(auth_location));
    (void)snapshot_extract_line_value(run->request_snapshot, "auth_username: ", auth_username, sizeof(auth_username));
    (void)snapshot_extract_line_value(run->request_snapshot, "header: ", header, sizeof(header));

    const char *body = snapshot_body_start(run->request_snapshot);
    if (body != NULL) {
      request_body = body[0] != '\0' ? body : "(empty)";
    } else {
      request_body = "(request body unavailable for this run)";
    }
  }

  const char *response_body =
      (run->response_body != NULL && run->response_body[0] != '\0') ? run->response_body : "(empty)";
  const char *error_text = run->error[0] != '\0' ? run->error : "none";

  size_t needed = strlen(method) + strlen(url) + strlen(auth) + strlen(secret_ref) + strlen(auth_key_name) +
                  strlen(auth_location) + strlen(auth_username) + strlen(header) + strlen(request_body) +
                  strlen(response_body) + strlen(error_text) + 1024;
  char *text = malloc(needed);
  if (text == NULL) {
    return NULL;
  }

  size_t off = 0;
  append_fmt(text, needed, &off, "Request\n");
  append_fmt(text, needed, &off, "method: %s\n", method[0] != '\0' ? method : run->method);
  append_fmt(text, needed, &off, "url: %s\n", url[0] != '\0' ? url : run->url);
  if (has_meaningful_value(auth)) {
    append_fmt(text, needed, &off, "auth: %s\n", auth);
  }
  if (has_meaningful_value(secret_ref)) {
    append_fmt(text, needed, &off, "secret_ref: %s\n", secret_ref);
  }
  if (has_meaningful_value(auth_key_name)) {
    append_fmt(text, needed, &off, "auth_key_name: %s\n", auth_key_name);
  }
  if (has_meaningful_value(auth_location)) {
    append_fmt(text, needed, &off, "auth_location: %s\n", auth_location);
  }
  if (has_meaningful_value(auth_username)) {
    append_fmt(text, needed, &off, "auth_username: %s\n", auth_username);
  }
  if (has_meaningful_value(header)) {
    append_fmt(text, needed, &off, "header: %s\n", header);
  }
  append_fmt(text, needed, &off, "body:\n%s\n\n", request_body);

  append_fmt(text, needed, &off, "Response\n");
  append_fmt(text, needed, &off, "error: %s\n", error_text);
  append_fmt(text, needed, &off, "body:\n%s", response_body);

  return text;
}

static void line_reset(char *buf, size_t *len) {
  buf[0] = '\0';
  *len = 0;
}

static void line_backspace(char *buf, size_t *len) {
  if (*len == 0) {
    return;
  }
  (*len)--;
  buf[*len] = '\0';
}

static void line_append_char(char *buf, size_t cap, size_t *len, int ch) {
  if (ch < 32 || ch > 126) {
    return;
  }
  if (*len + 1 >= cap) {
    return;
  }
  buf[*len] = (char)ch;
  (*len)++;
  buf[*len] = '\0';
}

static void line_backspace_word(char *buf, size_t *len) {
  if (buf == NULL || len == NULL || *len == 0) {
    return;
  }

  while (*len > 0 && isspace((unsigned char)buf[*len - 1])) {
    (*len)--;
  }
  while (*len > 0 && !isspace((unsigned char)buf[*len - 1])) {
    (*len)--;
  }
  buf[*len] = '\0';
}

static int read_next_key_nowait(void) {
  nodelay(stdscr, TRUE);
  int next = getch();
  nodelay(stdscr, FALSE);
  return next;
}

static int launch_editor_and_restore_tui(const char *initial_text, char *out, size_t out_len, const char *suffix) {
  def_prog_mode();
  endwin();

  int rc = edit_text_with_editor(initial_text, out, out_len, suffix);

  reset_prog_mode();
  clearok(stdscr, TRUE);
  refresh();
  curs_set(0);
  return rc;
}

static void pane_add_text(int y, int x, int pane_right_exclusive, const char *text) {
  int h = 0;
  int w = 0;
  getmaxyx(stdscr, h, w);

  if (text == NULL || y < 0 || y >= h) {
    return;
  }

  if (x < 0) {
    x = 0;
  }

  int right = pane_right_exclusive;
  if (right > w) {
    right = w;
  }

  if (right <= x) {
    return;
  }

  int max_len = right - x;
  int len = 0;
  while (text[len] != '\0' && text[len] != '\n' && text[len] != '\r' && len < max_len) {
    len++;
  }

  move(y, x);
  addnstr(text, len);
}

static void pane_printf_text(int y, int x, int pane_right_exclusive, const char *fmt, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  pane_add_text(y, x, pane_right_exclusive, buffer);
}

static void win_add_text(WINDOW *win, int y, int x, const char *text) {
  if (win == NULL || text == NULL) {
    return;
  }

  int h = 0;
  int w = 0;
  getmaxyx(win, h, w);
  if (y < 0 || y >= h) {
    return;
  }

  if (x < 0) {
    x = 0;
  }
  if (x >= w) {
    return;
  }

  int max_len = w - x;
  int len = 0;
  while (text[len] != '\0' && text[len] != '\n' && text[len] != '\r' && len < max_len) {
    len++;
  }

  mvwaddnstr(win, y, x, text, len);
}

static void win_printf_text(WINDOW *win, int y, int x, const char *fmt, ...) {
  char buffer[2048];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  win_add_text(win, y, x, buffer);
}

static void win_add_labeled_text(WINDOW *win, int y, int x, const char *label, const char *value) {
  if (win == NULL || label == NULL || value == NULL) {
    return;
  }
  if (has_colors()) {
    wattron(win, COLOR_PAIR(COLOR_LABEL));
  }
  wattron(win, A_BOLD);
  win_add_text(win, y, x, label);
  wattroff(win, A_BOLD);
  if (has_colors()) {
    wattroff(win, COLOR_PAIR(COLOR_LABEL));
  }
  win_add_text(win, y, x + (int)strlen(label), value);
}

static void win_add_section_title(WINDOW *win, int y, int x, const char *title) {
  if (win == NULL || title == NULL) {
    return;
  }
  if (has_colors()) {
    wattron(win, COLOR_PAIR(COLOR_SECTION));
  }
  wattron(win, A_BOLD);
  win_add_text(win, y, x, title);
  wattroff(win, A_BOLD);
  if (has_colors()) {
    wattroff(win, COLOR_PAIR(COLOR_SECTION));
  }
}

static void win_add_labeled_method(WINDOW *win, int y, int x, const char *label, const char *method) {
  if (win == NULL || label == NULL || method == NULL) {
    return;
  }
  if (has_colors()) {
    wattron(win, COLOR_PAIR(COLOR_LABEL));
  }
  wattron(win, A_BOLD);
  win_add_text(win, y, x, label);
  wattroff(win, A_BOLD);
  if (has_colors()) {
    wattroff(win, COLOR_PAIR(COLOR_LABEL));
  }

  int pair = method_color_pair(method);
  if (pair != 0 && has_colors()) {
    wattron(win, COLOR_PAIR(pair));
  }
  win_add_text(win, y, x + (int)strlen(label), method);
  if (pair != 0 && has_colors()) {
    wattroff(win, COLOR_PAIR(pair));
  }
}

static void clear_missing_url_error(app_t *app) {
  if (strcmp(app->status, "URL cannot be empty") == 0) {
    app->status[0] = '\0';
    app->status_is_error = false;
  }
}

static size_t wrapped_total_line_count(const char *text, int width) {
  if (text == NULL || width <= 0) {
    return 0;
  }

  size_t lines = 0;
  const char *p = text;
  while (*p != '\0') {
    while (*p == '\r') {
      p++;
    }

    int len = 0;
    while (p[len] != '\0' && p[len] != '\n' && p[len] != '\r' && len < width) {
      len++;
    }

    lines++;
    p += len;

    if (*p == '\r') {
      p++;
      if (*p == '\n') {
        p++;
      }
    } else if (*p == '\n') {
      p++;
    }
  }

  return lines;
}

static size_t win_draw_wrapped_text_view(WINDOW *win, int start_y, int start_x, int max_lines, int max_width,
                                         const char *text, size_t start_line) {
  if (win == NULL || text == NULL || max_lines <= 0 || max_width <= 0) {
    return 0;
  }

  int wh = 0;
  int ww = 0;
  getmaxyx(win, wh, ww);
  if (start_x < 0) {
    start_x = 0;
  }
  if (start_x >= ww || start_y >= wh) {
    return 0;
  }

  int width = max_width;
  if (width > ww - start_x) {
    width = ww - start_x;
  }
  if (width <= 0) {
    return 0;
  }

  const char *p = text;
  size_t line_index = 0;
  int drawn = 0;
  while (*p != '\0') {
    int y = start_y + drawn;
    if (drawn >= max_lines || y < 0 || y >= wh) {
      y = -1;
    }

    while (*p == '\r') {
      p++;
    }

    int len = 0;
    while (p[len] != '\0' && p[len] != '\n' && p[len] != '\r' && len < width) {
      len++;
    }

    if (line_index >= start_line && drawn < max_lines && y >= 0 && len > 0) {
      mvwaddnstr(win, y, start_x, p, len);
      drawn++;
    } else if (line_index >= start_line && drawn < max_lines && y >= 0) {
      drawn++;
    }

    line_index++;
    p += len;

    if (*p == '\r') {
      p++;
      if (*p == '\n') {
        p++;
      }
    } else if (*p == '\n') {
      p++;
    }
  }

  return line_index;
}

static void win_draw_wrapped_text(WINDOW *win, int start_y, int start_x, int max_lines, int max_width,
                                  const char *text) {
  (void)win_draw_wrapped_text_view(win, start_y, start_x, max_lines, max_width, text, 0);
}

static int wrapped_line_count(const char *text, int width, int max_lines) {
  if (text == NULL || width <= 0 || max_lines <= 0) {
    return 0;
  }

  int lines = 0;
  const char *p = text;
  while (*p != '\0' && lines < max_lines) {
    int used = 0;
    while (p[used] != '\0' && p[used] != '\n' && p[used] != '\r' && used < width) {
      used++;
    }

    lines++;
    p += used;

    if (*p == '\r') {
      p++;
      if (*p == '\n') {
        p++;
      }
    } else if (*p == '\n') {
      p++;
    }
  }

  return lines;
}

static size_t clamp_scroll_offset(size_t scroll, size_t total_lines, int view_lines) {
  if (view_lines <= 0) {
    return 0;
  }
  size_t view = (size_t)view_lines;
  if (total_lines <= view) {
    return 0;
  }
  size_t max_scroll = total_lines - view;
  if (scroll > max_scroll) {
    return max_scroll;
  }
  return scroll;
}

static void win_draw_wrapped_body_preview(WINDOW *win, int start_y, int max_lines, int width, const char *text,
                                          size_t *scroll_offset) {
  if (win == NULL || max_lines <= 0 || width <= 0) {
    return;
  }

  if (text == NULL || text[0] == '\0') {
    win_add_text(win, start_y, 0, "(empty)");
    if (scroll_offset != NULL) {
      *scroll_offset = 0;
    }
    return;
  }

  size_t total_lines = wrapped_total_line_count(text, width);
  size_t scroll = scroll_offset != NULL ? *scroll_offset : 0;

  int content_lines = max_lines;
  int show_hint = (total_lines > (size_t)max_lines || scroll > 0) ? 1 : 0;
  if (show_hint && max_lines >= 2) {
    content_lines = max_lines - 1;
  }
  if (content_lines < 1) {
    content_lines = 1;
  }

  scroll = clamp_scroll_offset(scroll, total_lines, content_lines);
  if (scroll_offset != NULL) {
    *scroll_offset = scroll;
  }

  (void)win_draw_wrapped_text_view(win, start_y, 0, content_lines, width, text, scroll);

  if (show_hint && max_lines >= 2) {
    size_t shown = 0;
    if (total_lines > scroll) {
      shown = total_lines - scroll;
      if (shown > (size_t)content_lines) {
        shown = (size_t)content_lines;
      }
    }

    char hint[128];
    int up = scroll > 0;
    int down = (scroll + shown) < total_lines;
    snprintf(hint, sizeof(hint), "%c body %zu-%zu/%zu %c", up ? '^' : ' ', scroll + 1, scroll + shown, total_lines,
             down ? 'v' : ' ');

    if (has_colors()) {
      wattron(win, COLOR_PAIR(COLOR_LABEL));
    }
    win_add_text(win, start_y + content_lines, 0, hint);
    if (has_colors()) {
      wattroff(win, COLOR_PAIR(COLOR_LABEL));
    }
  }
}

static void draw_multiline_text(int start_y, int start_x, int max_lines, int max_width, const char *text) {
  if (max_lines <= 0 || max_width <= 0 || text == NULL) {
    return;
  }

  const char *line = text;
  for (int i = 0; i < max_lines && line[0] != '\0'; i++) {
    const char *nl = strchr(line, '\n');
    size_t raw_len = nl ? (size_t)(nl - line) : strlen(line);
    if (raw_len > 0 && line[raw_len - 1] == '\r') {
      raw_len--;
    }

    int print_len = (int)raw_len;
    if (print_len > max_width) {
      print_len = max_width;
    }

    int pane_right = start_x + max_width;
    if (print_len > 0) {
      pane_add_text(start_y + i, start_x, pane_right, line);
    }

    if (nl == NULL) {
      break;
    }
    line = nl + 1;
  }
}

static int should_treat_body_as_json(const char *body) {
  if (body == NULL) {
    return 0;
  }

  const char *p = body;
  while (*p != '\0' && isspace((unsigned char)*p)) {
    p++;
  }

  if (*p == '\0') {
    return 0;
  }

  return *p == '{' || *p == '[';
}

static int apply_body_edit_result(app_t *app, const char *edited, char *dest, size_t dest_len,
                                  const char *plain_success, const char *json_success) {
  if (!should_treat_body_as_json(edited)) {
    snprintf(dest, dest_len, "%s", edited);
    set_status(app, plain_success);
    return 0;
  }

  char formatted[TUIMAN_BODY_LEN];
  char error_message[256];
  if (json_body_validate_and_pretty(edited, formatted, sizeof(formatted), error_message, sizeof(error_message)) != 0) {
    char status[STATUS_MAX];
    snprintf(status, sizeof(status), "Invalid JSON: %s", error_message[0] ? error_message : "parse error");
    set_status(app, status);
    return -1;
  }

  snprintf(dest, dest_len, "%s", formatted);
  set_status(app, json_success);
  return 0;
}

static int contains_case_insensitive(const char *haystack, const char *needle) {
  if (needle == NULL || needle[0] == '\0') {
    return 1;
  }

  size_t h_len = strlen(haystack);
  size_t n_len = strlen(needle);
  if (n_len > h_len) {
    return 0;
  }

  for (size_t i = 0; i <= h_len - n_len; i++) {
    size_t j = 0;
    while (j < n_len) {
      char hc = (char)tolower((unsigned char)haystack[i + j]);
      char nc = (char)tolower((unsigned char)needle[j]);
      if (hc != nc) {
        break;
      }
      j++;
    }
    if (j == n_len) {
      return 1;
    }
  }

  return 0;
}

static request_t *selected_request(app_t *app) {
  if (app->visible_len == 0 || app->selected_visible >= app->visible_len) {
    return NULL;
  }
  size_t index = app->visible_indices[app->selected_visible];
  if (index >= app->requests.len) {
    return NULL;
  }
  return &app->requests.items[index];
}

static void apply_filter(app_t *app, const char *select_id) {
  free(app->visible_indices);
  app->visible_indices = NULL;
  app->visible_len = 0;
  app->request_body_scroll = 0;

  for (size_t i = 0; i < app->requests.len; i++) {
    request_t *req = &app->requests.items[i];
    if (!contains_case_insensitive(req->name, app->filter) && !contains_case_insensitive(req->url, app->filter)) {
      continue;
    }

    size_t *next = realloc(app->visible_indices, (app->visible_len + 1) * sizeof(size_t));
    if (next == NULL) {
      break;
    }
    app->visible_indices = next;
    app->visible_indices[app->visible_len] = i;
    app->visible_len++;
  }

  if (app->visible_len == 0) {
    app->selected_visible = 0;
    app->scroll = 0;
    return;
  }

  app->selected_visible = 0;
  if (select_id != NULL && select_id[0] != '\0') {
    for (size_t i = 0; i < app->visible_len; i++) {
      size_t idx = app->visible_indices[i];
      if (strcmp(app->requests.items[idx].id, select_id) == 0) {
        app->selected_visible = i;
        break;
      }
    }
  }

  app->scroll = 0;
}

static int load_requests(app_t *app, const char *select_id) {
  request_list_free(&app->requests);
  if (request_store_list(&app->paths, &app->requests) != 0) {
    set_status(app, "Failed to load requests");
    return -1;
  }
  apply_filter(app, select_id);
  return 0;
}

static int method_color_pair(const char *method) {
  if (strcmp(method, "GET") == 0) {
    return COLOR_GET;
  }
  if (strcmp(method, "POST") == 0) {
    return COLOR_POST;
  }
  if (strcmp(method, "PUT") == 0) {
    return COLOR_PUT;
  }
  if (strcmp(method, "PATCH") == 0) {
    return COLOR_PATCH;
  }
  if (strcmp(method, "DELETE") == 0) {
    return COLOR_DELETE;
  }
  return 0;
}

static int status_color_pair(long status_code) {
  if (status_code >= 200 && status_code < 300) {
    return COLOR_STATUS_2XX;
  }
  if (status_code >= 300 && status_code < 400) {
    return COLOR_STATUS_3XX;
  }
  if (status_code >= 400 && status_code < 500) {
    return COLOR_STATUS_4XX;
  }
  if (status_code >= 500) {
    return COLOR_STATUS_5XX;
  }
  return 0;
}

static void draw_main(app_t *app) {
  int h = 0;
  int w = 0;
  getmaxyx(stdscr, h, w);

  main_layout_t layout;
  compute_main_layout(app, h, w, &layout);
  if (!layout.valid) {
    erase();
    mvprintw(h - 1, 0, "Window too small");
    refresh();
    return;
  }

  erase();

  WINDOW *left_win = newwin(layout.top_h, layout.left_w, 0, 0);
  WINDOW *right_win = NULL;
  if (layout.show_right) {
    right_win = newwin(layout.top_h, layout.right_w, 0, layout.right_x);
  }
  WINDOW *response_win = NULL;
  if (layout.response_h > 0) {
    response_win = newwin(layout.response_h, w, layout.response_y, 0);
  }

  if (left_win == NULL) {
    mvprintw(h - 1, 0, "Failed to create list pane");
    refresh();
    return;
  }

  werase(left_win);
  if (right_win != NULL) {
    werase(right_win);
  }
  if (response_win != NULL) {
    werase(response_win);
  }

  int method_x = layout.left_w / 2;
  if (method_x < 8) {
    method_x = 8;
  }
  int url_x = method_x + 8;

  win_add_text(left_win, 0, 1, "Name");
  win_add_text(left_win, 0, method_x, "Type");
  win_add_text(left_win, 0, url_x, "URL");

  int view_rows = layout.top_h - 1;
  if (view_rows < 1) {
    view_rows = 1;
  }

  if (app->selected_visible < app->scroll) {
    app->scroll = app->selected_visible;
  }
  if (app->selected_visible >= app->scroll + (size_t)view_rows) {
    app->scroll = app->selected_visible - (size_t)view_rows + 1;
  }

  for (int row = 0; row < view_rows; row++) {
    size_t visible_index = app->scroll + (size_t)row;
    if (visible_index >= app->visible_len) {
      break;
    }

    size_t req_index = app->visible_indices[visible_index];
    request_t *req = &app->requests.items[req_index];
    int y = row + 1;

    if (visible_index == app->selected_visible) {
      wattron(left_win, A_REVERSE);
      mvwhline(left_win, y, 0, ' ', layout.left_w);
    }

    win_printf_text(left_win, y, 1, "%-28.28s", req->name);
    int pair = method_color_pair(req->method);
    if (pair != 0 && has_colors()) {
      wattron(left_win, COLOR_PAIR(pair));
    }
    win_printf_text(left_win, y, method_x, "%-6.6s", req->method);
    if (pair != 0 && has_colors()) {
      wattroff(left_win, COLOR_PAIR(pair));
    }

    int url_space = layout.left_w - (url_x + 1);
    if (url_space > 0) {
      win_printf_text(left_win, y, url_x, "%-*.*s", url_space, url_space, req->url);
    }

    if (visible_index == app->selected_visible) {
      wattroff(left_win, A_REVERSE);
    }
  }

  if (app->visible_len == 0) {
    win_add_text(left_win, 1, 1, "(empty)");
    win_add_text(left_win, 2, 1, "Use :new [METHOD] [URL]");
  }

  if (right_win != NULL) {
    request_t *selected = selected_request(app);
    win_add_section_title(right_win, 0, 0, "Request");
    if (has_colors()) {
      wattron(right_win, COLOR_PAIR(COLOR_SECTION));
    }
    mvwhline(right_win, 1, 0, ACS_HLINE, layout.right_w);
    if (has_colors()) {
      wattroff(right_win, COLOR_PAIR(COLOR_SECTION));
    }

    if (selected == NULL) {
      app->request_body_scroll = 0;
      win_draw_wrapped_text(right_win, 2, 0, layout.top_h - 2, layout.right_w, "No requests. Use :new to create one.");
    } else {
      int row = 2;

      if (selected->name[0] == '\0') {
        win_add_labeled_text(right_win, row, 0, "name: ", "(unnamed)");
      } else {
        win_add_labeled_text(right_win, row, 0, "name: ", selected->name);
      }
      row++;

      wmove(right_win, row, 0);
      if (has_colors()) {
        wattron(right_win, COLOR_PAIR(COLOR_LABEL));
      }
      wattron(right_win, A_BOLD);
      waddstr(right_win, "method: ");
      wattroff(right_win, A_BOLD);
      if (has_colors()) {
        wattroff(right_win, COLOR_PAIR(COLOR_LABEL));
      }
      int method_pair = method_color_pair(selected->method);
      if (method_pair != 0 && has_colors()) {
        wattron(right_win, COLOR_PAIR(method_pair));
      }
      waddstr(right_win, selected->method);
      if (method_pair != 0 && has_colors()) {
        wattroff(right_win, COLOR_PAIR(method_pair));
      }
      row++;

      int reserve = 2; /* body title + 1 body line */
      int has_auth = selected->auth_type[0] != '\0';
      int has_header = selected->header_key[0] != '\0' || selected->header_value[0] != '\0';
      if (has_auth || has_header) {
        reserve += 1; /* config title */
        if (has_auth) {
          reserve += 1;
        }
        if (has_header) {
          reserve += 1;
        }
      }

      const int url_label_w = 5;
      if (row < layout.top_h) {
        win_add_labeled_text(right_win, row, 0, "url: ", "");
      }
      int url_width = layout.right_w - url_label_w;
      if (url_width < 1) {
        url_width = 1;
      }
      int url_lines_max = layout.top_h - row - reserve;
      if (url_lines_max < 1) {
        url_lines_max = 1;
      }
      if (url_lines_max > 5) {
        url_lines_max = 5;
      }
      if (row < layout.top_h) {
        win_draw_wrapped_text(right_win, row, url_label_w, url_lines_max, url_width, selected->url);
      }
      int url_lines = wrapped_line_count(selected->url, url_width, url_lines_max);
      if (url_lines < 1) {
        url_lines = 1;
      }
      row += url_lines;

      if ((has_auth || has_header) && row < layout.top_h) {
        win_add_section_title(right_win, row, 0, "Config");
        row++;
      }
      if (has_auth && row < layout.top_h) {
        win_add_labeled_text(right_win, row, 0, "auth: ", selected->auth_type);
        row++;
      }
      if (has_header && row < layout.top_h) {
        char header_line[384];
        snprintf(header_line, sizeof(header_line), "%s: %s", selected->header_key, selected->header_value);
        win_add_labeled_text(right_win, row, 0, "header: ", header_line);
        row++;
      }

      if (row < layout.top_h) {
        win_add_section_title(right_win, row, 0, "Body");
        row++;
      }
      int body_lines = layout.top_h - row;
      if (body_lines > 0) {
        win_draw_wrapped_body_preview(right_win, row, body_lines, layout.right_w, selected->body,
                                      &app->request_body_scroll);
      }
    }
  } else {
    win_add_text(left_win, 1, 1, "Preview hidden (window too narrow)");
    win_add_text(left_win, 2, 1, "Widen terminal to restore split-pane view.");
  }

  if (layout.show_right) {
    if (app->drag_mode == DRAG_VERTICAL) {
      attron(A_REVERSE);
    }
    for (int y = 0; y < layout.top_h; y++) {
      mvaddch(y, layout.separator_x, ACS_VLINE);
    }
    if (app->drag_mode == DRAG_VERTICAL) {
      attroff(A_REVERSE);
    }
  }

  if (response_win != NULL) {
    win_add_section_title(response_win, 0, 0, "Response");
    if (has_colors()) {
      wattron(response_win, COLOR_PAIR(COLOR_SECTION));
    }
    mvwhline(response_win, 1, 0, ACS_HLINE, w);
    if (has_colors()) {
      wattroff(response_win, COLOR_PAIR(COLOR_SECTION));
    }

    if (app->last_response_at[0] == '\0') {
      app->response_body_scroll = 0;
      win_add_text(response_win, 2, 0, "No response yet.");
      win_add_text(response_win, 3, 0, "Select a request, press Enter, then y.");
    } else {
      int row = 2;

      wmove(response_win, row, 0);
      if (has_colors()) {
        wattron(response_win, COLOR_PAIR(COLOR_LABEL));
      }
      wattron(response_win, A_BOLD);
      waddstr(response_win, "status: ");
      wattroff(response_win, A_BOLD);
      if (has_colors()) {
        wattroff(response_win, COLOR_PAIR(COLOR_LABEL));
      }

      int s_pair = status_color_pair(app->last_response_status);
      if (s_pair != 0 && has_colors()) {
        wattron(response_win, COLOR_PAIR(s_pair));
      }
      wprintw(response_win, "%ld", app->last_response_status);
      if (s_pair != 0 && has_colors()) {
        wattroff(response_win, COLOR_PAIR(s_pair));
      }
      wprintw(response_win, "  duration=%ldms", app->last_response_ms);
      row++;

      win_add_labeled_text(response_win, row, 0, "at: ", app->last_response_at);
      row++;

      char request_line[640];
      snprintf(request_line, sizeof(request_line), "%s %s", app->last_response_method, app->last_response_url);
      win_add_labeled_text(response_win, row, 0, "request: ", "");
      int request_label_w = 9;
      int request_width = w - request_label_w;
      if (request_width < 1) {
        request_width = 1;
      }
      int request_lines_max = 2;
      if (app->last_response_error[0] != '\0') {
        request_lines_max = 1;
      }
      if (layout.response_h - row < 5) {
        request_lines_max = 1;
      }
      win_draw_wrapped_text(response_win, row, request_label_w, request_lines_max, request_width, request_line);
      int request_lines = wrapped_line_count(request_line, request_width, request_lines_max);
      if (request_lines < 1) {
        request_lines = 1;
      }
      row += request_lines;

      if (app->last_response_request_name[0] != '\0' && row < layout.response_h) {
        win_add_labeled_text(response_win, row, 0, "name: ", app->last_response_request_name);
        row++;
      }

      if (app->last_response_error[0] != '\0' && row < layout.response_h) {
        if (has_colors()) {
          wattron(response_win, COLOR_PAIR(COLOR_LABEL));
        }
        wattron(response_win, A_BOLD);
        win_add_text(response_win, row, 0, "error: ");
        wattroff(response_win, A_BOLD);
        if (has_colors()) {
          wattroff(response_win, COLOR_PAIR(COLOR_LABEL));
        }
        if (has_colors()) {
          wattron(response_win, COLOR_PAIR(COLOR_STATUS_5XX));
        }
        win_draw_wrapped_text(response_win, row, 7, 2, w - 7, app->last_response_error);
        if (has_colors()) {
          wattroff(response_win, COLOR_PAIR(COLOR_STATUS_5XX));
        }
        int err_lines = wrapped_line_count(app->last_response_error, w - 7, 2);
        if (err_lines < 1) {
          err_lines = 1;
        }
        row += err_lines;
      }

      if (row < layout.response_h) {
        win_add_section_title(response_win, row, 0, "Body");
        row++;
      }
      if (row < layout.response_h) {
        int lines = layout.response_h - row;
        win_draw_wrapped_body_preview(response_win, row, lines, w, app->last_response_body, &app->response_body_scroll);
      }
    }
  }

  if (layout.horizontal_sep_y >= 0) {
    if (app->drag_mode == DRAG_HORIZONTAL) {
      attron(A_REVERSE);
    }
    mvhline(layout.horizontal_sep_y, 0, ACS_HLINE, w);
    if (layout.show_right && layout.separator_x >= 0 && layout.separator_x < w) {
      mvaddch(layout.horizontal_sep_y, layout.separator_x, ACS_PLUS);
    }
    if (app->drag_mode == DRAG_HORIZONTAL) {
      attroff(A_REVERSE);
    }
  }

  move(h - 1, 0);
  clrtoeol();
  curs_set(0);
  if (app->main_mode == MAIN_MODE_SEARCH) {
    mvprintw(h - 1, 0, "/%s", app->cmdline);
    move(h - 1, (int)app->cmdline_len + 1);
    curs_set(1);
  } else if (app->main_mode == MAIN_MODE_REVERSE) {
    mvprintw(h - 1, 0, "?%s", app->cmdline);
    move(h - 1, (int)app->cmdline_len + 1);
    curs_set(1);
  } else if (app->main_mode == MAIN_MODE_COMMAND) {
    mvprintw(h - 1, 0, ":%s", app->cmdline);
    move(h - 1, (int)app->cmdline_len + 1);
    curs_set(1);
  } else if (app->main_mode == MAIN_MODE_ACTION) {
    mvprintw(h - 1, 0, "[esc/n] cancel   [y] send request   [e] edit body   [a] edit auth");
  } else if (app->main_mode == MAIN_MODE_DELETE_CONFIRM) {
    int prompt_w = w - 1;
    if (prompt_w < 20) {
      prompt_w = 20;
    }
    int name_w = prompt_w - 30;
    if (name_w < 4) {
      name_w = 4;
    }
    mvprintw(h - 1, 0, "Delete '%.*s'? [y] yes  [n/Esc] cancel", name_w, app->delete_confirm_name);
  } else {
    mvprintw(h - 1, 0, "%.*s", w - 1, app->status);
  }

  wnoutrefresh(stdscr);
  wnoutrefresh(left_win);
  if (right_win != NULL) {
    wnoutrefresh(right_win);
  }
  if (response_win != NULL) {
    wnoutrefresh(response_win);
  }

  doupdate();

  delwin(left_win);
  if (right_win != NULL) {
    delwin(right_win);
  }
  if (response_win != NULL) {
    delwin(response_win);
  }
}

static void apply_vertical_resize_from_x(app_t *app, int mouse_x, const main_layout_t *layout) {
  if (!layout->show_right || layout->term_w < (MAIN_MIN_LEFT_W + MAIN_MIN_RIGHT_W + 1)) {
    return;
  }

  int min_left = MAIN_MIN_LEFT_W;
  int max_left = layout->term_w - MAIN_MIN_RIGHT_W - 1;
  int left = clamp_int(mouse_x, min_left, max_left);
  app->split_ratio = (double)left / (double)layout->term_w;
  set_resize_status(app, layout);
}

static void apply_horizontal_resize_from_y(app_t *app, int mouse_y, const main_layout_t *layout) {
  if (layout->available_h < (MAIN_MIN_TOP_H + MAIN_MIN_RESPONSE_H + 1)) {
    return;
  }

  int min_sep = MAIN_MIN_TOP_H;
  int max_sep = layout->available_h - MAIN_MIN_RESPONSE_H - 1;
  int sep = clamp_int(mouse_y, min_sep, max_sep);
  int response_h = layout->available_h - sep - 1;
  app->response_ratio = (double)response_h / (double)layout->available_h;
  set_resize_status(app, layout);
}

static void handle_main_mouse(app_t *app) {
  MEVENT ev;
  if (getmouse(&ev) != OK) {
    return;
  }

  int h = 0;
  int w = 0;
  getmaxyx(stdscr, h, w);

  main_layout_t layout;
  compute_main_layout(app, h, w, &layout);
  if (!layout.valid) {
    app->drag_mode = DRAG_NONE;
    return;
  }

  int on_vertical = layout.show_right && ev.y >= 0 && ev.y < layout.top_h && abs(ev.x - layout.separator_x) <= 2;
  int on_horizontal = layout.response_h > 0 && abs(ev.y - layout.horizontal_sep_y) <= 2;

  mmask_t release_mask = BUTTON1_RELEASED;
  mmask_t press_mask = BUTTON1_PRESSED;
  mmask_t click_mask = BUTTON1_CLICKED;

  mmask_t drag_mask = REPORT_MOUSE_POSITION;
#ifdef BUTTON1_DRAGGED
  drag_mask |= BUTTON1_DRAGGED;
#endif
#ifdef BUTTON1_MOVED
  drag_mask |= BUTTON1_MOVED;
#endif

  if (ev.bstate & release_mask) {
    app->drag_mode = DRAG_NONE;
    return;
  }

  if (app->drag_mode != DRAG_NONE && (ev.bstate & (press_mask | click_mask | drag_mask))) {
    if (app->drag_mode == DRAG_VERTICAL) {
      apply_vertical_resize_from_x(app, ev.x, &layout);
      return;
    }
    if (app->drag_mode == DRAG_HORIZONTAL) {
      apply_horizontal_resize_from_y(app, ev.y, &layout);
      return;
    }
  }

  if (ev.bstate & (press_mask | click_mask)) {
    if (on_vertical) {
      app->drag_mode = DRAG_VERTICAL;
      apply_vertical_resize_from_x(app, ev.x, &layout);
      if (ev.bstate & click_mask) {
        app->drag_mode = DRAG_NONE;
      }
      return;
    }
    if (on_horizontal) {
      app->drag_mode = DRAG_HORIZONTAL;
      apply_horizontal_resize_from_y(app, ev.y, &layout);
      if (ev.bstate & click_mask) {
        app->drag_mode = DRAG_NONE;
      }
      return;
    }
    app->drag_mode = DRAG_NONE;
    return;
  }
}


static void draw_help(void) {
  int h = 0;
  int w = 0;
  getmaxyx(stdscr, h, w);
  erase();

  mvprintw(1, 2, "tuiman help");
  mvprintw(3, 2, "Main: j/k gg G / ? : Enter E d Esc n N H/L K/J resize ZZ/ZQ quit { } req body [ ] resp body");
  mvprintw(4, 2, "Actions: y send, e edit body, a edit auth");
  mvprintw(5, 2, "Commands: :new [METHOD] [URL], :edit, :history, :export [DIR], :import [DIR], :help, :q");
  mvprintw(6, 2, "Request editor: j/k move, i edit (except Method), h/l method, { } body scroll, e body, :w/:q");
  mvprintw(7, 2, "History: j/k move, r replay, H/L resize, { } details scroll");
  mvprintw(8, 2, "Mouse: drag main/editor/history vertical divider and main horizontal divider");
  mvprintw(h - 1, 0, "Press Esc to return");
  refresh();
}

static const char *guess_name(const char *method, const char *url, char *out, size_t out_len) {
  if (url == NULL || url[0] == '\0') {
    snprintf(out, out_len, "%s request", method);
    return out;
  }
  snprintf(out, out_len, "%s %s", method, url);
  return out;
}

static const char *draft_field_label(int field) {
  switch (field) {
  case DRAFT_FIELD_NAME:
    return "Name";
  case DRAFT_FIELD_METHOD:
    return "Method";
  case DRAFT_FIELD_URL:
    return "URL";
  case DRAFT_FIELD_HEADER_KEY:
    return "Header Key";
  case DRAFT_FIELD_HEADER_VALUE:
    return "Header Value";
  case DRAFT_FIELD_AUTH_TYPE:
    return "Auth Type";
  case DRAFT_FIELD_AUTH_SECRET_REF:
    return "Secret Ref";
  case DRAFT_FIELD_AUTH_KEY_NAME:
    return "Auth Key Name";
  case DRAFT_FIELD_AUTH_LOCATION:
    return "Auth Location";
  case DRAFT_FIELD_AUTH_USERNAME:
    return "Auth Username";
  default:
    return "";
  }
}

static const char *draft_field_value(const app_t *app, int field) {
  switch (field) {
  case DRAFT_FIELD_NAME:
    return app->draft.name;
  case DRAFT_FIELD_METHOD:
    return app->draft.method;
  case DRAFT_FIELD_URL:
    return app->draft.url;
  case DRAFT_FIELD_HEADER_KEY:
    return app->draft.header_key;
  case DRAFT_FIELD_HEADER_VALUE:
    return app->draft.header_value;
  case DRAFT_FIELD_AUTH_TYPE:
    return app->draft.auth_type;
  case DRAFT_FIELD_AUTH_SECRET_REF:
    return app->draft.auth_secret_ref;
  case DRAFT_FIELD_AUTH_KEY_NAME:
    return app->draft.auth_key_name;
  case DRAFT_FIELD_AUTH_LOCATION:
    return app->draft.auth_location;
  case DRAFT_FIELD_AUTH_USERNAME:
    return app->draft.auth_username;
  default:
    return "";
  }
}

static void uppercase_copy(char *dst, size_t dst_len, const char *src) {
  size_t i = 0;
  for (; src[i] != '\0' && i + 1 < dst_len; i++) {
    dst[i] = (char)toupper((unsigned char)src[i]);
  }
  dst[i] = '\0';
}

static void draft_set_field_value(app_t *app, int field, const char *value) {
  switch (field) {
  case DRAFT_FIELD_NAME:
    snprintf(app->draft.name, sizeof(app->draft.name), "%s", value);
    break;
  case DRAFT_FIELD_METHOD:
    uppercase_copy(app->draft.method, sizeof(app->draft.method), value);
    break;
  case DRAFT_FIELD_URL:
    snprintf(app->draft.url, sizeof(app->draft.url), "%s", value);
    break;
  case DRAFT_FIELD_HEADER_KEY:
    snprintf(app->draft.header_key, sizeof(app->draft.header_key), "%s", value);
    break;
  case DRAFT_FIELD_HEADER_VALUE:
    snprintf(app->draft.header_value, sizeof(app->draft.header_value), "%s", value);
    break;
  case DRAFT_FIELD_AUTH_TYPE:
    snprintf(app->draft.auth_type, sizeof(app->draft.auth_type), "%s", value);
    break;
  case DRAFT_FIELD_AUTH_SECRET_REF:
    snprintf(app->draft.auth_secret_ref, sizeof(app->draft.auth_secret_ref), "%s", value);
    break;
  case DRAFT_FIELD_AUTH_KEY_NAME:
    snprintf(app->draft.auth_key_name, sizeof(app->draft.auth_key_name), "%s", value);
    break;
  case DRAFT_FIELD_AUTH_LOCATION:
    snprintf(app->draft.auth_location, sizeof(app->draft.auth_location), "%s", value);
    break;
  case DRAFT_FIELD_AUTH_USERNAME:
    snprintf(app->draft.auth_username, sizeof(app->draft.auth_username), "%s", value);
    break;
  default:
    break;
  }
}

static void cycle_method(request_t *req, int delta) {
  static const char *methods[] = {"GET", "POST", "PUT", "PATCH", "DELETE"};
  int index = 0;
  for (int i = 0; i < 5; i++) {
    if (strcmp(req->method, methods[i]) == 0) {
      index = i;
      break;
    }
  }
  index = (index + delta + 5) % 5;
  snprintf(req->method, sizeof(req->method), "%s", methods[index]);
}

static int save_draft(app_t *app) {
  if (app->draft.method[0] == '\0') {
    snprintf(app->draft.method, sizeof(app->draft.method), "GET");
  }
  if (app->draft.name[0] == '\0') {
    char guessed[TUIMAN_NAME_LEN];
    guess_name(app->draft.method, app->draft.url, guessed, sizeof(guessed));
    snprintf(app->draft.name, sizeof(app->draft.name), "%s", guessed);
  }
  if (app->draft.url[0] == '\0') {
    set_status_error(app, "URL cannot be empty");
    return -1;
  }

  request_set_updated_now(&app->draft);
  if (request_store_save(&app->paths, &app->draft) != 0) {
    set_status_error(app, "Failed to save request");
    return -1;
  }

  char selected_id[TUIMAN_ID_LEN];
  snprintf(selected_id, sizeof(selected_id), "%s", app->draft.id);
  load_requests(app, selected_id);

  set_status(app, "Request saved");
  app->screen = SCREEN_MAIN;
  app->main_mode = MAIN_MODE_NORMAL;
  return 0;
}

static void draw_editor_status_line(app_t *app, int y, int w) {
  move(y, 0);
  clrtoeol();
  curs_set(0);

  if (app->new_mode == NEW_MODE_COMMAND) {
    mvprintw(y, 0, ":%s", app->draft_cmdline);
    move(y, 1 + (int)app->draft_cmdline_len);
    curs_set(1);
    return;
  }

  const char *mode = app->new_mode == NEW_MODE_INSERT ? "INSERT" : "NORMAL";
  if (has_colors()) {
    attron(COLOR_PAIR(COLOR_SECTION));
  }
  attron(A_BOLD);
  mvprintw(y, 0, "%s", mode);
  attroff(A_BOLD);
  if (has_colors()) {
    attroff(COLOR_PAIR(COLOR_SECTION));
  }

  int x = (int)strlen(mode);
  if (x >= w - 1) {
    return;
  }

  if (app->new_mode == NEW_MODE_INSERT) {
    char prefix[64];
    snprintf(prefix, sizeof(prefix), " | %s: ", draft_field_label(app->draft_field));
    mvprintw(y, x, "%s", prefix);
    x += (int)strlen(prefix);
    if (x < w - 1) {
      mvprintw(y, x, "%.*s", w - x - 1, app->draft_input);
      move(y, x + (int)app->draft_input_len);
      curs_set(1);
    }
    return;
  }

  if (app->status[0] != '\0') {
    mvprintw(y, x, " | ");
    x += 3;
    if (x < w - 1) {
      if (app->status_is_error && has_colors()) {
        attron(COLOR_PAIR(COLOR_STATUS_5XX));
      }
      mvprintw(y, x, "%.*s", w - x - 1, app->status);
      if (app->status_is_error && has_colors()) {
        attroff(COLOR_PAIR(COLOR_STATUS_5XX));
      }
    }
    return;
  }

  const char *hint = " | j/k field | i edit | h/l method | { } body | e body | :w save | :q cancel";
  mvprintw(y, x, "%.*s", w - x - 1, hint);
}

static void draw_new_editor(app_t *app) {
  int h = 0;
  int w = 0;
  getmaxyx(stdscr, h, w);

  editor_layout_t layout;
  compute_editor_layout(app, h, w, &layout);
  if (!layout.valid) {
    erase();
    if (h > 0) {
      mvprintw(h - 1, 0, "Window too small");
    }
    refresh();
    return;
  }

  erase();

  WINDOW *left_win = newwin(layout.content_h, layout.left_w, 0, 0);
  WINDOW *right_win = NULL;
  if (layout.show_right) {
    right_win = newwin(layout.content_h, layout.right_w, 0, layout.right_x);
  }

  if (left_win == NULL) {
    mvprintw(h - 1, 0, "Failed to create editor pane");
    refresh();
    return;
  }

  werase(left_win);
  if (right_win != NULL) {
    werase(right_win);
  }

  win_add_section_title(left_win, 0, 0, app->draft_existing ? "Edit Request" : "New Request");
  if (has_colors()) {
    wattron(left_win, COLOR_PAIR(COLOR_SECTION));
  }
  mvwhline(left_win, 1, 0, ACS_HLINE, layout.left_w);
  if (has_colors()) {
    wattroff(left_win, COLOR_PAIR(COLOR_SECTION));
  }

  int row = 2;
  int value_x = 16;
  for (int i = 0; i < DRAFT_FIELD_COUNT && row < layout.content_h; i++) {
    if (i == app->draft_field) {
      wattron(left_win, A_REVERSE);
      mvwhline(left_win, row, 0, ' ', layout.left_w);
    }

    char label[32];
    snprintf(label, sizeof(label), "%s: ", draft_field_label(i));
    if (i == DRAFT_FIELD_METHOD) {
      if (has_colors()) {
        wattron(left_win, COLOR_PAIR(COLOR_LABEL));
      }
      wattron(left_win, A_BOLD);
      win_add_text(left_win, row, 1, label);
      wattroff(left_win, A_BOLD);
      if (has_colors()) {
        wattroff(left_win, COLOR_PAIR(COLOR_LABEL));
      }

      int method_pair = method_color_pair(app->draft.method);
      if (method_pair != 0 && has_colors()) {
        wattron(left_win, COLOR_PAIR(method_pair));
      }
      win_add_text(left_win, row, value_x, app->draft.method);
      if (method_pair != 0 && has_colors()) {
        wattroff(left_win, COLOR_PAIR(method_pair));
      }
    } else {
      win_add_labeled_text(left_win, row, 1, label, draft_field_value(app, i));
    }

    if (i == app->draft_field) {
      wattroff(left_win, A_REVERSE);
    }
    row++;
  }

  if (row < layout.content_h) {
    win_add_section_title(left_win, row, 1, "Notes");
    row++;
  }
  if (row < layout.content_h) {
    win_printf_text(left_win, row, 1, "Body bytes: %zu", strlen(app->draft.body));
    row++;
  }
  if (row < layout.content_h) {
    win_add_text(left_win, row, 1, "Method field uses h/l cycle only");
  }

  if (right_win != NULL) {
    win_add_section_title(right_win, 0, 0, "Preview");
    if (has_colors()) {
      wattron(right_win, COLOR_PAIR(COLOR_SECTION));
    }
    mvwhline(right_win, 1, 0, ACS_HLINE, layout.right_w);
    if (has_colors()) {
      wattroff(right_win, COLOR_PAIR(COLOR_SECTION));
    }

    row = 2;
    if (app->draft.name[0] == '\0') {
      win_add_labeled_text(right_win, row, 0, "name: ", "(unnamed)");
    } else {
      win_add_labeled_text(right_win, row, 0, "name: ", app->draft.name);
    }
    row++;

    win_add_labeled_method(right_win, row, 0, "method: ", app->draft.method);
    row++;

    int show_auth_type = app->draft.auth_type[0] != '\0' && strcmp(app->draft.auth_type, "none") != 0;

    int cfg_lines = 0;
    if (show_auth_type) {
      cfg_lines++;
    }
    if (app->draft.auth_secret_ref[0] != '\0') {
      cfg_lines++;
    }
    if (app->draft.auth_key_name[0] != '\0') {
      cfg_lines++;
    }
    if (app->draft.auth_location[0] != '\0') {
      cfg_lines++;
    }
    if (app->draft.auth_username[0] != '\0') {
      cfg_lines++;
    }
    if (app->draft.header_key[0] != '\0' || app->draft.header_value[0] != '\0') {
      cfg_lines++;
    }

    int reserve = 2;
    if (cfg_lines > 0) {
      reserve += 1 + cfg_lines;
    }

    const int url_label_w = 5;
    win_add_labeled_text(right_win, row, 0, "url: ", "");
    int url_w = layout.right_w - url_label_w;
    if (url_w < 1) {
      url_w = 1;
    }
    int url_lines_max = layout.content_h - row - reserve;
    if (url_lines_max < 1) {
      url_lines_max = 1;
    }
    if (url_lines_max > 5) {
      url_lines_max = 5;
    }
    win_draw_wrapped_text(right_win, row, url_label_w, url_lines_max, url_w, app->draft.url);
    int url_lines = wrapped_line_count(app->draft.url, url_w, url_lines_max);
    if (url_lines < 1) {
      url_lines = 1;
    }
    row += url_lines;

    if (cfg_lines > 0 && row < layout.content_h) {
      win_add_section_title(right_win, row, 0, "Config");
      row++;
    }
    if (show_auth_type && row < layout.content_h) {
      win_add_labeled_text(right_win, row, 0, "auth: ", app->draft.auth_type);
      row++;
    }
    if (app->draft.auth_secret_ref[0] != '\0' && row < layout.content_h) {
      win_add_labeled_text(right_win, row, 0, "secret: ", app->draft.auth_secret_ref);
      row++;
    }
    if (app->draft.auth_key_name[0] != '\0' && row < layout.content_h) {
      win_add_labeled_text(right_win, row, 0, "key: ", app->draft.auth_key_name);
      row++;
    }
    if (app->draft.auth_location[0] != '\0' && row < layout.content_h) {
      win_add_labeled_text(right_win, row, 0, "location: ", app->draft.auth_location);
      row++;
    }
    if (app->draft.auth_username[0] != '\0' && row < layout.content_h) {
      win_add_labeled_text(right_win, row, 0, "user: ", app->draft.auth_username);
      row++;
    }
    if ((app->draft.header_key[0] != '\0' || app->draft.header_value[0] != '\0') && row < layout.content_h) {
      char header_line[384];
      snprintf(header_line, sizeof(header_line), "%s: %s", app->draft.header_key, app->draft.header_value);
      win_add_labeled_text(right_win, row, 0, "header: ", header_line);
      row++;
    }

    if (row < layout.content_h) {
      win_add_section_title(right_win, row, 0, "Body");
      row++;
    }
    int body_lines = layout.content_h - row;
    if (body_lines > 0) {
      win_draw_wrapped_body_preview(right_win, row, body_lines, layout.right_w, app->draft.body, &app->editor_body_scroll);
    }
  } else {
    app->editor_body_scroll = 0;
    if (layout.content_h > 2) {
      win_add_text(left_win, layout.content_h - 2, 1, "Preview hidden (window too narrow)");
    }
  }

  if (layout.show_right) {
    if (app->drag_mode == DRAG_VERTICAL) {
      attron(A_REVERSE);
    }
    for (int y = 0; y < layout.content_h; y++) {
      mvaddch(y, layout.separator_x, ACS_VLINE);
    }
    if (app->drag_mode == DRAG_VERTICAL) {
      attroff(A_REVERSE);
    }
  }

  wnoutrefresh(stdscr);
  wnoutrefresh(left_win);
  if (right_win != NULL) {
    wnoutrefresh(right_win);
  }

  draw_editor_status_line(app, h - 1, w);
  doupdate();

  delwin(left_win);
  if (right_win != NULL) {
    delwin(right_win);
  }
}

static int send_request_and_record(app_t *app, request_t *req) {
  http_response_t response;
  int rc = http_send_request(req, &response);
  char now[40];
  now_iso(now);

  snprintf(app->last_response_request_id, sizeof(app->last_response_request_id), "%s", req->id);
  snprintf(app->last_response_request_name, sizeof(app->last_response_request_name), "%s", req->name);
  snprintf(app->last_response_method, sizeof(app->last_response_method), "%s", req->method);
  snprintf(app->last_response_url, sizeof(app->last_response_url), "%s", req->url);
  snprintf(app->last_response_at, sizeof(app->last_response_at), "%s", now);
  app->last_response_status = response.status_code;
  app->last_response_ms = response.duration_ms;
  snprintf(app->last_response_error, sizeof(app->last_response_error), "%s", response.error);
  free(app->last_response_body);
  app->last_response_body = NULL;
  app->last_response_body_len = 0;
  app->response_body_scroll = 0;

  const char *body = response.body != NULL ? response.body : "";
  size_t body_len = response.body_len;
  if (response.body == NULL) {
    body_len = strlen(body);
  }

  char *copied = dup_text_n(body, body_len);
  if (copied != NULL) {
    app->last_response_body = copied;
    app->last_response_body_len = body_len;
  } else {
    const char *fallback = "(response body unavailable: out of memory)";
    size_t fallback_len = strlen(fallback);
    copied = dup_text_n(fallback, fallback_len);
    if (copied != NULL) {
      app->last_response_body = copied;
      app->last_response_body_len = fallback_len;
    }
  }

  run_entry_t run;
  memset(&run, 0, sizeof(run));
  snprintf(run.request_id, sizeof(run.request_id), "%s", req->id);
  snprintf(run.request_name, sizeof(run.request_name), "%s", req->name);
  snprintf(run.method, sizeof(run.method), "%s", req->method);
  snprintf(run.url, sizeof(run.url), "%s", req->url);
  run.status_code = (int)response.status_code;
  run.duration_ms = response.duration_ms;
  snprintf(run.error, sizeof(run.error), "%s", response.error);
  run.request_snapshot = build_request_snapshot(req);
  if (run.request_snapshot == NULL) {
    const char *fallback = "(request snapshot unavailable: out of memory)";
    run.request_snapshot = dup_text_n(fallback, strlen(fallback));
  }
  run.response_body = dup_text_n(body, body_len);
  if (run.response_body == NULL) {
    const char *fallback = "(response body unavailable: out of memory)";
    run.response_body = dup_text_n(fallback, strlen(fallback));
  }
  now_iso(run.created_at);
  history_store_add_run(app->db, &run);
  free(run.request_snapshot);
  free(run.response_body);
  run.request_snapshot = NULL;
  run.response_body = NULL;

  if (rc == 0) {
    set_status(app, "Request sent");
  } else {
    char msg[STATUS_MAX];
    snprintf(msg, sizeof(msg), "Request failed: %s", response.error[0] ? response.error : "unknown error");
    set_status(app, msg);
  }

  http_response_free(&response);
  return rc;
}

static void load_history(app_t *app) {
  run_list_free(&app->runs);
  history_store_list_runs(app->db, 500, &app->runs);
  app->history_selected = 0;
  app->history_scroll = 0;
  app->history_detail_scroll = 0;
  app->drag_mode = DRAG_NONE;
}

static void draw_history(app_t *app) {
  int h = 0;
  int w = 0;
  getmaxyx(stdscr, h, w);

  history_layout_t layout;
  compute_history_layout(app, h, w, &layout);
  if (!layout.valid) {
    erase();
    if (h > 0) {
      mvprintw(h - 1, 0, "Window too small");
    }
    refresh();
    return;
  }

  erase();

  WINDOW *left_win = newwin(layout.content_h, layout.left_w, 0, 0);
  WINDOW *right_win = NULL;
  if (layout.show_right) {
    right_win = newwin(layout.content_h, layout.right_w, 0, layout.right_x);
  }

  if (left_win == NULL) {
    mvprintw(h - 1, 0, "Failed to create history pane");
    refresh();
    return;
  }

  werase(left_win);
  if (right_win != NULL) {
    werase(right_win);
  }

  win_add_section_title(left_win, 0, 0, "History");
  if (has_colors()) {
    wattron(left_win, COLOR_PAIR(COLOR_SECTION));
  }
  mvwhline(left_win, 1, 0, ACS_HLINE, layout.left_w);
  if (has_colors()) {
    wattroff(left_win, COLOR_PAIR(COLOR_SECTION));
  }

  int method_x = 22;
  int status_x = method_x + 8;
  int duration_x = status_x + 8;
  int name_x = duration_x + 7;
  if (method_x >= layout.left_w - 8) {
    method_x = layout.left_w / 2;
    status_x = method_x + 8;
    duration_x = status_x + 8;
    name_x = duration_x + 7;
  }
  if (name_x >= layout.left_w - 4) {
    name_x = layout.left_w - 4;
  }

  int header_y = 2;
  if (header_y < layout.content_h) {
    if (has_colors()) {
      wattron(left_win, COLOR_PAIR(COLOR_LABEL));
    }
    wattron(left_win, A_BOLD);
    win_add_text(left_win, header_y, 1, "When");
    win_add_text(left_win, header_y, method_x, "Method");
    win_add_text(left_win, header_y, status_x, "Status");
    win_add_text(left_win, header_y, duration_x, "ms");
    win_add_text(left_win, header_y, name_x, "Name");
    wattroff(left_win, A_BOLD);
    if (has_colors()) {
      wattroff(left_win, COLOR_PAIR(COLOR_LABEL));
    }
  }

  int rows = layout.content_h - (header_y + 1);
  if (rows < 1) {
    rows = 1;
  }
  if (app->history_selected < app->history_scroll) {
    app->history_scroll = app->history_selected;
  }
  if (app->history_selected >= app->history_scroll + (size_t)rows) {
    app->history_scroll = app->history_selected - (size_t)rows + 1;
  }

  for (int i = 0; i < rows; i++) {
    size_t idx = app->history_scroll + (size_t)i;
    if (idx >= app->runs.len) {
      break;
    }
    run_entry_t *run = &app->runs.items[idx];
    int y = i + header_y + 1;
    if (idx == app->history_selected) {
      wattron(left_win, A_REVERSE);
      mvwhline(left_win, y, 0, ' ', layout.left_w);
    }

    win_printf_text(left_win, y, 1, "%-19.19s", run->created_at);

    int m_pair = method_color_pair(run->method);
    if (m_pair != 0 && has_colors()) {
      wattron(left_win, COLOR_PAIR(m_pair));
    }
    win_printf_text(left_win, y, method_x, "%-7.7s", run->method);
    if (m_pair != 0 && has_colors()) {
      wattroff(left_win, COLOR_PAIR(m_pair));
    }

    int s_pair = status_color_pair(run->status_code);
    if (s_pair != 0 && has_colors()) {
      wattron(left_win, COLOR_PAIR(s_pair));
    }
    win_printf_text(left_win, y, status_x, "%-7d", run->status_code);
    if (s_pair != 0 && has_colors()) {
      wattroff(left_win, COLOR_PAIR(s_pair));
    }

    win_printf_text(left_win, y, duration_x, "%-5ld", run->duration_ms);

    int name_w = layout.left_w - name_x - 1;
    if (name_w > 0) {
      win_printf_text(left_win, y, name_x, "%-*.*s", name_w, name_w, run->request_name);
    }

    if (idx == app->history_selected) {
      wattroff(left_win, A_REVERSE);
    }
  }

  if (app->runs.len == 0) {
    win_add_text(left_win, 3, 1, "No history yet");
    win_add_text(left_win, 4, 1, "Send requests from main to populate history");
    app->history_detail_scroll = 0;
  }

  if (right_win != NULL) {
    win_add_section_title(right_win, 0, 0, "Run Detail");
    if (has_colors()) {
      wattron(right_win, COLOR_PAIR(COLOR_SECTION));
    }
    mvwhline(right_win, 1, 0, ACS_HLINE, layout.right_w);
    if (has_colors()) {
      wattroff(right_win, COLOR_PAIR(COLOR_SECTION));
    }

    if (app->runs.len == 0) {
      win_add_text(right_win, 2, 0, "No history yet.");
    } else {
      run_entry_t *run = &app->runs.items[app->history_selected];
      int row = 2;

      if (run->request_name[0] == '\0') {
        win_add_labeled_text(right_win, row, 0, "name: ", "(unnamed)");
      } else {
        win_add_labeled_text(right_win, row, 0, "name: ", run->request_name);
      }
      row++;

      win_add_labeled_method(right_win, row, 0, "method: ", run->method);
      row++;

      wmove(right_win, row, 0);
      if (has_colors()) {
        wattron(right_win, COLOR_PAIR(COLOR_LABEL));
      }
      wattron(right_win, A_BOLD);
      waddstr(right_win, "status: ");
      wattroff(right_win, A_BOLD);
      if (has_colors()) {
        wattroff(right_win, COLOR_PAIR(COLOR_LABEL));
      }
      int s_pair = status_color_pair(run->status_code);
      if (s_pair != 0 && has_colors()) {
        wattron(right_win, COLOR_PAIR(s_pair));
      }
      wprintw(right_win, "%d", run->status_code);
      if (s_pair != 0 && has_colors()) {
        wattroff(right_win, COLOR_PAIR(s_pair));
      }
      wprintw(right_win, "  duration=%ldms", run->duration_ms);
      row++;

      win_add_labeled_text(right_win, row, 0, "at: ", run->created_at);
      row++;
      win_add_labeled_text(right_win, row, 0, "id: ", run->request_id);
      row++;

      if (row < layout.content_h) {
        if (has_colors()) {
          wattron(right_win, COLOR_PAIR(COLOR_SECTION));
        }
        mvwhline(right_win, row, 0, ACS_HLINE, layout.right_w);
        if (has_colors()) {
          wattroff(right_win, COLOR_PAIR(COLOR_SECTION));
        }
        row++;
      }

      if (row < layout.content_h) {
        win_add_section_title(right_win, row, 0, "Request + Response");
        row++;
      }

      if (row < layout.content_h) {
        char *details = build_history_detail_text(run);
        if (details == NULL) {
          const char *fallback = "(history detail unavailable: out of memory)";
          details = dup_text_n(fallback, strlen(fallback));
        }

        if (details != NULL) {
          win_draw_wrapped_body_preview(right_win, row, layout.content_h - row, layout.right_w, details,
                                        &app->history_detail_scroll);
          free(details);
        }
      }
    }
  } else {
    if (layout.content_h > 2) {
      win_add_text(left_win, layout.content_h - 2, 1, "Run detail hidden (window too narrow)");
    }
  }

  if (layout.show_right) {
    if (app->drag_mode == DRAG_VERTICAL) {
      attron(A_REVERSE);
    }
    for (int y = 0; y < layout.content_h; y++) {
      mvaddch(y, layout.separator_x, ACS_VLINE);
    }
    if (app->drag_mode == DRAG_VERTICAL) {
      attroff(A_REVERSE);
    }
  }

  wnoutrefresh(stdscr);
  wnoutrefresh(left_win);
  if (right_win != NULL) {
    wnoutrefresh(right_win);
  }

  move(h - 1, 0);
  clrtoeol();
  mvprintw(h - 1, 0, "HISTORY | j/k move | r replay | H/L resize | { } details | drag divider | Esc back");
  doupdate();

  delwin(left_win);
  if (right_win != NULL) {
    delwin(right_win);
  }
}

static void enter_new_screen(app_t *app, const request_t *from_request, int initial_field) {
  if (from_request != NULL) {
    app->draft = *from_request;
    app->draft_existing = true;
  } else {
    request_init_defaults(&app->draft);
    app->draft_existing = false;
  }
  app->draft_field = initial_field;
  app->new_mode = NEW_MODE_NORMAL;
  app->drag_mode = DRAG_NONE;
  app->editor_body_scroll = 0;
  line_reset(app->draft_input, &app->draft_input_len);
  line_reset(app->draft_cmdline, &app->draft_cmdline_len);
  app->status[0] = '\0';
  app->status_is_error = false;
  app->screen = SCREEN_NEW;
}

static void execute_main_command(app_t *app, bool *running, const char *line) {
  char copy[CMDLINE_MAX];
  snprintf(copy, sizeof(copy), "%s", line);

  char *cmd = strtok(copy, " ");
  if (cmd == NULL) {
    return;
  }

  if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
    *running = false;
    return;
  }

  if (strcmp(cmd, "help") == 0) {
    app->screen = SCREEN_HELP;
    return;
  }

  if (strcmp(cmd, "new") == 0) {
    request_t draft;
    request_init_defaults(&draft);
    char *method = strtok(NULL, " ");
    char *url = strtok(NULL, "");
    if (method != NULL) {
      uppercase_copy(draft.method, sizeof(draft.method), method);
    }
    if (url != NULL) {
      while (*url == ' ') {
        url++;
      }
      snprintf(draft.url, sizeof(draft.url), "%s", url);
    }
    guess_name(draft.method, draft.url, draft.name, sizeof(draft.name));
    enter_new_screen(app, &draft, DRAFT_FIELD_NAME);
    app->draft_existing = false;
    return;
  }

  if (strcmp(cmd, "edit") == 0) {
    request_t *selected = selected_request(app);
    if (selected == NULL) {
      set_status(app, "No request selected");
      return;
    }
    enter_new_screen(app, selected, DRAFT_FIELD_NAME);
    return;
  }

  if (strcmp(cmd, "history") == 0) {
    load_history(app);
    app->screen = SCREEN_HISTORY;
    return;
  }

  if (strcmp(cmd, "export") == 0) {
    char destination[PATH_MAX];
    char *arg = strtok(NULL, "");
    if (arg == NULL) {
      time_t now = time(NULL);
      struct tm tm_local;
      localtime_r(&now, &tm_local);
      strftime(destination, sizeof(destination), "./tuiman-export-%Y%m%d-%H%M%S", &tm_local);
    } else {
      while (*arg == ' ') {
        arg++;
      }
      snprintf(destination, sizeof(destination), "%s", arg);
    }

    export_report_t report;
    if (export_requests(&app->paths, &app->requests, destination, &report) == 0) {
      char msg[STATUS_MAX];
      snprintf(msg, sizeof(msg), "Exported %zu requests to %s (scrubbed %zu secret refs)", report.exported_count,
               destination, report.scrubbed_secret_refs);
      set_status(app, msg);
    } else {
      set_status(app, "Export failed");
    }
    return;
  }

  if (strcmp(cmd, "import") == 0) {
    char *arg = strtok(NULL, "");
    if (arg == NULL) {
      set_status(app, "Usage: :import /path/to/export-dir");
      return;
    }
    while (*arg == ' ') {
      arg++;
    }

    size_t imported = 0;
    if (import_requests(&app->paths, arg, &imported) == 0) {
      load_requests(app, NULL);
      char msg[STATUS_MAX];
      snprintf(msg, sizeof(msg), "Imported %zu requests", imported);
      set_status(app, msg);
    } else {
      set_status(app, "Import failed");
    }
    return;
  }

  set_status(app, "Unknown command");
}

static void handle_main_key(app_t *app, bool *running, int ch) {
  if (ch == KEY_MOUSE) {
    handle_main_mouse(app);
    return;
  }

  if (ch == KEY_RESIZE) {
    return;
  }

  if (app->drag_mode != DRAG_NONE) {
    app->drag_mode = DRAG_NONE;
  }

  request_t *selected = selected_request(app);

  if (app->main_mode == MAIN_MODE_SEARCH || app->main_mode == MAIN_MODE_REVERSE ||
      app->main_mode == MAIN_MODE_COMMAND) {
    app->pending_Z = false;
    if (ch == 27) {
      app->main_mode = MAIN_MODE_NORMAL;
      line_reset(app->cmdline, &app->cmdline_len);
      set_default_main_status(app);
      return;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      line_backspace(app->cmdline, &app->cmdline_len);
      return;
    }
    if (ch == '\n' || ch == KEY_ENTER) {
      if (app->main_mode == MAIN_MODE_SEARCH || app->main_mode == MAIN_MODE_REVERSE) {
        snprintf(app->filter, sizeof(app->filter), "%s", app->cmdline);
        apply_filter(app, NULL);
        if (app->filter[0] != '\0') {
          char msg[STATUS_MAX];
          snprintf(msg, sizeof(msg), "FILTER: %s (%zu results)", app->filter, app->visible_len);
          set_status(app, msg);
        } else {
          set_default_main_status(app);
        }
      } else {
        execute_main_command(app, running, app->cmdline);
      }

      line_reset(app->cmdline, &app->cmdline_len);
      app->main_mode = MAIN_MODE_NORMAL;
      return;
    }

    if (isprint(ch)) {
      line_append_char(app->cmdline, sizeof(app->cmdline), &app->cmdline_len, ch);
    }
    return;
  }

  if (app->main_mode == MAIN_MODE_ACTION) {
    app->pending_Z = false;
    if (ch == 'y' && selected != NULL) {
      send_request_and_record(app, selected);
      app->main_mode = MAIN_MODE_NORMAL;
      return;
    }
    if (ch == 'e' && selected != NULL) {
      char edited[TUIMAN_BODY_LEN];
      if (launch_editor_and_restore_tui(selected->body, edited, sizeof(edited), ".txt") == 0) {
        if (apply_body_edit_result(app, edited, selected->body, sizeof(selected->body), "Body updated",
                                   "Body updated (JSON formatted)") == 0) {
          request_store_save(&app->paths, selected);
          load_requests(app, selected->id);
        }
      } else {
        set_status(app, "Body edit cancelled or failed");
      }
      app->main_mode = MAIN_MODE_NORMAL;
      return;
    }
    if (ch == 'a' && selected != NULL) {
      enter_new_screen(app, selected, DRAFT_FIELD_AUTH_TYPE);
      app->main_mode = MAIN_MODE_NORMAL;
      return;
    }
    if (ch == 'n' || ch == 27) {
      app->main_mode = MAIN_MODE_NORMAL;
      set_default_main_status(app);
      return;
    }
    return;
  }

  if (app->main_mode == MAIN_MODE_DELETE_CONFIRM) {
    app->pending_Z = false;
    if (ch == 'y') {
      size_t old_visible = app->selected_visible;
      char next_select_id[TUIMAN_ID_LEN] = {0};

      if (app->visible_len > 1) {
        size_t next_visible = 0;
        if (old_visible + 1 < app->visible_len) {
          next_visible = old_visible + 1;
        } else if (old_visible > 0) {
          next_visible = old_visible - 1;
        }

        size_t next_index = app->visible_indices[next_visible];
        if (next_index < app->requests.len) {
          snprintf(next_select_id, sizeof(next_select_id), "%s", app->requests.items[next_index].id);
        }
      }

      if (request_store_delete(&app->paths, app->delete_confirm_id) == 0) {
        char deleted_name[TUIMAN_NAME_LEN];
        snprintf(deleted_name, sizeof(deleted_name), "%s", app->delete_confirm_name);
        load_requests(app, next_select_id[0] != '\0' ? next_select_id : NULL);
        char msg[STATUS_MAX];
        snprintf(msg, sizeof(msg), "Deleted request: %s", deleted_name);
        set_status(app, msg);
      } else {
        set_status(app, "Failed to delete request");
      }

      app->delete_confirm_id[0] = '\0';
      app->delete_confirm_name[0] = '\0';
      app->main_mode = MAIN_MODE_NORMAL;
      return;
    }
    if (ch == 'n' || ch == 27) {
      app->delete_confirm_id[0] = '\0';
      app->delete_confirm_name[0] = '\0';
      app->main_mode = MAIN_MODE_NORMAL;
      set_default_main_status(app);
      return;
    }
    return;
  }

  if (app->pending_Z) {
    if (ch == 'Z' || ch == 'Q') {
      *running = false;
      app->pending_Z = false;
      return;
    }
    app->pending_Z = false;
  }

  if (ch == '{') {
    if (app->request_body_scroll > 0) {
      app->request_body_scroll--;
    }
    return;
  }
  if (ch == '}') {
    app->request_body_scroll++;
    return;
  }
  if (ch == '[') {
    if (app->response_body_scroll > 0) {
      app->response_body_scroll--;
    }
    return;
  }
  if (ch == ']') {
    app->response_body_scroll++;
    return;
  }

  if (ch == '{') {
    if (app->editor_body_scroll > 0) {
      app->editor_body_scroll--;
    }
    return;
  }
  if (ch == '}') {
    app->editor_body_scroll++;
    return;
  }

  if (ch == 'j') {
    app->pending_g = false;
    if (app->visible_len > 0 && app->selected_visible + 1 < app->visible_len) {
      app->selected_visible++;
      app->request_body_scroll = 0;
    }
    return;
  }

  if (ch == 'H') {
    nudge_split_ratio(app, -0.03);
    refresh_resize_status(app);
    return;
  }
  if (ch == 'L') {
    nudge_split_ratio(app, +0.03);
    refresh_resize_status(app);
    return;
  }
  if (ch == 'K') {
    nudge_response_ratio(app, -0.03);
    refresh_resize_status(app);
    return;
  }
  if (ch == 'J') {
    nudge_response_ratio(app, +0.03);
    refresh_resize_status(app);
    return;
  }
  if (ch == 'k') {
    app->pending_g = false;
    if (app->visible_len > 0 && app->selected_visible > 0) {
      app->selected_visible--;
      app->request_body_scroll = 0;
    }
    return;
  }
  if (ch == 'g') {
    if (app->pending_g) {
      app->selected_visible = 0;
      app->scroll = 0;
      app->request_body_scroll = 0;
      app->pending_g = false;
    } else {
      app->pending_g = true;
    }
    return;
  }
  app->pending_g = false;

  if (ch == 'G') {
    if (app->visible_len > 0) {
      app->selected_visible = app->visible_len - 1;
      app->request_body_scroll = 0;
    }
    return;
  }

  if (ch == 'd') {
    if (selected != NULL) {
      snprintf(app->delete_confirm_id, sizeof(app->delete_confirm_id), "%s", selected->id);
      snprintf(app->delete_confirm_name, sizeof(app->delete_confirm_name), "%s", selected->name);
      app->main_mode = MAIN_MODE_DELETE_CONFIRM;
    }
    return;
  }

  if (ch == 'E') {
    if (selected != NULL) {
      enter_new_screen(app, selected, DRAFT_FIELD_NAME);
    }
    return;
  }

  if (ch == '/') {
    app->main_mode = MAIN_MODE_SEARCH;
    line_reset(app->cmdline, &app->cmdline_len);
    return;
  }
  if (ch == '?') {
    app->main_mode = MAIN_MODE_REVERSE;
    line_reset(app->cmdline, &app->cmdline_len);
    return;
  }
  if (ch == ':') {
    app->main_mode = MAIN_MODE_COMMAND;
    line_reset(app->cmdline, &app->cmdline_len);
    return;
  }
  if (ch == '\n' || ch == KEY_ENTER) {
    if (selected != NULL) {
      app->main_mode = MAIN_MODE_ACTION;
    }
    return;
  }
  if (ch == 27) {
    if (app->filter[0] != '\0') {
      app->filter[0] = '\0';
      apply_filter(app, NULL);
      set_default_main_status(app);
    } else {
      set_default_main_status(app);
    }
    return;
  }
  if (ch == 'n') {
    if (app->visible_len > 0 && app->selected_visible + 1 < app->visible_len) {
      app->selected_visible++;
      app->request_body_scroll = 0;
    }
    return;
  }
  if (ch == 'N') {
    if (app->visible_len > 0 && app->selected_visible > 0) {
      app->selected_visible--;
      app->request_body_scroll = 0;
    }
    return;
  }

  if (ch == 'Z') {
    app->pending_Z = true;
    return;
  }
}

static void apply_editor_resize_from_x(app_t *app, int mouse_x, const editor_layout_t *layout) {
  if (!layout->show_right || layout->term_w < (EDITOR_MIN_LEFT_W + EDITOR_MIN_RIGHT_W + 1)) {
    return;
  }

  int min_left = EDITOR_MIN_LEFT_W;
  int max_left = layout->term_w - EDITOR_MIN_RIGHT_W - 1;
  int left = clamp_int(mouse_x, min_left, max_left);
  app->split_ratio = (double)left / (double)layout->term_w;
}

static void handle_new_mouse(app_t *app) {
  MEVENT ev;
  if (getmouse(&ev) != OK) {
    return;
  }

  int h = 0;
  int w = 0;
  getmaxyx(stdscr, h, w);

  editor_layout_t layout;
  compute_editor_layout(app, h, w, &layout);
  if (!layout.valid) {
    app->drag_mode = DRAG_NONE;
    return;
  }

  int on_vertical = layout.show_right && ev.y >= 0 && ev.y < layout.content_h && abs(ev.x - layout.separator_x) <= 2;

  mmask_t release_mask = BUTTON1_RELEASED;
  mmask_t press_mask = BUTTON1_PRESSED;
  mmask_t click_mask = BUTTON1_CLICKED;

  mmask_t drag_mask = REPORT_MOUSE_POSITION;
#ifdef BUTTON1_DRAGGED
  drag_mask |= BUTTON1_DRAGGED;
#endif
#ifdef BUTTON1_MOVED
  drag_mask |= BUTTON1_MOVED;
#endif

  if (ev.bstate & release_mask) {
    app->drag_mode = DRAG_NONE;
    return;
  }

  if (app->drag_mode == DRAG_VERTICAL && (ev.bstate & (press_mask | click_mask | drag_mask))) {
    apply_editor_resize_from_x(app, ev.x, &layout);
    return;
  }

  if (ev.bstate & (press_mask | click_mask)) {
    if (on_vertical) {
      app->drag_mode = DRAG_VERTICAL;
      apply_editor_resize_from_x(app, ev.x, &layout);
      if (ev.bstate & click_mask) {
        app->drag_mode = DRAG_NONE;
      }
      return;
    }
    app->drag_mode = DRAG_NONE;
  }
}

static void execute_new_command(app_t *app, const char *command_line) {
  if (strcmp(command_line, "w") == 0 || strcmp(command_line, "wq") == 0) {
    save_draft(app);
    return;
  }
  if (strcmp(command_line, "q") == 0) {
    app->screen = SCREEN_MAIN;
    return;
  }
  if (strncmp(command_line, "secret ", 7) == 0) {
    const char *value = command_line + 7;
    while (*value == ' ') {
      value++;
    }
    if (app->draft.auth_secret_ref[0] == '\0') {
      set_status(app, "Set Secret Ref first");
      return;
    }
    if (value[0] == '\0') {
      set_status(app, "Usage: :secret VALUE");
      return;
    }
    if (keychain_set_secret(app->draft.auth_secret_ref, value) == 0) {
      set_status(app, "Secret stored in macOS Keychain");
    } else {
      set_status(app, "Failed to store secret in Keychain");
    }
    return;
  }
  set_status(app, "Unknown editor command");
}

static void handle_new_key(app_t *app, int ch) {
  if (ch == KEY_MOUSE) {
    handle_new_mouse(app);
    return;
  }

  if (ch == KEY_RESIZE) {
    return;
  }

  if (app->drag_mode != DRAG_NONE) {
    app->drag_mode = DRAG_NONE;
  }

  if (ch == 27) {
    int next = read_next_key_nowait();
    if (next != ERR) {
      if (next == KEY_BACKSPACE || next == 127 || next == 8) {
        if (app->new_mode == NEW_MODE_COMMAND) {
          line_backspace_word(app->draft_cmdline, &app->draft_cmdline_len);
        } else if (app->new_mode == NEW_MODE_INSERT) {
          line_backspace_word(app->draft_input, &app->draft_input_len);
          draft_set_field_value(app, app->draft_field, app->draft_input);
          if (app->draft_field == DRAFT_FIELD_URL) {
            clear_missing_url_error(app);
          }
        }
        return;
      }

      ungetch(next);
      return;
    }
  }

  if (app->new_mode == NEW_MODE_COMMAND) {
    if (ch == 27) {
      app->new_mode = NEW_MODE_NORMAL;
      line_reset(app->draft_cmdline, &app->draft_cmdline_len);
      return;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      line_backspace(app->draft_cmdline, &app->draft_cmdline_len);
      return;
    }
    if (ch == '\n' || ch == KEY_ENTER) {
      execute_new_command(app, app->draft_cmdline);
      app->new_mode = NEW_MODE_NORMAL;
      line_reset(app->draft_cmdline, &app->draft_cmdline_len);
      return;
    }
    if (isprint(ch)) {
      line_append_char(app->draft_cmdline, sizeof(app->draft_cmdline), &app->draft_cmdline_len, ch);
    }
    return;
  }

  if (app->new_mode == NEW_MODE_INSERT) {
    if (ch == 27) {
      app->new_mode = NEW_MODE_NORMAL;
      return;
    }
    if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
      line_backspace(app->draft_input, &app->draft_input_len);
      draft_set_field_value(app, app->draft_field, app->draft_input);
      if (app->draft_field == DRAFT_FIELD_URL) {
        clear_missing_url_error(app);
      }
      return;
    }
    if (ch == '\n' || ch == KEY_ENTER) {
      draft_set_field_value(app, app->draft_field, app->draft_input);
      if (app->draft_field == DRAFT_FIELD_URL) {
        clear_missing_url_error(app);
      }
      app->new_mode = NEW_MODE_NORMAL;
      return;
    }
    if (isprint(ch)) {
      line_append_char(app->draft_input, sizeof(app->draft_input), &app->draft_input_len, ch);
      draft_set_field_value(app, app->draft_field, app->draft_input);
      if (app->draft_field == DRAFT_FIELD_URL) {
        clear_missing_url_error(app);
      }
    }
    return;
  }

  if (ch == 'j') {
    if (app->draft_field + 1 < DRAFT_FIELD_COUNT) {
      app->draft_field++;
    }
    return;
  }
  if (ch == 'k') {
    if (app->draft_field > 0) {
      app->draft_field--;
    }
    return;
  }
  if (ch == 'h' && app->draft_field == DRAFT_FIELD_METHOD) {
    cycle_method(&app->draft, -1);
    return;
  }
  if (ch == 'l' && app->draft_field == DRAFT_FIELD_METHOD) {
    cycle_method(&app->draft, +1);
    return;
  }
  if (ch == 'i' || ch == '\n' || ch == KEY_ENTER) {
    if (app->draft_field == DRAFT_FIELD_METHOD) {
      set_status(app, "Method uses h/l cycle");
      return;
    }
    snprintf(app->draft_input, sizeof(app->draft_input), "%s", draft_field_value(app, app->draft_field));
    app->draft_input_len = strlen(app->draft_input);
    app->new_mode = NEW_MODE_INSERT;
    return;
  }
  if (ch == 'e') {
    char edited[TUIMAN_BODY_LEN];
    if (launch_editor_and_restore_tui(app->draft.body, edited, sizeof(edited), ".json") == 0) {
      apply_body_edit_result(app, edited, app->draft.body, sizeof(app->draft.body), "Draft body updated",
                             "Draft body updated (JSON formatted)");
      app->editor_body_scroll = 0;
    } else {
      set_status(app, "Body edit cancelled or failed");
    }
    return;
  }
  if (ch == ':') {
    app->new_mode = NEW_MODE_COMMAND;
    line_reset(app->draft_cmdline, &app->draft_cmdline_len);
    return;
  }
  if (ch == 19) {
    save_draft(app);
    return;
  }
  if (ch == 27) {
    app->screen = SCREEN_MAIN;
    set_status(app, "Draft cancelled");
  }
}

static void apply_history_resize_from_x(app_t *app, int mouse_x, const history_layout_t *layout) {
  if (!layout->show_right || layout->term_w < (MAIN_MIN_LEFT_W + MAIN_MIN_RIGHT_W + 1)) {
    return;
  }

  int min_left = MAIN_MIN_LEFT_W;
  int max_left = layout->term_w - MAIN_MIN_RIGHT_W - 1;
  int left = clamp_int(mouse_x, min_left, max_left);
  app->split_ratio = (double)left / (double)layout->term_w;
}

static void handle_history_mouse(app_t *app) {
  MEVENT ev;
  if (getmouse(&ev) != OK) {
    return;
  }

  int h = 0;
  int w = 0;
  getmaxyx(stdscr, h, w);

  history_layout_t layout;
  compute_history_layout(app, h, w, &layout);
  if (!layout.valid) {
    app->drag_mode = DRAG_NONE;
    return;
  }

  int on_vertical = layout.show_right && ev.y >= 0 && ev.y < layout.content_h && abs(ev.x - layout.separator_x) <= 2;

  mmask_t release_mask = BUTTON1_RELEASED;
  mmask_t press_mask = BUTTON1_PRESSED;
  mmask_t click_mask = BUTTON1_CLICKED;

  mmask_t drag_mask = REPORT_MOUSE_POSITION;
#ifdef BUTTON1_DRAGGED
  drag_mask |= BUTTON1_DRAGGED;
#endif
#ifdef BUTTON1_MOVED
  drag_mask |= BUTTON1_MOVED;
#endif

  if (ev.bstate & release_mask) {
    app->drag_mode = DRAG_NONE;
    return;
  }

  if (app->drag_mode == DRAG_VERTICAL && (ev.bstate & (press_mask | click_mask | drag_mask))) {
    apply_history_resize_from_x(app, ev.x, &layout);
    return;
  }

  if (ev.bstate & (press_mask | click_mask)) {
    if (on_vertical) {
      app->drag_mode = DRAG_VERTICAL;
      apply_history_resize_from_x(app, ev.x, &layout);
      if (ev.bstate & click_mask) {
        app->drag_mode = DRAG_NONE;
      }
      return;
    }
    app->drag_mode = DRAG_NONE;
  }
}

static void handle_history_key(app_t *app, int ch) {
  if (ch == KEY_MOUSE) {
    handle_history_mouse(app);
    return;
  }

  if (ch == KEY_RESIZE) {
    return;
  }

  if (app->drag_mode != DRAG_NONE) {
    app->drag_mode = DRAG_NONE;
  }

  if (ch == '{') {
    if (app->history_detail_scroll > 0) {
      app->history_detail_scroll--;
    }
    return;
  }
  if (ch == '}') {
    app->history_detail_scroll++;
    return;
  }

  if (ch == 'H') {
    nudge_split_ratio(app, -0.03);
    return;
  }
  if (ch == 'L') {
    nudge_split_ratio(app, +0.03);
    return;
  }

  if (ch == 27) {
    app->screen = SCREEN_MAIN;
    return;
  }
  if (ch == 'j') {
    if (app->history_selected + 1 < app->runs.len) {
      app->history_selected++;
      app->history_detail_scroll = 0;
    }
    return;
  }
  if (ch == 'k') {
    if (app->history_selected > 0) {
      app->history_selected--;
      app->history_detail_scroll = 0;
    }
    return;
  }
  if (ch == 'r' && app->runs.len > 0) {
    run_entry_t *run = &app->runs.items[app->history_selected];
    request_t req;
    if (request_store_load_by_id(&app->paths, run->request_id, &req) == 0) {
      send_request_and_record(app, &req);
      app->screen = SCREEN_MAIN;
      load_requests(app, req.id);
    } else {
      set_status(app, "Could not load request for replay");
    }
  }
}

static void init_colors(void) {
  if (!has_colors()) {
    return;
  }
  start_color();
  use_default_colors();
  init_pair(COLOR_GET, COLOR_GREEN, COLOR_BLACK);
  init_pair(COLOR_POST, COLOR_YELLOW, COLOR_BLACK);
  init_pair(COLOR_PUT, COLOR_CYAN, COLOR_BLACK);
  init_pair(COLOR_PATCH, COLOR_MAGENTA, COLOR_BLACK);
  init_pair(COLOR_DELETE, COLOR_RED, COLOR_BLACK);
  init_pair(COLOR_STATUS_2XX, COLOR_GREEN, COLOR_BLACK);
  init_pair(COLOR_STATUS_3XX, COLOR_CYAN, COLOR_BLACK);
  init_pair(COLOR_STATUS_4XX, COLOR_YELLOW, COLOR_BLACK);
  init_pair(COLOR_STATUS_5XX, COLOR_RED, COLOR_BLACK);
  init_pair(COLOR_LABEL, COLOR_CYAN, COLOR_BLACK);
  init_pair(COLOR_SECTION, COLOR_BLUE, COLOR_BLACK);
}

int main(void) {
  app_t app;
  memset(&app, 0, sizeof(app));
  app.split_ratio = 0.66;
  app.response_ratio = 0.28;
  app.drag_mode = DRAG_NONE;
  clear_last_response(&app);

  if (paths_init(&app.paths) != 0) {
    fprintf(stderr, "failed to initialize paths\n");
    return 1;
  }

  if (history_store_open(app.paths.history_db, &app.db) != 0) {
    fprintf(stderr, "failed to open history db\n");
    return 1;
  }

  if (http_client_global_init() != 0) {
    fprintf(stderr, "failed to initialize http client\n");
    history_store_close(app.db);
    return 1;
  }

  load_requests(&app, NULL);
  set_default_main_status(&app);
  app.screen = SCREEN_MAIN;
  app.main_mode = MAIN_MODE_NORMAL;

  setenv("ESCDELAY", "25", 1);
  initscr();
  cbreak();
  noecho();
  keypad(stdscr, TRUE);
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  mouseinterval(0);
  enable_extended_mouse_tracking();
  init_colors();

  bool running = true;
  while (running) {
    if (app.screen == SCREEN_MAIN) {
      draw_main(&app);
      int ch = getch();
      handle_main_key(&app, &running, ch);
    } else if (app.screen == SCREEN_NEW) {
      draw_new_editor(&app);
      int ch = getch();
      handle_new_key(&app, ch);
    } else if (app.screen == SCREEN_HISTORY) {
      draw_history(&app);
      int ch = getch();
      handle_history_key(&app, ch);
    } else if (app.screen == SCREEN_HELP) {
      draw_help();
      int ch = getch();
      if (ch == 27) {
        app.screen = SCREEN_MAIN;
      }
    }
  }

  disable_extended_mouse_tracking();
  endwin();

  run_list_free(&app.runs);
  request_list_free(&app.requests);
  free(app.visible_indices);
  clear_last_response(&app);
  history_store_close(app.db);
  http_client_global_cleanup();
  return 0;
}
