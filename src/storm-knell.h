#ifndef STORM_KNELL_H_
#define STORM_KNELL_H_

#include "sk_cli.h"
#include "sk_cmd_cache.h"

#define SK_MAX_SURGE_TOOL_ARGS 32

struct sk_surge_tool
{
    const char *name;
    char       *args[SK_MAX_SURGE_TOOL_ARGS];
    i32         args_count;
};

struct sk_ctx
{
    // initialized in sk_lexer.c
    struct sk_tokens *tokens;
    // initialized in sk_parser.c
    struct sk_ast_nodes *nodes;

    vx_sv stormfile;

    sk_cmd active_cmd;
    sk_opt active_opt;

    const char *rpath;

    u32 cores;
    u32 threads;

    struct sk_cache_config ccfg;

    const char *sk_global_config_dir;

    const char *surge_target;

    char **surge_passthrough_argv;
    i32    surge_passthrough_argc;

    struct sk_arena_array *setvars;

    vx_mutex console_lock;

    struct sk_surge_tool surge_tool;

    // NYI
    //    char sk_server_ssname[16];
    //    char sk_server_sskey[16];
};

void sk_shutdown(void);

#endif  // STORM-KNELL_H_
