#include "sk_globals.h"

char g_sk_profile_buf[VX_PATH_MAX];

vx_sbuf g_sk_profile_sbuf = {.data   = g_sk_profile_buf,
                             .size   = sizeof(g_sk_profile_buf),
                             .offset = 0};

struct sk_cache_proj_entry *g_sk_cache_records;
_Atomic u32                 g_sk_cache_record_count;
