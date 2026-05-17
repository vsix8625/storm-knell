#include "sk_cache.h"
#include "sk_paths.h"
#include <errno.h>

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

    u64 h = *(u64 *) out_hash;

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
