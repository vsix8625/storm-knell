#ifndef STORM_KNELL_H_
#define STORM_KNELL_H_

#include "sk_cli.h"

struct sk_ctx
{
    // initialized in sk_lexer.c
    struct sk_tokens *tokens;
    // initialized in sk_parser.c
    struct sk_ast_nodes *nodes;

    vx_sv stormfile;

    sk_cmd active_cmd;
    sk_opt active_opt;

    const char *init_dir;
    const char *rpath;

    u32 cores;
    u32 threads;
};

void sk_shutdown(void);

#endif  // STORM-KNELL_H_
