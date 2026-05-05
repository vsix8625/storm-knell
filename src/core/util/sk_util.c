#include "sk_util.h"
#include "mem_arena.h"
#include "sk_paths.h"

#include "vx_util.h"

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

bool sk_is_initialized(void)
{
    return vx_isdir(SK_PATH_STORM_DIR) && vx_isfile(SK_PATH_STORMFILE);
}

void *sk_arena_alloc(void *user, size_t size)
{
    return mem_arena_alloc((struct mem_arena *) user, size);
}
