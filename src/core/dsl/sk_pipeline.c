#include "sk_pipeline.h"
#include "sk_lexer.h"
#include "sk_parser.h"
#include "sk_eval.h"
#include "sk_globals.h"
#include "sk_paths.h"
#include "vx_fs.h"
#include "vx_io.h"
#include <string.h>

vx_status sk_pipeline_run(struct sk_ctx         *ctx,
                          struct sk_lexer       *lx,
                          struct sk_parser      *p,
                          struct sk_eval_result *ev_result)
{
    if (ctx == nullptr || lx == nullptr || p == nullptr || ev_result == nullptr)
    {
        return VX_ERROR;
    }

    if (sk_resolve_project_root(ctx) != VX_OK)
    {
        vx_errlog("Storm-knell is not initialized in '%s'  directory or any parent",
                  ctx->rpath ? ctx->rpath : vx_getcwd_fn());
        return VX_ERROR;
    }

    if (vx_chdir(ctx->init_dir) != VX_OK)
    {
        vx_errlog("Failed to chdir to project root: %s", ctx->init_dir);
        return VX_ERROR;
    }
    vx_dbglog("Working directory: %s", ctx->init_dir);

    ctx->stormfile = vx_fs_read(SK_PATH_STORMFILE, sk_arena_alloc, g_sk_global_arena);

    if (ctx->stormfile.data == nullptr)
    {
        return VX_ERROR;
    }

    //----------------------------------------------------------------------------------------------------
    // Lex

    vx_status pipeline_status = VX_OK;

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

    //----------------------------------------------------------------------------------------------------
    // Parse

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

    //----------------------------------------------------------------------------------------------------
    // Eval

    if (sk_eval(p, ev_result) != VX_OK)
    {
        vx_errlog("Eval failed");
        pipeline_status = VX_ERROR;
    }

    return pipeline_status;
}
