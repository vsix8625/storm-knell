#include "sk_eval.h"
#include "mem_arena.h"
#include "sk_globals.h"

#include "vx_io.h"
#include "vx_util.h"
#include <string.h>

vx_status sk_eval_init(struct sk_ctx *ctx, struct sk_eval_result *result)
{
    if (ctx == nullptr || result == nullptr)
    {
        return VX_ERROR;
    }

    result->targets = mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_target) * SK_MAX_TARGETS);

    if (result->targets == nullptr)
    {
        VX_ASSERT_LOG("Failed to allocate targets");
        return VX_FATAL;
    }

    memset(result->targets, 0, sizeof(struct sk_target) * SK_MAX_TARGETS);
    memset(&result->global, 0, sizeof(struct sk_cfg));

    result->target_count = 0;

    return VX_OK;
}

//----------------------------------------------------------------------------------------------------

static void cfg_set_or_append(struct sk_parser *p, u32 *field, u32 val_node, bool append)
{
    if (append && *field != 0)
    {
        u32 last = *field;
        while (p->nodes->nexts[last] != 0)
        {
            last = p->nodes->nexts[last];
        }
        p->nodes->nexts[last] = val_node;
    }
    else
    {
        *field = val_node;
    }
}

static void eval_cfg(struct sk_parser *p, u32 node, struct sk_cfg *cfg)
{
    u32 tok_idx = p->nodes->token_idxs[node];

    sk_token_kind tok_kind = p->tokens->kinds[tok_idx];

    bool append = p->nodes->kinds[node] == SK_NODE_APPEND;

    // get first value token index
    u32 val_node = p->nodes->data_a[node];
    if (val_node == 0)
    {
        return;
    }
    u32 val_tok = p->nodes->token_idxs[val_node];

    switch (tok_kind)
    {
        case SK_TOKEN_KWORD_CC:
        case SK_TOKEN_KWORD_COMPILER:
        {
            cfg->cc = val_tok;
            break;
        }

        case SK_TOKEN_KWORD_LINKER:
        {
            cfg->linker = val_tok;
            break;
        }

        case SK_TOKEN_KWORD_CFLAGS:
        {
            cfg_set_or_append(p, &cfg->cflags, val_node, append);
            break;
        }

        case SK_TOKEN_KWORD_LFLAGS:
        {
            cfg_set_or_append(p, &cfg->lflags, val_node, append);
            break;
        }

        case SK_TOKEN_KWORD_DEFINES:
        {
            cfg_set_or_append(p, &cfg->defines, val_node, append);
            break;
        }

        default:
        {
            break;
        }
    }
}

static bool eval_expr(struct sk_parser *p, u32 node)
{
    VX_CAST(void, p);
    VX_CAST(void, node);
    vx_warn("eval_expr: not implemented yet");
    return false;
}

static void eval_if(struct sk_parser *p, u32 node, struct sk_target *target)
{
    u32 cond_node = p->nodes->data_a[node];

    bool result = eval_expr(p, cond_node);

    if (result)
    {
        // walk then body — data_b
        u32 child = p->nodes->data_b[node];

        while (child != 0)
        {
            sk_ast_node_kind kind = p->nodes->kinds[child];
            switch (kind)
            {
                case SK_NODE_ASSIGN:
                case SK_NODE_APPEND:
                {
                    eval_cfg(p, child, &target->cfg);
                    break;
                }

                case SK_NODE_IF:
                {
                    eval_if(p, child, target);
                    break;
                }

                default:
                {
                    break;
                }
            }
            child = p->nodes->nexts[child];
        }
    }
    else if (p->nodes->data_c[node] != 0)
    {
        // else block — data_c
        u32              child = p->nodes->data_c[node];
        sk_ast_node_kind kind  = p->nodes->kinds[child];
        if (kind == SK_NODE_IF)
        {
            // else if
            eval_if(p, child, target);
        }
        else
        {
            // else body
            while (child != 0)
            {
                kind = p->nodes->kinds[child];
                switch (kind)
                {
                    case SK_NODE_ASSIGN:
                    case SK_NODE_APPEND:
                    {
                        eval_cfg(p, child, &target->cfg);
                        break;
                    }
                    case SK_NODE_IF:
                    {
                        eval_if(p, child, target);
                        break;
                    }

                    default:
                    {
                        break;
                    }
                }
                child = p->nodes->nexts[child];
            }
        }
    }
}

static void eval_target(struct sk_parser *p, u32 node, struct sk_target *target)
{
    target->name_tok = p->nodes->data_a[node];

    // walk body
    u32 child = p->nodes->data_b[node];

    while (child != 0)
    {
        sk_ast_node_kind kind = p->nodes->kinds[child];

        switch (kind)
        {
            case SK_NODE_ASSIGN:
            case SK_NODE_APPEND:
            {
                u32 tok_idx = p->nodes->token_idxs[child];
                if (p->tokens->kinds[tok_idx] == SK_TOKEN_KWORD_SOURCES)
                {
                    // collect sources
                    u32 src = p->nodes->data_a[child];
                    while (src != 0 && target->source_count < 256)
                    {
                        target->source_toks[target->source_count++] = p->nodes->token_idxs[src];
                        src                                         = p->nodes->nexts[src];
                    }
                }
                else if (p->tokens->kinds[tok_idx] == SK_TOKEN_KWORD_DEPENDS)
                {
                    u32 dep = p->nodes->data_a[child];
                    while (dep != 0 && target->depend_count < 32)
                    {
                        target->depend_toks[target->depend_count++] = p->nodes->token_idxs[dep];
                        dep                                         = p->nodes->nexts[dep];
                    }
                }
                else
                {
                    eval_cfg(p, child, &target->cfg);
                }
                break;
            }

            case SK_NODE_FN_CALL:
            {
                break;
            }

            case SK_NODE_IF:
            {
                eval_if(p, child, target);
                break;
            }
            default:
            {
                break;
            }
        }
        child = p->nodes->nexts[child];
    }
}

