#include "sk_pipeline.h"
#include "sk_lexer.h"
#include "sk_parser.h"
#include "sk_eval.h"
#include "sk_globals.h"
#include "sk_paths.h"
#include "sk_array.h"
#include "sk_globals.h"

#include "vx_fs.h"
#include "vx_io.h"
#include "vx_time.h"
#include <string.h>

static vx_status resolve_depends(struct sk_eval_result *result);
static char     *resolve_val_or_var(struct sk_eval_result *r, char *cfg_str);

static char *
resolve_token_or_var(struct sk_parser *p, vx_sv stormfile, struct sk_eval_result *r, u32 token_idx);

static vx_status
sk_pipeline_codegen(struct sk_parser *p, vx_sv stormfile, u32 node, struct sk_eval_result *r);

static vx_status finalize_evaluation(struct sk_eval_result *result)
{
    if (result == nullptr)
    {
        return VX_ERROR;
    }

    for (u32 i = 0; i < result->target_count; i++)
    {
        struct sk_target *t = &result->targets[i];

        u32 saved_excl_count = t->exclude_count;
        u32 extra_excl_count = 2;

        char **final_excludes = mem_arena_alloc(
            g_sk_global_arena, sizeof(char *) * (saved_excl_count + extra_excl_count));

        for (u32 j = 0; j < saved_excl_count; j++)
        {
            final_excludes[j] = t->excludes[j];
        }

        final_excludes[saved_excl_count]     = t->out_dir;
        final_excludes[saved_excl_count + 1] = SK_PATH_STORM_DIR;

        t->excludes      = final_excludes;
        t->exclude_count = saved_excl_count + extra_excl_count;

        for (u32 j = 0; j < t->scan_dirs->count; j++)
        {
            char *dir_to_scan = (char *) t->scan_dirs->items[j];

            sk_scan_dir_r(t->sources, t->excludes, t->exclude_count, dir_to_scan);
        }
        if (t->install_dir)
        {
            vx_log("Installing: %s", t->install_dir);
        }
    }

    return VX_OK;
}

vx_status sk_pipeline_run(struct sk_ctx         *ctx,
                          struct sk_lexer       *lx,
                          struct sk_parser      *p,
                          struct sk_eval_result *ev_result)
{
    if (ctx == nullptr || lx == nullptr || p == nullptr || ev_result == nullptr)
    {
        return VX_ERROR;
    }

    ctx->stormfile = vx_fs_read(SK_PATH_STORMFILE, sk_arena_alloc, g_sk_global_arena);

    if (ctx->stormfile.data == nullptr)
    {
        return VX_ERROR;
    }

    //----------------------------------------------------------------------------------------------------
    // Lex

    vx_status pipeline_status = VX_OK;

    vx_ticks lexer_time  = {0};
    vx_ticks parser_time = {0};
    vx_ticks eval_time   = {0};

    if (ctx->active_opt & SK_OPT_PROFILE)
    {
        vx_ticks_start(&lexer_time);
    }

    if (sk_lx_init(ctx, lx) != VX_OK)
    {
        VX_ASSERT_LOG("Failed to initialize lexer");
        pipeline_status = VX_ERROR;
    }

    if (sk_lex(ctx, lx) != VX_OK)
    {
        VX_ASSERT_LOG("Lexer failed");
        pipeline_status = VX_ERROR;
    }

    if (ctx->active_opt & SK_OPT_PROFILE)
    {
        vx_ticks_end(&lexer_time);
        sk_log_time("Lexer", &lexer_time);
    }

    if (ctx->tokens->err_count > 0)
    {
        pipeline_status = VX_ERROR;
    }

    //----------------------------------------------------------------------------------------------------
    // Parse

    if (ctx->active_opt & SK_OPT_PROFILE)
    {
        vx_ticks_start(&parser_time);
    }
    if (sk_parser_init(ctx, p) != VX_OK)
    {
        VX_ASSERT_LOG("Failed to initialize parser");
        pipeline_status = VX_ERROR;
    }

    if (sk_top_level_parse(p) != VX_OK)
    {
        vx_errlog("Parser failed");
        pipeline_status = VX_ERROR;
    }

    if (ctx->active_opt & SK_OPT_PROFILE)
    {
        vx_ticks_end(&parser_time);
        sk_log_time("Parser", &parser_time);
    }

    //----------------------------------------------------------------------------------------------------
    // Eval

