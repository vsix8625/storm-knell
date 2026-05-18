#include "sk_cmd_clean.h"
#include "sk_cmd_strike.h"

#include "sk_globals.h"
#include "sk_paths.h"
#include "sk_util.h"

#include "storm-knell.h"
#include "vx.h"

vx_status sk_cmd_clean_fn(struct sk_ctx *ctx)
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

    const char *manifest_path = SK_PATH_STORM_MANIFEST_BIN;

    FILE *f = fopen(manifest_path, "rb");

    if (f == nullptr)
    {
        vx_log("Nothing to clean (no active workspace manifest found).");
        return VX_ERROR;
    }

    u32 target_count = 0;
    if (fread(&target_count, sizeof(u32), 1, f) != 1 || target_count == 0)
    {
        fclose(f);
        vx_errlog("Workspace manifest is empty or corrupted.");
        return VX_ERROR;
    }

    struct sk_target_persist *saved_targets =
        sk_arena_alloc(g_sk_global_arena, sizeof(struct sk_target_persist) * target_count);

    size_t elements_read = fread(saved_targets, sizeof(struct sk_target_persist), target_count, f);
    fclose(f);

    if (elements_read != target_count)
    {
        vx_errlog("Failed to fully read target metadata blocks from manifest.");
        return VX_ERROR;
    }

    vx_log("Cleaning workspace targets...");
    u32 wiped_count = 0;

    for (u32 i = 0; i < target_count; i++)
    {
        struct sk_target_persist *t = &saved_targets[i];

        if (t->out_dir[0] != '\0')
        {
            if (!vx_isdir(t->out_dir))
            {
                wiped_count++;
                continue;
            }

            vx_log("  Wiping target [%s] outputs -> %s/", t->name, t->out_dir);
            if (vx_fs_rmrf(t->out_dir))
            {
                wiped_count++;
            }
            else
            {
                vx_warn("  Could not fully remove directory: %s", t->out_dir);
            }
        }
    }

    vx_fs_rmrf(SK_PATH_STORM_MANIFEST_BIN);

    vx_log("[summary]: Successfully cleared %u/%u target artifacts.", wiped_count, target_count);
    return VX_OK;
}
