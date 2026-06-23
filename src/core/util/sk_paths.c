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
        else if (entry.name_len >= 2)
        {
            const char *ext = nullptr;

            size_t ext_len = 0;

            for (size_t i = entry.name_len; i > 0; i--)
            {
                if (entry.name[i - 1] == CHAR_DOT)
                {
                    ext     = &entry.name[i - 1];
                    ext_len = entry.name_len - (i - 1);
                    break;
                }
            }

            if (ext &&
                (vx_strncmplit(ext, ext_len, ".c", 2) || vx_strncmplit(ext, ext_len, ".cc", 3) ||
                 vx_strncmplit(ext, ext_len, ".cpp", 4) || vx_strncmplit(ext, ext_len, ".cxx", 4) ||
                 vx_strncmplit(ext, ext_len, ".s", 2) || vx_strncmplit(ext, ext_len, ".S", 2)))
            {
                if (!sk_arena_array_contains(sources, presisten_path))
                {
                    sk_arena_array_push(sources, presisten_path);
                }
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

const char *sk_expand_path(struct mem_arena *ar, const char *path)
{
    if (ar == nullptr)
    {
        return nullptr;
    }

    const char *rpath_var = "__sk_rpath__";
    const char *found     = strstr(path, rpath_var);

    if (found != nullptr)
    {
        if (found != path)
        {
            vx_warn("__sk_rpath__ must appear at the start of the path: %s", path);
            return path;
        }

        const char *rpath     = g_sk_global_ctx.rpath;
        size_t      rpath_len = strlen(rpath);
        size_t      var_len   = strlen(rpath_var);

        size_t prefix_len = (size_t) (found - path);
        size_t suffix_len = strlen(found + var_len);
        size_t total      = prefix_len + rpath_len + suffix_len + 1;

        char *result = mem_arena_alloc(ar, total);
        memcpy(result, path, prefix_len);
        memcpy(result + prefix_len, rpath, rpath_len);
        memcpy(result + prefix_len + rpath_len, found + var_len, suffix_len + 1);

        path = result;
    }

    if (path[0] == CHAR_TILDE)
    {
        const char *home = vx_platform_get_home_dir();
        if (home == nullptr)
        {
            return path;
        }

        const char *rest = path + 1;

        if (*rest == VX_PATH_SEP)
        {
            rest++;
        }

        return sk_path_join(ar, home, rest);
    }

    return path;
}

void sk_path_canonicalize(char *path)
{
    if (path == nullptr || *path == CHAR_NULTERM)
    {
        return;
    }

    char is_absolute = (path[0] == CHAR_SLASH);

    char *parts[VX_BUF_SIZE_256];
    u32   depth = 0;
    char *out   = path;

    char tmp[VX_PATH_MAX];
    strncpy(tmp, path, VX_PATH_MAX - 1);
    tmp[VX_PATH_MAX - 1] = CHAR_NULTERM;

    char *saveptr = nullptr;
    char *tok     = strtok_r(tmp, "/", &saveptr);

    while (tok)
    {
        if (strcmp(tok, ".") == 0)
        {
            // skip
        }
        else if (strcmp(tok, "..") == 0)
        {
            if (depth > 0)
            {
                depth--;
            }
        }
        else
        {
            parts[depth++] = tok;
        }

        tok = strtok_r(nullptr, "/", &saveptr);
    }

    u32 written = 0;

    if (is_absolute)
    {
        out[written++] = CHAR_SLASH;
    }

    for (u32 i = 0; i < depth; i++)
    {
        written +=
            snprintf(out + written, VX_PATH_MAX - written, "%s%s", i == 0 ? "" : "/", parts[i]);
    }

    out[written] = CHAR_NULTERM;
}
