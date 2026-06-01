#include "sk_eval.h"
#include "mem.h"
#include "sk_globals.h"
#include "sk_util.h"
#include "sk_paths.h"
#include "sk_array.h"
#include "sk_config.h"

#include "vx_fs.h"
#include "vx_cpu.h"
#include "vx_io.h"
#include <stdlib.h>

static bool eval_expr(struct sk_parser *p,
                      vx_sv             stormfile,
                      u32               node,
                      char            **var_keys,
                      char            **var_vals,
                      u32               var_count);

static struct sk_target *
target_init(struct mem_arena *ar, struct sk_eval_result *result, vx_sv name_sv);
static void eval_target(struct sk_parser      *p,
                        vx_sv                  stormfile,
                        u32                    node,
                        struct sk_target      *target,
                        struct sk_eval_result *result);

static void load_builtin_vars(struct sk_eval_result *result);

static void eval_var(struct sk_parser *p, vx_sv stormfile, u32 node, struct sk_eval_result *result);

static void
eval_print(struct sk_parser *p, vx_sv stormfile, u32 node, struct sk_eval_result *result);

static void sk_eval_set_builtin(struct sk_eval_result *result, char *key, char *val);

static const char *eval_lookup_var(struct sk_eval_result *result, const char *key, size_t len);

//----------------------------------------------------------------------------------------------------

static void cfg_push_flags(struct sk_parser *parser,
                           vx_sv             stormfile,
                           u32               val_node,
                           u32               key_tok_idx,
                           char            **flags,
                           u32              *count,
                           u32               max_limit,
                           bool              append)
{
    if (!append)
    {
        *count = 0;
    }

    if (val_node == SK_NODE_INVALID)
    {
        syntax_error_at(parser, key_tok_idx, "flag list cannot be empty");
        return;
    }

    u32 cur = val_node;

    while (cur != 0 && *count < max_limit)
    {
        vx_sv sv = tok_to_sv(parser, stormfile, parser->nodes->token_idxs[cur]);

        if (parser->nodes->kinds[cur] == SK_NODE_FLAG)
        {
            flags[(*count)++] = sv_to_arena(g_sk_global_arena, sv);
        }
        else if (parser->nodes->kinds[cur] == SK_NODE_LIT_STRING)
        {
            vx_sv inner = {.data = sv.data + 1, .len = sv.len - 2};  // strip quotes

            const char *p   = inner.data;
            const char *end = inner.data + inner.len;

            // split args
            while (p < end && *count < max_limit)
            {
                while (p < end && *p == CHAR_SPACE)
                {
                    p++;
                }

                if (p >= end)
                {
                    break;
                }

                const char *start = p;

                while (p < end && *p != CHAR_SPACE)
                {
                    p++;
                }

                u32   len   = p - start;
                char *entry = mem_arena_alloc(g_sk_global_arena, len + 1);
                memcpy(entry, start, len);

                entry[len]        = CHAR_NULTERM;
                flags[(*count)++] = entry;
            }
        }
        else
        {
            syntax_error_at(parser, parser->nodes->token_idxs[cur], "expected SK_TOKEN_FLAG");
            return;
        }

        cur = parser->nodes->nexts[cur];
    }
}

static void cfg_push_paths(struct sk_parser *p,
                           vx_sv             stormfile,
                           u32               val_node,
                           u32               key_tok_idx,
                           char            **paths,
                           u32              *count,
                           u32               max_limit)
{
    if (val_node == SK_NODE_INVALID)
    {
        syntax_error_at(p, key_tok_idx, "path list cannot be empty");
        return;
    }

    u32 cur = val_node;

    while (cur != 0 && *count < max_limit)
    {
        vx_sv sv = tok_to_sv(p, stormfile, p->nodes->token_idxs[cur]);

        if (p->nodes->kinds[cur] != SK_NODE_PATH)
        {
            syntax_error_at(p, p->nodes->token_idxs[cur], "expected SK_TOKEN_PATH");
            return;
        }

        paths[*count] = sv_to_arena(g_sk_global_arena, sv);
        sk_path_strip_trailing_sep(paths[*count]);
        (*count)++;
        cur = p->nodes->nexts[cur];
    }
}

static void eval_cfg(struct sk_parser *p,
                     vx_sv             stormfile,
                     u32               node,
                     struct sk_cfg    *cfg,
                     struct sk_target *target)
{
    sk_ast_node_kind kind     = p->nodes->kinds[node];
    u32              tok_idx  = p->nodes->token_idxs[node];
    sk_token_kind    tok_kind = p->tokens->kinds[tok_idx];
    bool             append   = (kind == SK_NODE_APPEND);

    u32 val_node = p->nodes->data_a[node];

    if (val_node == 0)
    {
        return;
    }

