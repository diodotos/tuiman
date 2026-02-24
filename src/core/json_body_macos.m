#include "tuiman/json_body.h"

#include <stdio.h>
#include <string.h>

#import <Foundation/Foundation.h>

static void write_error(char *out, size_t out_len, const char *message) {
  if (out == NULL || out_len == 0) {
    return;
  }
  snprintf(out, out_len, "%s", message ? message : "unknown error");
}

static void write_parse_error_with_location(const char *input, NSError *error, char *out, size_t out_len) {
  NSString *description = error.localizedDescription ?: @"Invalid JSON";
  NSDictionary *user_info = error.userInfo;
  NSNumber *index_number = user_info[@"NSJSONSerializationErrorIndex"];

  if (index_number == nil || input == NULL) {
    write_error(out, out_len, description.UTF8String);
    return;
  }

  size_t index = (size_t)index_number.unsignedLongLongValue;
  size_t input_len = strlen(input);
  if (index > input_len) {
    index = input_len;
  }

  size_t line = 1;
  size_t col = 1;
  for (size_t i = 0; i < index; i++) {
    if (input[i] == '\n') {
      line++;
      col = 1;
    } else {
      col++;
    }
  }

  char message[256];
  snprintf(message, sizeof(message), "%s (line %zu, col %zu)", description.UTF8String, line, col);
  write_error(out, out_len, message);
}

int json_body_validate_and_pretty(const char *input, char *out, size_t out_len, char *error_out,
                                  size_t error_out_len) {
  if (input == NULL || out == NULL || out_len == 0) {
    write_error(error_out, error_out_len, "invalid arguments");
    return -1;
  }

  @autoreleasepool {
    NSData *data = [NSData dataWithBytes:input length:strlen(input)];
    NSError *parse_error = nil;
    id json = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingAllowFragments error:&parse_error];
    if (json == nil) {
      write_parse_error_with_location(input, parse_error, error_out, error_out_len);
      return -1;
    }

    NSJSONWritingOptions options = NSJSONWritingPrettyPrinted;
    if (@available(macOS 10.13, *)) {
      options |= NSJSONWritingSortedKeys;
    }

    NSError *write_error_ns = nil;
    NSData *pretty_data = [NSJSONSerialization dataWithJSONObject:json options:options error:&write_error_ns];
    if (pretty_data == nil) {
      NSString *description = write_error_ns.localizedDescription ?: @"failed to serialize JSON";
      write_error(error_out, error_out_len, description.UTF8String);
      return -1;
    }

    NSString *pretty_string = [[NSString alloc] initWithData:pretty_data encoding:NSUTF8StringEncoding];
    if (pretty_string == nil) {
      write_error(error_out, error_out_len, "failed to decode formatted JSON");
      return -1;
    }

    const char *formatted = pretty_string.UTF8String;
    size_t formatted_len = strlen(formatted);

    int has_trailing_newline = formatted_len > 0 && formatted[formatted_len - 1] == '\n';
    size_t needed = formatted_len + (has_trailing_newline ? 1 : 2);
    if (needed > out_len) {
      write_error(error_out, error_out_len, "formatted JSON exceeds body size limit");
      return -1;
    }

    if (has_trailing_newline) {
      snprintf(out, out_len, "%s", formatted);
    } else {
      snprintf(out, out_len, "%s\n", formatted);
    }
  }

  if (error_out != NULL && error_out_len > 0) {
    error_out[0] = '\0';
  }
  return 0;
}
