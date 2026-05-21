#include "sk_cmd_cache.h"
#include "mem_arena.h"
#include "sk_cache.h"
#include "storm-knell.h"
#include "sk_paths.h"
#include "sk_globals.h"
#include "sk_util.h"

#include "vx_defs.h"
#include "vx_fs.h"
#include "vx_platform.h"
#include <stdio.h>
#include <stdlib.h>

vx_status sk_global_config_resolve_path(char *out_buf, size_t buf_len)
{
    const char *base_config_dir = vx_platform_get_config_dir();
    if (base_config_dir == nullptr)
    {
        return VX_ERROR;
    }

    snprintf(out_buf,
             buf_len,
             "%s%s%s%sconfig",
             base_config_dir,
             VX_PATH_SEP_STR,
             SK_PATH_STORM_KNELL,
             VX_PATH_SEP_STR);

    return VX_OK;
}

vx_status sk_cache_config_write(const char *config_path, const struct sk_cache_config *cfg)
{
    return vx_fwrite(config_path,
                     "max_size_mb=%u\nprune_threshold_mb=%u\n",
                     cfg->max_size_mb,
                     cfg->prune_threshold_mb);
}

vx_status sk_cache_config_load(const char *config_path, struct sk_cache_config *cfg)
{
    if (config_path == nullptr)
    {
        return VX_ERROR;
    }

    cfg->max_size_mb        = 500;
    cfg->prune_threshold_mb = 400;

    FILE *f = fopen(config_path, "r");
    if (f == nullptr)
    {
        sk_cache_config_write(config_path, cfg);
        return VX_OK;
    }

    char line[VX_BUF_SIZE_128];

    while (fgets(line, sizeof(line), f))
    {
        char *key = strtok(line, "= \t\r\n");
        char *val = strtok(NULL, " \t\r\n");

        if (key == nullptr || val == nullptr)
        {
            continue;
        }

        if (strcmp(key, "max_size_mb") == 0)
        {
            cfg->max_size_mb = (u32) atoi(val);
        }
        else if (strcmp(key, "prune_threshold_mb") == 0)
        {
            cfg->prune_threshold_mb = (u32) atoi(val);
        }
    }
    fclose(f);

    return VX_OK;
}

void sk_cache_config_init_global(struct sk_cache_config *cfg)
{
    char resolved_config_fpath[VX_PATH_MAX];

    if (sk_global_config_resolve_path(resolved_config_fpath, sizeof(resolved_config_fpath)) !=
        VX_OK)
    {
        cfg->max_size_mb        = 500;
        cfg->prune_threshold_mb = 400;
        return;
    }

    const char *base_config_dir = vx_platform_get_config_dir();

    char *global_dir = sk_path_join(g_sk_global_arena, base_config_dir, SK_PATH_STORM_KNELL);

    g_sk_global_ctx.sk_global_config_dir = mem_arena_strdup(g_sk_global_arena, global_dir);

    if (vx_mkdir_p(global_dir) != VX_OK)
    {
        vx_errlog("Failed to create: '%s' directory", global_dir);
        return;
    }

    if (sk_cache_config_load(resolved_config_fpath, cfg) != VX_OK)
    {
        cfg->max_size_mb        = 500;
        cfg->prune_threshold_mb = 400;
        sk_cache_config_write(resolved_config_fpath, cfg);
    }
}

struct sk_cache_info sk_cache_calculate_size(void)
{
    struct sk_cache_info info = {0};

    const char *cache_dir = vx_platform_get_cache_dir();

    if (cache_dir == nullptr)
    {
        return info;
    }

    char *base_path = sk_path_join(g_sk_global_arena, cache_dir, SK_PATH_STORM_KNELL);

    u64 total_size = 0;
    u64 obj_count  = 0;

    for (i32 i = 0x00; i <= 0xff; i++)
    {
        char *shard_path = sk_path_join_hex(g_sk_global_arena, base_path, i);

        vx_dir_handle dir = vx_fs_dir_open(shard_path);

        if (dir == nullptr)
        {
            continue;
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

            char *file_path = sk_path_join(g_sk_global_arena, shard_path, entry.name);

            u64 file_size   = 0;
            u64 dummy_mtime = 0;

            if (vx_fs_get_file_metrics(file_path, &file_size, &dummy_mtime) == VX_OK)
            {
                total_size += file_size;
                obj_count++;
            }
        }

        vx_fs_dir_close(dir);
    }

    info.total_size   = total_size;
    info.object_count = obj_count;

    return info;
}