    switch (tok_kind)
    {
        case SK_TOKEN_KWORD_CC:
        case SK_TOKEN_KWORD_COMPILER:
        {
            vx_sv sv = tok_to_sv(p, stormfile, p->nodes->token_idxs[val_node]);
            cfg->cc  = sv_to_arena(g_sk_global_arena, sv);
            break;
        }

        case SK_TOKEN_KWORD_LINKER:
        {
            vx_sv sv    = tok_to_sv(p, stormfile, p->nodes->token_idxs[val_node]);
            cfg->linker = sv_to_arena(g_sk_global_arena, sv);
            break;
        }

        case SK_TOKEN_KWORD_CFLAGS:
        {
            u32 key_tok = p->nodes->token_idxs[node];
            cfg_push_flags(p,
                           stormfile,
                           val_node,
                           key_tok,
                           cfg->cflags,
                           &cfg->cflags_count,
                           SK_MAX_FLAGS,
                           append);
            break;
        }

        case SK_TOKEN_KWORD_LFLAGS:
        {
            u32 key_tok = p->nodes->token_idxs[node];
            cfg_push_flags(p,
                           stormfile,
                           val_node,
                           key_tok,
                           cfg->lflags,
                           &cfg->lflags_count,
                           SK_MAX_FLAGS,
                           append);
            break;
        }

        case SK_TOKEN_KWORD_DEFINES:
        {
            u32 key_tok = p->nodes->token_idxs[node];

            cfg_push_flags(p,
                           stormfile,
                           val_node,
                           key_tok,
                           cfg->defines,
                           &cfg->defines_count,
                           SK_MAX_DEFINES,
                           append);
            break;
        }

        case SK_TOKEN_KWORD_INCLUDES:
        {
            u32 key_tok = p->nodes->token_idxs[node];
            cfg_push_flags(p,
                           stormfile,
                           val_node,
                           key_tok,
                           cfg->includes,
                           &cfg->includes_count,
                           SK_MAX_FLAGS,
                           append);
            break;
        }

        case SK_TOKEN_KWORD_KIND:
        {
            vx_sv sv     = tok_to_sv(p, stormfile, p->nodes->token_idxs[val_node]);
            char *t_kind = sv_to_arena(g_sk_global_arena, sv);
            if (target)
            {
                if (strcmp(t_kind, "exec") == 0)
                {
                    target->kind = SK_TARGET_KIND_EXEC;
                }
                else if (strcmp(t_kind, "static") == 0)
                {
                    target->kind = SK_TARGET_KIND_STATIC;
                }
                else if (strcmp(t_kind, "shared") == 0)
                {
                    target->kind = SK_TARGET_KIND_SHARED;
                }
                else if (strcmp(t_kind, "pch") == 0)
                {
                    target->kind = SK_TARGET_KIND_PCH;
                }
            }
            break;
        }

        case SK_TOKEN_KWORD_MODE:
        {
            vx_sv sv   = tok_to_sv(p, stormfile, p->nodes->token_idxs[val_node]);
            char *mode = sv_to_arena(g_sk_global_arena, sv);

            if (target)
            {
                target->build_mode = mode;
            }
            break;
        }

        case SK_TOKEN_KWORD_OUT:
        {
            if (target)
            {
                vx_sv sv = tok_to_sv(p, stormfile, p->nodes->token_idxs[val_node]);

                target->out_name = sv_to_arena(g_sk_global_arena, sv);
            }
            break;
        }

        case SK_TOKEN_KWORD_OUT_DIR:
        {
            if (target)
            {
                vx_sv sv = tok_to_sv(p, stormfile, p->nodes->token_idxs[val_node]);

                target->out_dir = sv_to_arena(g_sk_global_arena, sv);
            }
            break;
        }

        case SK_TOKEN_KWORD_EXCLUDE:
        {
            if (target == nullptr)
            {
                VX_ASSERT_LOG("Keyword 'exclude' is not allowed in global scope");
                break;
            }

            u32 key_tok = p->nodes->token_idxs[node];
            cfg_push_paths(p,
                           stormfile,
                           val_node,
                           key_tok,
                           target->excludes,
                           &target->exclude_count,
                           SK_MAX_EXCLUDES);
            break;
        }

        case SK_TOKEN_KWORD_SOURCES:
        {
            if (target == nullptr)
            {
                VX_ASSERT_LOG("Keyword 'sources' is not allowed in global scope");
                break;
            }

            u32 cur = val_node;

            while (cur != SK_NODE_INVALID)
            {
                vx_sv sv = tok_to_sv(p, stormfile, p->nodes->token_idxs[cur]);

                char *path = sv_to_arena(g_sk_global_arena, sv);
                sk_path_strip_trailing_sep(path);

                if (vx_isdir(path))
                {
                    sk_arena_array_push(target->scan_dirs, path);
                }
                else
                {
                    if (!sk_arena_array_contains(target->sources, path))
                    {
                        sk_arena_array_push(target->sources, path);
                    }
                }
                cur = p->nodes->nexts[cur];
            }
            break;
        }

        default:
        {
            break;
        }
    }
}