    if (ctx->active_opt & SK_OPT_PROFILE)
    {
        vx_ticks_start(&eval_time);
    }

    if (sk_top_level_eval(p, ev_result) != VX_OK)
    {
        vx_errlog("Eval failed");
        pipeline_status = VX_ERROR;
    }

    // ----
    // vars

    for (u32 i = 0; i < ev_result->target_count; i++)
    {
        struct sk_target *t = &ev_result->targets[i];

        t->build_mode = resolve_val_or_var(ev_result, t->build_mode);
        t->out_name   = resolve_val_or_var(ev_result, t->out_name);
        t->out_dir    = resolve_val_or_var(ev_result, t->out_dir);
    }

    // ----
    // Codegen

    if (p->nodes->codegen_nodes > 0)
    {
        for (u32 i = 0; i < ev_result->codegen_count; i++)
        {
            u32 codegen_node_idx = ev_result->codegen_node_idxs[i];

            if (sk_pipeline_codegen(p, ctx->stormfile, codegen_node_idx, ev_result) != VX_OK)
            {
                pipeline_status = VX_ERROR;
            }
        }
    }

    if (resolve_depends(ev_result) != VX_OK)
    {
        pipeline_status = VX_ERROR;
    }

    if (finalize_evaluation(ev_result) != VX_OK)
    {
        vx_errlog("Finalize evaluation failed");
        pipeline_status = VX_ERROR;
    }

    if (ctx->active_opt & SK_OPT_PROFILE)
    {
        vx_ticks_end(&eval_time);
        sk_log_time("Eval", &eval_time);
    }

    if (ctx->nodes->err_count > 0)
    {
        pipeline_status = VX_ERROR;
    }

    return pipeline_status;
}

static char *
resolve_token_or_var(struct sk_parser *p, vx_sv stormfile, struct sk_eval_result *r, u32 token_idx)
{
    vx_sv raw_sv = tok_to_sv(p, stormfile, token_idx);
    if (raw_sv.len == 0)
    {
        return "";
    }

    if ((raw_sv.data[0] == CHAR_DOUBLE_QUOTE && raw_sv.data[raw_sv.len - 1] == CHAR_DOUBLE_QUOTE) ||
        (raw_sv.data[0] == CHAR_SINGLE_QUOTE && raw_sv.data[raw_sv.len - 1] == CHAR_SINGLE_QUOTE))
    {
        raw_sv.data++;
        raw_sv.len -= 2;
    }

    char *flat_str = sv_to_arena(g_sk_global_arena, raw_sv);
    for (u32 i = 0; i < r->var_count; i++)
    {
        if (strcmp(r->var_keys[i], flat_str) == 0)
        {
            return r->var_vals[i];
        }
    }

    bool has_delim = false;
    for (u32 i = 0; i < raw_sv.len; i++)
    {
        char c = raw_sv.data[i];

        if (c == CHAR_DOT || c == CHAR_SLASH || c == CHAR_MINUS || c == CHAR_UNDERSCORE)
        {
            has_delim = true;
            break;
        }
    }

    if (!has_delim)
    {
        char *flat_str = sv_to_arena(g_sk_global_arena, raw_sv);
        for (u32 i = 0; i < r->var_count; i++)
        {
            if (strcmp(r->var_keys[i], flat_str) == 0)
            {
                return r->var_vals[i];
            }
        }
        return flat_str;
    }

    u32   out_cap = VX_BUF_SIZE_512;
    char *out_str = mem_arena_alloc(g_sk_global_arena, out_cap);
    u32   out_len = 0;

    char token_buf[VX_BUF_SIZE_64];
    u32  tok_len = 0;

    for (u32 i = 0; i <= raw_sv.len; i++)
    {
        char c = (i == raw_sv.len) ? CHAR_NULTERM : raw_sv.data[i];

        if (c == CHAR_DOT || c == CHAR_SLASH || c == CHAR_MINUS || c == CHAR_UNDERSCORE ||
            c == CHAR_NULTERM)
        {
            if (tok_len > 0)
            {
                token_buf[tok_len] = CHAR_NULTERM;

                const char *resolved_sub = nullptr;
                for (u32 v = 0; v < r->var_count; v++)
                {
                    if (strcmp(r->var_keys[v], token_buf) == 0)
                    {
                        resolved_sub = r->var_vals[v];
                        break;
                    }
                }

                const char *to_append = (resolved_sub != nullptr) ? resolved_sub : token_buf;

                u32 app_len = (u32) strlen(to_append);

                // clean out quotes
                if (app_len >= 2 && ((to_append[0] == CHAR_DOUBLE_QUOTE &&
                                      to_append[app_len - 1] == CHAR_DOUBLE_QUOTE) ||
                                     (to_append[0] == CHAR_SINGLE_QUOTE &&
                                      to_append[app_len - 1] == CHAR_SINGLE_QUOTE)))
                {
                    to_append++;
                    app_len -= 2;
                }

                if (out_len + app_len < out_cap - 1)
                {
                    memcpy(out_str + out_len, to_append, app_len);
                    out_len += app_len;
                }
                tok_len = 0;
            }

            if (c != CHAR_NULTERM && out_len < out_cap - 1)
            {
                out_str[out_len++] = c;
            }
        }
        else
        {
            if (tok_len < sizeof(token_buf) - 1)
            {
                token_buf[tok_len++] = c;
            }
        }
    }

    out_str[out_len] = CHAR_NULTERM;
    return out_str;
}

