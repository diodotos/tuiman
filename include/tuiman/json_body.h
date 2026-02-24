#ifndef TUIMAN_JSON_BODY_H
#define TUIMAN_JSON_BODY_H

#include <stddef.h>

int json_body_validate_and_pretty(const char *input, char *out, size_t out_len, char *error_out,
                                  size_t error_out_len);

#endif
