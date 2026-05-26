#include "sk_cmd_purge.h"
#include "sk_globals.h"
#include "sk_paths.h"
#include "sk_util.h"
#include "vx_fs.h"
#include "vx_io.h"

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

    char *stormfile = sk_path_join(g_sk_global_arena, target, SK_PATH_STORMFILE);
    vx_fs_rmrf(stormfile);
    vx_log("Removed: %s", stormfile);

    char *storm_dir = sk_path_join(g_sk_global_arena, target, SK_PATH_STORM_DIR);
    vx_fs_rmrf(storm_dir);
    vx_log("Removed: %s", storm_dir);

    vx_log("Storm-Knell purged for: %s", target);
}