vx_status sk_eval(struct sk_ctx *ctx, struct sk_parser *p, struct sk_eval_result *result)
{
    if (ctx == nullptr || p == nullptr || result == nullptr)
    {
        return VX_ERROR;
    }

    u32 node = p->nodes->data_a[1];  // program first child
    while (node != 0)
    {
        sk_ast_node_kind kind = p->nodes->kinds[node];
        switch (kind)
        {
            case SK_NODE_ASSIGN:
            case SK_NODE_APPEND:
            {
                eval_cfg(p, node, &result->global);
                break;
            }
            case SK_NODE_TARGET:
            {
                if (result->target_count >= SK_MAX_TARGETS)
                {
                    vx_errlog("Too many targets, max is %u", SK_MAX_TARGETS);
                    return VX_ERROR;
                }

                eval_target(p, node, &result->targets[result->target_count++]);
                break;
            }

            default:
            {
                break;
            }
        }

        node = p->nodes->nexts[node];
    }
    return VX_OK;
}

static inline vx_sv cfg_val_to_sv(struct sk_parser *p, struct sk_ctx *ctx, u32 node_idx)
{
    if (node_idx == 0)
    {
        return (vx_sv) {.data = "(none)", .len = 6};
    }

    u32 tok_idx = p->nodes->token_idxs[node_idx];
    return tok_to_sv(p, ctx, tok_idx);
}

void sk_dbg_dump_eval(struct sk_ctx *ctx, struct sk_parser *p, struct sk_eval_result *result)
{
    if (ctx == nullptr || p == nullptr || result == nullptr)
    {
        return;
    }

    vx_sv cc      = cfg_val_to_sv(p, ctx, result->global.cc);
    vx_sv linker  = cfg_val_to_sv(p, ctx, result->global.linker);
    vx_sv cflags  = cfg_val_to_sv(p, ctx, result->global.cflags);
    vx_sv lflags  = cfg_val_to_sv(p, ctx, result->global.lflags);
    vx_sv defines = cfg_val_to_sv(p, ctx, result->global.defines);

    vx_printf("=== EVAL DUMP ===\n");
    vx_printf("global:\n");
    vx_printf("  cc:      %.*s\n", (i32) cc.len, cc.data);
    vx_printf("  linker:  %.*s\n", (i32) linker.len, linker.data);
    vx_printf("  cflags:  %.*s\n", (i32) cflags.len, cflags.data);
    vx_printf("  lflags:  %.*s\n", (i32) lflags.len, lflags.data);
    vx_printf("  defines: %.*s\n", (i32) defines.len, defines.data);

    vx_printf("targets (%u):\n", result->target_count);
    for (u32 i = 0; i < result->target_count; i++)
    {
        struct sk_target *t = &result->targets[i];

        vx_sv name = tok_to_sv(p, ctx, t->name_tok);
        vx_printf("  [%u] name: %.*s\n", i, (i32) name.len, name.data);

        vx_printf("      sources (%u):\n", t->source_count);
        for (u32 j = 0; j < t->source_count; j++)
        {
            vx_sv src = tok_to_sv(p, ctx, t->source_toks[j]);
            vx_printf("        [%u] %.*s\n", j, (i32) src.len, src.data);
        }

        vx_printf("      depends (%u):\n", t->depend_count);
        for (u32 j = 0; j < t->depend_count; j++)
        {
            vx_sv dep = tok_to_sv(p, ctx, t->depend_toks[j]);
            vx_printf("        [%u] %.*s\n", j, (i32) dep.len, dep.data);
        }

        vx_sv t_cc      = cfg_val_to_sv(p, ctx, t->cfg.cc);
        vx_sv t_linker  = cfg_val_to_sv(p, ctx, t->cfg.linker);
        vx_sv t_cflags  = cfg_val_to_sv(p, ctx, t->cfg.cflags);
        vx_sv t_lflags  = cfg_val_to_sv(p, ctx, t->cfg.lflags);
        vx_sv t_defines = cfg_val_to_sv(p, ctx, t->cfg.defines);

        vx_printf("      cfg:\n");
        vx_printf("        cc:      %.*s\n", (i32) t_cc.len, t_cc.data);
        vx_printf("        linker:  %.*s\n", (i32) t_linker.len, t_linker.data);
        vx_printf("        cflags:  %.*s\n", (i32) t_cflags.len, t_cflags.data);
        vx_printf("        lflags:  %.*s\n", (i32) t_lflags.len, t_lflags.data);
        vx_printf("        defines: %.*s\n", (i32) t_defines.len, t_defines.data);
    }
    vx_printf("=================\n");
}
