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

static void fmt_file_size(u64 bytes, bool fexists, char *buf, size_t buf_size)
{
    if (!fexists)
    {
        snprintf(buf, buf_size, "-");
        return;
    }

    if (bytes == 0)
    {
        snprintf(buf, buf_size, "0 B");
        return;
    }

    const char *suffixes[] = {"B", "KB", "MB", "GB"};

    u32 i            = 0;
    f64 double_bytes = (f64) bytes;

    while (double_bytes >= 1024.0 && i < 3)
    {
        double_bytes /= 1024.0;
        i++;
    }

    if (i == 0)
    {
        snprintf(buf, buf_size, "%llu B", (unsigned long long) bytes);
    }
    else
    {
        snprintf(buf, buf_size, "%.2f %s", double_bytes, suffixes[i]);
    }
}

// ----------------------------------------------------------------------------------------------------

struct sk_theme
{
    const char *check;
    const char *warn;
    const char *arrow;
    const char *reset;
    const char *cyan;
    const char *green;
    const char *yellow;
    const char *red;
    const char *gray;
    const char *bold;
};

static const struct sk_theme THEME_UTF8 = {.check  = "✔",
                                           .warn   = "⚠",
                                           .arrow  = "↳",
                                           .reset  = ANSI_RESET,
                                           .cyan   = ANSI_CYAN,
                                           .green  = ANSI_GREEN,
                                           .yellow = ANSI_YELLOW,
                                           .red    = ANSI_RED,
                                           .gray   = SK_ANSI_GRAY,
                                           .bold   = ANSI_BOLD};

static const struct sk_theme THEME_ASCII = {.check  = "[OK]",
                                            .warn   = "[!]",
                                            .arrow  = "->",
                                            .reset  = "",
                                            .cyan   = "",
                                            .green  = "",
                                            .yellow = "",
                                            .red    = "",
                                            .gray   = "",
                                            .bold   = ""};

