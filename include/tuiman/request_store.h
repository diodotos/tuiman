#ifndef TUIMAN_REQUEST_STORE_H
#define TUIMAN_REQUEST_STORE_H

#include <stddef.h>

#include "tuiman/paths.h"

#define TUIMAN_ID_LEN 37
#define TUIMAN_NAME_LEN 128
#define TUIMAN_METHOD_LEN 16
#define TUIMAN_URL_LEN 512
#define TUIMAN_HEADER_KEY_LEN 128
#define TUIMAN_HEADER_VAL_LEN 256
#define TUIMAN_AUTH_TYPE_LEN 32
#define TUIMAN_SECRET_REF_LEN 128
#define TUIMAN_AUTH_LOC_LEN 32
#define TUIMAN_AUTH_USER_LEN 128
#define TUIMAN_BODY_LEN 8192
#define TUIMAN_UPDATED_AT_LEN 40

typedef struct {
  char id[TUIMAN_ID_LEN];
  char name[TUIMAN_NAME_LEN];
  char method[TUIMAN_METHOD_LEN];
  char url[TUIMAN_URL_LEN];
  char header_key[TUIMAN_HEADER_KEY_LEN];
  char header_value[TUIMAN_HEADER_VAL_LEN];
  char body[TUIMAN_BODY_LEN];
  char auth_type[TUIMAN_AUTH_TYPE_LEN];
  char auth_secret_ref[TUIMAN_SECRET_REF_LEN];
  char auth_key_name[TUIMAN_HEADER_KEY_LEN];
  char auth_location[TUIMAN_AUTH_LOC_LEN];
  char auth_username[TUIMAN_AUTH_USER_LEN];
  char updated_at[TUIMAN_UPDATED_AT_LEN];
} request_t;

typedef struct {
  request_t *items;
  size_t len;
} request_list_t;

void request_init_defaults(request_t *req);
void request_generate_id(char out[TUIMAN_ID_LEN]);
void request_set_updated_now(request_t *req);

int request_store_list(const app_paths_t *paths, request_list_t *out);
int request_store_load_by_id(const app_paths_t *paths, const char *request_id, request_t *out);
int request_store_save(const app_paths_t *paths, const request_t *req);
int request_store_delete(const app_paths_t *paths, const char *request_id);
void request_list_free(request_list_t *list);

int request_store_read_file(const char *file_path, request_t *out);
int request_store_write_file(const char *file_path, const request_t *req);

#endif
