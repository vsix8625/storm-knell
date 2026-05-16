#ifndef SK_INVOKE_H_
#define SK_INVOKE_H_

#include "vx_defs.h"
#include "sk_xxhash.h"

struct sk_target;
struct mem_arena;

char  *sk_invoke_compile(struct sk_target *t, u32 source_idx);
char **sk_invoke_compile_nularr(struct sk_target *t, u32 source_idx, struct mem_arena *arena);
char **sk_invoke_link_nularr(struct sk_target *t, struct mem_arena *arena);
char **sk_invoke_ar_nularr(struct sk_target *t, struct sk_meta *meta, struct mem_arena *arena);
char **sk_invoke_syntax_check_nularr(struct sk_target *t, u32 source_idx, struct mem_arena *arena);

#endif  // SK_INVOKE_H_
