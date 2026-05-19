#include "sk_lexer.h"
#include "mem_arena.h"
#include "sk_globals.h"
#include "sk_util.h"
#include "storm-knell.h"

#include "vx.h"
#include "vx_io.h"
#include "vx_util.h"
#include <string.h>

// ----------------------------------------------------------------------------------------------------

static void record(struct sk_lexer *lx, sk_token_kind kind, u32 start_col);

static inline bool isatend(struct sk_lexer *lx);
static inline char peek(struct sk_lexer *lx);
static inline char peek_next(struct sk_lexer *lx);
static inline char advance(struct sk_lexer *lx);

static void skip_wspac_scalar(struct sk_lexer *lx);

static sk_token_kind check_keywords(struct sk_lexer *lx);

static void handle_ident(struct sk_lexer *lx, u32 start_col);
static void handle_number(struct sk_lexer *lx, u32 start_col);
static void handle_string(struct sk_lexer *lx, char quote_type, u32 start_col);
static void handle_path(struct sk_lexer *lx, u32 start_col);

static void sk_lx_next_token(struct sk_lexer *lx);

// ---------------------------------------------------------------------------------------------------

vx_status sk_lx_init(struct sk_ctx *ctx, struct sk_lexer *lx)
{
    if (ctx == nullptr || lx == nullptr)
    {
        lx->status |= SK_LEXER_FATAL;
        VX_ASSERT_LOG("sk_lexer or sk_ctx are nullptr");
        return VX_FATAL;
    }

    lx->source = ctx->stormfile;

    lx->lex_start = 0;
    lx->current   = 0;
    lx->line_n    = 1;
    lx->col_n     = 1;

    ctx->tokens = mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_tokens));

    if (ctx->tokens == nullptr)
    {
        VX_ASSERT_LOG("Failed to allocate for sk_tokens");
        return VX_FATAL;
    }

    size_t arr_size = (5 * sizeof(u32) * SK_LX_MAX_TOKENS);

    void *block = mem_arena_alloc(g_sk_global_arena, arr_size);

    if (block == nullptr)
    {
        VX_ASSERT_LOG("Failed to allocate token block");
        return VX_FATAL;
    }

    lx->status = SK_LEXER_OK;

    static_assert(sizeof(sk_token_kind) == sizeof(u32));

    ctx->tokens->offsets = (u32 *) block;
    ctx->tokens->lens    = ctx->tokens->offsets + SK_LX_MAX_TOKENS;
    ctx->tokens->lines   = ctx->tokens->lens + SK_LX_MAX_TOKENS;
    ctx->tokens->cols    = ctx->tokens->lines + SK_LX_MAX_TOKENS;
    ctx->tokens->kinds   = ctx->tokens->cols + SK_LX_MAX_TOKENS;

    ctx->tokens->count     = 1;
    ctx->tokens->err_count = 0;

    return VX_OK;
}