i32 sk_cache_sort_evict(const void *a, const void *b)
{
    const struct sk_evict_item *item_a = (const struct sk_evict_item *) a;
    const struct sk_evict_item *item_b = (const struct sk_evict_item *) b;

    if (item_a->access_time < item_b->access_time)
    {
        return -1;
    }

    if (item_a->access_time > item_b->access_time)
    {
        return 1;
    }
    return 0;
}

void sk_cache_prune_to_size(u32 prune_threshold_mb)
{
    const char *base_cache = vx_platform_get_cache_dir();
    if (base_cache == nullptr)
    {
        return;
    }

    char *cache_root = sk_path_join(g_sk_global_arena, base_cache, SK_PATH_STORM_KNELL);

    vx_dir_handle root_dir = vx_fs_dir_open(cache_root);
    if (root_dir == nullptr)
    {
        return;
    }

    u32 max_entries       = VX_BUF_SIZE_4096;
    u32 current_count     = 0;
    u64 total_cache_bytes = 0;

    struct sk_evict_item *items =
        mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_evict_item) * max_entries);

    vx_dir_entry shard_entry;

    // loop root dir
    while (vx_fs_dir_read(root_dir, &shard_entry) && current_count < max_entries)
    {
        if (vx_fs_is_dot_dir(shard_entry.name, shard_entry.name_len) ||
            shard_entry.name[0] == CHAR_DOT)
        {
            continue;
        }

        if (shard_entry.is_dir)
        {
            char *shard_path = sk_path_join(g_sk_global_arena, cache_root, shard_entry.name);

            vx_dir_handle shard_dir = vx_fs_dir_open(shard_path);
            if (shard_dir == nullptr)
            {
                continue;
            }

            vx_dir_entry file_entry;

            // loop shard dirs
            while (vx_fs_dir_read(shard_dir, &file_entry) && current_count < max_entries)
            {
                if (vx_fs_is_dot_dir(file_entry.name, file_entry.name_len) ||
                    file_entry.name[0] == CHAR_DOT)
                {
                    continue;
                }

                if (!file_entry.is_dir)
                {
                    struct sk_evict_item *item = &items[current_count];

                    u32 needed = snprintf(item->name,
                                          sizeof(item->name),
                                          "%s%s%s",
                                          shard_entry.name,
                                          VX_PATH_SEP_STR,
                                          file_entry.name);

                    item->name_len = needed < 127 ? needed : 127;

                    item->name[item->name_len] = CHAR_NULTERM;

                    char *file_path = sk_path_join(g_sk_global_arena, cache_root, item->name);

                    vx_stat_struct st;
                    if (vx_stat(file_path, &st) == 0)
                    {
                        item->size_bytes   = (u64) st.st_size;
                        item->access_time  = (u64) st.st_atime;
                        total_cache_bytes += (u64) st.st_size;
                    }

                    current_count++;
                }
            }
            vx_fs_dir_close(shard_dir);
        }
    }
    vx_fs_dir_close(root_dir);

    if (current_count == 0)
    {
        return;
    }

    u64 target_bytes = (u64) prune_threshold_mb * 1024 * 1024;

    if (total_cache_bytes <= target_bytes)
    {
        return;
    }

    qsort(items, current_count, sizeof(struct sk_evict_item), sk_cache_sort_evict);

    for (u32 i = 0; i < current_count; i++)
    {
        if (total_cache_bytes <= target_bytes)
        {
            break;
        }

        char *target_kill_file = sk_path_join(g_sk_global_arena, cache_root, items[i].name);

        u64 freed_bytes = items[i].size_bytes;
        if (vx_fs_rmrf(target_kill_file))
        {
            total_cache_bytes -= freed_bytes;
        }
    }
}

//----------------------------------------------------------------------------------------------------

vx_status sk_cmd_cache_fn(struct sk_ctx *ctx)
{
    if (ctx == nullptr)
    {
        return VX_ERROR;
    }

    if (ctx->active_opt & SK_OPT_CACHE_NUKE)
    {
        const char *cache_dir = vx_platform_get_cache_dir();

        char *sk_cache_dir = sk_path_join(g_sk_global_arena, cache_dir, SK_PATH_STORM_KNELL);

        if (!vx_fs_rmrf(sk_cache_dir))
        {
            return VX_ERROR;
        }

        vx_log("Cache nuked");
        return VX_OK;
    }

    struct sk_cache_info cache_info = sk_cache_calculate_size();

    vx_log("Cache objects: %llu | size: %.2f MB",
           (unsigned long long) cache_info.object_count,
           (f32) cache_info.total_size / 1048576.0f);
    return VX_OK;
}
