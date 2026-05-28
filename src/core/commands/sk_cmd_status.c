#include "sk_cmd_status.h"
#include "mem_arena.h"
#include "sk_globals.h"
#include "sk_cmd_strike.h"
#include "sk_paths.h"
#include "vx.h"

#define ANSI_RED    "\x1b[31m"
#define ANSI_GREEN  "\x1b[32m"
#define ANSI_YELLOW "\x1b[33m"
#define ANSI_CYAN   "\x1b[36m"
#define ANSI_BOLD   "\x1b[1m"
#define ANSI_RESET  "\x1b[0m"

void sk_cmd_status_fn(struct sk_ctx *ctx)
{
    if (sk_resolve_project_root(ctx) != VX_OK)
    {
        vx_errlog("Storm-knell is not initialized in '%s'  directory or any parent",
                  ctx->rpath ? ctx->rpath : vx_getcwd_fn());
        return;
    }
    if (vx_chdir(ctx->rpath) != VX_OK)
    {
        vx_errlog("Failed to change dir to project root: %s", ctx->rpath);
        return;
    }
    vx_dbglog("Working directory: %s", ctx->rpath);

    const char *manifest_path =
        sk_path_join(g_sk_global_arena, ctx->rpath, SK_PATH_STORM_MANIFEST_BIN);

    FILE *f = fopen(manifest_path, "rb");

    if (f == nullptr)
    {
        vx_printf(ANSI_YELLOW
                  "Workspace status: PRISTINE / UNBUILT (No manifest found).\n" ANSI_RESET);
        return;
    }

    struct sk_manifest_header header = {0};
    if (fread(&header, sizeof(struct sk_manifest_header), 1, f) != 1 || header.target_count == 0)
    {
        fclose(f);
        vx_printf(ANSI_RED "Workspace manifest is empty or corrupted.\n" ANSI_RESET);
        return;
    }

    struct sk_target_persist *saved_targets =
        mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_target_persist) * header.target_count);

    size_t bytes_read =
        fread(saved_targets, sizeof(struct sk_target_persist), header.target_count, f);

    if (bytes_read != header.target_count)
    {
        fclose(f);
        return;
    }

    fclose(f);

    vx_printf("Storm-Knell Version: %s\n", SK_VERSION_STRING);
    vx_printf("Working directory: %s\n", ctx->rpath ? ctx->rpath : vx_getcwd_fn());

    struct sk_cache_info cache_info = sk_cache_calculate_size();

    u64 cache_size = cache_info.total_size;
    vx_printf("Global Cache Size: %.2f MB\n", (f32) cache_size / 1048576.0f);

    vx_printf(ANSI_BOLD ANSI_CYAN "=============================== STORM-KNELL STATUS "
                                  "============================================\n" ANSI_RESET);
    vx_printf(ANSI_BOLD "  %-20s%-15s%-20s%-18s%s\n",
              "Target Name",
              "Kind",
              "Status Check",
              "Total Files",
              "Age" ANSI_RESET);
    vx_printf(
        "  "
        "--------------------------------------------------------------------------------------\n");

    u32 total_missing_artifacts = 0;

    for (u32 i = 0; i < header.target_count; i++)
    {
        struct sk_target_persist *m = &saved_targets[i];

        char time_buf[64];
        sk_fmt_relative_time(m->last_strike_ts, time_buf, sizeof(time_buf));

        const char *kind_str = "UNKNOWN";
        switch (m->kind)
        {
            case SK_TARGET_KIND_NONE: kind_str = "UNKNOWN  "; break;
            case SK_TARGET_KIND_EXEC: kind_str = "EXEC  "; break;
            case SK_TARGET_KIND_STATIC: kind_str = "STATIC"; break;
            case SK_TARGET_KIND_SHARED: kind_str = "SHARED"; break;
            case SK_TARGET_KIND_PCH: kind_str = "PCH"; break;
        }
        bool artifact_ok = vx_isfile(m->bin_path);

        if (!artifact_ok)
        {
            total_missing_artifacts++;
        }

        const char *status_str;
        const char *status_color;
        if (header.global_compile_errors > 0)
        {
            status_str   = "[COMPILE ERROR] ";
            status_color = ANSI_RED;
        }
        else if (!artifact_ok)
        {
            status_str =
                (m->kind == SK_TARGET_KIND_EXEC) ? "[MISSING BINARY]" : "[MISSING LIBRARY]";
            status_color = ANSI_YELLOW;
        }
        else
        {
            status_str   = "[OPERATIONAL]   ";
            status_color = ANSI_GREEN;
        }

        vx_printf("  %s%s %-20s%-15s%-20s%-15u%s\n" ANSI_RESET,
                  status_color,
                  artifact_ok ? "✔" : "⚠",
                  m->name,
                  kind_str,
                  status_str,
                  m->total_files,
                  time_buf);
    }

    vx_printf(
        "  "
        "--------------------------------------------------------------------------------------\n");

    vx_printf(ANSI_BOLD "  Cache Summary:" ANSI_RESET "  %u hits, %u misses (Total Ops: %u)\n",
              header.global_cache_hits,
              header.global_cache_misses,
              (header.global_cache_hits + header.global_cache_misses));

    if (header.global_compile_errors > 0)
    {
        vx_printf(ANSI_BOLD "  Workspace Status: " ANSI_RED "BROKEN" ANSI_RESET
                            " (%u compile errors detected. Run 'sk strike' to debug).\n",
                  header.global_compile_errors);
    }
    else if (total_missing_artifacts > 0)
    {
        vx_printf(ANSI_BOLD "  Workspace Status: " ANSI_YELLOW "DEGRADED" ANSI_RESET
                            " (%u/%u outputs missing. Run 'sk strike' to rebuild).\n",
                  total_missing_artifacts,
                  header.target_count);
    }
    else
    {
        vx_printf(ANSI_BOLD "  Workspace Status: " ANSI_GREEN "READY / HEALTHY" ANSI_RESET
                            " (All targets verified up-to-date).\n");
    }
    vx_printf(ANSI_BOLD ANSI_CYAN "================================================================"
                                  "===============================\n" ANSI_RESET);
}
