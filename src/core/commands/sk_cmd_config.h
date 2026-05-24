#pragma once

#include "vx_defs.h"

struct sk_ctx;

vx_status sk_cmd_config_fn(struct sk_ctx *ctx);
vx_status sk_config_add_cc_path_b(const char *cc_realpath);
