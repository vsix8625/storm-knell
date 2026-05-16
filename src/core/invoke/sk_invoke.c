#include "sk_invoke.h"
#include "sk_array.h"
#include "sk_cmd_init.h"
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

char **sk_invoke_compile_nularr(struct sk_target *t, u32 source_idx, struct mem_arena *arena)
{
    if (t == nullptr || arena == nullptr)
    {
        return nullptr;
    }

    // cc + cflags + includes + defines + "-c" + src + "-o" + obj + NULL
    u32 total_args =
        1 + t->cfg.cflags_count + t->cfg.includes_count + t->cfg.defines_count + 2 + 2 + 1;

    char **argv = mem_arena_alloc(arena, sizeof(char *) * total_args);

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

    char *obj_path = mem_arena_alloc(arena, VX_PATH_MAX);
    snprintf(
        obj_path, VX_PATH_MAX, "%s%s%s.o", t->finalized_obj_dirpath, VX_PATH_SEP_STR, file_name);

    argv[arg_idx++] = "-c";
    argv[arg_idx++] = (char *) src_path;
    argv[arg_idx++] = "-o";
    argv[arg_idx++] = obj_path;

    argv[arg_idx] = nullptr;

    return argv;
}

char **sk_invoke_link_nularr(struct sk_target *t, struct mem_arena *arena)
{
    if (t == nullptr || arena == nullptr)
    {
        return nullptr;
    }

    // cc + -fuse-ld + objs + lflags + lib_paths + libs + -o + out + NULL
    u32 obj_count  = t->sources->count;
    u32 total_args = 1            // cc
                     + 1          // -fuse-ld=<linker>
                     + obj_count  // all .o files
                     + t->cfg.lflags_count + t->cfg.lib_paths_count + t->cfg.libs_count +
                     2  // -o <output>
                     + 1;

    char **argv = mem_arena_alloc(arena, sizeof(char *) * total_args);
    if (argv == nullptr)
    {
        return nullptr;
    }

    u32 i     = 0;
    argv[i++] = t->cfg.cc;

    // -fuse-ld=mold / lld / ld
    if (t->cfg.linker != nullptr)
    {
        char *fuse = mem_arena_alloc(arena, VX_BUF_SIZE_32);
        snprintf(fuse, VX_BUF_SIZE_32, "-fuse-ld=%s", t->cfg.linker);
        argv[i++] = fuse;
    }

    if (t->kind == SK_TARGET_KIND_SHARED)
    {
        argv[i++] = "-shared";
        total_args++;
    }

    // object files
    for (u32 j = 0; j < obj_count; j++)
    {
        const char *src      = (const char *) t->sources->items[j];
        const char *filename = strrchr(src, VX_PATH_SEP);
        filename             = filename ? filename + 1 : src;
        char *obj            = mem_arena_alloc(arena, VX_PATH_MAX);

        snprintf(obj, VX_PATH_MAX, "%s%s%s.o", t->finalized_obj_dirpath, VX_PATH_SEP_STR, filename);
        argv[i++] = obj;
    }

    for (u32 j = 0; j < t->cfg.lflags_count; j++)
    {
        argv[i++] = t->cfg.lflags[j];
    }

    for (u32 j = 0; j < t->cfg.lib_paths_count; j++)
    {
        argv[i++] = t->cfg.lib_paths[j];
    }

    for (u32 j = 0; j < t->cfg.libs_count; j++)
    {
        argv[i++] = t->cfg.libs[j];
    }

    // output path
    char *out = mem_arena_alloc(arena, VX_PATH_MAX);
    snprintf(out, VX_PATH_MAX, "%s%s%s", t->finalized_bin_dirpath, VX_PATH_SEP_STR, t->out_name);

    argv[i++] = "-o";
    argv[i++] = out;
    argv[i]   = nullptr;

    return argv;
}

char **sk_invoke_ar_nularr(struct sk_target *t, struct sk_meta *meta, struct mem_arena *arena)
{
    if (t == nullptr || meta == nullptr || arena == nullptr)
    {
        return nullptr;
    }

    // ar + rcs + output.a + objs + NULL
    u32 total_args = 1 + 1 + 1 + t->sources->count + 1;

    char **argv = mem_arena_alloc(arena, sizeof(char *) * total_args);

    if (argv == nullptr)
    {
        return nullptr;
    }

    u32 i     = 0;
    argv[i++] = meta->ar_path;
    argv[i++] = "rcs";

    char *out = mem_arena_alloc(arena, VX_PATH_MAX);
    snprintf(
        out, VX_PATH_MAX, "%s%slib%s.a", t->finalized_bin_dirpath, VX_PATH_SEP_STR, t->out_name);

    argv[i++] = out;

    for (u32 j = 0; j < t->sources->count; j++)
    {
        const char *src      = (const char *) t->sources->items[j];
        const char *filename = strrchr(src, VX_PATH_SEP);
        filename             = filename ? filename + 1 : src;
        char *obj            = mem_arena_alloc(arena, VX_PATH_MAX);

        snprintf(obj, VX_PATH_MAX, "%s%s%s.o", t->finalized_obj_dirpath, VX_PATH_SEP_STR, filename);
        argv[i++] = obj;
    }

    argv[i] = nullptr;
    return argv;
}
