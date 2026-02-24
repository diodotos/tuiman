#include "tuiman/request_store.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <limits.h>
#include <uuid/uuid.h>

static int has_json_suffix(const char *name) {
  size_t len = strlen(name);
  return len > 5 && strcmp(name + len - 5, ".json") == 0;
}

static int read_file_to_buffer(const char *path, char **out_buffer) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return -1;
  }

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return -1;
  }

  long size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return -1;
  }
  rewind(fp);

  char *buffer = (char *)malloc((size_t)size + 1);
  if (buffer == NULL) {
    fclose(fp);
    return -1;
  }

  size_t read_count = fread(buffer, 1, (size_t)size, fp);
  fclose(fp);
  if (read_count != (size_t)size) {
    free(buffer);
    return -1;
  }

  buffer[size] = '\0';
  *out_buffer = buffer;
  return 0;
}

static void json_escape(const char *in, char *out, size_t out_len) {
  size_t oi = 0;
  for (size_t i = 0; in[i] != '\0' && oi + 1 < out_len; i++) {
    unsigned char c = (unsigned char)in[i];
    if (c == '"' || c == '\\') {
      if (oi + 2 >= out_len) {
        break;
      }
      out[oi++] = '\\';
      out[oi++] = (char)c;
    } else if (c == '\n') {
      if (oi + 2 >= out_len) {
        break;
      }
      out[oi++] = '\\';
      out[oi++] = 'n';
    } else if (c == '\r') {
      if (oi + 2 >= out_len) {
        break;
      }
      out[oi++] = '\\';
      out[oi++] = 'r';
    } else if (c == '\t') {
      if (oi + 2 >= out_len) {
        break;
      }
      out[oi++] = '\\';
      out[oi++] = 't';
    } else if (isprint(c)) {
      out[oi++] = (char)c;
    }
  }
  out[oi] = '\0';
}

static int json_extract_string(const char *json, const char *key, char *out, size_t out_len) {
  char needle[96];
  if (snprintf(needle, sizeof(needle), "\"%s\"", key) < 0) {
    return -1;
  }

  const char *p = strstr(json, needle);
  if (p == NULL) {
    return -1;
  }

  p = strchr(p, ':');
  if (p == NULL) {
    return -1;
  }
  p++;
  while (*p != '\0' && isspace((unsigned char)*p)) {
    p++;
  }

  if (*p != '"') {
    return -1;
  }
  p++;

  size_t oi = 0;
  while (*p != '\0' && *p != '"' && oi + 1 < out_len) {
    if (*p == '\\') {
      p++;
      if (*p == '\0') {
        break;
      }
      switch (*p) {
      case 'n':
        out[oi++] = '\n';
        break;
      case 'r':
        out[oi++] = '\r';
        break;
      case 't':
        out[oi++] = '\t';
        break;
      case '\\':
      case '"':
        out[oi++] = *p;
        break;
      default:
        out[oi++] = *p;
        break;
      }
      p++;
      continue;
    }

    out[oi++] = *p;
    p++;
  }

  out[oi] = '\0';
  return 0;
}

void request_generate_id(char out[TUIMAN_ID_LEN]) {
  uuid_t uuid;
  uuid_generate_random(uuid);
  uuid_unparse_lower(uuid, out);
}

void request_set_updated_now(request_t *req) {
  time_t now = time(NULL);
  struct tm tm_utc;
  gmtime_r(&now, &tm_utc);
  strftime(req->updated_at, sizeof(req->updated_at), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);
}

void request_init_defaults(request_t *req) {
  memset(req, 0, sizeof(*req));
  request_generate_id(req->id);
  strncpy(req->name, "New Request", sizeof(req->name) - 1);
  strncpy(req->method, "GET", sizeof(req->method) - 1);
  strncpy(req->auth_type, "none", sizeof(req->auth_type) - 1);
  request_set_updated_now(req);
}

