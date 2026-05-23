#pragma once

#include "vx.h"

struct sk_cache_entry
{
    char cache_path[VX_PATH_MAX * 2];
    char shard_dir[VX_PATH_MAX];
    char hash_str[17];
};

struct sk_cache_proj_header
{
    u32 count;
    u32 _pad;
};

struct sk_cache_proj_entry
{
    u8 hash[8];

    char s_path[VX_PATH_MAX];
    char o_path[VX_PATH_MAX];
    char t_name[64];
};

/*
 * Resolves the cache path for a given hash into entry.
 * Does not create any directories or files.
 *
 * @param out_hash  the xxh3 hash bytes from sk_hash_setup.
 * @param entry     output populated with cache_path, shard_dir, hash_str.
 * @return VX_OK on success, VX_ERROR if cache dir could not be resolved.
 */
vx_status sk_cache_resolve(const u8 *out_hash, struct sk_cache_entry *entry);

/*
 * Checks whether a cached object exists for this entry.
 *
 * @return true if the .o exists in cache, false otherwise.
 */
bool sk_cache_exists(const struct sk_cache_entry *entry);

/*
 * Stores a compiled object into the global cache via hardlink.
 * Creates the shard directory if it does not exist.
 *
 * @param local_obj     path to the freshly compiled .o file.
 * @param entry         resolved cache entry.
 * @return VX_OK on success, VX_ERROR otherwise.
 */
vx_status sk_cache_store(const struct sk_cache_entry *entry, const char *local_obj);

/*
 * Restores a cached object to the local obj dir via hardlink.
 *
 * @param entry         resolved cache entry.
 * @param local_obj     destination path in the local build dir.
 * @return VX_OK on success, VX_ERROR otherwise.
 */
vx_status sk_cache_restore(const struct sk_cache_entry *entry, const char *local_obj);

void sk_cache_record(const u8 *hash, const char *s_path, const char *o_path, const char *t_name);
