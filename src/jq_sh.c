#include "jq_sh.h"

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "jv_unicode.h"

jv jq_sh(const char *cmd, int raw) {
  FILE* pipe = popen(cmd, "r");
  if (pipe == NULL) {
    return jv_invalid_with_msg(jv_string_fmt("Could not run command: %s\n %s",
                                             cmd,
                                             strerror(errno)));
  }

  struct jv_parser* parser = NULL;
  jv data;

  if (raw) {
    data = jv_string("");
  } else {
    data = jv_array();
    parser = jv_parser_new(0);
  }

  // To avoid mangling UTF-8 multi-byte sequences that cross the end of our read
  // buffer, we need to be able to read the remainder of a sequence and add that
  // before appending.
  const int max_utf8_len = 4;
  char buf[4096+max_utf8_len];
  while (!feof(pipe) && !ferror(pipe)) {
    size_t n = fread(buf, 1, sizeof(buf)-max_utf8_len, pipe);
    int len = 0;

    if (n == 0)
      continue;
    if (jvp_utf8_backtrack(buf+(n-1), buf, &len) && len > 0 &&
        !feof(pipe) && !ferror(pipe)) {
      n += fread(buf+n, 1, len, pipe);
    }

    if (raw) {
      data = jv_string_append_buf(data, buf, n);
    } else {
      jv_parser_set_buf(parser, buf, n, !feof(pipe));
      jv value;
      while (jv_is_valid((value = jv_parser_next(parser))))
        data = jv_array_append(data, value);
      if (jv_invalid_has_msg(jv_copy(value))) {
        jv_free(data);
        data = value;
        break;
      }
    }
  }
  if (!raw)
      jv_parser_free(parser);
  int badread = ferror(pipe);
  int status, rc;
  if ((status = pclose(pipe)) != 0 || badread) {
    jv_free(data);
    if (status == ECHILD) {
      return jv_invalid_with_msg(jv_string_fmt("Cannot obtain exit code executing command: %s",
                                               cmd));
    } else if (WIFEXITED(status) && (rc = WEXITSTATUS(status)) != 0) {
      return jv_invalid_with_msg(jv_string_fmt("Exit code (%d) isn't zero executing command: %s",
                                               rc, cmd));
    } else {
      return jv_invalid_with_msg(jv_string_fmt("Error executing command: %s\n %s",
                                               cmd,
                                               strerror(errno)));
    }
  }
  return data;
}
