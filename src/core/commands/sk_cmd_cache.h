#ifndef SK_CMD_CACHE_H_
#define SK_CMD_CACHE_H_

#include "vx_limits.h"

struct sk_ctx;

struct sk_cache_config
{
    u32 max_size_mb;
    u32 prune_threshold_mb;
};

struct sk_evict_item
{
    char name[VX_BUF_SIZE_128];
    u32  name_len;

    u64 size_bytes;
    u64 access_time;
};

struct sk_cache_info
{
    u64 total_size;
    u64 object_count;
};

vx_status sk_global_config_resolve_path(char *out_buf, size_t buf_len);
vx_status sk_cache_config_write(const char *config_path, const struct sk_cache_config *ccfg);
vx_status sk_cache_config_load(const char *config_path, struct sk_cache_config *cfg);

void sk_cache_prune_to_size(u32 prune_threshold_mb);

void sk_cache_config_init_global(struct sk_cache_config *cfg);

struct sk_cache_info sk_cache_calculate_size(void);

vx_status sk_cmd_cache_fn(struct sk_ctx *ctx);

u64 sk_cache_get_size(void);

u64 sk_cache_get_obj_count(void);

#endif  // SK_CMD_CACHE_H_
