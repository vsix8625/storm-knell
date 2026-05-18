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
    VX_CAST(void, ctx);

    const char *manifest_path = SK_PATH_STORM_MANIFEST_BIN;

    FILE *f = fopen(manifest_path, "rb");

    if (f == nullptr)
    {
        vx_printf(ANSI_YELLOW
                  "Workspace status: PRISTINE / UNBUILT (No manifest found).\n" ANSI_RESET);
        return;
    }

    // Read the static size layout header block first
    struct sk_manifest_header header = {0};
    if (fread(&header, sizeof(struct sk_manifest_header), 1, f) != 1 || header.target_count == 0)
    {
        fclose(f);
        vx_printf(ANSI_RED "Workspace manifest is empty or corrupted.\n" ANSI_RESET);
        return;
    }

    struct sk_target_persist *saved_targets =
        mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_target_persist) * header.target_count);
    fread(saved_targets, sizeof(struct sk_target_persist), header.target_count, f);
    fclose(f);

    vx_printf(ANSI_BOLD ANSI_CYAN
              "======================== STORM-KNELL STATUS ========================\n" ANSI_RESET);
    vx_printf(ANSI_BOLD
              "  Target Name     Kind      Status Check         Total Files    Age\n" ANSI_RESET);
    vx_printf("  ------------------------------------------------------------------\n");

    u32 total_missing_artifacts = 0;

    for (u32 i = 0; i < header.target_count; i++)
    {
        struct sk_target_persist *m = &saved_targets[i];

        char time_buf[64];
        sk_fmt_relative_time(m->last_strike_ts, time_buf, sizeof(time_buf));

        const char *kind_str    = (m->kind == SK_TARGET_KIND_EXEC) ? "EXEC  " : "STATIC";
        bool        artifact_ok = vx_isfile(m->bin_path);

        if (!artifact_ok)
        {
            total_missing_artifacts++;
        }

        if (header.global_compile_errors > 0)
        {
            vx_printf("  " ANSI_RED "✘ %-13s" ANSI_RESET " %s  " ANSI_RED
                      "![COMPILE ERROR]" ANSI_RESET "   %-14u %s\n",
                      m->name,
                      kind_str,
                      m->total_files,
                      time_buf);
        }
        else if (!artifact_ok)
        {
            vx_printf("  " ANSI_YELLOW "⚠ %-13s" ANSI_RESET " %s  " ANSI_YELLOW
                      "[MISSING BINARY]" ANSI_RESET "  %-14u %s\n",
                      m->name,
                      kind_str,
                      m->total_files,
                      time_buf);
        }
        else
        {
            vx_printf("  " ANSI_GREEN "✔ %-13s" ANSI_RESET " %s  " ANSI_GREEN
                      "[OPERATIONAL]" ANSI_RESET "     %-14u %s\n",
                      m->name,
                      kind_str,
                      m->total_files,
                      time_buf);
        }
    }

    vx_printf("  ------------------------------------------------------------------\n");

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
    vx_printf(ANSI_BOLD ANSI_CYAN
              "====================================================================\n" ANSI_RESET);
}