void sk_cmd_status_fn(struct sk_ctx *ctx)
{
    const struct sk_theme *t = vx_isatty(STDOUT_FILENO) ? &THEME_UTF8 : &THEME_ASCII;

    if (sk_resolve_project_root(ctx) != VX_OK)
    {
        vx_errlog("Storm-knell is not initialized in '%s' directory or any parent",
                  ctx->rpath ? ctx->rpath : vx_getcwd_fn());
        return;
    }
    if (vx_chdir(ctx->rpath) != VX_OK)
    {
        vx_errlog("Failed to change dir to project root: %s", ctx->rpath);
        return;
    }

    const char *manifest_path =
        sk_path_join(g_sk_global_arena, ctx->rpath, SK_PATH_STORM_MANIFEST_BIN);

    FILE *f = fopen(manifest_path, "rb");

    if (f == nullptr)
    {
        vx_printf(
            "%sWorkspace status: PRISTINE / UNBUILT (No manifest found).%s\n", t->yellow, t->reset);
        return;
    }

    struct sk_manifest_header header = {0};
    if (fread(&header, sizeof(struct sk_manifest_header), 1, f) != 1 || header.target_count == 0)
    {
        fclose(f);
        vx_printf("%sWorkspace manifest is empty or corrupted.%s\n", t->red, t->reset);
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

    vx_printf("%s%s================================= STORM-KNELL STATUS "
              "==============================================%s\n",
              t->bold,
              t->cyan,
              t->reset);
    vx_printf("%s  %-24s%-10s%-22s%-16s%-14s%-12s%s\n",
              t->bold,
              "Target Name",
              "Kind",
              "Status Check",
              "Total Files",
              "Size",
              "Age",
              t->reset);
    vx_printf("  "
              "------------------------------------------------------------------------------------"
              "------------\n");

    u32 total_missing_artifacts = 0;
    u64 total_workspace_bytes   = 0;

    for (u32 i = 0; i < header.target_count; i++)
    {
        struct sk_target_persist *m = &saved_targets[i];

        char time_buf[64];
        sk_fmt_relative_time(m->last_strike_ts, time_buf, sizeof(time_buf));

        const char *kind_str = "UNKNOWN";
        switch (m->kind)
        {
            case SK_TARGET_KIND_NONE: kind_str = "UNKNOWN"; break;
            case SK_TARGET_KIND_EXEC: kind_str = "EXEC"; break;
            case SK_TARGET_KIND_STATIC: kind_str = "STATIC"; break;
            case SK_TARGET_KIND_SHARED: kind_str = "SHARED"; break;
            case SK_TARGET_KIND_PCH: kind_str = "PCH"; break;
            case SK_TARGET_KIND_TEST: kind_str = "TEST"; break;
        }

        bool artifact_ok = vx_isfile(m->bin_path);

        u64 file_size = 0;
        if (artifact_ok)
        {
            vx_stat_struct st;
            if (vx_stat(m->bin_path, &st) == 0)
            {
                file_size              = (u64) st.st_size;
                total_workspace_bytes += file_size;
            }
        }

        char size_buf[VX_BUF_SIZE_32];
        fmt_file_size(file_size, artifact_ok, size_buf, sizeof(size_buf));

        if (!artifact_ok)
        {
            total_missing_artifacts++;
        }

        const char *status_str;
        const char *status_color;
        if (header.global_compile_errors > 0)
        {
            status_str   = "[COMPILE ERROR]";
            status_color = t->red;
        }
        else if (!artifact_ok)
        {
            status_str =
                (m->kind == SK_TARGET_KIND_EXEC) ? "[MISSING BINARY]" : "[MISSING LIBRARY]";
            status_color = t->yellow;
        }
        else
        {
            status_str   = "[OPERATIONAL]";
            status_color = t->green;
        }

        vx_printf("  %s%s %-22s%s%-10s%s%-22s%s%-16u%-14s%-12s\n",
                  status_color,
                  artifact_ok ? t->check : t->warn,
                  m->name,
                  t->reset,
                  kind_str,
                  status_color,
                  status_str,
                  t->reset,
                  m->total_files,
                  size_buf,
                  time_buf);

        if ((ctx->active_opt & SK_OPT_VERBOSE) && m->bin_path[0] != '\0')
        {
            vx_printf("%s    %s Out Path:%s %s\n", t->gray, t->arrow, t->reset, m->bin_path);
        }
    }

    vx_printf("  "
              "------------------------------------------------------------------------------------"
              "------------\n");

    u32   total_ops = header.global_cache_hits + header.global_cache_misses;
    float hit_rate = total_ops > 0 ? ((float) header.global_cache_hits / total_ops) * 100.0f : 0.0f;

    char total_size_buf[VX_BUF_SIZE_32];
    fmt_file_size(total_workspace_bytes, true, total_size_buf, sizeof(total_size_buf));

    vx_printf("%s  Cache Summary    :%s  %u hits, %u misses (Total Ops: %u, %.1f%% cached)\n",
              t->bold,
              t->reset,
              header.global_cache_hits,
              header.global_cache_misses,
              total_ops,
              hit_rate);

    vx_printf("%s  Total Footprint  :%s  %s\n", t->bold, t->reset, total_size_buf);

    if (header.global_compile_errors > 0)
    {
        vx_printf("%s  Workspace Status : %sBROKEN%s (%u compile errors detected. Run 'sk strike' "
                  "to debug).\n",
                  t->bold,
                  t->red,
                  t->reset,
                  header.global_compile_errors);
    }
    else if (total_missing_artifacts > 0)
    {
        vx_printf("%s  Workspace Status : %sDEGRADED%s (%u/%u outputs missing. Run 'sk strike' to "
                  "rebuild).\n",
                  t->bold,
                  t->yellow,
                  t->reset,
                  total_missing_artifacts,
                  header.target_count);
    }
    else
    {
        vx_printf("%s  Workspace Status : %sREADY / HEALTHY%s\n", t->bold, t->green, t->reset);
    }

    vx_printf("%s%s================================================================================"
              "==================%s\n",
              t->bold,
              t->cyan,
              t->reset);
}

// ----------------------------------------------------------------------------------------------------
