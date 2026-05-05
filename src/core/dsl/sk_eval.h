#ifndef SK_EVAL_H_
#define SK_EVAL_H_

#include "vx_defs.h"
#include "storm-knell.h"
#include "sk_lexer.h"
#include "sk_parser.h"

#define SK_MAX_TARGETS 64

struct sk_cfg
{
    u32 cc;
    u32 linker;
    u32 cflags;
    u32 lflags;
    u32 defines;
    u32 libs;
    u32 lib_paths;
};

struct sk_target
{
    u32 name_tok;
    u32 out_name;
    u32 build_dir;

    u32 source_toks[256];
    u32 depend_toks[32];

    u32 source_count;
    u32 depend_count;

    struct sk_cfg cfg;
};

struct sk_eval_result
{
    struct sk_cfg     global;
    struct sk_target *targets;

    u32 target_count;
};

vx_status sk_eval_init(struct sk_ctx *ctx, struct sk_eval_result *result);

vx_status sk_eval(struct sk_ctx *ctx, struct sk_parser *p, struct sk_eval_result *result);

void sk_dbg_dump_eval(struct sk_ctx *ctx, struct sk_parser *p, struct sk_eval_result *result);

static inline vx_sv tok_to_sv(struct sk_parser *p, struct sk_ctx *ctx, u32 tok_idx)
{
    vx_sv sv;
    sv.data = ctx->stormfile.data + p->tokens->offsets[tok_idx];
    sv.len  = p->tokens->lens[tok_idx];
    return sv;
}

#endif  // SK_EVAL_H_
