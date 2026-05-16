#ifndef SK_LEXER_H_
#define SK_LEXER_H_

#include "vx_defs.h"

struct sk_ctx;

#define SK_LX_MAX_TOKENS VX_BUF_SIZE_64K

typedef enum sk_token_kind : u32
{
    SK_TOKEN_NONE = 0,

    SK_TOKEN_KWORD_TARGET,
    SK_TOKEN_KWORD_CC,
    SK_TOKEN_KWORD_COMPILER,
    SK_TOKEN_KWORD_LINKER,
    SK_TOKEN_KWORD_CFLAGS,
    SK_TOKEN_KWORD_LFLAGS,
    SK_TOKEN_KWORD_DEPENDS,
    SK_TOKEN_KWORD_DEFINES,
    SK_TOKEN_KWORD_BIND,
    SK_TOKEN_KWORD_SOURCES,
    SK_TOKEN_KWORD_INCLUDES,
    SK_TOKEN_KWORD_KIND,
    SK_TOKEN_KWORD_MODE,
    SK_TOKEN_KWORD_IF,
    SK_TOKEN_KWORD_ELSE,
    SK_TOKEN_KWORD_OUT,
    SK_TOKEN_KWORD_OUT_DIR,
    SK_TOKEN_KWORD_EXCLUDE,
    SK_TOKEN_KWORD_CODEGEN,

    SK_TOKEN_LIT_TRUE,
    SK_TOKEN_LIT_FALSE,
    SK_TOKEN_LIT_INT,
    SK_TOKEN_LIT_STRING,

    SK_TOKEN_IDENT,
    SK_TOKEN_FLAG,
    SK_TOKEN_PATH,
    SK_TOKEN_BUILTIN,

    SK_TOKEN_EQUAL,        /* = */
    SK_TOKEN_DOUBLE_EQUAL, /* == */
    SK_TOKEN_NOT_EQUAL,    /* != */

    SK_TOKEN_LT,   /* < */
    SK_TOKEN_GT,   /* > */
    SK_TOKEN_LE,   /* <= */
    SK_TOKEN_GE,   /* >= */
    SK_TOKEN_BANG, /* ! */
    SK_TOKEN_AND,  /* && */
    SK_TOKEN_OR,   /* || */

    SK_TOKEN_LPAREN,   /* ( */
    SK_TOKEN_RPAREN,   /* ) */
    SK_TOKEN_LBRACE,   /* { */
    SK_TOKEN_RBRACE,   /* } */
    SK_TOKEN_LBRACKET, /* [ */
    SK_TOKEN_RBRACKET, /* ] */
    SK_TOKEN_COMMA,
    SK_TOKEN_PIPE, /* | */
    SK_TOKEN_COLON,
    SK_TOKEN_DOUBLE_COLON, /* :: */
    SK_TOKEN_ARROW,        /* -> */
    SK_TOKEN_DOT,

    SK_TOKEN_PLUS,
    SK_TOKEN_MINUS,
    SK_TOKEN_SLASH, /* / */
    SK_TOKEN_PERCENT,
    SK_TOKEN_CARET, /* ^ */
    SK_TOKEN_TILDE,

    SK_TOKEN_EOF,
    SK_TOKEN_ERROR,

    SK_TOKEN_UNKNOWN,

    TOKEN_KIND_COUNT
} sk_token_kind;

typedef enum sk_lx_status : u8
{
    SK_LEXER_OK = 0,

    SK_LEXER_EOF   = 1 << 0,
    SK_LEXER_ERROR = 1 << 1,
    SK_LEXER_FATAL = 1 << 2,
} sk_lx_status;

struct sk_tokens
{
    u32 *offsets;
    u32 *lens;
    u32 *lines, *cols;

    sk_token_kind *kinds;

    u32 count;
    u32 err_count;

    u8 pad[16];
};

struct sk_lexer
{
    vx_sv source;

    u32 lex_start;
    u32 current;
    u32 line_n, col_n;

    sk_lx_status status;

    u8 pad[31];
};

vx_status sk_lx_init(struct sk_ctx *ctx, struct sk_lexer *lx);
vx_status sk_lex(struct sk_ctx *ctx, struct sk_lexer *lx);

void sk_lx_dbg_dump_tokens(struct sk_ctx *ctx);

// ----------------------------------------------------------------------------------------------------