static void sk_lx_next_token(struct sk_lexer *lx)
{
    if (lx == nullptr)
    {
        lx->status |= SK_LEXER_ERROR;
        return;
    }

    if (isatend(lx))
    {
        lx->status |= SK_LEXER_EOF;
        record(lx, SK_TOKEN_EOF, lx->col_n);
        return;
    }

    skip_wspac_scalar(lx);

    lx->lex_start   = lx->current;
    u32 token_start = lx->col_n;

    char c = advance(lx);

    if (sk_util_is_ident(c))
    {
        handle_ident(lx, token_start);
        return;
    }

    if (sk_util_is_digit(c))
    {
        handle_number(lx, token_start);
        return;
    }

    switch (c)
    {
        case CHAR_SINGLE_QUOTE:
        case CHAR_DOUBLE_QUOTE:
        {
            handle_string(lx, c, token_start);
            return;
        }

        case CHAR_NULTERM:
        {
            record(lx, SK_TOKEN_EOF, token_start);
            return;
        }

        case CHAR_LPAREN:
        {
            record(lx, SK_TOKEN_LPAREN, token_start);
            return;
        }
        case CHAR_RPAREN:
        {
            record(lx, SK_TOKEN_RPAREN, token_start);
            return;
        }
        case CHAR_LBRACE:
        {
            record(lx, SK_TOKEN_LBRACE, token_start);
            return;
        }
        case CHAR_RBRACE:
        {
            record(lx, SK_TOKEN_RBRACE, token_start);
            return;
        }
        case CHAR_LBRACKET:
        {
            record(lx, SK_TOKEN_LBRACKET, token_start);
            return;
        }
        case CHAR_RBRACKET:
        {
            record(lx, SK_TOKEN_RBRACKET, token_start);
            return;
        }
        case CHAR_COMMA:
        {
            record(lx, SK_TOKEN_COMMA, token_start);
            return;
        }

        case CHAR_COLON:
        {
            if (peek(lx) == CHAR_COLON)
            {
                advance(lx);
                record(lx, SK_TOKEN_DOUBLE_COLON, token_start);
                return;
            }
            record(lx, SK_TOKEN_COLON, token_start);
            return;
        }

        case CHAR_BANG:
        {
            if (peek(lx) == CHAR_EQUAL)
            {
                advance(lx);
                record(lx, SK_TOKEN_NOT_EQUAL, token_start);
                return;
            }
            record(lx, SK_TOKEN_BANG, token_start);
            return;
        }

        case CHAR_DOT:
        case CHAR_SLASH:
        {
            handle_path(lx, token_start);
            return;
        }

        case CHAR_EQUAL:
        {
            if (peek(lx) == CHAR_EQUAL)
            {
                advance(lx);
                record(lx, SK_TOKEN_DOUBLE_EQUAL, token_start);
                return;
            }
            record(lx, SK_TOKEN_EQUAL, token_start);
            return;
        }

        case CHAR_GT:
        {
            if (peek(lx) == CHAR_EQUAL)
            {
                advance(lx);
                record(lx, SK_TOKEN_GE, token_start);
                return;
            }

            record(lx, SK_TOKEN_GT, token_start);
            return;
        }

        case CHAR_LT:
        {
            if (peek(lx) == CHAR_EQUAL)
            {
                advance(lx);
                record(lx, SK_TOKEN_LE, token_start);
                return;
            }

            record(lx, SK_TOKEN_LT, token_start);
            return;
        }

        case CHAR_MINUS:
        {
            if (sk_util_is_ident(peek(lx)) || sk_util_is_digit(peek(lx)))
            {
                while (!isatend(lx) && (sk_util_is_ident(lx->source.data[lx->current]) ||
                                        sk_util_is_digit(lx->source.data[lx->current]) ||
                                        lx->source.data[lx->current] == CHAR_EQUAL ||
                                        lx->source.data[lx->current] == CHAR_COMMA ||
                                        lx->source.data[lx->current] == CHAR_COLON ||
                                        lx->source.data[lx->current] == CHAR_DOT ||
                                        lx->source.data[lx->current] == CHAR_PLUS ||
                                        lx->source.data[lx->current] == CHAR_SLASH ||
                                        lx->source.data[lx->current] == CHAR_MINUS))
                {
                    advance(lx);
                }
                record(lx, SK_TOKEN_FLAG, token_start);
                return;
            }

            if (peek(lx) == CHAR_GT)
            {
                advance(lx);
                record(lx, SK_TOKEN_ARROW, token_start);
                return;
            }

            record(lx, SK_TOKEN_MINUS, token_start);
            return;
        }

        case CHAR_AMPERSAND:
        {
            if (peek(lx) == CHAR_AMPERSAND)
            {
                advance(lx);
                record(lx, SK_TOKEN_AND, token_start);  // &&
                return;
            }
            return;
        }

        case CHAR_PIPE:
        {
            if (peek(lx) == CHAR_PIPE)
            {
                advance(lx);
                record(lx, SK_TOKEN_OR, token_start);  // ||
                return;
            }

            record(lx, SK_TOKEN_PIPE, token_start);  // |
            return;
        }

        case CHAR_PLUS:
        {
            record(lx, SK_TOKEN_PLUS, token_start);
            return;
        }

        case CHAR_PERCENT:
        {
            record(lx, SK_TOKEN_PERCENT, token_start);
            return;
        }

        case CHAR_CARET:
        {
            record(lx, SK_TOKEN_CARET, token_start);
            return;
        }
        case CHAR_TILDE:
        {
            record(lx, SK_TOKEN_TILDE, token_start);
            return;
        }

        default:
        {
            record(lx, SK_TOKEN_ERROR, token_start);
            return;
        }
    }
}