static void eval_if(struct sk_parser      *p,
                    vx_sv                  stormfile,
                    u32                    node,
                    struct sk_target      *target,
                    struct sk_eval_result *result,
                    struct mem_arena      *ar,
                    char                 **snapshot)
{
    u32 cond_node = p->nodes->data_a[node];

    bool cond =
        eval_expr(p, stormfile, cond_node, result->var_keys, result->var_vals, result->var_count);

    if (cond)
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
                    if (target != nullptr)
                    {
                        eval_cfg(p, stormfile, child, &target->cfg, target);
                    }
                    else
                    {
                        eval_cfg(p, stormfile, child, &result->global, nullptr);
                    }
                    break;
                }

                case SK_NODE_IF:
                {
                    eval_if(p, stormfile, child, target, result, ar, snapshot);
                    break;
                }

                case SK_NODE_PRINT:
                {
                    eval_print(p, stormfile, child, result);
                    break;
                }

                case SK_NODE_EXIT:
                {
                    u32 val_node = p->nodes->data_a[child];
                    u32 exit_idx = p->nodes->token_idxs[val_node];

                    vx_sv sv = tok_to_sv(p, stormfile, exit_idx);

                    if (sv.len >= 2)
                    {
                        sv.data++;
                        sv.len -= 2;
                    }
                    vx_errlog("%.*s", (i32) sv.len, sv.data);

                    sk_shutdown();
                    exit(1);
                }

                case SK_NODE_TARGET:
                {
                    if (result->target_count >= SK_MAX_TARGETS)
                    {
                        VX_ASSERT_LOG("Max targets limit reached: %d", SK_MAX_TARGETS);
                        return;
                    }

                    u32   name_tok = p->nodes->data_a[child];
                    vx_sv name_sv  = tok_to_sv(p, stormfile, name_tok);

                    for (u32 i = 0; i < result->target_count; i++)
                    {
                        if (strncmp(result->targets[i].name, name_sv.data, name_sv.len) == 0 &&
                            result->targets[i].name[name_sv.len] == CHAR_NULTERM)
                        {
                            vx_errlog(
                                "Duplicate target name: '%.*s'", (i32) name_sv.len, name_sv.data);
                            return;
                        }
                    }

                    struct sk_target *t = target_init(ar, result, name_sv);

                    u32 saved_var_count = result->var_count;
                    memcpy(snapshot, result->var_vals, sizeof(char *) * result->var_count);

                    eval_target(p, stormfile, child, t, result);

                    result->var_count = saved_var_count;
                    memcpy(result->var_vals, snapshot, sizeof(char *) * saved_var_count);

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
            eval_if(p, stormfile, child, target, result, ar, snapshot);
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
                        if (target != nullptr)
                        {
                            eval_cfg(p, stormfile, child, &target->cfg, target);
                        }
                        else
                        {
                            eval_cfg(p, stormfile, child, &result->global, nullptr);
                        }
                        break;
                    }

                    case SK_NODE_IF:
                    {
                        eval_if(p, stormfile, child, target, result, ar, snapshot);
                        break;
                    }

                    case SK_NODE_PRINT:
                    {
                        eval_print(p, stormfile, child, result);
                        break;
                    }

                    case SK_NODE_EXIT:
                    {
                        u32 val_node = p->nodes->data_a[child];
                        u32 exit_idx = p->nodes->token_idxs[val_node];

                        vx_sv sv = tok_to_sv(p, stormfile, exit_idx);

                        if (sv.len >= 2)
                        {
                            sv.data++;
                            sv.len -= 2;
                        }
                        vx_errlog("%.*s", (i32) sv.len, sv.data);

                        sk_shutdown();
                        exit(1);
                    }

                    case SK_NODE_TARGET:
                    {
                        if (result->target_count >= SK_MAX_TARGETS)
                        {
                            VX_ASSERT_LOG("Max targets limit reached: %d", SK_MAX_TARGETS);
                            return;
                        }

                        u32   name_tok = p->nodes->data_a[child];
                        vx_sv name_sv  = tok_to_sv(p, stormfile, name_tok);

                        for (u32 i = 0; i < result->target_count; i++)
                        {
                            if (strncmp(result->targets[i].name, name_sv.data, name_sv.len) == 0 &&
                                result->targets[i].name[name_sv.len] == CHAR_NULTERM)
                            {
                                vx_errlog("Duplicate target name: '%.*s'",
                                          (i32) name_sv.len,
                                          name_sv.data);
                                return;
                            }
                        }

                        struct sk_target *t = target_init(ar, result, name_sv);

                        u32 saved_var_count = result->var_count;
                        memcpy(snapshot, result->var_vals, sizeof(char *) * result->var_count);

                        eval_target(p, stormfile, child, t, result);

                        result->var_count = saved_var_count;
                        memcpy(result->var_vals, snapshot, sizeof(char *) * saved_var_count);

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

static void eval_target(struct sk_parser      *p,
                        vx_sv                  stormfile,
                        u32                    node,
                        struct sk_target      *target,
                        struct sk_eval_result *result)
{
    if (target->out_name == nullptr)
    {
        target->out_name = target->name;
    }

    u32 child = p->nodes->data_b[node];

    while (child != 0)
    {
        sk_ast_node_kind kind = p->nodes->kinds[child];

        switch (kind)
        {
            case SK_NODE_ASSIGN:
            case SK_NODE_APPEND:
            {
                u32 tok_kind = p->tokens->kinds[p->nodes->token_idxs[child]];

                if (tok_kind == SK_TOKEN_IDENT)
                    eval_var(p, stormfile, child, result);
                else
                {
                    eval_cfg(p, stormfile, child, &target->cfg, target);
                }
                break;
            }

            case SK_NODE_IF:
            {
                char **snapshot = mem_arena_alloc(g_sk_global_arena, sizeof(char *) * SK_MAX_VARS);
                eval_if(p, stormfile, child, target, result, g_sk_global_arena, snapshot);
                break;
            }

            case SK_NODE_PRINT:
            {
                eval_print(p, stormfile, child, result);
                break;
            }

            case SK_NODE_EXIT:
            {
                u32 val_node = p->nodes->data_a[child];
                u32 exit_idx = p->nodes->token_idxs[val_node];

                vx_sv sv = tok_to_sv(p, stormfile, exit_idx);

                if (sv.len >= 2)
                {
                    sv.data++;
                    sv.len -= 2;
                }
                vx_errlog("%.*s", (i32) sv.len, sv.data);

                sk_shutdown();
                exit(1);
            }

            case SK_NODE_INSTALL:
            {
                u32 val_node    = p->nodes->data_a[child];
                u32 path_to_idx = p->nodes->token_idxs[val_node];

                vx_sv sv = tok_to_sv(p, stormfile, path_to_idx);

                const char *raw     = sv_to_arena(g_sk_global_arena, sv);
                target->install_dir = (char *) sk_expand_path(g_sk_global_arena, raw);
                break;
            }

            case SK_NODE_DEPENDS:
            {
                u32 dep_node = p->nodes->data_a[child];

                while (dep_node != 0)
                {
                    u32   dep_tok_idx = p->nodes->token_idxs[dep_node];
                    vx_sv sv          = tok_to_sv(p, stormfile, dep_tok_idx);

                    target->depends[target->depend_count++] = sv_to_arena(g_sk_global_arena, sv);

                    dep_node = p->nodes->nexts[dep_node];
                }
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

static void
eval_print(struct sk_parser *p, vx_sv stormfile, u32 node, struct sk_eval_result *result)
{
    u32 val_node = p->nodes->data_a[node];
    u32 val_tok  = p->nodes->token_idxs[val_node];

    sk_token_kind kind = p->tokens->kinds[val_tok];

    vx_sv key = tok_to_sv(p, stormfile, val_tok);

    if (kind == SK_TOKEN_LIT_STRING)
    {
        vx_printf("[sk]: %.*s\n", (i32) key.len - 2, key.data + 1);
    }
    else
    {
        const char *val = eval_lookup_var(result, key.data, key.len);
        vx_printf("[sk]: %.*s = %s\n", (i32) key.len, key.data, val ? val : "(unset)");
    }
}

static void eval_var(struct sk_parser *p, vx_sv stormfile, u32 node, struct sk_eval_result *result)
{
    if (result->var_count >= SK_MAX_VARS)
    {
        return;
    }

    u32 key_tok  = p->nodes->token_idxs[node];
    u32 val_node = p->nodes->data_a[node];

    if (val_node == 0)
    {
        return;
    }

    u32 val_tok = p->nodes->token_idxs[val_node];

    vx_sv key = tok_to_sv(p, stormfile, key_tok);
    vx_sv val = tok_to_sv(p, stormfile, val_tok);

    if (key.len >= 2 && key.data[0] == CHAR_UNDERSCORE && key.data[1] == CHAR_UNDERSCORE)
    {
        vx_errlog("Cannot assign to built-in variable '%.*s'", (i32) key.len, key.data);
        return;
    }

    for (u32 i = 0; i < result->var_count; i++)
    {
        if (strncmp(result->var_keys[i], key.data, key.len) == 0 &&
            result->var_keys[i][key.len] == CHAR_NULTERM)
        {
            result->var_vals[i] = sv_to_arena(g_sk_global_arena, val);
            return;
        }
    }

    if (result->var_count < SK_MAX_VARS)
    {
        result->var_keys[result->var_count] = sv_to_arena(g_sk_global_arena, key);
        result->var_vals[result->var_count] = sv_to_arena(g_sk_global_arena, val);
        result->var_count++;
    }
}

static bool eval_expr(struct sk_parser *p,
                      vx_sv             stormfile,
                      u32               node,
                      char            **var_keys,
                      char            **var_vals,
                      u32               var_count)
{
    if (node == 0)
    {
        return false;
    }

    sk_ast_node_kind kind = p->nodes->kinds[node];

    if (kind == SK_NODE_IDENT)
    {
        vx_sv key = tok_to_sv(p, stormfile, p->nodes->token_idxs[node]);

        for (u32 i = 0; i < var_count; i++)
        {
            if (strncmp(var_keys[i], key.data, key.len) == 0 &&
                var_keys[i][key.len] == CHAR_NULTERM)
            {
                return strcmp(var_vals[i], "1") == 0;
            }
        }
        return false;
    }

    if (kind == SK_NODE_EXPR)
    {
        u32 op_tok = p->nodes->token_idxs[node];

        sk_token_kind op_kind = p->tokens->kinds[op_tok];

        u32 left  = p->nodes->data_a[node];
        u32 right = p->nodes->data_b[node];

        vx_sv lhs = {0};
        if (p->nodes->kinds[left] == SK_NODE_IDENT)
        {
            vx_sv key = tok_to_sv(p, stormfile, p->nodes->token_idxs[left]);

            for (u32 i = 0; i < var_count; i++)
            {
                if (strncmp(var_keys[i], key.data, key.len) == 0 &&
                    var_keys[i][key.len] == CHAR_NULTERM)
                {
                    lhs.data = var_vals[i];
                    lhs.len  = strlen(var_vals[i]);
                    break;
                }
            }
        }
        else if (p->nodes->kinds[left] == SK_NODE_LIT_NUMBER)
        {
            lhs = tok_to_sv(p, stormfile, p->nodes->token_idxs[left]);
        }

        // resolve right — literal ident value
        vx_sv rhs = tok_to_sv(p, stormfile, p->nodes->token_idxs[right]);

        sk_token_kind rhs_tok_kind = p->tokens->kinds[p->nodes->token_idxs[right]];
        if (rhs_tok_kind == SK_TOKEN_LIT_STRING && rhs.len >= 2)
        {
            rhs.data++;
            rhs.len -= 2;
        }

        switch (op_kind)
        {
            case SK_TOKEN_DOUBLE_EQUAL:
            {
                return lhs.len == rhs.len && strncmp(lhs.data, rhs.data, rhs.len) == 0;
            }

            case SK_TOKEN_NOT_EQUAL:
            {
                return !(lhs.len == rhs.len && strncmp(lhs.data, rhs.data, rhs.len) == 0);
            }

            case SK_TOKEN_LT:
            case SK_TOKEN_GT:
            case SK_TOKEN_LE:
            case SK_TOKEN_GE:
            {
                i32 l = atoi(lhs.data ? lhs.data : "0");
                i32 r = atoi(rhs.data ? rhs.data : "0");
                if (op_kind == SK_TOKEN_LT)
                {
                    return l < r;
                }
                if (op_kind == SK_TOKEN_GT)
                {
                    return l > r;
                }
                if (op_kind == SK_TOKEN_LE)
                {
                    return l <= r;
                }
                if (op_kind == SK_TOKEN_GE)
                {
                    return l >= r;
                }
                return false;
            }

            default:
            {
                return false;
            }
        }
    }

    return false;
}

static struct sk_target *
target_init(struct mem_arena *ar, struct sk_eval_result *result, vx_sv name_sv)
{
    if (ar == nullptr || result == nullptr)
    {
        return nullptr;
    }

    struct sk_target *t = &result->targets[result->target_count++];

    t->name       = sv_to_arena(ar, name_sv);
    t->out_dir    = "crater";
    t->build_mode = "debug";
    t->kind       = SK_TARGET_KIND_EXEC;

    t->exclude_count = 0;
    t->depend_count  = 0;
    t->cfg.cflags    = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_FLAGS);
    t->cfg.lflags    = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_FLAGS);
    t->cfg.defines   = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_DEFINES);
    t->cfg.libs      = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_LIBS);
    t->cfg.lib_paths = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_LIBS);
    t->cfg.includes  = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_FLAGS);

    t->excludes = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_EXCLUDES);
    t->depends  = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_DEPS);

    t->cfg.cflags_count = result->global.cflags_count;
    memcpy(t->cfg.cflags, result->global.cflags, sizeof(char *) * t->cfg.cflags_count);

    t->cfg.lflags_count = result->global.lflags_count;
    memcpy(t->cfg.lflags, result->global.lflags, sizeof(char *) * t->cfg.lflags_count);

    t->cfg.includes_count = result->global.includes_count;
    memcpy(t->cfg.includes, result->global.includes, sizeof(char *) * t->cfg.includes_count);

    t->cfg.defines_count = result->global.defines_count;
    memcpy(t->cfg.defines, result->global.defines, sizeof(char *) * t->cfg.defines_count);

    t->cfg.libs_count = result->global.libs_count;
    memcpy(t->cfg.libs, result->global.libs, sizeof(char *) * t->cfg.libs_count);

    t->cfg.lib_paths_count = result->global.lib_paths_count;
    memcpy(t->cfg.lib_paths, result->global.lib_paths, sizeof(char *) * t->cfg.lib_paths_count);

    t->sources   = sk_arena_array_create(g_sk_global_arena, VX_BUF_SIZE_8192);
    t->scan_dirs = sk_arena_array_create(g_sk_global_arena, VX_BUF_SIZE_8192);

    t->cfg.cc     = result->global.cc;
    t->cfg.linker = result->global.linker;

    return t;
}

