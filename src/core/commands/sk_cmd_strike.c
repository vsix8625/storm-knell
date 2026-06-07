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

// NOTE: No worker cleanup atm but OS reclaims the allocation when process exits
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
        mem_arena_zalloc(g_sk_global_arena, sizeof(struct sk_eval_result));

    vx_status strike_status = VX_OK;

    if (sk_pipeline_run(ctx, &lx, &p, eval_result) != VX_OK)
    {
        strike_status = VX_ERROR;
    }

    bool dry_run    = ctx->active_opt & SK_OPT_STRIKE_DRY;
    bool skip_build = ctx->active_opt & (SK_OPT_EVAL_DUMP | SK_OPT_NODE_DUMP | SK_OPT_TOK_DUMP);

    //----------------------------------------------------------------------------------------------------

    u32 total_tasks = 0;

    // skip build if parser pipeline fails
    if (!skip_build && strike_status == VX_OK)
    {
        struct vx_thread_pool pool;

        vx_mutex_init(&g_proc_spawn_mutex);
        vx_mutex_init(&ctx->console_lock);

        u32 thread_count = (ctx->threads > 0) ? ctx->threads : ctx->cores;
        ctx->threads     = thread_count;

        u32 total_sources = 0;

        for (u32 i = 0; i < eval_result->target_count; i++)
        {
            total_sources += eval_result->targets[i].sources->count;
        }

        total_tasks = total_sources + eval_result->target_count;

        g_sk_ccmds =
            mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_ccmds_entry) * total_tasks);

        if (eval_result->target_count == 0)
        {
            vx_log("Nothing to build");
            return VX_OK;
        }

        // cache record
        g_sk_cache_records =
            mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_cache_proj_entry) * total_sources);
        atomic_store(&g_sk_cache_record_count, 0);

        if (vx_thread_pool_create(&pool, thread_count, total_tasks) != VX_OK)
        {
            VX_ASSERT_LOG("Failed to create thread pool");
            return VX_ERROR;
        }

        u32 t_count = eval_result->target_count;

        //----------------------------------------------------------------------------------------------------
        // topo_visit

        u32  *sorted   = mem_arena_zalloc(g_sk_global_arena, sizeof(u32) * SK_MAX_TARGETS);
        bool *visited  = mem_arena_zalloc(g_sk_global_arena, sizeof(bool) * SK_MAX_TARGETS);
        bool *in_stack = mem_arena_zalloc(g_sk_global_arena, sizeof(bool) * SK_MAX_TARGETS);

        u32 sorted_count = 0;
        // ----------------------------------------------------------

        for (u32 i = 0; i < t_count; i++)
        {
            if (topo_visit(eval_result, i, sorted, &sorted_count, visited, in_stack) != VX_OK)
            {
                return VX_ERROR;
            }
        }

        //----------------------------------------------------------------------------------------------------
        // MAIN LOOP

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
                vx_errlog("Compiler %s not initialized (try: sk config --add-cc=<path>)", abs_cc);
                continue;
            }

            t->cfg.cc = mem_arena_strdup(g_sk_global_arena, abs_cc);

            if (sk_target_prepare_dirs(ctx, t) != VX_OK)
            {
                strike_status = VX_ERROR;
                break;
            }

            //----------------------------------------------------------------------------------------------------
            // pch depends injection

            for (u32 d = 0; d < t->depend_count; d++)
            {
                for (u32 j = 0; j < eval_result->target_count; j++)
                {
                    struct sk_target *dep = &eval_result->targets[j];
                    if (strcmp(dep->name, t->depends[d]) == 0)
                    {
                        if (dep->kind == SK_TARGET_KIND_PCH)
                        {
                            char *pch_include_dir = mem_arena_alloc(g_sk_global_arena, VX_PATH_MAX);
                            snprintf(
                                pch_include_dir, VX_PATH_MAX, "-I%s", dep->finalized_obj_dirpath);

                            // move pch front of includes, could not be needed
                            memmove(&t->cfg.includes[1],
                                    &t->cfg.includes[0],
                                    t->cfg.includes_count * sizeof(char *));
                            t->cfg.includes[0] = pch_include_dir;
                            t->cfg.includes_count++;

                            if (dep->sources->count > 0)
                            {
                                const char *src_path  = (const char *) dep->sources->items[0];
                                const char *file_name = strrchr(src_path, VX_PATH_SEP);

                                file_name = file_name ? file_name + 1 : src_path;

                                t->cfg.cflags[t->cfg.cflags_count++] = "-include";

                                char *pch_file_arg =
                                    mem_arena_alloc(g_sk_global_arena, VX_PATH_MAX);
                                snprintf(pch_file_arg, VX_PATH_MAX, "%s", file_name);
                                t->cfg.cflags[t->cfg.cflags_count++] = pch_file_arg;
                            }
                        }
                        break;
                    }
                }
            }

            if (t->kind == SK_TARGET_KIND_PCH)
            {
                for (u32 s = 0; s < t->sources->count; s++)
                {
                    char **argv = sk_invoke_compile_nularr(t, s, g_sk_global_arena, nullptr);

                    if (argv == nullptr)
                    {
                        vx_errlog("Failed to build argv for PCH: %s",
                                  (const char *) t->sources->items[s]);
                        strike_status = VX_ERROR;
                        break;
                    }

                    struct vx_process  proc = {0};
                    struct vx_proc_cfg cfg  = {.flags = VX_PROCESS_FLAGS_CAPTURE};

                    vx_status status = vx_process_spawn(&proc, argv[0], argv, &cfg);
                    if (status != VX_OK)
                    {
                        vx_errlog("Could not spawn compiler for PCH: %s",
                                  (const char *) t->sources->items[s]);
                        strike_status = VX_ERROR;
                        break;
                    }
                    vx_process_wait(&proc);
                    if (proc.exit_code != 0)
                    {
                        vx_errlog("Failed to compile PCH: %s", (const char *) t->sources->items[s]);
                        strike_status = VX_ERROR;
                        break;
                    }
                }
                if (strike_status == VX_ERROR)
                {
                    break;
                }
                continue;
            }

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

                if (ctx->active_opt & SK_OPT_GEN_CMDS)
                {
                    unit->gen_ccmds = true;
                }

                vx_thread_pool_push(&pool, sk_worker_compile_fn, unit);
            }

            //----------------------------------------------------------------------------------------------------
        }

        vx_thread_pool_wait(&pool);
        vx_thread_pool_destroy(&pool);

        if (ctx->active_opt & SK_OPT_PROFILE)
        {
            vx_ticks compile_time = {.start = atomic_load(&g_compile_start_ns),
                                     .end   = atomic_load(&g_compile_end_ns)};

            sk_log_time("Compile", &compile_time);
        }

        // Diagnostic logs
        if (atomic_load(&g_compile_errors) > 0)
        {
            for (u32 j = 0; j < total_sources; j++)
            {
                struct sk_work_unit *unit = work_units[j];

                if (unit == nullptr)
                {
                    continue;
                }

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

                struct sk_target_persist out_meta = {.kind           = t->kind,
                                                     .total_files    = t->sources->count,
                                                     .last_strike_ts = vx_time_epoch_s()};

                sk_strncpy_safe(out_meta.name, t->name, sizeof(out_meta.name));
                sk_strncpy_safe(out_meta.out_dir, t->out_dir, sizeof(out_meta.out_dir));

                if (t->finalized_bin_dirpath && t->out_name)
                {
                    sk_strncpy_safe(out_meta.bin_dirpath,
                                    t->finalized_bin_dirpath,
                                    sizeof(out_meta.bin_dirpath));
                }

                if (t->artifact_path)
                {
                    sk_strncpy_safe(out_meta.bin_path, t->artifact_path, sizeof(out_meta.bin_path));
                }

                fwrite(&out_meta, sizeof(struct sk_target_persist), 1, manifest_f);
            }

            fclose(manifest_f);
        }

        // obj manifest
        u32 record_count = atomic_load(&g_sk_cache_record_count);
        if (record_count > 0)
        {
            u32 max_pos_entries = record_count;
            u32 merged_count    = 0;

            struct sk_cache_proj_entry *merged_entries = nullptr;

            FILE *read_f = fopen(SK_PATH_STORM_PROJ_CACHE_BIN, "rb");
            if (read_f != nullptr)
            {
                struct sk_cache_proj_header old_hdr = {0};
                if (fread(&old_hdr, sizeof(old_hdr), 1, read_f) == 1 && old_hdr.count > 0)
                {
                    max_pos_entries += old_hdr.count;
                    merged_entries   = mem_arena_alloc(
                        g_sk_global_arena, sizeof(struct sk_cache_proj_entry) * max_pos_entries);

                    if (fread(merged_entries,
                              sizeof(struct sk_cache_proj_entry),
                              old_hdr.count,
                              read_f) == old_hdr.count)
                    {
                        merged_count = old_hdr.count;
                    }
                }
                fclose(read_f);
            }

            if (merged_entries == nullptr)
            {
                merged_entries = mem_arena_alloc(g_sk_global_arena,
                                                 sizeof(struct sk_cache_proj_entry) * record_count);
            }

            // merge/dedup
            for (u32 i = 0; i < record_count; i++)
            {
                bool hash_exists = false;
                for (u32 j = 0; j < merged_count; j++)
                {
                    // compare the binary hashes directly
                    if (memcmp(merged_entries[j].hash,
                               g_sk_cache_records[i].hash,
                               sizeof(merged_entries[j].hash)) == 0)
                    {
                        hash_exists = true;
                        break;
                    }
                }

                // append if this specific compilation hash hasn't been tracked yet
                if (!hash_exists)
                {
                    merged_entries[merged_count++] = g_sk_cache_records[i];
                }
            }

            FILE *cache_f = fopen(SK_PATH_STORM_PROJ_CACHE_BIN, "wb");
            if (cache_f != nullptr)
            {
                struct sk_cache_proj_header hdr = {.count = merged_count};
                fwrite(&hdr, sizeof(hdr), 1, cache_f);
                fwrite(merged_entries, sizeof(struct sk_cache_proj_entry), merged_count, cache_f);
                fclose(cache_f);
            }
        }

        //----------------------------------------------------------------------------------------------------

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
        // POST-LINK: test pass
        //----------------------------------------------------------------------------------------------------
        if (!dry_run && g_compile_errors == 0 && strike_status != VX_ERROR)
        {
            u32 tests_run    = 0;
            u32 tests_passed = 0;

            for (u32 i = 0; i < sorted_count; i++)
            {
                struct sk_target *t = &eval_result->targets[sorted[i]];

                if (t->kind == SK_TARGET_KIND_TEST)
                {
                    if (tests_run == 0)
                    {
                        vx_printf("\n==================================================\n");
                        vx_printf("Storm-Knell Test Suite Runner\n");
                        vx_printf("==================================================\n");
                    }

                    tests_run++;

                    for (u32 d = 0; d < t->depend_count; d++)
                    {
                        for (u32 j = 0; j < eval_result->target_count; j++)
                        {
                            struct sk_target *dep = &eval_result->targets[j];
                            if (strcmp(dep->name, t->depends[d]) == 0)
                            {
                                if (dep->kind == SK_TARGET_KIND_STATIC ||
                                    dep->kind == SK_TARGET_KIND_SHARED)
                                {
                                    char *lflag_L = mem_arena_alloc(g_sk_global_arena, VX_PATH_MAX);
                                    snprintf(
                                        lflag_L, VX_PATH_MAX, "-L%s", dep->finalized_bin_dirpath);
                                    t->cfg.lflags[t->cfg.lflags_count++] = lflag_L;

                                    char *lflag_l =
                                        mem_arena_alloc(g_sk_global_arena, VX_BUF_SIZE_64);
                                    snprintf(lflag_l, VX_BUF_SIZE_64, "-l%s", dep->out_name);
                                    t->cfg.lflags[t->cfg.lflags_count++] = lflag_l;
                                }
                                break;
                            }
                        }
                    }

                    struct vx_process proc = {0};

                    char **argv = sk_invoke_link_nularr(t, g_sk_global_arena);

                    if (vx_process_spawn(&proc, argv[0], argv, nullptr) != VX_OK)
                    {
                        vx_errlog("Could not spawn linker for test target: %s", t->name);
                        strike_status = VX_ERROR;
                        break;
                    }

                    vx_process_wait(&proc);
                    if (proc.exit_code != 0)
                    {
                        vx_errlog("Failed to link test target: %s", t->name);
                        strike_status = VX_ERROR;
                        break;
                    }

                    vx_log("Running: '%s'", t->name);
                    vx_printf("--------------------------------------------------\n\n");

                    struct vx_process  run_proc = {0};
                    struct vx_proc_cfg run_cfg  = {0};

                    char *run_argv[] = {t->artifact_path, nullptr};

                    if (vx_process_spawn(&run_proc, run_argv[0], run_argv, &run_cfg) != VX_OK)
                    {
                        vx_printf("--------------------------------------------------\n");
                        vx_errlog("Could not execute test binary: %s", t->artifact_path);
                        strike_status = VX_ERROR;
                        break;
                    }

                    vx_process_wait(&run_proc);
                    vx_printf("\n--------------------------------------------------\n");

                    if (run_proc.exit_code != 0)
                    {
                        vx_errlog("Target '%s' crashed or exited with code %d",
                                  t->name,
                                  run_proc.exit_code);
                        strike_status = VX_ERROR;
                        break;
                    }

                    tests_passed++;
                    vx_log("Test '%s' completed successfully.\n\n", t->name);
                }
            }

            if (tests_run > 0)
            {
                vx_printf("==================================================\n");
                if (strike_status == VX_ERROR)
                {
                    vx_errlog("TEST SUITE ABORTED: %u/%u targets passed.", tests_passed, tests_run);
                }
                else
                {
                    vx_log("All %u test targets passed successfully.", tests_run);
                }
                vx_printf("==================================================\n\n");
            }
        }

        //----------------------------------------------------------------------------------------------------
        // LINK

        if (!dry_run && g_compile_errors == 0 && strike_status != VX_ERROR)
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
                    for (u32 j = 0; j < eval_result->target_count; j++)
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

                if (t->kind == SK_TARGET_KIND_PCH || t->kind == SK_TARGET_KIND_TEST)
                {
                    continue;  // nothing to link
                }

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
                        char *dest_path =
                            sk_path_join(g_sk_global_arena, t->install_dir, t->out_name);
                        bool needs_install = true;

                        if (g_cache_hits == total_sources && vx_isfile(dest_path))
                        {
                            needs_install = false;
                        }

                        if (t->kind == SK_TARGET_KIND_EXEC && t->install_dir != nullptr &&
                            needs_install)
                        {
                            if (vx_isfile(dest_path))
                            {
                                vx_fs_rmrf(dest_path);
                            }

                            if (vx_mkdir_p(t->install_dir) != VX_OK)
                            {
                                vx_errlog("Failed to create installation directory: %s",
                                          t->install_dir);
                            }

                            vx_log("Installing: %s -> %s", t->out_name, dest_path);

                            if (!vx_fs_cp(t->artifact_path, dest_path))
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
                sk_log_time("Link", &link_time);
            }

            if (ctx->active_opt & SK_OPT_GEN_CMDS)
            {
                sk_ccmds_write(ctx->rpath);
            }
        }

        //----------------------------------------------------------------------------------------------------
        // POST-BUILD: Runtime test pass

        //----------------------------------------------------------------------------------------------------

        // cache check and prune if needed
        struct sk_cache_info current_cache_info = sk_cache_calculate_size();

        u64 current_cache_bytes = current_cache_info.total_size;

        u64 max_allowed_b  = (u64) ctx->ccfg.max_size_mb * 1024 * 1024;
        u64 prune_target_b = (u64) ctx->ccfg.prune_threshold_mb * 1024 * 1024;

        if (current_cache_bytes >= max_allowed_b)
        {
            vx_log("Cache size (%.2f MB) exceeds limit (%.2f MB). Initiating prune",
                   current_cache_bytes / 1048576.0f,
                   max_allowed_b / 1048576.0f);

            sk_cache_prune_to_size(ctx->ccfg.prune_threshold_mb);

            struct sk_cache_info new_cache_info = sk_cache_calculate_size();

            u64 new_cache_bytes = new_cache_info.total_size;
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
        vx_log("Total tasks: %d", total_tasks);
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
        sk_log_time("Strike", &profile);
    }

    vx_mutex_destroy(&g_proc_spawn_mutex);
    vx_mutex_destroy(&ctx->console_lock);
    return strike_status;
}

