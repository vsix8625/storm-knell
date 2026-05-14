#ifndef SK_INVOKE_H_
#define SK_INVOKE_H_

#include "vx_defs.h"
#include "sk_xxhash.h"

struct sk_target;

char  *sk_invoke_compile(struct sk_target *t, u32 source_idx);
char **sk_invoke_compile_nularr(struct sk_target *t, u32 source_idx);

#endif  // SK_INVOKE_H_
