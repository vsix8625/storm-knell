#include "sk_cmd_surge.h"
#include "sk_cmd_strike.h"

#include "sk_globals.h"
#include "sk_paths.h"
#include "sk_util.h"

#include "storm-knell.h"
#include "vx.h"

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
    vx_log("Working directory: %s", ctx->rpath);

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

    struct sk_target_persist *saved_targets =
        mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_target_persist) * header.target_count);

    size_t elements_read =
        fread(saved_targets, sizeof(struct sk_target_persist), header.target_count, f);
    fclose(f);

    if (elements_read != header.target_count)
    {
        vx_fs_rmrf(manifest_path);
        vx_errlog("Failed to fully read target metadata blocks from manifest. Purged manifest.");
        return VX_ERROR;
    }

    VX_ASSERT_LOG("SURGING BABY WOHOO WIP");

    return VX_OK;
}