// MAIN LEXER
vx_status sk_lex(struct sk_ctx *ctx, struct sk_lexer *lx)
{
    if (ctx == nullptr || lx == nullptr)
    {
        lx->status |= SK_LEXER_FATAL;
        return VX_FATAL;
    }

    for (;;)
    {
        sk_lx_next_token(lx);

        if (ctx->tokens->count >= SK_LX_MAX_TOKENS)
        {
            VX_ASSERT_LOG("Token limit reached");
            return VX_FATAL;
        }

        sk_token_kind kind = ctx->tokens->kinds[ctx->tokens->count - 1];

        if (kind == SK_TOKEN_ERROR)
        {
            ctx->tokens->err_count++;
        }

        if (kind == SK_TOKEN_EOF)
        {
            break;
        }
    }

    return ctx->tokens->err_count == 0 ? VX_OK : VX_ERROR;
}

// ---------------------------------------------------------------------------------------------------

static void record(struct sk_lexer *lx, sk_token_kind kind, u32 start_col)
{
    if (lx == nullptr)
    {
        return;
    }

    struct sk_ctx *ctx = &g_sk_global_ctx;

    struct sk_tokens *pts = ctx->tokens;

    u32 i = pts->count;

    pts->offsets[i] = lx->lex_start;
    pts->lens[i]    = lx->current - lx->lex_start;
    pts->lines[i]   = lx->line_n;
    pts->cols[i]    = start_col;
    pts->kinds[i]   = kind;

    pts->count++;
}

// ---------------------------------------------------------------------------------------------------

static inline bool isatend(struct sk_lexer *lx)
{
    return lx->current >= lx->source.len;
}

static inline char peek(struct sk_lexer *lx)
{
    if (isatend(lx))
    {
        return CHAR_NULTERM;
    }

    return lx->source.data[lx->current];
}

static inline char peek_next(struct sk_lexer *lx)
{
    if (isatend(lx) || lx->current + 1 >= lx->source.len)
    {
        return CHAR_NULTERM;
    }

    return lx->source.data[lx->current + 1];
}

static inline char advance(struct sk_lexer *lx)
{
    if (isatend(lx))
    {
        return CHAR_NULTERM;
    }

    char c = lx->source.data[lx->current++];

    if (c == CHAR_NEWLINE)
    {
        lx->line_n++;
        lx->col_n = 0;
    }
    else
    {
        lx->col_n++;
    }

    return c;
}

// ---------------------------------------------------------------------------------------------------