static char *resolve_val_or_var(struct sk_eval_result *r, char *cfg_str)
{
    if (r == nullptr || cfg_str == nullptr || cfg_str[0] == CHAR_NULTERM)
    {
        return cfg_str;
    }

    for (u32 i = 0; i < r->var_count; i++)
    {
        if (strcmp(r->var_keys[i], cfg_str) == 0)
        {
            return r->var_vals[i];
        }
    }

    return cfg_str;
}

static vx_status
sk_pipeline_codegen(struct sk_parser *p, vx_sv stormfile, u32 node, struct sk_eval_result *r)
{
    vx_sv path_sv = tok_to_sv(p, stormfile, p->nodes->data_a[node]);

    char *path = sv_to_arena(g_sk_global_arena, path_sv);

    FILE *f = fopen(path, "w");
    if (f == nullptr)
    {
        vx_errlog("codegen: failed to open '%s' for writing", path);
        return VX_ERROR;
    }

    fprintf(f, "// Auto-generated by Storm-Knell. Do not edit.\n");

    u32 cur = p->nodes->data_b[node];

    while (cur != SK_NODE_INVALID)
    {
        sk_ast_node_kind kind = p->nodes->kinds[cur];

        if (kind == SK_NODE_CODEGEN_DEFINE)
        {
            vx_sv key = tok_to_sv(p, stormfile, p->nodes->data_a[cur]);

            u32 val_token_idx = p->nodes->data_b[cur];

            sk_token_kind val_tok_kind = p->tokens->kinds[val_token_idx];

            vx_sv raw_val_sv  = tok_to_sv(p, stormfile, val_token_idx);
            bool  is_char_lit = (raw_val_sv.len >= 2 && raw_val_sv.data[0] == CHAR_SINGLE_QUOTE);

            char *resolved = resolve_token_or_var(p, stormfile, r, val_token_idx);

            if (val_tok_kind == SK_TOKEN_LIT_STRING)
            {
                if (is_char_lit)
                {
                    fprintf(f, "#define %.*s '%s'\n", (i32) key.len, key.data, resolved);
                }
                else
                {
                    fprintf(f, "#define %.*s \"%s\"\n", (i32) key.len, key.data, resolved);
                }
            }
            else
            {
                fprintf(f, "#define %.*s %s\n", (i32) key.len, key.data, resolved);
            }
        }
        else if (kind == SK_NODE_CODEGEN_LITERAL)
        {
            vx_sv lit = tok_to_sv(p, stormfile, p->nodes->data_a[cur]);
            fprintf(f, "%.*s\n", (i32) lit.len - 2, lit.data + 1);
        }

        cur = p->nodes->nexts[cur];
    }

    fclose(f);
    vx_log("Generated: %s", path);
    return VX_OK;
}

static vx_status resolve_depends(struct sk_eval_result *result)
{
    if (result == nullptr)
    {
        return VX_ERROR;
    }

    for (u32 i = 0; i < result->target_count; i++)
    {
        struct sk_target *t = &result->targets[i];

        for (u32 d = 0; d < t->depend_count; d++)
        {
            const char *dep_name = t->depends[d];

            bool found = false;

            for (u32 j = 0; j < result->target_count; j++)
            {
                if (strcmp(result->targets[j].name, dep_name) == 0)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                vx_errlog("Target '%s' depends on unknown target '%s'", t->name, dep_name);
                return VX_ERROR;
            }
        }
    }
    return VX_OK;
}
