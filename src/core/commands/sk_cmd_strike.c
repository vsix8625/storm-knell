#include "sk_cmd_strike.h"
#include "mem.h"
#include "sk_cli.h"
#include "sk_globals.h"
#include "sk_lexer.h"
#include "sk_eval.h"
#include "sk_parser.h"
#include "sk_pipeline.h"
#include "sk_invoke.h"
#include "sk_array.h"

#include "vx_io.h"
#include "vx_cpu.h"
#include "vx_util.h"
#include "vx_thread.h"
#include "vx_process.h"
#include <string.h>

//----------------------------------------------------------------------------------------------------

struct sk_work_unit
{
    const char *bin;
    char      **argv;
    const char *tag;
};

static vx_status sk_target_prepare_dirs(struct sk_ctx *ctx, struct sk_target *t);

static void *sk_worker_compile_fn(void *arg);

//----------------------------------------------------------------------------------------------------

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
    // NOTE: Initial Build stage

    // skip build if pipeline fails
    if (!dry_run && strike_status == VX_OK)
    {
        struct vx_thread_pool pool;

        ctx->cores       = ctx->cores == 0 ? vx_cpu_get_nproc() : ctx->cores;
        u32 thread_count = (ctx->threads == 0) ? ctx->cores - 1 : ctx->threads;

        u32 total_tasks = 0;
        for (u32 i = 0; i < eval_result->target_count; i++)
        {
            total_tasks += eval_result->targets[i].sources->count;
            total_tasks += 16;
        }

        if (vx_thread_pool_create(&pool, thread_count, total_tasks) != VX_OK)
        {
            // maybe fallback to single thread?
            VX_ASSERT_LOG("Failed to create thread pool");
            return VX_ERROR;
        }

        u32 t_count = eval_result->target_count;

        for (u32 i = 0; i < t_count; i++)
        {
            struct sk_target *t = &eval_result->targets[i];

            if (sk_target_prepare_dirs(ctx, t) != VX_OK)
            {
                strike_status = VX_ERROR;
                break;
            }

            //----------------------------------------------------------------------------------------------------
            // Invoke

            for (u32 j = 0; j < t->sources->count; j++)
            {
                u8 src_out_hash[SK_XXHASH_LEN];

                struct sk_hash_input *src_hsh_input = sk_hash_input_create();

                if (sk_hash_setup(t, j, src_hsh_input, src_out_hash) != VX_OK)
                {
                    VX_ASSERT_LOG("Failed to hash");
                }

                char **argv = sk_invoke_compile_nularr(t, j);

                struct sk_work_unit *unit =
                    mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_work_unit));

                unit->bin  = argv[0];
                unit->argv = argv;
                unit->tag  = (const char *) t->sources->items[j];

                vx_thread_pool_push(&pool, sk_worker_compile_fn, unit);

                if (ctx->active_opt & SK_OPT_VERBOSE)
                {
                    const char *cmd = sk_invoke_compile(t, j);
                    vx_log("%s: %s", t->name, cmd);
                }

                if (ctx->active_opt & SK_OPT_VERBOSE)
                {
                    vx_log("Total tasks: %d", total_tasks);
                }
            }

            //----------------------------------------------------------------------------------------------------
        }

        vx_thread_pool_wait(&pool);

        vx_thread_pool_destroy(&pool);
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
        vx_log("Cores: %u | Threads: %u", ctx->cores, ctx->threads);
    }
    //----------------------------------------------------------------------------------------------------

    if (dry_run)
    {
        if (strike_status != VX_OK)
        {
            vx_warn("DRY-RUN status: Build will fail (%u error(s))",
                    ctx->tokens->err_count + ctx->nodes->err_count);
        }
        else
        {
            vx_log("DRY-RUN status: Strike is clean");
        }
    }
    return strike_status;
}

//----------------------------------------------------------------------------------------------------

static vx_status sk_target_prepare_dirs(struct sk_ctx *ctx, struct sk_target *t)
{
    if (ctx == nullptr || t == nullptr)
    {
        return VX_ERROR;
    }

    if (ctx->active_opt & SK_OPT_STRIKE_REL)
    {
        t->build_mode = "release";
        vx_log("Mode: %s", t->build_mode);
    }

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

    t->finalized_obj_dirpath = mem_heap_strdup(object_dir_buf);

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
        return VX_ERROR;
    }

    if (vx_mkdir_p(object_dir_buf) != VX_OK)
    {
        VX_ASSERT_LOG("Failed to create target '%s' object directory", t->name);
        return VX_ERROR;
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

    return VX_OK;
}

// ----------------------------------------------------------------------------------------------------

static void *sk_worker_compile_fn(void *arg)
{
    struct sk_work_unit *unit = (struct sk_work_unit *) arg;
    struct vx_process    proc = {0};

    if (vx_process_spawn(&proc, unit->bin, unit->argv, nullptr) == VX_OK)
    {
        vx_process_wait(&proc);

        if (proc.exit_code != 0)
        {
            vx_errlog("Failed to compile: %s", unit->tag);
        }
    }
    else
    {
        vx_errlog("Could not spawn compiler for: %s", unit->tag);
    }

    return nullptr;
}
