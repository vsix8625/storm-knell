#include "sk_cmd_strike.h"
#include "mem.h"
#include "sk_cli.h"
#include "sk_globals.h"
#include "sk_lexer.h"
#include "sk_eval.h"
#include "sk_parser.h"
#include "sk_pipeline.h"

#include "vx_io.h"
#include "vx_util.h"
#include <string.h>

vx_status sk_cmd_strike_fn(struct sk_ctx *ctx)
{
    if (ctx == nullptr)
    {
        return VX_ERROR;
    }

    struct sk_lexer  lx = {0};
    struct sk_parser p  = {0};

    struct sk_eval_result *eval_result =
        mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_eval_result));
    memset(eval_result, 0, sizeof(struct sk_eval_result));

    vx_status strike_status = VX_OK;

    if (sk_pipeline_run(ctx, &lx, &p, eval_result) != VX_OK)
    {
        strike_status = VX_ERROR;
    }

    bool dry_run = false;
    if (ctx->active_opt &
        (SK_OPT_EVAL_DUMP | SK_OPT_NODE_DUMP | SK_OPT_TOK_DUMP | SK_OPT_STRIKE_DRY))
    {
        dry_run = true;
    }

    //----------------------------------------------------------------------------------------------------
    // Build

    // skip build if pipeline fails
    if (!dry_run && strike_status == VX_OK)
    {
        u32 t_count = eval_result->target_count;

        for (u32 i = 0; i < t_count; i++)
        {
            struct sk_target *t = &eval_result->targets[i];

            char  final_bin_dir_buf[VX_PATH_MAX];
            char *bin_container = "lib";

            char object_dir_buf[VX_PATH_MAX];
            snprintf(object_dir_buf,
                     sizeof(object_dir_buf),
                     "%s%s%s%s%s%s%s",
                     t->build_dir,
                     VX_PATH_SEP_STR,
                     t->name,
                     VX_PATH_SEP_STR,
                     t->build_mode,
                     VX_PATH_SEP_STR,
                     "obj");

            // TODO: maybe a so/ for shared
            if (t->kind == SK_TARGET_KIND_EXEC)
            {
                bin_container = "bin";
            }

            snprintf(final_bin_dir_buf,
                     sizeof(final_bin_dir_buf),
                     "%s%s%s%s%s%s%s",
                     t->build_dir,
                     VX_PATH_SEP_STR,
                     t->name,
                     VX_PATH_SEP_STR,
                     t->build_mode,
                     VX_PATH_SEP_STR,
                     bin_container);

            if (vx_mkdir_p(final_bin_dir_buf) != VX_OK)
            {
                VX_ASSERT_LOG("Failed to create target '%s' build directory", t->name);
                strike_status = VX_ERROR;
                break;
            }

            if (vx_mkdir_p(object_dir_buf) != VX_OK)
            {
                VX_ASSERT_LOG("Failed to create target '%s' object directory", t->name);
                strike_status = VX_ERROR;
                break;
            }

            t->finalized_bin_dirpath = mem_heap_strdup(final_bin_dir_buf);
            t->finalized_bin_rpath   = nullptr;

            if (t->kind == SK_TARGET_KIND_EXEC)
            {
                // executable
                char bin_rpath_buf[VX_PATH_MAX];
                snprintf(bin_rpath_buf,
                         sizeof(bin_rpath_buf),
                         "%s%s%s",
                         final_bin_dir_buf,
                         VX_PATH_SEP_STR,
                         t->out_name);

                t->finalized_bin_rpath = mem_heap_strdup(bin_rpath_buf);
            }

            if (ctx->active_opt & SK_OPT_VERBOSE)
            {
                vx_log("Created: %s", final_bin_dir_buf);
            }
        }
    }

    //----------------------------------------------------------------------------------------------------
    // Logs

    if (ctx->active_opt & SK_OPT_TOK_DUMP)
    {
        sk_lx_dbg_dump_tokens(ctx);
    }
    if (ctx->active_opt & SK_OPT_NODE_DUMP)
    {
        sk_parser_dbg_dump_ast(&p);
    }
    if (ctx->active_opt & SK_OPT_EVAL_DUMP)
    {
        sk_dbg_dump_eval(&p, eval_result);
    }

    if (ctx->active_opt & SK_OPT_VERBOSE)
    {
        vx_log("Tokens: %u | Nodes: %u | Ratio: %.2f%%",
               p.tokens->count,
               p.nodes->count,
               ((f32) p.nodes->count / p.tokens->count) * 100);
        vx_log("Targets: %u | Variables: %u", eval_result->target_count, eval_result->var_count);
        vx_log("Errors: %u in tokens | %u in nodes", p.tokens->err_count, p.nodes->err_count);
    }
    //----------------------------------------------------------------------------------------------------

    if (dry_run)
    {
        vx_warn("DRY-RUN... Nothing generated");
    }
    return strike_status;
}
