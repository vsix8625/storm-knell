#include "sk_cmd_strike.h"
#include "mem.h"
#include "sk_cli.h"
#include "sk_cmd_init.h"
#include "sk_globals.h"
#include "sk_lexer.h"
#include "sk_parser.h"
#include "sk_pipeline.h"
#include "sk_invoke.h"
#include "sk_array.h"
#include "sk_cache.h"
#include "sk_paths.h"

#include "vx_fs.h"
#include "vx_io.h"
#include "vx_cpu.h"
#include "vx_time.h"
#include "vx_util.h"
#include "vx_thread.h"
#include "vx_process.h"
#include <string.h>

//----------------------------------------------------------------------------------------------------

_Atomic u32 g_cache_hits     = 0;
_Atomic u32 g_cache_misses   = 0;
_Atomic u32 g_compile_errors = 0;

_Atomic u64 g_compile_start_ns = 0;
_Atomic u64 g_compile_end_ns   = 0;

static _Thread_local struct mem_arena *tls_worker_arena = nullptr;

vx_mutex g_proc_spawn_mutex;

struct sk_work_unit
{
    struct sk_target *target;

    u32 source_idx;
    u8  dry_run;
    u8  gen_ccmds;
    u8  pad[2];

    struct sk_meta *meta;

    const char *tag;

    vx_sbuf diagnostic_log;
};

static vx_status sk_target_prepare_dirs(struct sk_ctx *ctx, struct sk_target *t);

static void *sk_worker_compile_fn(void *arg);

static vx_status topo_visit(struct sk_eval_result *result,
                            u32                    idx,
                            u32                   *sorted,
                            u32                   *sorted_count,
                            bool                  *visited,
                            bool                  *in_stack);

//----------------------------------------------------------------------------------------------------

