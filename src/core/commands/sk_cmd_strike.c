#include "sk_cmd_strike.h"
#include "mem_arena.h"
#include "sk_cli.h"
#include "sk_globals.h"
#include "sk_lexer.h"
#include "sk_eval.h"
#include "sk_parser.h"
#include "sk_util.h"
#include "sk_paths.h"

#include "vx_io.h"
#include "vx_fs.h"

vx_status sk_cmd_strike_fn(struct sk_ctx *ctx, const char *path)
{
    if (ctx == nullptr)
    {
        return VX_ERROR;
    }

    const char *rpath = path ? path : vx_getcwd_fn();

    if (!sk_is_initialized_at(rpath))
    {
        vx_errlog("Storm-knell is not initialized for: %s", rpath);
        return VX_ERROR;
    }

    char stormfile_path[VX_PATH_MAX] = {0};
    snprintf(stormfile_path, sizeof(stormfile_path), "%s/" SK_PATH_STORMFILE, rpath);

    ctx->stormfile = vx_fs_read(stormfile_path, sk_arena_alloc, g_sk_global_arena);

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

    struct sk_eval_result result = {0};

    if (sk_eval_init(ctx, &result) != VX_OK)
    {
        VX_ASSERT_LOG("Failed to initialize eval");
        return VX_ERROR;
    }

    if (sk_eval(ctx, &p, &result) != VX_OK)
    {
        vx_errlog("Eval failed");
        return VX_ERROR;
    }

    if (ctx->active_opt & SK_OPT_VERBOSE)
    {
        sk_lx_dbg_dump_tokens(ctx);
        sk_parser_dbg_dump_ast(&p);
        sk_dbg_dump_eval(ctx, &p, &result);
        mem_arena_log_all_stats();
    }
    return VX_OK;
}
