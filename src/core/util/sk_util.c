#include "sk_util.h"
#include "mem_arena.h"
#include "sk_globals.h"
#include "sk_paths.h"

#include "vx_fs.h"
#include "vx_string.h"
#include "vx_time.h"
#include "vx_util.h"

bool sk_is_initialized_at(const char *dir)
{
    if (dir == nullptr)
    {
        return false;
    }

    char *storm = sk_path_join(g_sk_arena, dir, SK_PATH_STORM_DIR);

    return vx_isdir(storm);
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
        ctx->rpath = mem_arena_strdup(g_sk_arena, discovered);
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

void sk_util_show_tips(bool show)
{
    const char *cache = vx_platform_get_cache_dir();

    char path_buf[VX_PATH_MAX];

    char misc_dir[VX_BUF_SIZE_2048];

    snprintf(misc_dir,
             sizeof(misc_dir),
             "%s%s%s%s%s",
             cache,
             VX_PATH_SEP_STR,
             SK_PATH_STORM_KNELL,
             VX_PATH_SEP_STR,
             "misc");

    if (vx_mkdir_p(misc_dir) != VX_OK)
    {
        return;
    }

    snprintf(path_buf, sizeof(path_buf), "%s%s%s", misc_dir, VX_PATH_SEP_STR, "show_tips.lock");

    if (show)
    {
        if (!vx_isfile(path_buf))
        {
            vx_fwrite(path_buf, "%lu", vx_time_epoch_s());
        }
    }
    else
    {
        if (vx_isfile(path_buf))
        {
            vx_fs_rmrf(path_buf);
        }
    }
}

bool sk_util_is_show_tips_on(void)
{
    const char *cache = vx_platform_get_cache_dir();

    char path_buf[VX_BUF_SIZE_2048];

    snprintf(path_buf,
             sizeof(path_buf),
             "%s%s%s%s%s%s%s",
             cache,
             VX_PATH_SEP_STR,
             SK_PATH_STORM_KNELL,
             VX_PATH_SEP_STR,
             "misc",
             VX_PATH_SEP_STR,
             "show_tips.lock");

    return vx_isfile(path_buf);
}

bool sk_util_array_contains_str(vx_array *arr, const char *str)
{
    if (arr == nullptr || arr->count == 0 || str == nullptr || str[0] == CHAR_NULTERM)
    {
        return false;
    }

    if ((0 == strcmp(arr->elements[0], str)) || (0 == strcmp(arr->elements[arr->count], str)))
    {
        return true;
    }

    for (size_t i = 1; i < arr->count - 1; i++)
    {
        if ((0 == strcmp(arr->elements[i], str)))
        {
            return true;
        }
    }

    return false;
}
