#ifndef SK_EVAL_H_
#define SK_EVAL_H_

#include "mem_arena.h"
#include "sk_util.h"
#include "vx_defs.h"

#include "storm-knell.h"
#include "sk_lexer.h"
#include "sk_parser.h"

#include <string.h>

struct sk_cfg
{
    char *cc;
    char *linker;

    char **cflags;
    char **lflags;
    char **defines;
    char **libs;
    char **lib_paths;
    char **includes;

    u32 cflags_count;
    u32 lflags_count;
    u32 defines_count;
    u32 libs_count;
    u32 lib_paths_count;
    u32 includes_count;
};

typedef enum sk_target_kind : u8
{
    SK_TARGET_KIND_NONE = 0,

    SK_TARGET_KIND_EXEC,
    SK_TARGET_KIND_STATIC,
    SK_TARGET_KIND_SHARED,
    SK_TARGET_KIND_PCH,
} sk_target_kind;

struct sk_target
{
    char *name;
    char *out_name;
    char *out_dir;
    char *build_mode;
    char *finalized_bin_dirpath;
    char *artifact_path;
    char *finalized_obj_dirpath;
    char *finalized_filename;
    char *install_dir;

    struct sk_arena_array *sources;    // final resolved list
    struct sk_arena_array *scan_dirs;  // paths ending in /

    char **excludes;
    char **depends;

    struct sk_cfg cfg;

    u32 exclude_count;
    u32 depend_count;

    sk_target_kind kind;

    u8 pad[7];
};

struct sk_eval_result
{
    struct sk_cfg    global;
    struct sk_target targets[SK_MAX_TARGETS];

    u32 target_count;

    u32 *codegen_node_idxs;
    u32  codegen_count;

    char **var_keys;
    char **var_vals;
    u32    var_count;
};

vx_status sk_top_level_eval(struct sk_parser *p, struct sk_eval_result *result);

void sk_dbg_dump_eval(struct sk_parser *p, struct sk_eval_result *result);

static inline vx_sv tok_to_sv(struct sk_parser *p, vx_sv stormfile, u32 tok_idx)
{
    vx_sv sv;
    sv.data = stormfile.data + p->tokens->offsets[tok_idx];
    sv.len  = p->tokens->lens[tok_idx];
    return sv;
}

static inline char *sv_to_arena(struct mem_arena *arena, vx_sv sv)
{
    if (arena == nullptr || sv.len == 0)
    {
        return nullptr;
    }

    char *res = mem_arena_alloc(arena, sv.len + 1);
    memcpy(res, sv.data, sv.len);
    res[sv.len] = CHAR_NULTERM;

    return res;
}

#endif  // SK_EVAL_H_
