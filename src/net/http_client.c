#include "tuiman/http_client.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tuiman/keychain_macos.h"

typedef struct {
  char *data;
  size_t len;
} mem_buffer_t;

static size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
  size_t chunk_size = size * nmemb;
  mem_buffer_t *buffer = (mem_buffer_t *)userdata;

  char *next = realloc(buffer->data, buffer->len + chunk_size + 1);
  if (next == NULL) {
    return 0;
  }

  buffer->data = next;
  memcpy(buffer->data + buffer->len, ptr, chunk_size);
  buffer->len += chunk_size;
  buffer->data[buffer->len] = '\0';

  return chunk_size;
}

static int append_query_param(const char *url, const char *key, const char *value, char *out, size_t out_len) {
  const char *sep = strchr(url, '?') ? "&" : "?";
  char temp[TUIMAN_URL_LEN + TUIMAN_HEADER_KEY_LEN + TUIMAN_HEADER_VAL_LEN + 8];
  if (snprintf(temp, sizeof(temp), "%s%s%s=%s", url, sep, key, value) < 0) {
    return -1;
  }
  if (snprintf(out, out_len, "%s", temp) < 0) {
    return -1;
  }
  return 0;
}

int http_client_global_init(void) {
  return curl_global_init(CURL_GLOBAL_DEFAULT) == 0 ? 0 : -1;
}

void http_client_global_cleanup(void) {
  curl_global_cleanup();
}

int http_send_request(const request_t *req, http_response_t *out) {
  memset(out, 0, sizeof(*out));
  out->status_code = 0;

  CURL *curl = curl_easy_init();
  if (curl == NULL) {
    snprintf(out->error, sizeof(out->error), "failed to initialize libcurl");
    return -1;
  }

  char url_buffer[TUIMAN_URL_LEN + TUIMAN_HEADER_KEY_LEN + TUIMAN_HEADER_VAL_LEN + 8];
  snprintf(url_buffer, sizeof(url_buffer), "%s", req->url);

  struct curl_slist *headers = NULL;
  char auth_secret[4096] = {0};

  if (req->header_key[0] != '\0') {
    char line[TUIMAN_HEADER_KEY_LEN + TUIMAN_HEADER_VAL_LEN + 8];
    snprintf(line, sizeof(line), "%s: %s", req->header_key, req->header_value);
    headers = curl_slist_append(headers, line);
  }

  if ((strcmp(req->auth_type, "bearer") == 0 || strcmp(req->auth_type, "jwt") == 0) &&
      req->auth_secret_ref[0] != '\0') {
    if (keychain_get_secret(req->auth_secret_ref, auth_secret, sizeof(auth_secret)) == 0) {
      char auth_line[4200];
      snprintf(auth_line, sizeof(auth_line), "Authorization: Bearer %s", auth_secret);
      headers = curl_slist_append(headers, auth_line);
    }
  } else if (strcmp(req->auth_type, "api_key") == 0 && req->auth_secret_ref[0] != '\0') {
    if (keychain_get_secret(req->auth_secret_ref, auth_secret, sizeof(auth_secret)) == 0) {
      const char *key_name = req->auth_key_name[0] != '\0' ? req->auth_key_name : "X-API-Key";
      const char *location = req->auth_location[0] != '\0' ? req->auth_location : "header";
      if (strcmp(location, "query") == 0) {
        append_query_param(url_buffer, key_name, auth_secret, url_buffer, sizeof(url_buffer));
      } else {
        char header_line[4200];
        snprintf(header_line, sizeof(header_line), "%s: %s", key_name, auth_secret);
        headers = curl_slist_append(headers, header_line);
      }
    }
  } else if (strcmp(req->auth_type, "basic") == 0 && req->auth_secret_ref[0] != '\0') {
    if (keychain_get_secret(req->auth_secret_ref, auth_secret, sizeof(auth_secret)) == 0) {
      curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
      curl_easy_setopt(curl, CURLOPT_USERNAME, req->auth_username);
      curl_easy_setopt(curl, CURLOPT_PASSWORD, auth_secret);
    }
  }

  mem_buffer_t response = {.data = malloc(1), .len = 0};
  if (response.data == NULL) {
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    snprintf(out->error, sizeof(out->error), "out of memory");
    return -1;
  }
  response.data[0] = '\0';

  curl_easy_setopt(curl, CURLOPT_URL, url_buffer);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, req->method);

  if (headers != NULL) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  if (req->body[0] != '\0') {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(req->body));
  }

  CURLcode rc = curl_easy_perform(curl);
  if (rc != CURLE_OK) {
    snprintf(out->error, sizeof(out->error), "%s", curl_easy_strerror(rc));
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &out->status_code);
  double total_seconds = 0.0;
  curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_seconds);
  out->duration_ms = (long)(total_seconds * 1000.0);

  out->body = response.data;
  out->body_len = response.len;

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  return rc == CURLE_OK ? 0 : -1;
}

void http_response_free(http_response_t *response) {
  if (response == NULL) {
    return;
  }
  free(response->body);
  response->body = NULL;
  response->body_len = 0;
}
