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
#define PREVIEW_MAX 2048
#define DEFAULT_MAIN_STATUS "j/k move | / search | : command | Enter actions | d delete"

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

  screen_t screen;
  main_mode_t main_mode;
  new_mode_t new_mode;
  bool pending_g;

  char cmdline[CMDLINE_MAX];
  size_t cmdline_len;

  char status[STATUS_MAX];

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
  char last_response_body[PREVIEW_MAX];
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
  COLOR_JSON_STRING = 10,
  COLOR_JSON_NUMBER = 11,
  COLOR_JSON_PUNCT = 12,
  COLOR_JSON_BRACE = 13,
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

static void set_status(app_t *app, const char *message) {
  snprintf(app->status, sizeof(app->status), "%s", message);
}

static void set_default_main_status(app_t *app) {
  set_status(app, DEFAULT_MAIN_STATUS);
}

static void now_iso(char out[40]) {
  time_t now = time(NULL);
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);
  strftime(out, 40, "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
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

static void win_draw_wrapped_text(WINDOW *win, int start_y, int start_x, int max_lines, int max_width,
                                  const char *text) {
  if (win == NULL || text == NULL || max_lines <= 0 || max_width <= 0) {
    return;
  }

  int wh = 0;
  int ww = 0;
  getmaxyx(win, wh, ww);
  if (start_x < 0) {
    start_x = 0;
  }
  if (start_x >= ww || start_y >= wh) {
    return;
  }

  int width = max_width;
  if (width > ww - start_x) {
    width = ww - start_x;
  }
  if (width <= 0) {
    return;
  }

  const char *p = text;
  for (int row = 0; row < max_lines; row++) {
    int y = start_y + row;
    if (y < 0 || y >= wh) {
      break;
    }
    if (*p == '\0') {
      break;
    }

    while (*p == '\r') {
      p++;
    }

    int len = 0;
    while (p[len] != '\0' && p[len] != '\n' && p[len] != '\r' && len < width) {
      len++;
    }

    if (len > 0) {
      mvwaddnstr(win, y, start_x, p, len);
      p += len;
    }

    if (*p == '\r') {
      p++;
      if (*p == '\n') {
        p++;
      }
    } else if (*p == '\n') {
      p++;
    }
  }
}

static void win_draw_wrapped_jsonish(WINDOW *win, int start_y, int start_x, int max_lines, int max_width,
                                     const char *text, int colorize_json) {
  if (win == NULL || text == NULL || max_lines <= 0 || max_width <= 0) {
    return;
  }

  int wh = 0;
  int ww = 0;
  getmaxyx(win, wh, ww);
  if (start_x < 0) {
    start_x = 0;
  }
  if (start_x >= ww || start_y >= wh) {
    return;
  }

  int width = max_width;
  if (width > ww - start_x) {
    width = ww - start_x;
  }
  if (width <= 0) {
    return;
  }

  const char *p = text;
  int in_string = 0;
  int escaped = 0;

  for (int row = 0; row < max_lines; row++) {
    int y = start_y + row;
    if (y < 0 || y >= wh) {
      break;
    }
    if (*p == '\0') {
      break;
    }

    while (*p == '\r') {
      p++;
    }

    int col = 0;
    while (*p != '\0' && *p != '\n' && *p != '\r' && col < width) {
      char c = *p;
      int pair = 0;

      if (colorize_json) {
        if (in_string) {
          pair = COLOR_JSON_STRING;
          if (escaped) {
            escaped = 0;
          } else if (c == '\\') {
            escaped = 1;
          } else if (c == '"') {
            in_string = 0;
          }
        } else {
          if (c == '"') {
            pair = COLOR_JSON_STRING;
            in_string = 1;
            escaped = 0;
          } else if (c == '{' || c == '}' || c == '[' || c == ']') {
            pair = COLOR_JSON_BRACE;
          } else if (c == ':' || c == ',') {
            pair = COLOR_JSON_PUNCT;
          } else if (isdigit((unsigned char)c) || c == '-') {
            pair = COLOR_JSON_NUMBER;
          } else if (c == 't' || c == 'f' || c == 'n') {
            pair = COLOR_JSON_NUMBER;
          }
        }
      }

      if (pair != 0 && has_colors()) {
        wattron(win, COLOR_PAIR(pair));
      }
      mvwaddch(win, y, start_x + col, (chtype)c);
      if (pair != 0 && has_colors()) {
        wattroff(win, COLOR_PAIR(pair));
      }

      p++;
      col++;
    }

    if (*p == '\r') {
      p++;
      if (*p == '\n') {
        p++;
      }
    } else if (*p == '\n') {
      p++;
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

  int status_h = 1;
  int available_h = h - status_h;
  if (available_h < 3 || w < 24) {
    erase();
    mvprintw(h - 1, 0, "Window too small");
    refresh();
    return;
  }

  int response_h = 0;
  if (available_h >= 14) {
    response_h = 8;
  } else if (available_h >= 10) {
    response_h = 5;
  }

  int horizontal_sep_y = -1;
  if (response_h > 0) {
    horizontal_sep_y = available_h - response_h - 1;
    if (horizontal_sep_y < 4) {
      response_h = 0;
      horizontal_sep_y = -1;
    }
  }

  int top_h = response_h > 0 ? horizontal_sep_y : available_h;
  int response_y = response_h > 0 ? horizontal_sep_y + 1 : -1;
  if (top_h < 2) {
    erase();
    mvprintw(h - 1, 0, "Window too small");
    refresh();
    return;
  }

  int left_w = w;
  int separator_x = -1;
  int rx = 0;
  int rw = 0;
  int show_right = 0;

  if (w >= 58) {
    left_w = (w * 2) / 3;
    if (left_w < 24) {
      left_w = 24;
    }
    if (left_w > w - 18) {
      left_w = w - 18;
    }

    separator_x = left_w;
    rx = separator_x + 1;
    rw = w - rx;
    if (rw >= 20) {
      show_right = 1;
    } else {
      left_w = w;
      separator_x = -1;
      rx = 0;
      rw = 0;
    }
  }

  erase();

  WINDOW *left_win = newwin(top_h, left_w, 0, 0);
  WINDOW *right_win = NULL;
  if (show_right) {
    right_win = newwin(top_h, rw, 0, rx);
  }
  WINDOW *response_win = NULL;
  if (response_h > 0) {
    response_win = newwin(response_h, w, response_y, 0);
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

  int method_x = left_w / 2;
  if (method_x < 8) {
    method_x = 8;
  }
  int url_x = method_x + 8;

  win_add_text(left_win, 0, 1, "Name");
  win_add_text(left_win, 0, method_x, "Type");
  win_add_text(left_win, 0, url_x, "URL");

  int view_rows = top_h - 1;
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
      mvwhline(left_win, y, 0, ' ', left_w);
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

    int url_space = left_w - (url_x + 1);
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
    win_add_text(right_win, 0, 0, "Request Preview");
    if (has_colors()) {
      wattron(right_win, COLOR_PAIR(COLOR_JSON_PUNCT));
    }
    mvwhline(right_win, 1, 0, ACS_HLINE, rw);
    if (has_colors()) {
      wattroff(right_win, COLOR_PAIR(COLOR_JSON_PUNCT));
    }

    if (selected == NULL) {
      win_draw_wrapped_text(right_win, 2, 0, top_h - 2, rw, "No requests. Use :new to create one.");
    } else {
      int url_w = rw - 10;
      int auth_w = rw - 6;
      int header_val_w = rw - 30;
      if (url_w < 4) {
        url_w = 4;
      }
      if (auth_w < 4) {
        auth_w = 4;
      }
      if (header_val_w < 1) {
        header_val_w = 1;
      }

      int method_pair = method_color_pair(selected->method);
      if (method_pair != 0 && has_colors()) {
        wattron(right_win, COLOR_PAIR(method_pair));
      }
      win_printf_text(right_win, 2, 0, "%.*s", 8, selected->method);
      if (method_pair != 0 && has_colors()) {
        wattroff(right_win, COLOR_PAIR(method_pair));
      }
      win_printf_text(right_win, 2, 6, "%.*s", url_w, selected->url);
      win_printf_text(right_win, 3, 0, "auth=%.*s", auth_w, selected->auth_type[0] ? selected->auth_type : "none");

      if (selected->header_key[0] == '\0' && selected->header_value[0] == '\0') {
        win_add_text(right_win, 4, 0, "headers=none");
      } else {
        win_printf_text(right_win, 4, 0, "header=%.*s: %.*s", 20, selected->header_key, header_val_w,
                        selected->header_value);
      }
      win_add_text(right_win, 6, 0, "Body:");

      int body_start = 7;
      int body_lines = top_h - body_start;
      if (body_lines > 0) {
        int colorize_json = should_treat_body_as_json(selected->body);
        win_draw_wrapped_jsonish(right_win, body_start, 0, body_lines, rw, selected->body, colorize_json);
      }
    }
  } else {
    win_add_text(left_win, 1, 1, "Preview hidden (window too narrow)");
    win_add_text(left_win, 2, 1, "Widen terminal to restore split-pane view.");
  }

  if (show_right) {
    for (int y = 0; y < top_h; y++) {
      mvaddch(y, separator_x, ACS_VLINE);
    }
  }

  if (response_win != NULL) {
    win_add_text(response_win, 0, 0, "Response");
    if (has_colors()) {
      wattron(response_win, COLOR_PAIR(COLOR_JSON_PUNCT));
    }
    mvwhline(response_win, 1, 0, ACS_HLINE, w);
    if (has_colors()) {
      wattroff(response_win, COLOR_PAIR(COLOR_JSON_PUNCT));
    }

    if (app->last_response_at[0] == '\0') {
      win_add_text(response_win, 2, 0, "No response yet. Select a request, press Enter, then y.");
    } else {
      win_printf_text(response_win, 2, 0, "%s %s", app->last_response_method, app->last_response_url);
      win_printf_text(response_win, 3, 0, "request=%s", app->last_response_request_name);
      win_printf_text(response_win, 4, 0, "at=%s", app->last_response_at);

      wmove(response_win, 5, 0);
      waddstr(response_win, "status=");
      int s_pair = status_color_pair(app->last_response_status);
      if (s_pair != 0 && has_colors()) {
        wattron(response_win, COLOR_PAIR(s_pair));
      }
      wprintw(response_win, "%ld", app->last_response_status);
      if (s_pair != 0 && has_colors()) {
        wattroff(response_win, COLOR_PAIR(s_pair));
      }
      wprintw(response_win, "  duration=%ldms", app->last_response_ms);

      int body_row = 6;
      if (app->last_response_error[0] != '\0' && body_row < response_h) {
        if (has_colors()) {
          wattron(response_win, COLOR_PAIR(COLOR_STATUS_5XX));
        }
        win_printf_text(response_win, body_row, 0, "error=%s", app->last_response_error);
        if (has_colors()) {
          wattroff(response_win, COLOR_PAIR(COLOR_STATUS_5XX));
        }
        body_row++;
      }

      if (body_row < response_h) {
        win_add_text(response_win, body_row, 0, "Body:");
        body_row++;
      }
      if (body_row < response_h) {
        int lines = response_h - body_row;
        int colorize_json = should_treat_body_as_json(app->last_response_body);
        win_draw_wrapped_jsonish(response_win, body_row, 0, lines, w, app->last_response_body, colorize_json);
      }
    }
  }

  if (horizontal_sep_y >= 0) {
    mvhline(horizontal_sep_y, 0, ACS_HLINE, w);
    if (show_right && separator_x >= 0 && separator_x < w) {
      mvaddch(horizontal_sep_y, separator_x, ACS_PLUS);
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


static void draw_help(void) {
  int h = 0;
  int w = 0;
  getmaxyx(stdscr, h, w);
  erase();

  mvprintw(1, 2, "tuiman help");
  mvprintw(3, 2, "Main: j/k gg G / ? : Enter d Esc n N");
  mvprintw(4, 2, "Actions: y send, e edit body, a edit auth");
  mvprintw(5, 2, "Commands: :new [METHOD] [URL], :history, :export [DIR], :import [DIR], :help, :q");
  mvprintw(6, 2, "New request editor: j/k move, i edit field, e edit body, :w save, :q cancel, :secret VALUE");
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
    set_status(app, "URL cannot be empty");
    return -1;
  }

  request_set_updated_now(&app->draft);
  if (request_store_save(&app->paths, &app->draft) != 0) {
    set_status(app, "Failed to save request");
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

static void draw_new_editor(app_t *app) {
  int h = 0;
  int w = 0;
  getmaxyx(stdscr, h, w);
  erase();

  int left_w = w / 2;
  if (left_w < 40) {
    left_w = w - 20;
  }

  mvprintw(0, 1, "New Request Editor");
  mvprintw(0, left_w + 2, "Preview");
  for (int y = 0; y < h - 1; y++) {
    mvaddch(y, left_w, ACS_VLINE);
  }

  int row = 2;
  for (int i = 0; i < DRAFT_FIELD_COUNT; i++) {
    if (i == app->draft_field && app->new_mode == NEW_MODE_NORMAL) {
      attron(A_REVERSE);
    }
    mvprintw(row + i, 2, "%-14s: %-*.*s", draft_field_label(i), left_w - 20, left_w - 20, draft_field_value(app, i));
    if (i == app->draft_field && app->new_mode == NEW_MODE_NORMAL) {
      attroff(A_REVERSE);
    }
  }

  mvprintw(row + DRAFT_FIELD_COUNT + 1, 2, "Body bytes: %zu", strlen(app->draft.body));

  int rx = left_w + 2;
  int rw = w - rx - 1;
  int url_w = rw - 10;
  int header_val_w = rw - 30;
  int auth_w = rw - 6;
  int secret_w = rw - 12;
  if (url_w < 4) {
    url_w = 4;
  }
  if (header_val_w < 1) {
    header_val_w = 1;
  }
  if (auth_w < 4) {
    auth_w = 4;
  }
  if (secret_w < 4) {
    secret_w = 4;
  }

  mvprintw(2, rx, "%.*s %.*s", 8, app->draft.method, url_w, app->draft.url);
  mvprintw(3, rx, "name=%.*s", rw - 6, app->draft.name);
  mvprintw(4, rx, "header=%.*s: %.*s", 18, app->draft.header_key, header_val_w, app->draft.header_value);
  mvprintw(5, rx, "auth=%.*s", auth_w, app->draft.auth_type);
  mvprintw(6, rx, "secret_ref=%.*s", secret_w, app->draft.auth_secret_ref);
  mvprintw(8, rx, "Body preview:");

  int max_lines = h - 12;
  if (max_lines < 2) {
    max_lines = 2;
  }
  draw_multiline_text(9, rx, max_lines, rw, app->draft.body);

  move(h - 1, 0);
  clrtoeol();
  curs_set(0);

  if (app->new_mode == NEW_MODE_INSERT) {
    mvprintw(h - 1, 0, "-- INSERT -- %s", app->draft_input);
    move(h - 1, 13 + (int)app->draft_input_len);
    curs_set(1);
  } else if (app->new_mode == NEW_MODE_COMMAND) {
    mvprintw(h - 1, 0, ":%s", app->draft_cmdline);
    move(h - 1, 1 + (int)app->draft_cmdline_len);
    curs_set(1);
  } else {
    mvprintw(h - 1, 0,
             "NEW NORMAL | j/k move | i/Enter edit | h/l method cycle | e edit body | :w save | :q cancel | :secret VALUE");
  }

  refresh();
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
  snprintf(app->last_response_body, sizeof(app->last_response_body), "%s", response.body ? response.body : "");

  run_entry_t run;
  memset(&run, 0, sizeof(run));
  snprintf(run.request_id, sizeof(run.request_id), "%s", req->id);
  snprintf(run.request_name, sizeof(run.request_name), "%s", req->name);
  snprintf(run.method, sizeof(run.method), "%s", req->method);
  snprintf(run.url, sizeof(run.url), "%s", req->url);
  run.status_code = (int)response.status_code;
  run.duration_ms = response.duration_ms;
  snprintf(run.error, sizeof(run.error), "%s", response.error);
  now_iso(run.created_at);
  history_store_add_run(app->db, &run);

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
}

static void draw_history(app_t *app) {
  int h = 0;
  int w = 0;
  getmaxyx(stdscr, h, w);
  erase();

  int left_w = w / 2;
  if (left_w < 40) {
    left_w = w - 20;
  }
  mvprintw(0, 1, "History");
  for (int y = 0; y < h - 1; y++) {
    mvaddch(y, left_w, ACS_VLINE);
  }
  mvprintw(1, 1, "When                 Method  Status   ms   Name");

  int rows = h - 3;
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
    int y = i + 2;
    if (idx == app->history_selected) {
      attron(A_REVERSE);
      mvhline(y, 0, ' ', left_w);
    }
    mvprintw(y, 1, "%-20.20s %-7.7s %-7d %-5ld %-20.20s", run->created_at, run->method, run->status_code,
             run->duration_ms, run->request_name);
    if (idx == app->history_selected) {
      attroff(A_REVERSE);
    }
  }

  int rx = left_w + 2;
  if (app->runs.len == 0) {
    mvprintw(2, rx, "No history yet");
  } else {
    run_entry_t *run = &app->runs.items[app->history_selected];
    mvprintw(2, rx, "%s %s", run->method, run->url);
    mvprintw(3, rx, "status=%d duration=%ldms", run->status_code, run->duration_ms);
    mvprintw(4, rx, "request_id=%s", run->request_id);
    mvprintw(5, rx, "error=%s", run->error[0] ? run->error : "-");
  }

  mvprintw(h - 1, 0, "HISTORY | j/k move | r replay selected | Esc back");
  refresh();
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
  line_reset(app->draft_input, &app->draft_input_len);
  line_reset(app->draft_cmdline, &app->draft_cmdline_len);
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
  request_t *selected = selected_request(app);

  if (app->main_mode == MAIN_MODE_SEARCH || app->main_mode == MAIN_MODE_REVERSE ||
      app->main_mode == MAIN_MODE_COMMAND) {
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

  if (ch == 'j') {
    app->pending_g = false;
    if (app->visible_len > 0 && app->selected_visible + 1 < app->visible_len) {
      app->selected_visible++;
    }
    return;
  }
  if (ch == 'k') {
    app->pending_g = false;
    if (app->visible_len > 0 && app->selected_visible > 0) {
      app->selected_visible--;
    }
    return;
  }
  if (ch == 'g') {
    if (app->pending_g) {
      app->selected_visible = 0;
      app->scroll = 0;
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
    }
    return;
  }
  if (ch == 'N') {
    if (app->visible_len > 0 && app->selected_visible > 0) {
      app->selected_visible--;
    }
    return;
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
      return;
    }
    if (ch == '\n' || ch == KEY_ENTER) {
      draft_set_field_value(app, app->draft_field, app->draft_input);
      app->new_mode = NEW_MODE_NORMAL;
      return;
    }
    if (isprint(ch)) {
      line_append_char(app->draft_input, sizeof(app->draft_input), &app->draft_input_len, ch);
      draft_set_field_value(app, app->draft_field, app->draft_input);
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

static void handle_history_key(app_t *app, int ch) {
  if (ch == 27) {
    app->screen = SCREEN_MAIN;
    return;
  }
  if (ch == 'j') {
    if (app->history_selected + 1 < app->runs.len) {
      app->history_selected++;
    }
    return;
  }
  if (ch == 'k') {
    if (app->history_selected > 0) {
      app->history_selected--;
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
  init_pair(COLOR_JSON_STRING, COLOR_YELLOW, COLOR_BLACK);
  init_pair(COLOR_JSON_NUMBER, COLOR_CYAN, COLOR_BLACK);
  init_pair(COLOR_JSON_PUNCT, COLOR_BLUE, COLOR_BLACK);
  init_pair(COLOR_JSON_BRACE, COLOR_MAGENTA, COLOR_BLACK);
}

int main(void) {
  app_t app;
  memset(&app, 0, sizeof(app));

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

  endwin();

  run_list_free(&app.runs);
  request_list_free(&app.requests);
  free(app.visible_indices);
  history_store_close(app.db);
  http_client_global_cleanup();
  return 0;
}