static const char *SK_TOKEN_STR[TOKEN_KIND_COUNT] = {
    [SK_TOKEN_NONE]           = "SK_TOKEN_NONE",
    [SK_TOKEN_KWORD_TARGET]   = "SK_TOKEN_KWORD_TARGET",
    [SK_TOKEN_KWORD_CC]       = "SK_TOKEN_KWORD_CC",
    [SK_TOKEN_KWORD_COMPILER] = "SK_TOKEN_KWORD_COMPILER",
    [SK_TOKEN_KWORD_LINKER]   = "SK_TOKEN_KWORD_LINKER",
    [SK_TOKEN_KWORD_CFLAGS]   = "SK_TOKEN_KWORD_CFLAGS",
    [SK_TOKEN_KWORD_LFLAGS]   = "SK_TOKEN_KWORD_LFLAGS",
    [SK_TOKEN_KWORD_DEPENDS]  = "SK_TOKEN_KWORD_DEPENDS",
    [SK_TOKEN_KWORD_DEFINES]  = "SK_TOKEN_KWORD_DEFINES",
    [SK_TOKEN_KWORD_BIND]     = "SK_TOKEN_KWORD_BIND",
    [SK_TOKEN_KWORD_SOURCES]  = "SK_TOKEN_KWORD_SOURCES",
    [SK_TOKEN_KWORD_INCLUDES] = "SK_TOKEN_KWORD_INCLUDES",
    [SK_TOKEN_KWORD_KIND]     = "SK_TOKEN_KWORD_KIND",
    [SK_TOKEN_KWORD_MODE]     = "SK_TOKEN_KWORD_MODE",
    [SK_TOKEN_KWORD_IF]       = "SK_TOKEN_KWORD_IF",
    [SK_TOKEN_KWORD_ELSE]     = "SK_TOKEN_KWORD_ELSE",
    [SK_TOKEN_KWORD_OUT]      = "SK_TOKEN_KWORD_OUT",
    [SK_TOKEN_KWORD_OUT_DIR]  = "SK_TOKEN_KWORD_OUT_DIR",
    [SK_TOKEN_KWORD_EXCLUDE]  = "SK_TOKEN_KWORD_EXCLUDE",
    [SK_TOKEN_KWORD_CODEGEN]  = "SK_TOKEN_KWORD_CODEGEN",
    [SK_TOKEN_LIT_TRUE]       = "SK_TOKEN_LIT_TRUE",
    [SK_TOKEN_LIT_FALSE]      = "SK_TOKEN_LIT_FALSE",
    [SK_TOKEN_LIT_INT]        = "SK_TOKEN_LIT_INT",
    [SK_TOKEN_LIT_STRING]     = "SK_TOKEN_LIT_STRING",
    [SK_TOKEN_IDENT]          = "SK_TOKEN_IDENT",
    [SK_TOKEN_FLAG]           = "SK_TOKEN_FLAG",
    [SK_TOKEN_PATH]           = "SK_TOKEN_PATH",
    [SK_TOKEN_BUILTIN]        = "SK_TOKEN_BUILTIN",
    [SK_TOKEN_EQUAL]          = "SK_TOKEN_EQUAL",
    [SK_TOKEN_DOUBLE_EQUAL]   = "SK_TOKEN_DOUBLE_EQUAL",
    [SK_TOKEN_NOT_EQUAL]      = "SK_TOKEN_NOT_EQUAL",
    [SK_TOKEN_LT]             = "SK_TOKEN_LT",
    [SK_TOKEN_GT]             = "SK_TOKEN_GT",
    [SK_TOKEN_LE]             = "SK_TOKEN_LE",
    [SK_TOKEN_GE]             = "SK_TOKEN_GE",
    [SK_TOKEN_BANG]           = "SK_TOKEN_BANG",
    [SK_TOKEN_AND]            = "SK_TOKEN_AND",
    [SK_TOKEN_OR]             = "SK_TOKEN_OR",
    [SK_TOKEN_LPAREN]         = "SK_TOKEN_LPAREN",
    [SK_TOKEN_RPAREN]         = "SK_TOKEN_RPAREN",
    [SK_TOKEN_LBRACE]         = "SK_TOKEN_LBRACE",
    [SK_TOKEN_RBRACE]         = "SK_TOKEN_RBRACE",
    [SK_TOKEN_LBRACKET]       = "SK_TOKEN_LBRACKET",
    [SK_TOKEN_RBRACKET]       = "SK_TOKEN_RBRACKET",
    [SK_TOKEN_COMMA]          = "SK_TOKEN_COMMA",
    [SK_TOKEN_PIPE]           = "SK_TOKEN_PIPE",
    [SK_TOKEN_COLON]          = "SK_TOKEN_COLON",
    [SK_TOKEN_DOUBLE_COLON]   = "SK_TOKEN_DOUBLE_COLON",
    [SK_TOKEN_ARROW]          = "SK_TOKEN_ARROW",
    [SK_TOKEN_DOT]            = "SK_TOKEN_DOT",
    [SK_TOKEN_PLUS]           = "SK_TOKEN_PLUS",
    [SK_TOKEN_MINUS]          = "SK_TOKEN_MINUS",
    [SK_TOKEN_SLASH]          = "SK_TOKEN_SLASH",
    [SK_TOKEN_PERCENT]        = "SK_TOKEN_PERCENT",
    [SK_TOKEN_CARET]          = "SK_TOKEN_CARET",
    [SK_TOKEN_TILDE]          = "SK_TOKEN_TILDE",
    [SK_TOKEN_EOF]            = "SK_TOKEN_EOF",
    [SK_TOKEN_ERROR]          = "SK_TOKEN_ERROR",
    [SK_TOKEN_UNKNOWN]        = "SK_TOKEN_UNKNOWN",
};

static inline const char *sk_token_tostr(sk_token_kind kind)
{
    if (kind >= TOKEN_KIND_COUNT)
    {
        return "SK_TOKEN_OUT_OF_BOUNDS";
    }
    const char *s = SK_TOKEN_STR[kind];
    return s ? s : "SK_TOKEN_UNDEFINED";
}

// ----------------------------------------------------------------------------------------------------

#endif  // SK_LEXER_H_