vx_status sk_cmd_strike_fn(struct sk_ctx *ctx)
{
    if (ctx == nullptr)
    {
        return VX_ERROR;
    }
    if (sk_resolve_project_root(ctx) != VX_OK)
    {
        vx_errlog("Storm-knell is not initialized in '%s'  directory or any parent",
                  ctx->rpath ? ctx->rpath : vx_getcwd_fn());
        return VX_ERROR;
    }
    if (vx_chdir(ctx->rpath) != VX_OK)
    {
        vx_errlog("Failed to change dir to project root: %s", ctx->rpath);
        return VX_ERROR;
    }
    vx_log("Working directory: %s", ctx->rpath);

    bool is_tty = vx_isatty(STDOUT_FILENO);

    vx_ticks profile = {0};
    if (ctx->active_opt & SK_OPT_PROFILE)
    {
        vx_ticks_start(&profile);
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

    bool dry_run    = ctx->active_opt & SK_OPT_STRIKE_DRY;
    bool skip_build = ctx->active_opt & (SK_OPT_EVAL_DUMP | SK_OPT_NODE_DUMP | SK_OPT_TOK_DUMP);

    //----------------------------------------------------------------------------------------------------

    // skip build if parser pipeline fails
    if (!skip_build && strike_status == VX_OK)
    {
        struct vx_thread_pool pool;

        vx_mutex_init(&g_proc_spawn_mutex);

        ctx->cores       = ctx->cores == 0 ? vx_cpu_get_nproc() : ctx->cores;
        u32 thread_count = (ctx->threads == 0) ? ctx->cores - 1 : ctx->threads;

        u32 total_sources = 0;

        for (u32 i = 0; i < eval_result->target_count; i++)
        {
            total_sources += eval_result->targets[i].sources->count;
        }

        u32 total_tasks = total_sources + (eval_result->target_count * 16);

        g_sk_ccmds =
            mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_ccmds_entry) * total_tasks);

        if (vx_thread_pool_create(&pool, thread_count, total_tasks) != VX_OK)
        {
            VX_ASSERT_LOG("Failed to create thread pool");
            return VX_ERROR;
        }

        u32 t_count = eval_result->target_count;

        //----------------------------------------------------------------------------------------------------
        // topo_visit

        u32  *sorted   = mem_arena_alloc(g_sk_global_arena, sizeof(u32) * SK_MAX_TARGETS);
        bool *visited  = mem_arena_alloc(g_sk_global_arena, sizeof(bool) * SK_MAX_TARGETS);
        bool *in_stack = mem_arena_alloc(g_sk_global_arena, sizeof(bool) * SK_MAX_TARGETS);

        u32 sorted_count = 0;
        memset(visited, 0, sizeof(bool) * SK_MAX_TARGETS);
        memset(in_stack, 0, sizeof(bool) * SK_MAX_TARGETS);
        // ----------------------------------------------------------

        for (u32 i = 0; i < t_count; i++)
        {
            if (topo_visit(eval_result, i, sorted, &sorted_count, visited, in_stack) != VX_OK)
            {
                return VX_ERROR;
            }
        }

        //----------------------------------------------------------------------------------------------------
        // Main loop

        struct sk_work_unit **work_units =
            mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_work_unit *) * total_sources);

        u32 unit_idx = 0;

        vx_platform_get_cache_dir();  // warm up

        for (u32 i = 0; i < sorted_count; i++)
        {
            struct sk_target *t    = &eval_result->targets[sorted[i]];
            struct sk_meta    meta = {0};

            char abs_cc[VX_PATH_MAX];
            if (vx_fs_which(t->cfg.cc, abs_cc, sizeof(abs_cc)) != VX_OK)
            {
                vx_errlog("Compiler executable '%s' not found in PATH", t->cfg.cc);
                continue;
            }

            if (!sk_meta_load(&meta, abs_cc))
            {
                vx_errlog("Compiler %s not initialized", abs_cc);
                continue;
            }

            t->cfg.cc = mem_arena_strdup(g_sk_global_arena, abs_cc);

            if (sk_target_prepare_dirs(ctx, t) != VX_OK)
            {
                strike_status = VX_ERROR;
                break;
            }

            //----------------------------------------------------------------------------------------------------

            for (u32 j = 0; j < t->sources->count; j++)
            {
                struct sk_work_unit *unit =
                    mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_work_unit));

                work_units[unit_idx++] = unit;

                unit->target     = t;
                unit->source_idx = j;
                unit->meta       = &meta;
                unit->tag        = (const char *) t->sources->items[j];
                unit->dry_run    = dry_run;

                u32 log_size = VX_BUF_SIZE_4096;

                char *log_mem = mem_arena_alloc(g_sk_global_arena, log_size);
                log_mem[0]    = CHAR_NULTERM;

                unit->diagnostic_log.data   = log_mem;
                unit->diagnostic_log.size   = log_size;
                unit->diagnostic_log.offset = 0;

                if (ctx->active_opt & SK_OPT_GEN_CCMDS)
                {
                    unit->gen_ccmds = true;
                }

                vx_thread_pool_push(&pool, sk_worker_compile_fn, unit);
            }

            //----------------------------------------------------------------------------------------------------
        }

        if (ctx->active_opt & SK_OPT_VERBOSE)
        {
            vx_log("Total tasks: %d", total_tasks);
        }

        vx_thread_pool_wait(&pool);
        vx_thread_pool_destroy(&pool);

        // Diagnostic logs
        if (atomic_load(&g_compile_errors) > 0)
        {
            for (u32 j = 0; j < total_sources; j++)
            {
                struct sk_work_unit *unit = work_units[j];

                if (unit->diagnostic_log.offset > 0)
                {
                    char *tty_color = "";
                    char *tty_bold  = "";
                    char *tty_reset = "";
                    if (is_tty)
                    {
                        tty_color = "\033[38;5;160m";
                        tty_bold  = "\033[0;1m";
                        tty_reset = "\033[0m";
                    }

                    vx_printf("\n[%sERROR LOG FOR %s %s%s]:\n%s\n",
                              tty_color,
                              tty_bold,
                              unit->tag,
                              tty_reset,
                              unit->diagnostic_log.data);
                    vx_printf(
                        "------------------------------------------------------------------\n");
                }
            }
        }

        //----------------------------------------------------------------------------------------------------
        // SERIALIZE

        FILE *manifest_f = fopen(SK_PATH_STORM_MANIFEST_BIN, "wb");
        if (manifest_f != nullptr)
        {
            struct sk_manifest_header header = {.target_count        = t_count,
                                                .global_cache_hits   = atomic_load(&g_cache_hits),
                                                .global_cache_misses = atomic_load(&g_cache_misses),
                                                .global_compile_errors =
                                                    atomic_load(&g_compile_errors)};

            fwrite(&header, sizeof(struct sk_manifest_header), 1, manifest_f);

            for (u32 i = 0; i < t_count; i++)
            {
                struct sk_target *t = &eval_result->targets[i];

                struct sk_target_persist out_meta = {0};

                sk_strncpy_safe(out_meta.name, t->name, sizeof(out_meta.name));
                sk_strncpy_safe(out_meta.out_dir, t->out_dir, sizeof(out_meta.out_dir));

                if (t->finalized_bin_dirpath && t->out_name)
                {
                    sk_strncpy_safe(out_meta.bin_dirpath,
                                    t->finalized_bin_dirpath,
                                    sizeof(out_meta.bin_dirpath));
                }

                if (t->kind == SK_TARGET_KIND_EXEC)
                {
                    if (t->finalized_bin_rpath)
                    {
                        sk_strncpy_safe(
                            out_meta.bin_path, t->finalized_bin_rpath, sizeof(out_meta.bin_path));
                    }
                }
                else
                {
                    if (t->finalized_bin_dirpath && t->out_name)
                    {
                        sk_strncpy_safe(out_meta.bin_dirpath,
                                        t->finalized_bin_dirpath,
                                        sizeof(out_meta.bin_dirpath));

                        snprintf(out_meta.bin_path,
                                 sizeof(out_meta.bin_path),
                                 "%s%s%s",
                                 t->finalized_bin_dirpath,
                                 VX_PATH_SEP_STR,
                                 t->out_name);
                    }
                }

                out_meta.kind           = t->kind;
                out_meta.total_files    = t->sources->count;
                out_meta.last_strike_ts = vx_time_epoch_s();

                fwrite(&out_meta, sizeof(struct sk_target_persist), 1, manifest_f);
            }

            fclose(manifest_f);
        }

        //----------------------------------------------------------------------------------------------------

        if (ctx->active_opt & SK_OPT_PROFILE)
        {
            vx_ticks compile_time = {.start = atomic_load(&g_compile_start_ns),
                                     .end   = atomic_load(&g_compile_end_ns)};
            char     elapsed[32];
            char    *elapsed_fmt = vx_ticks_format(&compile_time, elapsed, sizeof(elapsed));
            vx_sbuf_append(&g_sk_profile_sbuf, "%s: Compile: %s\n", __func__, elapsed_fmt);
        }

        vx_log("[cache]: %u hits, %u, %u total",
               g_cache_hits,
               g_cache_misses,
               g_cache_hits + g_cache_misses);

        if (g_compile_errors > 0)
        {
            vx_errlog("Build failed: %u file(s) failed to compile", g_compile_errors);
            strike_status = VX_ERROR;
        }

        //----------------------------------------------------------------------------------------------------
        // LINK

        if (!dry_run && g_compile_errors == 0)
        {
            vx_ticks link_time = {0};
            vx_ticks_start(&link_time);

            for (u32 i = 0; i < sorted_count; i++)
            {
                struct sk_target *t    = &eval_result->targets[sorted[i]];
                struct sk_meta    meta = {0};
                sk_meta_load(&meta, t->cfg.cc);

                //----------------------------------------------------------------------------------------------------
                // depends injection

                for (u32 d = 0; d < t->depend_count; d++)
                {
                    for (u32 j = 0; j < t->depend_count; j++)
                    {
                        struct sk_target *dep = &eval_result->targets[j];
                        if (strcmp(dep->name, t->depends[d]) == 0)
                        {
                            if (dep->kind == SK_TARGET_KIND_STATIC ||
                                dep->kind == SK_TARGET_KIND_SHARED)
                            {
                                char *lflag_L = mem_arena_alloc(g_sk_global_arena, VX_PATH_MAX);
                                snprintf(lflag_L, VX_PATH_MAX, "-L%s", dep->finalized_bin_dirpath);
                                t->cfg.lflags[t->cfg.lflags_count++] = lflag_L;

                                char *lflag_l = mem_arena_alloc(g_sk_global_arena, VX_BUF_SIZE_64);
                                snprintf(lflag_l, VX_BUF_SIZE_64, "-l%s", dep->out_name);
                                t->cfg.lflags[t->cfg.lflags_count++] = lflag_l;
                            }
                            break;
                        }
                    }
                }

                //----------------------------------------------------------------------------------------------------

                struct vx_process proc = {0};

                char **argv;

                if (t->kind == SK_TARGET_KIND_STATIC)
                {
                    argv = sk_invoke_ar_nularr(t, &meta, g_sk_global_arena);
                }
                else
                {
                    argv = sk_invoke_link_nularr(t, g_sk_global_arena);
                }

                if (vx_process_spawn(&proc, argv[0], argv, nullptr) == VX_OK)
                {
                    vx_process_wait(&proc);
                    if (proc.exit_code != 0)
                    {
                        vx_errlog("Failed to link target: %s", t->name);
                    }
                    else
                    {
                        if (t->kind == SK_TARGET_KIND_EXEC && t->install_dir != nullptr)
                        {
                            char *dest_path =
                                sk_path_join(g_sk_global_arena, t->install_dir, t->out_name);

                            if (vx_isfile(dest_path))
                            {
                                vx_fs_rmrf(dest_path);
                            }

                            if (vx_mkdir_p(t->install_dir) != VX_OK)
                            {
                                vx_errlog("Failed to create installation directory: %s",
                                          t->install_dir);
                            }

                            vx_log("Copying: '%s' to '%s'", t->out_name, dest_path);

                            if (!vx_fs_cp(t->finalized_bin_rpath, dest_path))
                            {
                                vx_errlog("Failed to copy binary to install location: %s",
                                          dest_path);
                            }
                        }
                    }
                }
            }

            if (ctx->active_opt & SK_OPT_PROFILE)
            {
                vx_ticks_end(&link_time);
                char  elapsed[32];
                char *elapsed_fmt = vx_ticks_format(&link_time, elapsed, sizeof(elapsed));
                vx_sbuf_append(&g_sk_profile_sbuf, "%s: Link: %s\n", __func__, elapsed_fmt);
            }

            if (ctx->active_opt & SK_OPT_GEN_CCMDS)
            {
                sk_ccmds_write(ctx->rpath);
            }
        }

        // cache check and prune if needed
        u64 current_cache_bytes = sk_cache_calculate_size();

        u64 max_allowed_b  = (u64) ctx->ccfg.max_size_mb * 1024 * 1024;
        u64 prune_target_b = (u64) ctx->ccfg.prune_threshold_mb * 1024 * 1024;

        if (current_cache_bytes >= max_allowed_b)
        {
            vx_log("Cache size (%.2f MB) exceeds limit (%.2f MB). Initiating prune",
                   current_cache_bytes / 1048576.0f,
                   max_allowed_b / 1048576.0f);

            sk_cache_prune_to_size(ctx->ccfg.prune_threshold_mb);

            u64 new_cache_bytes = sk_cache_calculate_size();
            vx_log("Prune complete. New cache size: %.2f MB", new_cache_bytes / 1048576.0f);
            VX_CAST(void, prune_target_b);
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
        vx_log("Cores: %u | Threads: %u", ctx->cores, ctx->threads);
    }

    //----------------------------------------------------------------------------------------------------

    if (dry_run)
    {
        if (strike_status != VX_OK)
        {
            vx_warn("DRY-RUN status: Build will fail (%u Stormfile error(s), %u compile error(s))",
                    ctx->tokens->err_count + ctx->nodes->err_count,
                    g_compile_errors);
        }
        else
        {
            vx_log("DRY-RUN status: Strike is clean");
        }
    }

    if (ctx->active_opt & SK_OPT_PROFILE)
    {
        vx_ticks_end(&profile);
        char  elapsed[32];
        char *elapsed_fmt = vx_ticks_format(&profile, elapsed, sizeof(elapsed));
        vx_sbuf_append(&g_sk_profile_sbuf, "%s: %s\n", __func__, elapsed_fmt);
    }

    vx_mutex_destroy(&g_proc_spawn_mutex);
    return strike_status;
}

