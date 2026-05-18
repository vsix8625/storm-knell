#ifndef SK_CMD_STRIKE_H_
#define SK_CMD_STRIKE_H_

#include "sk_eval.h"
#include "vx_defs.h"

#include "storm-knell.h"

struct sk_manifest_header
{
    u32 target_count;
    u32 global_cache_hits;
    u32 global_cache_misses;
    u32 global_compile_errors;
};

struct sk_target_persist
{
    char           name[64];
    char           out_dir[256];
    char           bin_path[256];
    char           bin_dirpath[256];
    sk_target_kind kind;
    u32            total_files;
    u64            last_strike_ts;
};

vx_status sk_cmd_strike_fn(struct sk_ctx *ctx);

#endif  // SK_CMD_STRIKE_H_
