#include "sk_cmd_config.h"
#include "sk_globals.h"
#include "sk_paths.h"
#include "sk_util.h"
#include "storm-knell.h"
#include "vx_fs.h"

vx_status sk_cmd_config_fn(struct sk_ctx *ctx)
{
    if (ctx == nullptr)
    {
        return VX_ERROR;
    }

    return VX_OK;
}

// v0.5.2
vx_status sk_config_add_cc_path(const char *cc_realpath)
{
    if (cc_realpath == nullptr)
    {
        return VX_ERROR;
    }

    const char *base_config_dir = vx_platform_get_config_dir();

    if (base_config_dir == nullptr)
    {
        return VX_ERROR;
    }

    char *global_dir = sk_path_join(g_sk_global_arena, base_config_dir, SK_PATH_STORM_KNELL);

    char *conf_path = sk_path_join(g_sk_global_arena, global_dir, "compilers.conf");

    if (vx_fs_exists(conf_path))
    {
        vx_sv file = vx_fs_read(conf_path, sk_arena_alloc, g_sk_global_arena);

        if (file.data != nullptr)
        {
            const char *cursor = (const char *) file.data;
            const char *end    = cursor + file.len;

            while (cursor < end)
            {
                const char *line_end = strchr(cursor, CHAR_NEWLINE);
                if (line_end == nullptr)
                {
                    line_end = end;
                }

                size_t line_len = line_end - cursor;
                while (line_len > 0 && (cursor[line_len - 1] == CHAR_CARRIAGE ||
                                        cursor[line_len - 1] == CHAR_SPACE))
                {
                    line_len--;
                }

                // If the path is already registered, skip appending
                if (line_len == strlen(cc_realpath) && strncmp(cc_realpath, cursor, line_len) == 0)
                {
                    return VX_OK;
                }

                cursor = (line_end == end) ? end : line_end + 1;
            }
        }
    }

    if (!vx_fs_is_exec(cc_realpath))
    {
        vx_warn("'%s' not found or not executable", cc_realpath);
        return VX_ERROR;
    }

    if (vx_fappend(conf_path, "%s\n", cc_realpath) != VX_OK)
    {
        return VX_ERROR;
    }
    vx_log("Added: %s' -> %s (To load: sk init [--force])", cc_realpath, conf_path);

    return VX_OK;
}