//----------------------------------------------------------------------------------------------------

// NOTE: keep an eye
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

    if (t->out_dir && t->out_dir[0] != CHAR_NULTERM)
    {
        snprintf(object_dir_buf,
                 sizeof(object_dir_buf),
                 "%s%s%s%s%s%s%s",
                 t->out_dir,
                 VX_PATH_SEP_STR,
                 t->name,
                 VX_PATH_SEP_STR,
                 t->build_mode,
                 VX_PATH_SEP_STR,
                 "obj");
    }
    else
    {
        snprintf(object_dir_buf,
                 sizeof(object_dir_buf),
                 "%s%s%s%s%s%s%s%s%s",
                 ctx->rpath,
                 VX_PATH_SEP_STR,
                 "crater",
                 VX_PATH_SEP_STR,
                 t->name,
                 VX_PATH_SEP_STR,
                 t->build_mode,
                 VX_PATH_SEP_STR,
                 "obj");
    }

    // NOTE: at this point object_dir_buf should be resolved
    if (vx_mkdir_p(object_dir_buf) != VX_OK)
    {
        VX_ASSERT_LOG("Failed to create: %s", object_dir_buf);
        return VX_ERROR;
    }

    char abs_dir_buf[VX_PATH_MAX];

    if (vx_fs_realpath(object_dir_buf, abs_dir_buf) == nullptr)
    {
        VX_ASSERT_LOG("realpath failed for: %s", object_dir_buf);
        return VX_ERROR;
    }
    t->finalized_obj_dirpath = mem_arena_strdup(g_sk_global_arena, abs_dir_buf);

    if (t->kind == SK_TARGET_KIND_EXEC)
    {
        bin_container = "bin";
    }

    snprintf(final_bin_dir_buf,
             sizeof(final_bin_dir_buf),
             "%s%s%s%s%s%s%s",
             t->out_dir,
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

    // the final ../../bin
    t->finalized_bin_dirpath = mem_arena_strdup(g_sk_global_arena, final_bin_dir_buf);

    t->finalized_bin_rpath = nullptr;

    if (t->kind == SK_TARGET_KIND_EXEC)
    {
        // executable
        size_t needed =
            strlen(final_bin_dir_buf) + strlen(VX_PATH_SEP_STR) + strlen(t->out_name) + 1;
        char *bin_rpath_buf = mem_arena_alloc(g_sk_global_arena, needed);
        snprintf(bin_rpath_buf, needed, "%s%s%s", final_bin_dir_buf, VX_PATH_SEP_STR, t->out_name);

        // the final ../../bin/out_name
        t->finalized_bin_rpath = mem_arena_strdup(g_sk_global_arena, bin_rpath_buf);
    }

    if (ctx->active_opt & SK_OPT_VERBOSE)
    {
        vx_log("Created: %s", final_bin_dir_buf);
    }

    return VX_OK;
}

