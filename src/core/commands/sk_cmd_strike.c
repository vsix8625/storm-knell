#include "sk_cmd_strike.h"
#include "mem.h"
#include "sk_cli.h"
#include "sk_globals.h"
#include "sk_lexer.h"
#include "sk_eval.h"
#include "sk_parser.h"
#include "sk_util.h"
#include "sk_paths.h"

#include "vx_io.h"
#include "vx_fs.h"
#include <string.h>

vx_status sk_cmd_strike_fn(struct sk_ctx *ctx)
{
    if (ctx == nullptr)
    {
        return VX_ERROR;
    }

    if (sk_resolve_project_root(ctx) != VX_OK)
    {
        vx_errlog("Storm-knell is not initialized in this directory or any parent.");
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

    struct sk_lexer lx = {0};

    if (sk_lx_init(ctx, &lx) != VX_OK)
    {
        VX_ASSERT_LOG("Failed to initialize lexer");
        return VX_ERROR;
    }

    if (sk_lex(ctx, &lx) != VX_OK)
    {
        VX_ASSERT_LOG("Lexer failed");
        return VX_ERROR;
    }

    struct sk_parser p = {0};

    if (sk_parser_init(ctx, &p) != VX_OK)
    {
        VX_ASSERT_LOG("Failed to initialize parser");
        return VX_ERROR;
    }

    if (sk_top_level_parse(&p) != VX_OK)
    {
        vx_errlog("Parser failed");
        return VX_ERROR;
    }

    struct sk_eval_result *eval_result =
        mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_eval_result));
    memset(eval_result, 0, sizeof(struct sk_eval_result));

    vx_status strike_status = VX_OK;
    if (sk_eval(&p, eval_result) != VX_OK)
    {
        vx_errlog("Eval failed");
        strike_status = VX_ERROR;
    }

    //----------------------------------------------------------------------------------------------------
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

    return strike_status;
}
