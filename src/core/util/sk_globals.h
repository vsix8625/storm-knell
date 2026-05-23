#ifndef SK_GLOBALS_H_
#define SK_GLOBALS_H_

#include "vx_limits.h"

extern struct sk_ctx g_sk_global_ctx;

extern struct mem_arena *g_sk_global_arena;

extern vx_sbuf g_sk_profile_sbuf;
extern char    g_sk_profile_buf[VX_PATH_MAX];

extern _Atomic u32            g_sk_ccmds_count;
extern struct sk_ccmds_entry *g_sk_ccmds;

extern struct sk_cache_proj_entry *g_sk_cache_records;
extern _Atomic u32                 g_sk_cache_record_count;

#endif  // SK_GLOBALS_H_
