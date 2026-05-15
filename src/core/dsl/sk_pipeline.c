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

        final_excludes[saved_excl_count]     = t->build_dir;
        final_excludes[saved_excl_count + 1] = SK_PATH_STORM_DIR;

        t->excludes      = final_excludes;
        t->exclude_count = saved_excl_count + extra_excl_count;

        for (u32 j = 0; j < t->scan_dirs->count; j++)
        {
            char *dir_to_scan = (char *) t->scan_dirs->items[j];

            sk_scan_dir_r(t->sources, t->excludes, t->exclude_count, dir_to_scan);
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
        char  elapsed[32];
        char *elapsed_fmt = vx_ticks_format(&lexer_time, elapsed, sizeof(elapsed));
        vx_sbuf_append(&g_sk_profile_sbuf, "%s: Lexer: %s\n", __func__, elapsed_fmt);
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
        char  elapsed[32];
        char *elapsed_fmt = vx_ticks_format(&parser_time, elapsed, sizeof(elapsed));
        vx_sbuf_append(&g_sk_profile_sbuf, "%s: Parser: %s\n", __func__, elapsed_fmt);
    }

    //----------------------------------------------------------------------------------------------------
    // Eval

    if (ctx->active_opt & SK_OPT_PROFILE)
    {
        vx_ticks_start(&eval_time);
    }
    if (sk_eval(p, ev_result) != VX_OK)
    {
        vx_errlog("Eval failed");
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
        char  elapsed[32];
        char *elapsed_fmt = vx_ticks_format(&eval_time, elapsed, sizeof(elapsed));
        vx_sbuf_append(&g_sk_profile_sbuf, "%s: Eval: %s\n", __func__, elapsed_fmt);
    }

    if (ctx->nodes->err_count > 0)
    {
        pipeline_status = VX_ERROR;
    }

    return pipeline_status;
}
