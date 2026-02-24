#ifndef TUIMAN_KEYCHAIN_MACOS_H
#define TUIMAN_KEYCHAIN_MACOS_H

#include <stddef.h>

int keychain_set_secret(const char *secret_ref, const char *value);
int keychain_get_secret(const char *secret_ref, char *out, size_t out_len);
int keychain_delete_secret(const char *secret_ref);

#endif
