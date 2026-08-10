#include "sk_cache.h"
#include "sk_paths.h"
#include "sk_globals.h"
#include "sk_util.h"

#include <errno.h>
#include <stdatomic.h>

vx_status sk_cache_resolve(const u8 *out_hash, struct sk_cache_entry *entry)
{
    if (out_hash == nullptr || entry == nullptr)
    {
        return VX_ERROR;
    }

    const char *cache_dir = vx_platform_get_cache_dir();

    if (cache_dir == nullptr)
    {
        return VX_ERROR;
    }

    u64 h;
    memcpy(&h, out_hash, sizeof(h));

    snprintf(entry->hash_str, sizeof(entry->hash_str), "%016llx", (unsigned long long) h);

    snprintf(entry->shard_dir,
             sizeof(entry->shard_dir),
             "%s%s%s%s%02x",
             cache_dir,
             VX_PATH_SEP_STR,
             SK_PATH_STORM_KNELL,
             VX_PATH_SEP_STR,
             out_hash[7]);

    snprintf(entry->cache_path,
             sizeof(entry->cache_path),
             "%s%s%s.o",
             entry->shard_dir,
             VX_PATH_SEP_STR,
             entry->hash_str);

    return VX_OK;
}

bool sk_cache_exists(const struct sk_cache_entry *entry)
{
    return vx_isfile(entry->cache_path);
}

static vx_status filecmp(const char *path1, const char *path2)
{
    FILE *f1 = fopen(path1, "rb");
    if (f1 == nullptr)
    {
        return VX_ERROR;
    }

    FILE *f2 = fopen(path2, "rb");
    if (f2 == nullptr)
    {
        fclose(f1);
        return VX_ERROR;
    }

    u8 buf1[VX_BUF_SIZE_8192];
    u8 buf2[VX_BUF_SIZE_8192];

    i32 result = 0;

    while (1)
    {
        size_t n1 = fread(buf1, 1, sizeof(buf1), f1);
        size_t n2 = fread(buf2, 1, sizeof(buf1), f2);

        if (n1 != n2 || memcmp(buf1, buf2, n1) != 0)
        {
            result = VX_ERROR;  // diff or len mismatch
            break;
        }

        if (n1 == 0)
        {
            result = VX_OK;
            break;
        }
    }

    fclose(f1);
    fclose(f2);
    return result;
}

bool sk_cache_identical(const struct sk_cache_entry *entry, const char *local_obj)
{
    vx_stat_struct st_cache, st_local;

    if (vx_stat(entry->cache_path, &st_cache) != 0)
    {
        return false;
    }

    if (vx_stat(local_obj, &st_local) != 0)
    {
        return false;
    }

    if (!S_ISREG(st_cache.st_mode) || !S_ISREG(st_local.st_mode))
    {
        return false;
    }

#if defined(__unix__) || defined(__APPLE__)
    if (st_cache.st_ino == st_local.st_ino && st_cache.st_dev == st_local.st_dev)
    {
        return true;
    }
#endif

    return (VX_OK == filecmp(entry->cache_path, local_obj));
}

vx_status sk_cache_store(const struct sk_cache_entry *entry, const char *local_obj)
{
    if (sk_cache_exists(entry))
    {
        return VX_OK;
    }

    if (vx_mkdir_p(entry->shard_dir) != VX_OK)
    {
        return VX_ERROR;
    }

    if (!vx_fs_ln(local_obj, entry->cache_path, false))
    {
        if (errno == EXDEV)
        {
            return vx_fs_cp(local_obj, entry->cache_path) ? VX_OK : VX_ERROR;
        }

        return VX_ERROR;
    }

    return vx_fs_ln(local_obj, entry->cache_path, false) ? VX_OK : VX_ERROR;
}

// TODO: validate cache integrity before reuse!!!
// BUG: if global cache entry has garbage sk will think its valid!!
vx_status sk_cache_restore(const struct sk_cache_entry *entry, const char *local_obj)
{
    if (!vx_fs_ln(entry->cache_path, local_obj, true))
    {
        if (errno == EXDEV)
        {
            return vx_fs_cp(entry->cache_path, local_obj) ? VX_OK : VX_ERROR;
        }
        return VX_ERROR;
    }
    return VX_OK;
}

void sk_cache_record(const u8 *hash, const char *s_path, const char *o_path, const char *t_name)
{
    u32 idx = atomic_fetch_add(&g_sk_cache_record_count, 1);

    struct sk_cache_proj_entry *e = &g_sk_cache_records[idx];

    memcpy(e->hash, hash, 8);

    sk_strncpy_safe(e->s_path, s_path, sizeof(e->s_path));
    sk_strncpy_safe(e->o_path, o_path, sizeof(e->o_path));
    sk_strncpy_safe(e->t_name, t_name, sizeof(e->t_name));
}
