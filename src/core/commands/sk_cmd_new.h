#ifndef SK_CMD_NEW_H_
#define SK_CMD_NEW_H_

#include "vx_defs.h"

#include "storm-knell.h"

vx_status sk_cmd_new_file(struct sk_ctx *ctx, const char *path);

void sk_cmd_init_fn(struct sk_ctx *ctx);
void sk_cmd_purge_fn(struct sk_ctx *ctx);

#endif  // SK_CMD_NEW_H_
