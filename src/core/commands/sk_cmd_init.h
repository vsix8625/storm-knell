#ifndef SK_CMD_INIT_H_
#define SK_CMD_INIT_H_

#include "vx_limits.h"

struct sk_ctx;

struct sk_meta
{
    char cc_name[VX_BUF_SIZE_256];
    char cc_path[VX_PATH_MAX];
    char cc_ver[VX_BUF_SIZE_256];
    char ar_path[VX_PATH_MAX];
};

void sk_cmd_init_fn(struct sk_ctx *ctx);

void sk_meta_init_cc(const char *cc_name, const char *rpath);
bool sk_meta_load(struct sk_meta *meta, const char *target_cc);

#endif  // SK_CMD_INIT_H_