static void skip_wspac_scalar(struct sk_lexer *lx)
{
    for (;;)
    {
        char c = peek(lx);

        switch (c)
        {
            case CHAR_SPACE:
            case CHAR_TAB:
            case CHAR_CARRIAGE:
            {
                advance(lx);
                break;
            }

            case CHAR_NEWLINE:
            {
                advance(lx);
                break;
            }

            case CHAR_SLASH:
            {
                if (peek_next(lx) == CHAR_SLASH)
                {
                    lx->current += 2;  // skip
                                       // advance(lx);  // consume first /
                                       // advance(lx);  // consume second /

                    char *next_nl = (char *) memchr(
                        lx->source.data + lx->current, CHAR_NEWLINE, lx->source.len - lx->current);

                    if (next_nl)
                    {
                        lx->current = (next_nl - lx->source.data);
                    }
                    else
                    {
                        lx->current = lx->source.len;  // EOF
                    }
                }
                else if (peek_next(lx) == CHAR_STAR)
                {
                    advance(lx);  // consume /
                    advance(lx);  // consume *

                    while (!isatend(lx))
                    {
                        char ch = peek(lx);

                        if (ch == CHAR_STAR && peek_next(lx) == CHAR_SLASH)
                        {
                            advance(lx);  // consume *
                            advance(lx);  // consume /
                            break;
                        }
                        advance(lx);
                    }
                }
                else
                {
                    // not a comment
                    return;
                }
            }
            break;

            default:
            {
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------------

static void handle_path(struct sk_lexer *lx, u32 start_col)
{
    while (!isatend(lx))
    {
        char c = peek(lx);

        if (c == CHAR_SPACE || c == CHAR_TAB || c == CHAR_NEWLINE || c == CHAR_CARRIAGE)
        {
            break;
        }

        if (c == CHAR_LBRACE || c == CHAR_COLON)
        {
            break;
        }

        if (c == CHAR_SLASH || c == CHAR_DOT || c == CHAR_MINUS || c == CHAR_UNDERSCORE ||
            sk_util_is_ident(c) || sk_util_is_digit(c))
        {
            advance(lx);
        }
        else
        {
            break;
        }
    }
    record(lx, SK_TOKEN_PATH, start_col);
}

static void handle_ident(struct sk_lexer *lx, u32 start_col)
{
    const char *p = lx->source.data + lx->current;

    while (sk_util_is_ident(*p) || sk_util_is_digit(*p))
    {
        p++;
    }

    size_t len = p - (lx->source.data + lx->current);

    lx->current += len;
    lx->col_n   += len;

    char next = peek(lx);
    if (!isatend(lx) && (next == CHAR_SLASH || next == CHAR_DOT))
    {
        handle_path(lx, start_col);
        return;
    }

    sk_token_kind kind = check_keywords(lx);
    record(lx, kind, start_col);
}

static void handle_number(struct sk_lexer *lx, u32 start_col)
{
    sk_token_kind kind = SK_TOKEN_LIT_INT;

    while (true)
    {
        char c = peek(lx);
        if (!sk_util_is_digit((u8) c))
        {
            break;
        }
        advance(lx);
    }

    record(lx, kind, start_col);
}

static void handle_string(struct sk_lexer *lx, char quote_type, u32 start_col)
{
    while (true)
    {
        char c = peek(lx);

        if (c == quote_type)
        {
            break;
        }

        switch (c)
        {
            case CHAR_NEWLINE:
            {
                record(lx, SK_TOKEN_ERROR, start_col);
                return;
            }

            case CHAR_NULTERM:
            {
                lx->status |= SK_LEXER_EOF;
                record(lx, SK_TOKEN_EOF, start_col);
                return;
            }

            case CHAR_BACKSLASH:
            {
                advance(lx);
                break;
            }
        }

        advance(lx);
    }

    if (isatend(lx))
    {
        lx->status |= SK_LEXER_EOF;
        record(lx, SK_TOKEN_EOF, start_col);
        return;
    }

    advance(lx);

    record(lx, SK_TOKEN_LIT_STRING, start_col);
}

static sk_token_kind check_keywords(struct sk_lexer *lx)
{
    const char *s = lx->source.data + lx->lex_start;

    size_t len = (size_t) (lx->current - lx->lex_start);

    if (len == 0)
    {
        return SK_TOKEN_IDENT;
    }

    switch (s[0])
    {
        case 'i':
        {
            switch (len)
            {
                case 2:
                {
                    if (vx_strncmplit(s, len, "if", 2))
                    {
                        return SK_TOKEN_KWORD_IF;
                    }

                    break;
                }

                case 7:
                {
                    if (vx_strncmplit(s, len, "install", 7))
                    {
                        return SK_TOKEN_KWORD_INSTALL;
                    }
                    break;
                }

                case 8:
                {
                    if (vx_strncmplit(s, len, "includes", 8))
                    {
                        return SK_TOKEN_KWORD_INCLUDES;
                    }
                    break;
                }

                default: break;
            }
            break;
        }

        case 'f':
        {
            switch (len)
            {
                case 5:
                {
                    if (vx_strncmplit(s, len, "false", 5))
                    {
                        return SK_TOKEN_LIT_FALSE;
                    }
                    break;
                }

                default: break;
            }
            break;
        }

        case 'b':
        {
            if (len == 4 && vx_strncmplit(s, len, "bind", 4))
            {
                return SK_TOKEN_KWORD_BIND;
            }

            break;
        }

        case 'k':
        {
            if (len == 4 && vx_strncmplit(s, len, "kind", 4))
            {
                return SK_TOKEN_KWORD_KIND;
            }

            break;
        }

        case 'm':
        {
            if (len == 4 && vx_strncmplit(s, len, "mode", 4))
            {
                return SK_TOKEN_KWORD_MODE;
            }

            break;
        }

        case 's':
        {
            if (len == 7 && vx_strncmplit(s, len, "sources", 7))
            {
                return SK_TOKEN_KWORD_SOURCES;
            }

            break;
        }

        case 'c':
        {
            if (len == 2 && vx_strncmplit(s, len, "cc", 2))
            {
                return SK_TOKEN_KWORD_CC;
            }

            if (len == 6 && vx_strncmplit(s, len, "cflags", 6))
            {
                return SK_TOKEN_KWORD_CFLAGS;
            }

            if (len == 7 && vx_strncmplit(s, len, "codegen", 7))
            {
                return SK_TOKEN_KWORD_CODEGEN;
            }

            if (len == 8 && vx_strncmplit(s, len, "compiler", 8))
            {
                return SK_TOKEN_KWORD_COMPILER;
            }

            break;
        }

        case 'p':
        {
            if (len == 5 && vx_strncmplit(s, len, "print", 5))
            {
                return SK_TOKEN_KWORD_PRINT;
            }

            break;
        }

        case 'd':
        {
            if (len == 7 && vx_strncmplit(s, len, "defines", 7))
            {
                return SK_TOKEN_KWORD_DEFINES;
            }

            if (len == 7 && vx_strncmplit(s, len, "depends", 7))
            {
                return SK_TOKEN_KWORD_DEPENDS;
            }
            break;
        }

        case 'e':
        {
            if (len == 4 && vx_strncmplit(s, len, "else", 4))
            {
                return SK_TOKEN_KWORD_ELSE;
            }

            if (len == 7 && vx_strncmplit(s, len, "exclude", 7))
            {
                return SK_TOKEN_KWORD_EXCLUDE;
            }

            break;
        }

        case 'o':
        {
            if (len == 3 && vx_strncmplit(s, len, "out", 3))
            {
                return SK_TOKEN_KWORD_OUT;
            }

            if (len == 7 && vx_strncmplit(s, len, "out_dir", 7))
            {
                return SK_TOKEN_KWORD_OUT_DIR;
            }

            break;
        }

        case 'l':
        {
            if (len == 6 && vx_strncmplit(s, len, "lflags", 6))
            {
                return SK_TOKEN_KWORD_LFLAGS;
            }

            if (len == 6 && vx_strncmplit(s, len, "linker", 6))
            {
                return SK_TOKEN_KWORD_LINKER;
            }

            break;
        }

        case 't':
        {
            if (len == 4 && vx_strncmplit(s, len, "true", 4))
            {
                return SK_TOKEN_LIT_TRUE;
            }

            if (len == 6 && vx_strncmplit(s, len, "target", 6))
            {
                return SK_TOKEN_KWORD_TARGET;
            }
            break;
        }
    }

    return SK_TOKEN_IDENT;
}

//----------------------------------------------------------------------------------------------------
// debug

void sk_lx_dbg_dump_tokens(struct sk_ctx *ctx)
{
    if (ctx == nullptr || ctx->tokens == nullptr)
    {
        return;
    }

    struct sk_tokens *t = ctx->tokens;
    vx_printf("=== TOKEN DUMP (%u tokens) ===\n", t->count);
    for (u32 i = 1; i < t->count; i++)
    {
        vx_printf("[%u] %-30s offset: %u  len: %u  line: %u  col: %u\n",
                  i,
                  sk_token_tostr(t->kinds[i]),
                  t->offsets[i],
                  t->lens[i],
                  t->lines[i],
                  t->cols[i]);
    }
}

//----------------------------------------------------------------------------------------------------
