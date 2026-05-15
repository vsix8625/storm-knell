#ifndef SK_XXHASH_H_
#define SK_XXHASH_H_

#include "vx_defs.h"

#include <string.h>

#define SK_XXHASH_LEN 8

struct sk_target;
struct sk_meta;
struct mem_arena;

struct sk_hash_input
{
    const char    *source_path;
    struct sk_cfg *cfg;

    vx_sv source;
    vx_sv cmd;
    vx_sv sk_version;
};

vx_status
sk_xxh3_hash(struct sk_hash_input *input, u8 out_hash[SK_XXHASH_LEN], struct mem_arena *arena);

void sk_xxh3_hash_merge(u8 h1[SK_XXHASH_LEN], u8 h2[SK_XXHASH_LEN], u8 out[SK_XXHASH_LEN]);

vx_status sk_hash_setup(struct sk_target     *t,
                        u32                   source_idx,
                        struct sk_meta       *meta,
                        struct sk_hash_input *hsh_input,
                        u8                    out_hash[SK_XXHASH_LEN],
                        struct mem_arena     *arena);

static inline bool sk_hash_eq(const u8 h1[SK_XXHASH_LEN], u8 h2[SK_XXHASH_LEN])
{
    return memcmp(h1, h2, SK_XXHASH_LEN) == 0;
}

#endif  // SK_XXHASH_H_
