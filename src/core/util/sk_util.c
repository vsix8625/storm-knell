#include "sk_util.h"
#include "mem_arena.h"
#include "mem_heap.h"
#include "sk_paths.h"

#include "vx_util.h"
#include "vx_string.h"

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
            ctx->init_dir = ctx->rpath;
            return VX_OK;
        }
        return VX_ERROR;
    }

    char discovered[VX_PATH_MAX];
    if (sk_discover_root(discovered, sizeof(discovered)))
    {
        ctx->init_dir = mem_heap_strdup(discovered);
        return VX_OK;
    }

    return VX_ERROR;
}

void *sk_arena_alloc(void *user, size_t size)
{
    return mem_arena_alloc((struct mem_arena *) user, size);
}
