#include "mem_arena.h"
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

static const char *sk_path_normalize(const char *path)
{
    if (path[0] == CHAR_DOT && path[1] == CHAR_SLASH)
    {
        return path + 2;
    }
    return path;
}

void sk_scan_dir_r(struct sk_arena_array *sources,
                   char                 **excludes,
                   u32                    exclude_count,
                   const char            *dirpath)
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

    const char *normalized_dir = sk_path_normalize(dirpath);

    if (excludes)
    {
        for (u32 i = 0; i < exclude_count; i++)
        {
            const char *normalized_excl = sk_path_normalize(excludes[i]);

            if (excludes[i] && strcmp(normalized_dir, normalized_excl) == 0)
            {
                vx_dbglog("Excluding directory: %s", normalized_dir);
                return;
            }
        }
    }

    vx_dir_entry entry;

    while (vx_fs_dir_read(dir, &entry))
    {
        if (vx_fs_is_dot_dir(entry.name, entry.name_len))
        {
            continue;
        }

        if (entry.name[0] == CHAR_DOT)
        {
            continue;
        }

        u32   needed = (u32) strlen(dirpath) + (u32) strlen(VX_PATH_SEP_STR) + entry.name_len + 1;
        char *presisten_path = sk_arena_alloc(g_sk_global_arena, needed);

        snprintf(presisten_path, needed, "%s%s%s", dirpath, VX_PATH_SEP_STR, entry.name);

        bool skip = false;
        if (excludes)
        {
            for (u32 i = 0; i < exclude_count; i++)
            {
                if (excludes[i] && strcmp(presisten_path, excludes[i]) == 0)
                {
                    vx_dbglog("Excluding: %s", presisten_path);
                    skip = true;
                    break;
                }
            }
        }

        if (skip)
        {
            continue;
        }

        bool is_dir = entry.is_dir;

        if (!is_dir && vx_isdir(presisten_path))
        {
            is_dir = true;
        }

        if (is_dir)
        {
            sk_scan_dir_r(sources, excludes, exclude_count, presisten_path);
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

char *sk_path_join(struct mem_arena *ar, const char *a, const char *b)
{
    if (ar == nullptr || a == nullptr || b == nullptr)
    {
        return nullptr;
    }

    size_t len = strlen(a) + strlen(b) + 2;
    char  *buf = mem_arena_alloc(ar, len);
    snprintf(buf, len, "%s%s%s", a, VX_PATH_SEP_STR, b);
    return buf;
}

char *sk_path_join_hex(struct mem_arena *ar, const char *a, u32 hex)
{
    size_t len = strlen(a) + 1 + 3 + 1;
    char  *buf = mem_arena_alloc(ar, len);
    snprintf(buf, len, "%s%s%02x", a, VX_PATH_SEP_STR, hex);
    return buf;
}