// EVAL ENTRY
vx_status sk_top_level_eval(struct sk_parser *p, struct sk_eval_result *result)
{
    if (p == nullptr || result == nullptr)
    {
        return VX_ERROR;
    }

    struct mem_arena *ar     = g_sk_global_arena;
    result->global.cflags    = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_FLAGS);
    result->global.lflags    = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_FLAGS);
    result->global.defines   = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_DEFINES);
    result->global.libs      = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_LIBS);
    result->global.lib_paths = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_LIBS);
    result->global.includes  = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_FLAGS);

    if (p->nodes->codegen_nodes > 0)
    {
        result->codegen_node_idxs = mem_arena_alloc(ar, sizeof(u32) * p->nodes->codegen_nodes);
    }
    else
    {
        result->codegen_node_idxs = nullptr;
    }
    result->codegen_count = 0;

    result->var_keys = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_VARS);
    result->var_vals = mem_arena_alloc(ar, sizeof(char *) * SK_MAX_VARS);

    if (result->var_vals == nullptr)
    {
        VX_ASSERT_LOG("Failed to allocate arrays");
        return VX_ERROR;
    }

    vx_sv stormfile = g_sk_global_ctx.stormfile;

    u32 node = p->nodes->data_a[1];  // program first child

    char **snapshot = mem_arena_alloc(g_sk_global_arena, sizeof(char *) * SK_MAX_VARS);

    load_builtin_vars(result);

    while (node != 0)
    {
        sk_ast_node_kind kind = p->nodes->kinds[node];
        switch (kind)
        {
            case SK_NODE_ASSIGN:
            case SK_NODE_APPEND:
            {
                u32 tok_idx = p->nodes->token_idxs[node];

                sk_token_kind tok_kind = p->tokens->kinds[tok_idx];

                if (tok_kind == SK_TOKEN_IDENT)
                {
                    eval_var(p, stormfile, node, result);
                }
                else
                {
                    eval_cfg(p, stormfile, node, &result->global, nullptr);
                }
                break;
            }

            case SK_NODE_TARGET:
            {
                if (result->target_count >= SK_MAX_TARGETS)
                {
                    VX_ASSERT_LOG("Max targets limit reached: %d", SK_MAX_TARGETS);
                    return VX_ERROR;
                }

                u32   name_tok = p->nodes->data_a[node];
                vx_sv name_sv  = tok_to_sv(p, stormfile, name_tok);

                for (u32 i = 0; i < result->target_count; i++)
                {
                    if (strncmp(result->targets[i].name, name_sv.data, name_sv.len) == 0 &&
                        result->targets[i].name[name_sv.len] == CHAR_NULTERM)
                    {
                        vx_errlog("Duplicate target name: '%.*s'", (i32) name_sv.len, name_sv.data);
                        return VX_ERROR;
                    }
                }

                struct sk_target *t = target_init(ar, result, name_sv);

                u32 saved_var_count = result->var_count;
                memcpy(snapshot, result->var_vals, sizeof(char *) * result->var_count);

                eval_target(p, stormfile, node, t, result);

                result->var_count = saved_var_count;
                memcpy(result->var_vals, snapshot, sizeof(char *) * saved_var_count);

                break;
            }

            case SK_NODE_CODEGEN:
            {
                result->codegen_node_idxs[result->codegen_count] = node;
                result->codegen_count++;
                break;
            }

            case SK_NODE_PRINT:
            {
                eval_print(p, stormfile, node, result);
                break;
            }

            case SK_NODE_IF:
            {
                eval_if(p, stormfile, node, nullptr, result, ar, snapshot);
                break;
            }

            // top
            case SK_NODE_EXIT:
            {
                u32 val_node = p->nodes->data_a[node];  //
                u32 exit_idx = p->nodes->token_idxs[val_node];

                vx_sv sv = tok_to_sv(p, stormfile, exit_idx);

                if (sv.len >= 2)
                {
                    sv.data++;
                    sv.len -= 2;
                }
                vx_errlog("%.*s", (i32) sv.len, sv.data);

                sk_shutdown();
                exit(1);
            }

            default:
            {
                break;
            }
        }

        node = p->nodes->nexts[node];
    }

    // other builtins are loaded before eval
    // but these are set after
    if (result->global.cc != nullptr)
    {
        char abs_cc[VX_PATH_MAX];
        if (vx_fs_which(result->global.cc, abs_cc, sizeof(abs_cc)) == VX_OK)
        {
            const char *base = strrchr(abs_cc, VX_PATH_SEP);

            base = base ? base + 1 : abs_cc;

            bool is_clang = strncmp(base, "clang", 5) == 0;
            bool is_gcc   = strncmp(base, "gcc", 3) == 0;

            sk_eval_set_builtin(result, "__clang__", is_clang ? "1" : "0");
            sk_eval_set_builtin(result, "__gcc__", is_gcc ? "1" : "0");
        }
    }

    return VX_OK;
}