//----------------------------------------------------------------------------------------------------

// NOTE: keep an eye on paths
static vx_status sk_target_prepare_dirs(struct sk_ctx *ctx, struct sk_target *t)
{
    if (ctx == nullptr || t == nullptr)
    {
        return VX_ERROR;
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

    if (t->kind == SK_TARGET_KIND_EXEC || t->kind == SK_TARGET_KIND_TEST)
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

    t->artifact_path = nullptr;

    const char *prefix = "";
    const char *ext    = "";

    if (t->kind == SK_TARGET_KIND_EXEC)
    {
        ext = VX_EXE_EXT;
    }
    else if (t->kind == SK_TARGET_KIND_STATIC)
    {
        prefix = VX_LIB_PREFIX;
        ext    = VX_LIB_EXT;
    }
    else if (t->kind == SK_TARGET_KIND_SHARED)
    {
        prefix = VX_LIB_PREFIX;
        ext    = VX_DLL_EXT;
    }

    const char *actual_out_name = (t->out_name != nullptr) ? t->out_name : t->name;

    char filename_buf[VX_BUF_SIZE_64];
    snprintf(filename_buf, sizeof(filename_buf), "%s%s%s", prefix, actual_out_name, ext);

    t->finalized_filename = mem_arena_strdup(g_sk_global_arena, filename_buf);

    t->artifact_path = sk_path_join(g_sk_global_arena, final_bin_dir_buf, t->finalized_filename);

    if (ctx->active_opt & SK_OPT_VERBOSE)
    {
        vx_log("Created: %s -> Artifact: %s", final_bin_dir_buf, t->finalized_filename);
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

    // for 256 max targets and 32 depends per target this should be fine

    struct sk_target *t = &result->targets[idx];

    for (u32 d = 0; d < t->depend_count; d++)
    {
        for (u32 j = 0; j < result->target_count; j++)
        {
            if (strcmp(result->targets[j].name, t->depends[d]) == 0)
            {
                if (result->targets[j].kind == SK_TARGET_KIND_TEST)
                {
                    vx_errlog("Target '%s' cannot depend on test target '%s'",
                              t->name,
                              result->targets[j].name);
                    return VX_ERROR;
                }

                if (topo_visit(result, j, sorted, sorted_count, visited, in_stack) != VX_OK)
                {
                    return VX_ERROR;
                }
                break;
            }
        }
    }

    in_stack[idx] = false;

    sorted[(*sorted_count)++] = idx;
    return VX_OK;
}

static const char *sk_obj_ext(struct sk_target *t, const char *src_path)
{
    const char *ext = strrchr(src_path, CHAR_DOT);
    if (ext && (strcmp(ext, ".h") == 0 || strcmp(ext, ".hpp") == 0 || strcmp(ext, ".hxx") == 0))
    {
        return strstr(t->cfg.cc, "clang") ? ".pch" : ".gch";
    }
    return ".o";
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

        const char *out_ext = sk_obj_ext(t, src_path);
        snprintf(obj_path,
                 VX_PATH_MAX,
                 "%s%s%s%s",
                 t->finalized_obj_dirpath,
                 VX_PATH_SEP_STR,
                 file_name,
                 out_ext);

        u32    arg_count = 0;
        char **argv = (unit->dry_run)
                          ? sk_invoke_syntax_check_nularr(t, unit->source_idx, arena, &arg_count)
                          : sk_invoke_compile_nularr(t, unit->source_idx, arena, &arg_count);

        if (unit->gen_ccmds && !unit->dry_run)
        {
            sk_ccmds_push(src_path, g_sk_global_ctx.rpath, (const char **) argv, arg_count);
        }

        struct sk_cache_entry cache_entry = {0};
        sk_cache_resolve(out_hash, &cache_entry);

        if (!unit->dry_run)
        {
            // TODO: add sk strike --fresh to skip cache checks and compile full
            if (sk_cache_exists(&cache_entry))
            {
                if (sk_cache_restore(&cache_entry, obj_path) == VX_OK)
                {
                    atomic_fetch_add(&g_cache_hits, 1);
                    atomic_store(&g_compile_end_ns, vx_time_ns());
                    mem_arena_soft_reset(arena);
                    return nullptr;
                }
                else
                {
                    vx_warn("Cache restore failed for '%s', recompiling", src_path);
                }
            }
        }

        bool verbose = (g_sk_global_ctx.active_opt & SK_OPT_VERBOSE);

        struct vx_proc_cfg cfg = {.flags = VX_PROCESS_FLAGS_CAPTURE};

        // NOTE: will probably remove the locks
        // vx_mutex_lock(&g_proc_spawn_mutex);
        vx_status status = vx_process_spawn(&proc, argv[0], argv, &cfg);
        // vx_mutex_unlock(&g_proc_spawn_mutex);

        if (status == VX_OK)
        {
            vx_process_consume_output(&proc, &unit->diagnostic_log);
            vx_process_wait(&proc);

            if (proc.exit_code == 0)
            {
                if (!unit->dry_run)
                {
                    if (sk_cache_store(&cache_entry, obj_path) == VX_OK)
                    {
                        sk_cache_record(out_hash, src_path, obj_path, t->name);
                    }
                    else
                    {
                        vx_errlog("Failed to store '%s' to cache", obj_path);
                    }
                }

                u32 c_idx = atomic_fetch_add(&g_cache_misses, 1) + 1;

                if (!verbose)
                {
                    vx_printf("    [%u]: %s\n", c_idx, unit->tag);
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
