#include "mem_heap.h"
#include "sk_globals.h"
#include "sk_util.h"
#include "vx_platform.h"

#include "vx_fs.h"
#include "vx_io.h"
#include "sk_paths.h"
#include "sk_array.h"

void sk_path_strip_trailing_sep(char *path)
{
    size_t len = strlen(path);
    while (len > 1 && (path[len - 1] == VX_PATH_SEP))
    {
        path[--len] = CHAR_NULTERM;
    }
}

void sk_scan_dir_r(struct sk_arena_array *sources, const char *dirpath)
{
    if (sources == nullptr || dirpath == nullptr)
    {
        return;
    }

    vx_dir_handle dir = vx_fs_dir_open(dirpath);

    if (dir == nullptr)
    {
        VX_ASSERT_LOG("Failed to open dir: %s", dirpath);
        return;
    }

    vx_dir_entry entry;

    while (vx_fs_dir_read(dir, &entry))
    {
        if (vx_fs_is_dot_dir(entry.name, entry.name_len))
        {
            continue;
        }

        u32   needed = (u32) strlen(dirpath) + (u32) strlen(VX_PATH_SEP_STR) + entry.name_len + 1;
        char *presisten_path = sk_arena_alloc(g_sk_global_arena, needed);

        snprintf(presisten_path, needed, "%s%s%s", dirpath, VX_PATH_SEP_STR, entry.name);

        bool is_dir = entry.is_dir;

        // TODO: add project type on sk_ctx
        if (!is_dir && vx_isdir(presisten_path))
        {
            is_dir = true;
        }

        if (is_dir)
        {
            sk_scan_dir_r(sources, presisten_path);
        }
        else if (entry.name_len >= 2 && sk_has_ext(entry.name, entry.name_len, ".c"))
        {
            if (!sk_arena_array_contains(sources, presisten_path))
            {
                sk_arena_array_push(sources, presisten_path);
            }
        }
    }

    vx_fs_dir_close(dir);
}