//----------------------------------------------------------------------------------------------------

static const char *sk_target_kind_to_str(sk_target_kind kind)
{
    switch (kind)
    {
        case SK_TARGET_KIND_EXEC: return "executable";
        case SK_TARGET_KIND_STATIC: return "static_lib";
        case SK_TARGET_KIND_SHARED: return "shared_lib";
        case SK_TARGET_KIND_PCH: return "precompiled_header";
        default: return "none";
    }
}

static void cfg_list_dump(const char *label, char **list, u32 count)
{
    vx_printf("        %-9s: ", label);

    if (count == 0 || list == nullptr)
    {
        vx_printf("(none)\n");
        return;
    }

    for (u32 i = 0; i < count; i++)
    {
        vx_printf("%s%s", list[i], (i == count - 1) ? "" : ", ");
    }
    vx_printf("\n");
}

static void cfg_dump(struct sk_cfg *cfg)
{
    if (cfg == nullptr)
    {
        return;
    }

    vx_printf("        cc       : %s\n", (cfg->cc && cfg->cc[0]) ? cfg->cc : "(none)");
    vx_printf("        linker   : %s\n", (cfg->linker && cfg->linker[0]) ? cfg->linker : "(none)");

    cfg_list_dump("cflags", cfg->cflags, cfg->cflags_count);
    cfg_list_dump("lflags", cfg->lflags, cfg->lflags_count);
    cfg_list_dump("defines", cfg->defines, cfg->defines_count);
    cfg_list_dump("includes", cfg->includes, cfg->includes_count);
    cfg_list_dump("libs", cfg->libs, cfg->libs_count);
    cfg_list_dump("libpaths", cfg->lib_paths, cfg->lib_paths_count);
}

