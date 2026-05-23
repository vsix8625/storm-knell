#include "sk_util.h"
#include "mem_arena.h"
#include "mem_heap.h"
#include "sk_globals.h"
#include "sk_paths.h"

#include "vx_fs.h"
#include "vx_string.h"
#include "vx_time.h"

bool sk_is_initialized_at(const char *dir)
{
    if (dir == nullptr)
    {
        return false;
    }

    char storm[VX_PATH_MAX];
    char stormfile[VX_PATH_MAX];

    snprintf(storm, sizeof(storm), "%s%s%s", dir, VX_PATH_SEP_STR, SK_PATH_STORM_DIR);
    snprintf(stormfile, sizeof(stormfile), "%s%s%s", dir, VX_PATH_SEP_STR, SK_PATH_STORMFILE);

    return vx_isdir(storm) && vx_isfile(stormfile);
}

bool sk_discover_root(char *out_path, size_t size)
{
    char current[VX_PATH_MAX];

    const char *cwd = vx_getcwd_fn();

    if (cwd == nullptr)
    {
        return false;
    }

    strncpy(current, cwd, VX_PATH_MAX);
    current[VX_PATH_MAX - 1] = '\0';

    char last[VX_PATH_MAX] = {0};

    while (current[0] != '\0')
    {
        if (sk_is_initialized_at(current))
        {
            snprintf(out_path, size, "%s", current);
            return true;
        }

        strncpy(last, current, VX_PATH_MAX);
        last[VX_PATH_MAX - 1] = '\0';

        if (!vx_path_parent(current))
        {
            break;
        }

        if (strcmp(current, last) == 0)
        {
            break;
        }
    }

    return false;
}

vx_status sk_resolve_project_root(struct sk_ctx *ctx)
{
    if (ctx == nullptr)
    {
        return VX_ERROR;
    }

    if (ctx->rpath)
    {
        if (sk_is_initialized_at(ctx->rpath))
        {
            return VX_OK;
        }

        vx_errlog("Directory not initialized: %s", ctx->rpath);
        return VX_ERROR;
    }

    char discovered[VX_PATH_MAX];
    if (sk_discover_root(discovered, sizeof(discovered)))
    {
        ctx->rpath = mem_arena_strdup(g_sk_global_arena, discovered);
        return VX_OK;
    }

    return VX_ERROR;
}

void *sk_arena_alloc(void *user, size_t size)
{
    return mem_arena_alloc((struct mem_arena *) user, size);
}

void sk_fmt_relative_time(u64 target_epoch, char *out_buf, size_t buf_size)
{
    u64 current_epoch = vx_time_epoch_s();

    if (target_epoch == 0)
    {
        snprintf(out_buf, buf_size, "Never");
        return;
    }

    if (current_epoch < target_epoch)
    {
        snprintf(out_buf, buf_size, "Just now");
        return;
    }

    u64 delta = current_epoch - target_epoch;

    if (delta < 60)
    {
        snprintf(out_buf, buf_size, "%lus ago", delta);
    }
    else if (delta < 3600)
    {
        snprintf(out_buf, buf_size, "%lu min ago", delta / 60);
    }
    else if (delta < 86400)
    {
        snprintf(out_buf, buf_size, "%lu hours ago", delta / 3600);
    }
    else
    {
        snprintf(out_buf, buf_size, "%lu days ago", delta / 86400);
    }
}

void sk_log_time(const char *phase, vx_ticks *t)
{
    char elapsed[VX_BUF_SIZE_32];

    vx_ticks_format(t, elapsed, sizeof(elapsed));
    vx_sbuf_append(&g_sk_profile_sbuf, "%-7s: %s\n", phase != nullptr ? phase : "n/a", elapsed);
}
