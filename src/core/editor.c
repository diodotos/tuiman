#include "tuiman/editor.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int read_file_to_buffer(const char *path, char *out, size_t out_len) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return -1;
  }

  size_t used = fread(out, 1, out_len - 1, fp);
  out[used] = '\0';
  fclose(fp);
  return 0;
}

int edit_text_with_editor(const char *initial_text, char *out, size_t out_len, const char *suffix) {
  char template_path[256];
  snprintf(template_path, sizeof(template_path), "/tmp/tuiman-edit-XXXXXX%s", suffix ? suffix : "");

  int fd = mkstemps(template_path, suffix ? (int)strlen(suffix) : 0);
  if (fd < 0) {
    return -1;
  }

  FILE *fp = fdopen(fd, "wb");
  if (fp == NULL) {
    close(fd);
    unlink(template_path);
    return -1;
  }

  if (initial_text != NULL) {
    fwrite(initial_text, 1, strlen(initial_text), fp);
  }
  fclose(fp);

  const char *editor = getenv("VISUAL");
  if (editor == NULL || editor[0] == '\0') {
    editor = getenv("EDITOR");
  }
  if (editor == NULL || editor[0] == '\0') {
    editor = "vi";
  }

  char command[1024];
  if (snprintf(command, sizeof(command), "%s %s", editor, template_path) < 0) {
    unlink(template_path);
    return -1;
  }

  int rc = system(command);
  if (rc != 0) {
    unlink(template_path);
    return -1;
  }

  int read_rc = read_file_to_buffer(template_path, out, out_len);
  unlink(template_path);
  return read_rc;
}