void sk_dbg_dump_eval(struct sk_parser *p, struct sk_eval_result *result)
{
    if (p == nullptr || result == nullptr)
    {
        vx_printf("[!] Cannot dump eval: NULL pointers\n");
        return;
    }

    vx_printf("\n--- SK EVALUATION RESULT DUMP ---\n");

    vx_printf("variables (%u):\n", result->var_count);

    for (u32 i = 0; i < result->var_count; i++)
    {
        vx_printf("    $%s = \"%s\"\n", result->var_keys[i], result->var_vals[i]);
    }

    vx_printf("\nglobal config:\n");
    cfg_dump(&result->global);

    vx_printf("\ntargets (%u):\n", result->target_count);

    for (u32 i = 0; i < result->target_count; i++)
    {
        struct sk_target *t = &result->targets[i];

        vx_printf("  [%02u] %s\n", i, t->name ? t->name : "UNNAMED");
        vx_printf("        kind     : %s (%u)\n", sk_target_kind_to_str(t->kind), t->kind);
        vx_printf("        mode     : %s\n", t->build_mode ? t->build_mode : "debug");
        vx_printf("        out_name : %s\n", t->out_name ? t->out_name : "(none)");
        vx_printf("        build_dir: %s\n", t->out_dir ? t->out_dir : "(none)");

        if (t->sources && t->sources->count > 0)
        {
            vx_printf("        sources  : %zu items\n", t->sources->count);
            for (u32 j = 0; j < (u32) t->sources->count; j++)
            {
                vx_printf("          - %s\n", (char *) t->sources->items[j]);
            }
        }
        else
        {
            vx_printf("        sources  : (none)\n");
        }

        if (t->exclude_count > 0 && t->excludes)
        {
            vx_printf("        excludes : ");
            for (u32 j = 0; j < t->exclude_count; j++)
            {
                vx_printf("%s%s", t->excludes[j], (j == t->exclude_count - 1) ? "" : ", ");
            }
            vx_printf("\n");
        }

        if (t->depend_count > 0 && t->depends)
        {
            vx_printf("        depends  : ");
            for (u32 j = 0; j < t->depend_count; j++)
            {
                vx_printf("%s%s", t->depends[j], (j == t->depend_count - 1) ? "" : ", ");
            }
            vx_printf("\n");
        }

        vx_printf("        config   :\n");
        cfg_dump(&t->cfg);
        vx_printf("\n");
    }
    vx_printf("--- END DUMP ---\n\n");
}