// ----------------------------------------------------------------------------------------------------

static vx_status topo_visit(struct sk_eval_result *result,
                            u32                    idx,
                            u32                   *sorted,
                            u32                   *sorted_count,
                            bool                  *visited,
                            bool                  *in_stack)
{
    if (in_stack[idx])
    {
        vx_errlog("Circular dependency detected at target '%s'", result->targets[idx].name);
        return VX_ERROR;
    }

    if (visited[idx])
    {
        return VX_OK;
    }
    visited[idx]  = true;
    in_stack[idx] = true;

    struct sk_target *t = &result->targets[idx];
    for (u32 d = 0; d < t->depend_count; d++)
    {
        for (u32 j = 0; j < result->target_count; j++)
        {
            if (strcmp(result->targets[j].name, t->depends[d]) == 0)
            {
                if (topo_visit(result, j, sorted, sorted_count, visited, in_stack) != VX_OK)
                {
                    return VX_ERROR;
                }
                break;
            }
        }
    }

    in_stack[idx]             = false;
    sorted[(*sorted_count)++] = idx;
    return VX_OK;
}

static void *sk_worker_compile_fn(void *arg)
{
    if (tls_worker_arena == nullptr)
    {
        tls_worker_arena = mem_arena_create("worker-scratch", 4 * 1024 * 1024);
    }

    struct mem_arena *arena = tls_worker_arena;

    struct sk_work_unit *unit = (struct sk_work_unit *) arg;
    struct vx_process    proc = {0};

    struct sk_target *t = unit->target;

    struct sk_hash_input h_in = {0};

    u8 out_hash[SK_XXHASH_LEN];

    u64 expected = 0;

    u64 now = vx_time_ns();
    atomic_compare_exchange_strong(&g_compile_start_ns, &expected, now);

    if (sk_hash_setup(t, unit->source_idx, unit->meta, &h_in, out_hash, arena) == VX_OK)
    {
        const char *src_path  = (const char *) t->sources->items[unit->source_idx];
        const char *file_name = strrchr(src_path, VX_PATH_SEP);
        file_name             = file_name ? file_name + 1 : src_path;
        char *obj_path        = mem_arena_alloc(arena, VX_PATH_MAX);

        snprintf(obj_path,
                 VX_PATH_MAX,
                 "%s%s%s.o",
                 t->finalized_obj_dirpath,
                 VX_PATH_SEP_STR,
                 file_name);

        char **argv = (unit->dry_run) ? sk_invoke_syntax_check_nularr(t, unit->source_idx, arena)
                                      : sk_invoke_compile_nularr(t, unit->source_idx, arena);

        if (unit->gen_ccmds && !unit->dry_run)
        {
            u32 arg_count = 0;
            for (size_t i = 0; argv[i] != nullptr; i++)
            {
                arg_count++;
            }
            sk_ccmds_push(src_path, g_sk_global_ctx.rpath, (const char **) argv, arg_count);
        }

        struct sk_cache_entry cache_entry = {0};
        sk_cache_resolve(out_hash, &cache_entry);

        if (!unit->dry_run)
        {
            if (sk_cache_exists(&cache_entry))
            {
                atomic_fetch_add(&g_cache_hits, 1);
                sk_cache_restore(&cache_entry, obj_path);
                atomic_store(&g_compile_end_ns, vx_time_ns());

                mem_arena_soft_reset(arena);
                return nullptr;
            }
        }

        struct vx_proc_cfg cfg = {.flags = VX_PROCESS_FLAGS_CAPTURE};

        vx_mutex_lock(&g_proc_spawn_mutex);
        vx_status status = vx_process_spawn(&proc, argv[0], argv, &cfg);
        vx_mutex_unlock(&g_proc_spawn_mutex);

        if (status == VX_OK)
        {
            vx_process_wait(&proc);
            vx_process_consume_output(&proc, &unit->diagnostic_log);

            if (proc.exit_code == 0)
            {
                if (!unit->dry_run)
                {
                    if (sk_cache_store(&cache_entry, obj_path) != VX_OK)
                    {
                        vx_errlog("Failed to store '%s' to cache", obj_path);
                    }
                    atomic_fetch_add(&g_cache_misses, 1);
                }
            }
            else
            {
                vx_errlog("Failed to compile: %s", unit->tag);

                atomic_fetch_add(&g_compile_errors, 1);
            }
        }
        else
        {
            vx_errlog("Could not spawn compiler for: %s", unit->tag);
        }

        atomic_store(&g_compile_end_ns, vx_time_ns());
        mem_arena_soft_reset(arena);
    }

    return nullptr;
}
