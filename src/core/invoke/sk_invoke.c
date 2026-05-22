#include "sk_invoke.h"
#include "sk_array.h"
#include "sk_cmd_init.h"
#include "sk_eval.h"
#include "sk_globals.h"
#include "sk_config.h"

#include "vx_io.h"
#include "vx_fs.h"
#include "vx_thread.h"

_Atomic u32 g_sk_ccmds_count = 0;

struct sk_ccmds_entry *g_sk_ccmds = {0};

#include <stdatomic.h>
#include <stdio.h>

static void verbose_argv_log(char **argv, u32 idx)
{
    if (g_sk_global_ctx.active_opt & SK_OPT_VERBOSE)
    {
        static _Thread_local char t_log_buf[VX_BUF_SIZE_1024];
        vx_sbuf sbuf = {.data = t_log_buf, .size = sizeof(t_log_buf), .offset = 0};

        vx_mutex_lock(&g_sk_global_ctx.console_lock);
        vx_sbuf_append(&sbuf, "\n%s\n", argv[idx - 1]);
        vx_sbuf_append(
            &sbuf,
            "================================================================================\n");
        for (size_t i = 0; i < idx; i++)
        {
            vx_sbuf_append(&sbuf, "%s ", argv[i]);
        }
        vx_printf("%s\n", t_log_buf);
        vx_mutex_unlock(&g_sk_global_ctx.console_lock);
    }
}

char *sk_invoke_compile(struct sk_target *t, u32 source_idx)
{
    if (t == nullptr)
    {
        return nullptr;
    }

    size_t buf_stride = VX_PATH_MAX * 4;

    char *buf = mem_arena_alloc(g_sk_global_arena, buf_stride);

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
        2 + t->cfg.cflags_count + t->cfg.includes_count + t->cfg.defines_count + 2 + 2 + 1;

    char **argv = mem_arena_alloc(arena, sizeof(char *) * total_args);

    if (argv == nullptr)
    {
        return nullptr;
    }

    u32 idx = 0;

    argv[idx++] = t->cfg.cc;

    if (vx_isatty(STDOUT_FILENO))
    {
        argv[idx++] = "-fdiagnostics-color=always";
    }

    for (u32 i = 0; i < t->cfg.cflags_count; i++)
    {
        argv[idx++] = t->cfg.cflags[i];
    }

    for (u32 i = 0; i < t->cfg.includes_count; i++)
    {
        argv[idx++] = t->cfg.includes[i];
    }

    for (u32 i = 0; i < t->cfg.defines_count; i++)
    {
        argv[idx++] = t->cfg.defines[i];
    }

    const char *src_path  = (const char *) t->sources->items[source_idx];
    const char *file_name = strrchr(src_path, VX_PATH_SEP);

    file_name = file_name ? file_name + 1 : src_path;

    char *obj_path = mem_arena_alloc(arena, VX_PATH_MAX);
    snprintf(
        obj_path, VX_PATH_MAX, "%s%s%s.o", t->finalized_obj_dirpath, VX_PATH_SEP_STR, file_name);

    argv[idx++] = "-c";
    argv[idx++] = (char *) src_path;
    argv[idx++] = "-o";
    argv[idx++] = obj_path;

    argv[idx] = nullptr;

    verbose_argv_log(argv, idx);
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
    u32 total_args = 1  // cc
                     + ((t->cfg.linker != nullptr) ? 1 : 0) +
                     ((t->kind == SK_TARGET_KIND_SHARED) ? 1 : 0) + obj_count  // all .o files
                     + t->cfg.lflags_count + t->cfg.lib_paths_count + t->cfg.libs_count +
                     2     // -o <output>
                     + 1;  // nullptr sentinel

    char **argv = mem_arena_alloc(arena, sizeof(char *) * total_args);
    if (argv == nullptr)
    {
        return nullptr;
    }

    u32 idx     = 0;
    argv[idx++] = t->cfg.cc;

    // -fuse-ld=mold / lld / ld
    if (t->cfg.linker != nullptr)
    {
        char *fuse = mem_arena_alloc(arena, VX_BUF_SIZE_32);
        snprintf(fuse, VX_BUF_SIZE_32, "-fuse-ld=%s", t->cfg.linker);
        argv[idx++] = fuse;
    }

    if (t->kind == SK_TARGET_KIND_SHARED)
    {
        argv[idx++] = "-shared";
    }

    // object files
    for (u32 j = 0; j < obj_count; j++)
    {
        const char *src      = (const char *) t->sources->items[j];
        const char *filename = strrchr(src, VX_PATH_SEP);
        filename             = filename ? filename + 1 : src;
        char *obj            = mem_arena_alloc(arena, VX_PATH_MAX);

        snprintf(obj, VX_PATH_MAX, "%s%s%s.o", t->finalized_obj_dirpath, VX_PATH_SEP_STR, filename);
        argv[idx++] = obj;
    }

    for (u32 j = 0; j < t->cfg.lflags_count; j++)
    {
        argv[idx++] = t->cfg.lflags[j];
    }

    for (u32 j = 0; j < t->cfg.lib_paths_count; j++)
    {
        argv[idx++] = t->cfg.lib_paths[j];
    }

    for (u32 j = 0; j < t->cfg.libs_count; j++)
    {
        argv[idx++] = t->cfg.libs[j];
    }

    // output path

    argv[idx++] = "-o";
    argv[idx++] = (char *) t->artifact_path;
    argv[idx]   = nullptr;

    verbose_argv_log(argv, idx);
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

    u32 idx = 0;

    argv[idx++] = meta->ar_path;
    argv[idx++] = "rcs";

    char *out = mem_arena_alloc(arena, VX_PATH_MAX);
    snprintf(out,
             VX_PATH_MAX,
             "%s%s%s%s%s",
             t->finalized_bin_dirpath,
             VX_PATH_SEP_STR,
             VX_LIB_PREFIX,
             t->out_name,
             VX_LIB_EXT);

    argv[idx++] = out;

    for (u32 j = 0; j < t->sources->count; j++)
    {
        const char *src      = (const char *) t->sources->items[j];
        const char *filename = strrchr(src, VX_PATH_SEP);
        filename             = filename ? filename + 1 : src;
        char *obj            = mem_arena_alloc(arena, VX_PATH_MAX);

        snprintf(obj, VX_PATH_MAX, "%s%s%s.o", t->finalized_obj_dirpath, VX_PATH_SEP_STR, filename);
        argv[idx++] = obj;
    }

    argv[idx] = nullptr;

    verbose_argv_log(argv, idx);
    return argv;
}

