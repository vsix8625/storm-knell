#include "sk_cmd_clean.h"
#include "sk_cmd_strike.h"

#include "sk_globals.h"
#include "sk_paths.h"
#include "sk_util.h"
#include "sk_cache.h"

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

    if (ctx->active_opt & SK_OPT_CLEAN_FULL)
    {
        struct sk_cache_info cinfo_current = sk_cache_calculate_size();

        FILE *cache_f = fopen(SK_PATH_STORM_PROJ_CACHE_BIN, "rb");
        if (cache_f != nullptr)
        {
            struct sk_cache_proj_header hdr = {0};
            if (fread(&hdr, sizeof(hdr), 1, cache_f) == 1 && hdr.count > 0)
            {
                struct sk_cache_proj_entry *entries = mem_arena_alloc(
                    g_sk_global_arena, sizeof(struct sk_cache_proj_entry) * hdr.count);

                if (fread(entries, sizeof(struct sk_cache_proj_entry), hdr.count, cache_f) ==
                    hdr.count)
                {
                    vx_log("Purging cached objects for this project...");
                    for (u32 i = 0; i < hdr.count; i++)
                    {
                        struct sk_cache_entry resolved = {0};
                        if (sk_cache_resolve(entries[i].hash, &resolved) == VX_OK)
                        {
                            if (ctx->active_opt & SK_OPT_VERBOSE)
                            {
                                vx_log("Purging: %s", resolved.cache_path);
                            }
                            vx_fs_rmrf(resolved.cache_path);
                        }
                        else
                        {
                            vx_warn("Resolve failed for entry %u", i);
                        }
                    }
                }
            }
            fclose(cache_f);
            vx_fs_rmrf(SK_PATH_STORM_PROJ_CACHE_BIN);
        }
        struct sk_cache_info cinfo_new = sk_cache_calculate_size();
        vx_log("[summary]: Cleaned cache saved %.2f MB",
               ((f32) cinfo_current.total_size - cinfo_new.total_size) / 1048576.0f);
    }

    // clean targets
    const char *manifest_path = SK_PATH_STORM_MANIFEST_BIN;

    FILE *f = fopen(manifest_path, "rb");

    if (f == nullptr)
    {
        vx_log("Nothing to clean (no active workspace manifest found).");
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

    vx_log("Cleaning workspace targets...");
    u32 wiped_count = 0;

    for (u32 i = 0; i < header.target_count; i++)
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
    vx_fs_rmrf(manifest_path);

    vx_log("[summary]: Successfully nuked %u/%u target build artifacts.",
           wiped_count,
           header.target_count);
    return VX_OK;
}
