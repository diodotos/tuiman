#include "tuiman/keychain_macos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TUIMAN_KEYCHAIN_SERVICE "tuiman"

static int shell_escape(const char *in, char *out, size_t out_len) {
  size_t oi = 0;
  if (out_len < 3) {
    return -1;
  }

  out[oi++] = '\'';
  for (size_t i = 0; in[i] != '\0'; i++) {
    if (in[i] == '\'') {
      if (oi + 4 >= out_len) {
        return -1;
      }
      out[oi++] = '\'';
      out[oi++] = '\\';
      out[oi++] = '\'';
      out[oi++] = '\'';
      continue;
    }

    if (oi + 1 >= out_len) {
      return -1;
    }
    out[oi++] = in[i];
  }

  if (oi + 1 >= out_len) {
    return -1;
  }
  out[oi++] = '\'';
  out[oi] = '\0';
  return 0;
}

int keychain_set_secret(const char *secret_ref, const char *value) {
  char account_esc[512];
  char value_esc[4096];
  char service_esc[64];
  char command[8192];

  if (shell_escape(secret_ref, account_esc, sizeof(account_esc)) != 0) {
    return -1;
  }
  if (shell_escape(value, value_esc, sizeof(value_esc)) != 0) {
    return -1;
  }
  if (shell_escape(TUIMAN_KEYCHAIN_SERVICE, service_esc, sizeof(service_esc)) != 0) {
    return -1;
  }

  if (snprintf(command, sizeof(command),
               "/usr/bin/security add-generic-password -a %s -s %s -w %s -U >/dev/null 2>&1", account_esc,
               service_esc, value_esc) < 0) {
    return -1;
  }

  return system(command) == 0 ? 0 : -1;
}

int keychain_get_secret(const char *secret_ref, char *out, size_t out_len) {
  char account_esc[512];
  char service_esc[64];
  char command[2048];

  if (out_len == 0) {
    return -1;
  }
  out[0] = '\0';

  if (shell_escape(secret_ref, account_esc, sizeof(account_esc)) != 0) {
    return -1;
  }
  if (shell_escape(TUIMAN_KEYCHAIN_SERVICE, service_esc, sizeof(service_esc)) != 0) {
    return -1;
  }

  if (snprintf(command, sizeof(command), "/usr/bin/security find-generic-password -a %s -s %s -w 2>/dev/null",
               account_esc, service_esc) < 0) {
    return -1;
  }

  FILE *pipe = popen(command, "r");
  if (pipe == NULL) {
    return -1;
  }

  if (fgets(out, (int)out_len, pipe) == NULL) {
    pclose(pipe);
    return -1;
  }

  pclose(pipe);

  size_t len = strlen(out);
  while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r')) {
    out[--len] = '\0';
  }

  return len > 0 ? 0 : -1;
}

int keychain_delete_secret(const char *secret_ref) {
  char account_esc[512];
  char service_esc[64];
  char command[2048];

  if (shell_escape(secret_ref, account_esc, sizeof(account_esc)) != 0) {
    return -1;
  }
  if (shell_escape(TUIMAN_KEYCHAIN_SERVICE, service_esc, sizeof(service_esc)) != 0) {
    return -1;
  }

  if (snprintf(command, sizeof(command),
               "/usr/bin/security delete-generic-password -a %s -s %s >/dev/null 2>&1", account_esc,
               service_esc) < 0) {
    return -1;
  }

  int rc = system(command);
  return rc == 0 ? 0 : -1;
}
