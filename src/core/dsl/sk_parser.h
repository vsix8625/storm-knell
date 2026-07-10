#ifndef SK_PARSER_H_
#define SK_PARSER_H_

#include "vx_defs.h"

struct sk_ctx;

typedef enum sk_ast_node_kind : u32
{
    SK_NODE_INVALID = 0,

    SK_NODE_PROGRAM,

    SK_NODE_TARGET,
    SK_NODE_SOURCES,
    SK_NODE_INCLUDES,
    SK_NODE_COMPILER,
    SK_NODE_LINKER,
    SK_NODE_CFLAGS,
    SK_NODE_LFLAGS,
    SK_NODE_DEPENDS,
    SK_NODE_BIND,

    SK_NODE_IF,
    SK_NODE_ASSIGN,
    SK_NODE_APPEND,
    SK_NODE_IDENT,
    SK_NODE_FLAG,
    SK_NODE_PATH,
    SK_NODE_CODEGEN,
    SK_NODE_CODEGEN_DEFINE,
    SK_NODE_CODEGEN_LITERAL,
    SK_NODE_PACKAGE,
    SK_NODE_PRINT,
    SK_NODE_INSTALL,
    SK_NODE_EXIT,

    SK_NODE_LIT_STRING,
    SK_NODE_LIT_BOOL,
    SK_NODE_LIT_NUMBER,

    SK_NODE_FN_ARG,

    SK_NODE_EXPR,

    SK_NODE_EOF,
    SK_NODE_ERROR,

    SK_NODE_KIND_COUNT
} sk_ast_node_kind;

struct sk_ast_nodes
{
    u32 *token_idxs;
    u32 *nexts;
    u32 *data_a;
    u32 *data_b;
    u32 *data_c;

    sk_ast_node_kind *kinds;

    u32 count;
    u32 err_count;

    u32 codegen_nodes;

    u8 pad[4];
};

struct sk_parser
{
    struct sk_tokens    *tokens;
    struct sk_ast_nodes *nodes;

    u32 current;
};

vx_status sk_parser_init(struct sk_ctx *ctx, struct sk_parser *p);

vx_status sk_top_level_parse(struct sk_parser *p);

static const char *SK_NODE_STR[SK_NODE_KIND_COUNT] = {
    [SK_NODE_INVALID]         = "SK_NODE_INVALID",
    [SK_NODE_PROGRAM]         = "SK_NODE_PROGRAM",
    [SK_NODE_TARGET]          = "SK_NODE_TARGET",
    [SK_NODE_SOURCES]         = "SK_NODE_SOURCES",
    [SK_NODE_INCLUDES]        = "SK_NODE_INCLUDES",
    [SK_NODE_COMPILER]        = "SK_NODE_COMPILER",
    [SK_NODE_LINKER]          = "SK_NODE_LINKER",
    [SK_NODE_CFLAGS]          = "SK_NODE_CFLAGS",
    [SK_NODE_LFLAGS]          = "SK_NODE_LFLAGS",
    [SK_NODE_DEPENDS]         = "SK_NODE_DEPENDS",
    [SK_NODE_BIND]            = "SK_NODE_BIND",
    [SK_NODE_IF]              = "SK_NODE_IF",
    [SK_NODE_ASSIGN]          = "SK_NODE_ASSIGN",
    [SK_NODE_APPEND]          = "SK_NODE_APPEND",
    [SK_NODE_EXPR]            = "SK_NODE_EXPR",
    [SK_NODE_IDENT]           = "SK_NODE_IDENT",
    [SK_NODE_FLAG]            = "SK_NODE_FLAG",
    [SK_NODE_PATH]            = "SK_NODE_PATH",
    [SK_NODE_CODEGEN]         = "SK_NODE_CODEGEN",
    [SK_NODE_CODEGEN_DEFINE]  = "SK_NODE_CODEGEN_DEFINE",
    [SK_NODE_CODEGEN_LITERAL] = "SK_NODE_CODEGEN_LITERAL",
    [SK_NODE_PRINT]           = "SK_NODE_PRINT",
    [SK_NODE_INSTALL]         = "SK_NODE_INSTALL",
    [SK_NODE_EXIT]            = "SK_NODE_EXIT",
    [SK_NODE_LIT_STRING]      = "SK_NODE_LIT_STRING",
    [SK_NODE_LIT_BOOL]        = "SK_NODE_LIT_BOOL",
    [SK_NODE_LIT_NUMBER]      = "SK_NODE_LIT_NUMBER",
    [SK_NODE_FN_ARG]          = "SK_NODE_FN_ARG",
    [SK_NODE_EOF]             = "SK_NODE_EOF",
    [SK_NODE_ERROR]           = "SK_NODE_ERROR",
};

static inline const char *sk_node_tostr(sk_ast_node_kind kind)
{
    if (kind >= SK_NODE_KIND_COUNT)
    {
        return "SK_NODE_OUT_OF_BOUNDS";
    }
    const char *s = SK_NODE_STR[kind];
    return s ? s : "SK_NODE_UNDEFINED";
}

void sk_parser_dbg_dump_ast(struct sk_parser *p);

void syntax_error(struct sk_parser *p, const char *msg);
void syntax_error_at(struct sk_parser *p, u32 tok_idx, const char *msg);

#endif  // SK_PARSER_H_