int request_store_write_file(const char *file_path, const request_t *req) {
  char id_esc[TUIMAN_ID_LEN * 2] = {0};
  char name_esc[TUIMAN_NAME_LEN * 2] = {0};
  char method_esc[TUIMAN_METHOD_LEN * 2] = {0};
  char url_esc[TUIMAN_URL_LEN * 2] = {0};
  char header_key_esc[TUIMAN_HEADER_KEY_LEN * 2] = {0};
  char header_value_esc[TUIMAN_HEADER_VAL_LEN * 2] = {0};
  char auth_type_esc[TUIMAN_AUTH_TYPE_LEN * 2] = {0};
  char auth_secret_ref_esc[TUIMAN_SECRET_REF_LEN * 2] = {0};
  char auth_key_name_esc[TUIMAN_HEADER_KEY_LEN * 2] = {0};
  char auth_location_esc[TUIMAN_AUTH_LOC_LEN * 2] = {0};
  char auth_username_esc[TUIMAN_AUTH_USER_LEN * 2] = {0};
  char updated_esc[TUIMAN_UPDATED_AT_LEN * 2] = {0};
  char body_esc[TUIMAN_BODY_LEN * 2] = {0};

  json_escape(req->id, id_esc, sizeof(id_esc));
  json_escape(req->name, name_esc, sizeof(name_esc));
  json_escape(req->method, method_esc, sizeof(method_esc));
  json_escape(req->url, url_esc, sizeof(url_esc));
  json_escape(req->header_key, header_key_esc, sizeof(header_key_esc));
  json_escape(req->header_value, header_value_esc, sizeof(header_value_esc));
  json_escape(req->auth_type, auth_type_esc, sizeof(auth_type_esc));
  json_escape(req->auth_secret_ref, auth_secret_ref_esc, sizeof(auth_secret_ref_esc));
  json_escape(req->auth_key_name, auth_key_name_esc, sizeof(auth_key_name_esc));
  json_escape(req->auth_location, auth_location_esc, sizeof(auth_location_esc));
  json_escape(req->auth_username, auth_username_esc, sizeof(auth_username_esc));
  json_escape(req->updated_at, updated_esc, sizeof(updated_esc));
  json_escape(req->body, body_esc, sizeof(body_esc));

  char tmp_path[PATH_MAX];
  if (snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", file_path) < 0) {
    return -1;
  }

  FILE *fp = fopen(tmp_path, "wb");
  if (fp == NULL) {
    return -1;
  }

  int wrote = fprintf(fp,
                      "{\n"
                      "  \"id\": \"%s\",\n"
                      "  \"name\": \"%s\",\n"
                      "  \"method\": \"%s\",\n"
                      "  \"url\": \"%s\",\n"
                      "  \"header_key\": \"%s\",\n"
                      "  \"header_value\": \"%s\",\n"
                      "  \"body\": \"%s\",\n"
                      "  \"auth_type\": \"%s\",\n"
                      "  \"auth_secret_ref\": \"%s\",\n"
                      "  \"auth_key_name\": \"%s\",\n"
                      "  \"auth_location\": \"%s\",\n"
                      "  \"auth_username\": \"%s\",\n"
                      "  \"updated_at\": \"%s\"\n"
                      "}\n",
                      id_esc, name_esc, method_esc, url_esc, header_key_esc, header_value_esc, body_esc,
                      auth_type_esc, auth_secret_ref_esc, auth_key_name_esc, auth_location_esc, auth_username_esc,
                      updated_esc);
  fclose(fp);

  if (wrote < 0) {
    unlink(tmp_path);
    return -1;
  }

  if (rename(tmp_path, file_path) != 0) {
    unlink(tmp_path);
    return -1;
  }

  return 0;
}

int request_store_read_file(const char *file_path, request_t *out) {
  char *json = NULL;
  if (read_file_to_buffer(file_path, &json) != 0) {
    return -1;
  }

  request_init_defaults(out);
  json_extract_string(json, "id", out->id, sizeof(out->id));
  json_extract_string(json, "name", out->name, sizeof(out->name));
  json_extract_string(json, "method", out->method, sizeof(out->method));
  json_extract_string(json, "url", out->url, sizeof(out->url));
  json_extract_string(json, "header_key", out->header_key, sizeof(out->header_key));
  json_extract_string(json, "header_value", out->header_value, sizeof(out->header_value));
  json_extract_string(json, "body", out->body, sizeof(out->body));
  json_extract_string(json, "auth_type", out->auth_type, sizeof(out->auth_type));
  json_extract_string(json, "auth_secret_ref", out->auth_secret_ref, sizeof(out->auth_secret_ref));
  json_extract_string(json, "auth_key_name", out->auth_key_name, sizeof(out->auth_key_name));
  json_extract_string(json, "auth_location", out->auth_location, sizeof(out->auth_location));
  json_extract_string(json, "auth_username", out->auth_username, sizeof(out->auth_username));
  json_extract_string(json, "updated_at", out->updated_at, sizeof(out->updated_at));

  if (out->id[0] == '\0') {
    request_generate_id(out->id);
  }

  free(json);
  return 0;
}

static int request_compare_name(const void *lhs, const void *rhs) {
  const request_t *a = (const request_t *)lhs;
  const request_t *b = (const request_t *)rhs;
  return strcasecmp(a->name, b->name);
}

int request_store_list(const app_paths_t *paths, request_list_t *out) {
  out->items = NULL;
  out->len = 0;

  DIR *dir = opendir(paths->requests_dir);
  if (dir == NULL) {
    return -1;
  }

  struct dirent *entry = NULL;
  while ((entry = readdir(dir)) != NULL) {
    if (!has_json_suffix(entry->d_name)) {
      continue;
    }

    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/%s", paths->requests_dir, entry->d_name) < 0) {
      continue;
    }

    request_t item;
    if (request_store_read_file(path, &item) != 0) {
      continue;
    }

    request_t *next = realloc(out->items, (out->len + 1) * sizeof(request_t));
    if (next == NULL) {
      closedir(dir);
      request_list_free(out);
      return -1;
    }

    out->items = next;
    out->items[out->len] = item;
    out->len++;
  }

  closedir(dir);

  if (out->len > 1) {
    qsort(out->items, out->len, sizeof(request_t), request_compare_name);
  }

  return 0;
}

int request_store_load_by_id(const app_paths_t *paths, const char *request_id, request_t *out) {
  char path[PATH_MAX];
  if (snprintf(path, sizeof(path), "%s/%s.json", paths->requests_dir, request_id) < 0) {
    return -1;
  }
  return request_store_read_file(path, out);
}

int request_store_save(const app_paths_t *paths, const request_t *req) {
  request_t copy = *req;
  if (copy.id[0] == '\0') {
    request_generate_id(copy.id);
  }
  request_set_updated_now(&copy);

  char path[PATH_MAX];
  if (snprintf(path, sizeof(path), "%s/%s.json", paths->requests_dir, copy.id) < 0) {
    return -1;
  }
  return request_store_write_file(path, &copy);
}

int request_store_delete(const app_paths_t *paths, const char *request_id) {
  char path[PATH_MAX];
  if (snprintf(path, sizeof(path), "%s/%s.json", paths->requests_dir, request_id) < 0) {
    return -1;
  }

  if (unlink(path) != 0 && errno != ENOENT) {
    return -1;
  }

  return 0;
}

void request_list_free(request_list_t *list) {
  if (list == NULL) {
    return;
  }
  free(list->items);
  list->items = NULL;
  list->len = 0;
}
