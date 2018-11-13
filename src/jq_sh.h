#ifndef JQ_SH_H
#define JQ_SH_H

#include "jq.h"

jv jq_sh(const char *cmd, int raw);

char *jq_sh_extract_cmdstr(jv cmd);
extern const char *JQ_SH_EXTRACT_CMDSTR_ERRMSG;

#endif
