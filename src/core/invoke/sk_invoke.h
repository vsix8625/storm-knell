#ifndef SK_INVOKE_H_
#define SK_INVOKE_H_

#include "vx_defs.h"
#include "sk_xxhash.h"

struct sk_target;
struct mem_arena;

struct sk_ccmds_entry
{
    const char  *file;
    const char  *directory;
    const char **arguments;
    u32          arg_count;
};

char *sk_invoke_compile(struct sk_target *t, u32 source_idx);

char **sk_invoke_compile_nularr(struct sk_target *t,
                                u32               source_idx,
                                struct mem_arena *arena,
                                u32              *out_count);

char **sk_invoke_link_nularr(struct sk_target *t, struct mem_arena *arena);
char **sk_invoke_ar_nularr(struct sk_target *t, struct sk_meta *meta, struct mem_arena *arena);

char **sk_invoke_syntax_check_nularr(struct sk_target *t,
                                     u32               source_idx,
                                     struct mem_arena *arena,
                                     u32              *arg_count);

vx_status sk_ccmds_write(const char *rpath);

void sk_ccmds_push(const char *file, const char *directory, const char **argv, u32 arg_count);

#endif  // SK_INVOKE_H_
