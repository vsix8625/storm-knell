#include "sk_cmd_surge.h"
#include "sk_cmd_strike.h"

#include "sk_globals.h"
#include "sk_paths.h"
#include "sk_util.h"

#include "storm-knell.h"
#include "vx.h"
#include "vx_process.h"

vx_status sk_cmd_surge_fn(struct sk_ctx *ctx)
{
    if (ctx == nullptr)
    {
        return VX_ERROR;
    }
    if (sk_resolve_project_root(ctx) != VX_OK)
    {
        vx_errlog("Storm-knell is not initialized in '%s' directory or any parent",
                  ctx->rpath ? ctx->rpath : vx_getcwd_fn());
        return VX_ERROR;
    }
    if (vx_chdir(ctx->rpath) != VX_OK)
    {
        vx_errlog("Failed to change dir to project root: %s", ctx->rpath);
        return VX_ERROR;
    }

    const char *manifest_path = SK_PATH_STORM_MANIFEST_BIN;

    FILE *f = fopen(manifest_path, "rb");

    if (f == nullptr)
    {
        vx_log("No active workspace manifest found).");
        return VX_OK;
    }

    struct sk_manifest_header header = {0};
    if (fread(&header, sizeof(struct sk_manifest_header), 1, f) != 1 || header.target_count == 0)
    {
        fclose(f);

        vx_fs_rmrf(manifest_path);
        vx_errlog("Workspace manifest was corrupted. Purged manifest file.");
        return VX_ERROR;
    }

    struct sk_target_persist *targets =
        mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_target_persist) * header.target_count);

    size_t elements_read = fread(targets, sizeof(struct sk_target_persist), header.target_count, f);
    fclose(f);

    if (elements_read != header.target_count)
    {
        vx_fs_rmrf(manifest_path);
        vx_errlog("Failed to fully read target metadata blocks from manifest. Purged manifest.");
        return VX_ERROR;
    }

    struct sk_target_persist *run_target = nullptr;

    if (ctx->surge_target != nullptr)
    {
        for (u32 i = 0; i < header.target_count; i++)
        {
            if (strcmp(targets[i].name, ctx->surge_target) == 0)
            {
                run_target = &targets[i];
                break;
            }
        }

        if (run_target == nullptr)
        {
            vx_errlog("Target '%s' not found in manifest.", ctx->surge_target);
            return VX_ERROR;
        }
    }
    else
    {
        // pick the most recent target
        for (u32 i = 0; i < header.target_count; i++)
        {
            if (targets[i].kind != SK_TARGET_KIND_EXEC)
            {
                continue;
            }

            if (run_target == nullptr || targets[i].last_strike_ts > run_target->last_strike_ts)
            {
                run_target = &targets[i];
            }
        }

        if (run_target == nullptr)
        {
            vx_errlog("No executables found for: %s", ctx->rpath);
            return VX_ERROR;
        }
    }

    if (run_target->kind != SK_TARGET_KIND_EXEC)
    {
        vx_errlog("Target '%s' is not an executable.", run_target->name);
        return VX_ERROR;
    }

    if (!vx_isfile(run_target->bin_path))
    {
        vx_errlog("Binary not found on disk: %s", run_target->bin_path);
        return VX_ERROR;
    }

    // build argv: [bin_path, ...passthrough_args, nullptr]
    i32 extra_argc = ctx->surge_passthrough_argc;

    char **argv = mem_arena_alloc(g_sk_global_arena, sizeof(char *) * (extra_argc + 2));

    argv[0] = run_target->bin_path;
    for (i32 i = 0; i < extra_argc; i++)
    {
        argv[i + 1] = ctx->surge_passthrough_argv[i];
    }
    argv[extra_argc + 1] = nullptr;

    vx_log("Working directory: %s", ctx->rpath);
    vx_log("Surging target [%s] -> %s", run_target->name, run_target->bin_path);
    vx_printf(
        "================================================================================\n\n");

    struct vx_process  proc = {0};
    struct vx_proc_cfg cfg  = {0};

    if (vx_process_spawn(&proc, run_target->bin_path, argv, &cfg) != VX_OK)
    {
        vx_errlog("Failed to launch: %s", run_target->bin_path);
        return VX_ERROR;
    }
    vx_process_wait(&proc);

    vx_printf(
        "\n\n================================================================================\n");
    vx_log("Target [%s] exited with code: %d", run_target->name, proc.exit_code);
    return VX_OK;
}
