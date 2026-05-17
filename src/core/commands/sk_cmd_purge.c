#include "sk_cmd_purge.h"
#include "sk_paths.h"
#include "sk_util.h"
#include "vx_fs.h"
#include "vx_io.h"
#include <stdio.h>

void sk_cmd_purge_fn(struct sk_ctx *ctx)
{
    if (ctx == nullptr)
    {
        return;
    }

    if (sk_resolve_project_root(ctx) != VX_OK)
    {
        vx_warn("Storm-Knell not initialized here or in any parent directory.");
    }

    const char *target = ctx->rpath ? ctx->rpath : vx_getcwd_fn();
    vx_log("Cleaning up lefovers in: %s", target);

    char stormfile[VX_PATH_MAX];
    snprintf(stormfile, sizeof(stormfile), "%s%s%s", target, VX_PATH_SEP_STR, SK_PATH_STORMFILE);
    vx_fs_rmrf(stormfile);

    char meta_path[VX_PATH_MAX];
    snprintf(meta_path, sizeof(meta_path), "%s/%s", target, SK_PATH_STORM_DIR);
    vx_fs_rmrf(meta_path);

    vx_log("Storm-Knell purged for: %s", target);
}