char **sk_invoke_syntax_check_nularr(struct sk_target *t, u32 source_idx, struct mem_arena *arena)
{
    if (t == nullptr || arena == nullptr)
    {
        return nullptr;
    }

    // cc + cflags + includes + defines + -fsyntax-only + src + NULL
    u32 total_args = 1 + t->cfg.cflags_count + t->cfg.includes_count + t->cfg.defines_count +
                     1     // -fsyntax-only
                     + 1   // src
                     + 1;  // NULL

    char **argv = mem_arena_alloc(arena, sizeof(char *) * total_args);
    if (argv == nullptr)
    {
        return nullptr;
    }

    u32 idx = 0;

    argv[idx++] = t->cfg.cc;

    if (vx_isatty(STDOUT_FILENO))
    {
        argv[idx++] = "-fdiagnostics-color=always";
    }

    for (u32 j = 0; j < t->cfg.cflags_count; j++)
    {
        argv[idx++] = t->cfg.cflags[j];
    }

    for (u32 j = 0; j < t->cfg.includes_count; j++)
    {
        argv[idx++] = t->cfg.includes[j];
    }

    for (u32 j = 0; j < t->cfg.defines_count; j++)
    {
        argv[idx++] = t->cfg.defines[j];
    }

    argv[idx++] = "-fsyntax-only";
    argv[idx++] = (char *) t->sources->items[source_idx];
    argv[idx]   = nullptr;

    verbose_argv_log(argv, idx);
    return argv;
}

vx_status sk_ccmds_write(const char *rpath)
{
    u32 count = atomic_load(&g_sk_ccmds_count);
    if (count == 0)
    {
        return VX_OK;
    }

    char path[VX_PATH_MAX];
    snprintf(path, sizeof(path), "%s/compile_commands.json", rpath);

    FILE *f = fopen(path, "w");
    if (f == nullptr)
    {
        return VX_ERROR;
    }

    fprintf(f, "[\n");
    for (u32 i = 0; i < count; i++)
    {
        struct sk_ccmds_entry *e = &g_sk_ccmds[i];

        fprintf(f, "  {\n");
        fprintf(f, "    \"directory\": \"%s\",\n", e->directory);
        fprintf(f, "    \"file\": \"%s\",\n", e->file);
        fprintf(f, "    \"arguments\": [\n");
        for (u32 j = 0; j < e->arg_count; j++)
        {
            fprintf(f, "      \"%s\"%s\n", e->arguments[j], j + 1 < e->arg_count ? "," : "");
        }
        fprintf(f, "    ]\n");
        fprintf(f, "  }%s\n", i + 1 < count ? "," : "");
    }
    fprintf(f, "]\n");

    fclose(f);
    return VX_OK;
}

void sk_ccmds_push(const char *file, const char *directory, const char **argv, u32 arg_count)
{
    u32 slot                   = atomic_fetch_add(&g_sk_ccmds_count, 1);
    g_sk_ccmds[slot].file      = mem_arena_strdup(g_sk_global_arena, file);
    g_sk_ccmds[slot].directory = directory;
    g_sk_ccmds[slot].arg_count = arg_count;

    const char **args = mem_arena_alloc(g_sk_global_arena, sizeof(char *) * arg_count);

    for (u32 i = 0; i < arg_count; i++)
    {
        args[i] = mem_arena_strdup(g_sk_global_arena, argv[i]);
    }
    g_sk_ccmds[slot].arguments = args;
}