static const char *sk_eval_get_builtin(struct sk_eval_result *result, const char *key)
{
    for (u32 i = 0; i < result->var_count; i++)
    {
        if (strcmp(result->var_keys[i], key) == 0)
        {
            return result->var_vals[i];
        }
    }
    return nullptr;
}

static void sk_eval_set_builtin(struct sk_eval_result *result, char *key, char *val)
{
    for (u32 i = 0; i < result->var_count; i++)
    {
        if (strcmp(result->var_keys[i], key) == 0)
        {
            result->var_vals[i] = val;
            return;
        }
    }

    if (result->var_count < SK_MAX_VARS)
    {
        result->var_keys[result->var_count] = key;
        result->var_vals[result->var_count] = val;
        result->var_count++;
    }
}

static const char *eval_lookup_var(struct sk_eval_result *result, const char *key, size_t len)
{
    for (u32 i = 0; i < result->var_count; i++)
    {
        if (strncmp(result->var_keys[i], key, len) == 0 && result->var_keys[i][len] == CHAR_NULTERM)
        {
            return result->var_vals[i];
        }
    }
    return nullptr;
}

static void load_builtin_vars(struct sk_eval_result *result)
{
    if (result == nullptr)
    {
        return;
    }

    char *maj_buf = mem_arena_alloc(g_sk_global_arena, VX_BUF_SIZE_16);
    char *min_buf = mem_arena_alloc(g_sk_global_arena, VX_BUF_SIZE_16);
    char *pat_buf = mem_arena_alloc(g_sk_global_arena, VX_BUF_SIZE_16);
    snprintf(maj_buf, VX_BUF_SIZE_16, "%d", SK_VERSION_MAJOR);
    snprintf(min_buf, VX_BUF_SIZE_16, "%d", SK_VERSION_MINOR);
    snprintf(pat_buf, VX_BUF_SIZE_16, "%d", SK_VERSION_PATCH);

    char *cache_line_buf = mem_arena_alloc(g_sk_global_arena, VX_BUF_SIZE_16);
    snprintf(cache_line_buf, VX_BUF_SIZE_16, "%d", vx_cpu_get_cache_line());

    sk_eval_set_builtin(result, "__sk_version__", SK_VERSION_STRING);
    sk_eval_set_builtin(result, "__sk_version_major__", maj_buf);
    sk_eval_set_builtin(result, "__sk_version_minor__", min_buf);
    sk_eval_set_builtin(result, "__sk_version_patch__", pat_buf);

    sk_eval_set_builtin(result, "__arch__", VX_ARCH_NAME);
    sk_eval_set_builtin(result, "__os__", VX_OS_NAME);

    sk_eval_set_builtin(result, "__has_avx__", vx_cpu_has_avx() ? "1" : "0");
    sk_eval_set_builtin(result, "__has_avx2__", vx_cpu_has_avx2() ? "1" : "0");
    sk_eval_set_builtin(result, "__has_sse4_2__", vx_cpu_has_sse4_2() ? "1" : "0");
    sk_eval_set_builtin(result, "__has_bmi__", vx_cpu_has_bmi() ? "1" : "0");
    sk_eval_set_builtin(result, "__cache_line__", cache_line_buf);

    if (g_sk_global_ctx.setvars == nullptr)
    {
        return;
    }

    for (u32 i = 0; i < g_sk_global_ctx.setvars->count; i++)
    {
        const char *name = (const char *) g_sk_global_ctx.setvars->items[i];

        if (sk_eval_get_builtin(result, name) != nullptr)
        {
            vx_warn("--set=%s conflicts with a builtin, ignored", name);
            continue;
        }

        sk_eval_set_builtin(result, (char *) name, "1");
    }
}
