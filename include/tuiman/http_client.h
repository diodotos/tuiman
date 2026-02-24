#ifndef TUIMAN_HTTP_CLIENT_H
#define TUIMAN_HTTP_CLIENT_H

#include <stddef.h>

#include "tuiman/request_store.h"

typedef struct {
  long status_code;
  long duration_ms;
  char *body;
  size_t body_len;
  char error[256];
} http_response_t;

int http_client_global_init(void);
void http_client_global_cleanup(void);

int http_send_request(const request_t *req, http_response_t *out);
void http_response_free(http_response_t *response);

#endif
