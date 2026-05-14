#include "sk_invoke.h"
#include "sk_array.h"
#include "sk_eval.h"
#include "sk_globals.h"
#include "sk_config.h"

#include "vx_io.h"
#include "vx_fs.h"

#include <stdio.h>

char *sk_invoke_compile(struct sk_target *t, u32 source_idx)
{
    if (t == nullptr)
    {
        return nullptr;
    }

    size_t buf_stride = VX_PATH_MAX * 4;
    char  *buf        = mem_arena_alloc(g_sk_global_arena, buf_stride);

    if (buf == nullptr)
    {
        return nullptr;
    }

    const char *src_path = (const char *) t->sources->items[source_idx];

    const char *file_name = strrchr(src_path, VX_PATH_SEP);
    file_name             = file_name ? file_name + 1 : src_path;

    char obj_path[VX_PATH_MAX];
    snprintf(obj_path,
             sizeof(obj_path),
             "%s%s%s.o",
             t->finalized_obj_dirpath,
             VX_PATH_SEP_STR,
             file_name);

    i32 written = snprintf(buf, buf_stride, "%s", t->cfg.cc);

    for (u32 i = 0; i < t->cfg.cflags_count; i++)
    {
        written += snprintf(buf + written, buf_stride - written, " %s", t->cfg.cflags[i]);
    }

    for (u32 i = 0; i < t->cfg.includes_count; i++)
    {
        written += snprintf(buf + written, buf_stride - written, " %s", t->cfg.includes[i]);
    }

    for (u32 i = 0; i < t->cfg.defines_count; i++)
    {
        written += snprintf(buf + written, buf_stride - written, " %s", t->cfg.defines[i]);
    }

    snprintf(buf + written, buf_stride - written, " -c %s -o %s", src_path, obj_path);

    return buf;
}

char **sk_invoke_compile_nularr(struct sk_target *t, u32 source_idx)
{
    if (t == nullptr)
    {
        return nullptr;
    }

    // cc + cflags + includes + defines + "-c" + src + "-o" + obj + NULL
    u32 total_args =
        1 + t->cfg.cflags_count + t->cfg.includes_count + t->cfg.defines_count + 2 + 2 + 1;

    char **argv = mem_arena_alloc(g_sk_global_arena, sizeof(char *) * total_args);

    if (argv == nullptr)
    {
        return nullptr;
    }

    u32 arg_idx = 0;

    argv[arg_idx++] = t->cfg.cc;

    for (u32 i = 0; i < t->cfg.cflags_count; i++)
    {
        argv[arg_idx++] = t->cfg.cflags[i];
    }

    for (u32 i = 0; i < t->cfg.includes_count; i++)
    {
        argv[arg_idx++] = t->cfg.includes[i];
    }

    for (u32 i = 0; i < t->cfg.defines_count; i++)
    {
        argv[arg_idx++] = t->cfg.defines[i];
    }

    const char *src_path  = (const char *) t->sources->items[source_idx];
    const char *file_name = strrchr(src_path, VX_PATH_SEP);

    file_name = file_name ? file_name + 1 : src_path;

    char *obj_path = mem_arena_alloc(g_sk_global_arena, VX_PATH_MAX);
    snprintf(
        obj_path, VX_PATH_MAX, "%s%s%s.o", t->finalized_obj_dirpath, VX_PATH_SEP_STR, file_name);

    argv[arg_idx++] = "-c";
    argv[arg_idx++] = (char *) src_path;
    argv[arg_idx++] = "-o";
    argv[arg_idx++] = obj_path;

    argv[arg_idx] = nullptr;

    return argv;
}
