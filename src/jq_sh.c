#include "jq_sh.h"

#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "jv_unicode.h"
#include "jv_alloc.h"

const char *JQ_SH_EXTRACT_CMDSTR_ERRMSG = "sh argument must be string or array";

static char *clone_str(const char *src) {
  int len = strlen(src);
  char *buff = jv_mem_calloc(len+1, sizeof(char));
  strcpy(buff, src);
  return buff;
}

static void copy_str(char **target, int index, const char *src) {
  target[index] = clone_str(src);
}

/** caller should free the pointer with jv_mem_free */
char *jq_sh_extract_cmdstr(jv cmd) {
  switch (jv_get_kind(cmd)) {
  case JV_KIND_STRING: return clone_str(jv_string_value(cmd));
  case JV_KIND_ARRAY: {
    int argc = jv_array_length(jv_copy(cmd)), i, j;
    char **argv = jv_mem_calloc(argc, sizeof(char*));
    char **escaped_argv = jv_mem_calloc(argc, sizeof(char*));

    // collect stringified arguments into to argv
    for (i=0;i<argc;++i) {
      jv val = jv_array_get(jv_copy(cmd), i);
      jv_kind kind = jv_get_kind(val);
      if (kind == JV_KIND_STRING) {
        copy_str(argv, i, jv_string_value(val));
      } else {
        jv str = jv_dump_string(jv_copy(val), 0);
        copy_str(argv, i, jv_string_value(str));
        jv_free(str);
      }
      jv_free(val);
    }

    // result length
    int rlen = 0;

    // escape single quotes in argv, saving results to escaped_argv
    // free memory claimed by argv and each of its element also
    //
    // each single quote is translated to '\'' (four characters in total)
    for (i=0;i<argc;++i) {
      char *arg = argv[i];
      int len = strlen(arg);
      int escaped_len = len + 2;
      for (j=0;j<len;++j) if (arg[j] == '\'') escaped_len += 3;
      char *buff = escaped_argv[i] = jv_mem_calloc(escaped_len+1, sizeof(char));
      int off = 0;
      buff[off++] = '\'';
      for (j=0;j<len;++j) {
        char orig = arg[j];
        if (orig == '\'') { buff[off++] = '\''; buff[off++] = '\\'; buff[off++] = '\''; buff[off++] = '\''; }
        else { buff[off++] = orig; }
      }
      buff[off++] = '\'';
      buff[off++] = '\0';
      rlen += escaped_len + 1;  // the +1 at the ends counts for the separator for joining later

      jv_mem_free(arg);
    }
    jv_mem_free(argv);

    // join esaped_argv into rbuff, separated by single space
    // free memory claimed by escaped_argv and each of its element also
    char *rbuff = jv_mem_calloc(rlen+1, sizeof(char)); int off = 0;
    for (i=0;i<argc;++i) {
      char *arg = escaped_argv[i];
      int len = strlen(arg);
      for (j=0;j<len;++j) rbuff[off++] = arg[j];
      rbuff[off++] = ' ';

      jv_mem_free(arg);
    } rbuff[off++] = '\0';
    jv_mem_free(escaped_argv);

    return rbuff;
  }

  // we return NULL as the indication of a kind error. see also JQ_SH_EXTRACT_CMDSTR_ERRMSG
  default: return NULL;
  }
}

// adopted from jv_load_file
jv jq_sh(const char *cmd, int raw, int verbose) {

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
  return data;
}
