#include "mem_arena.h"
#include "sk_globals.h"
#include "sk_lexer.h"
#include "vx_limits.h"
#include "vx_io.h"
#include "storm-knell.h"

#include "sk_parser.h"

//----------------------------------------------------------------------------------------------------
// helpers

static inline sk_token_kind peek(struct sk_parser *p);
static inline sk_token_kind peek_at(struct sk_parser *p, u32 dist);

static inline bool is_at_end(struct sk_parser *p);

/*
 * Consumes token on success , increments `nodes->err_count` if fail.
 * */
static inline bool expect(struct sk_parser *p, sk_token_kind kind);

static inline u32 advance(struct sk_parser *p);

static u32 emit(sk_ast_node_kind kind, u32 token_idx);

#define SK_AST_MAX_NODES VX_BUF_SIZE_64K

static sk_ast_node_kind literal_kind(sk_token_kind t);

//----------------------------------------------------------------------------------------------------

static void parse_value_list(struct sk_parser *p, u32 node);
static u32  parse_global(struct sk_parser *p);
static u32  parse_target(struct sk_parser *p);
static u32  parse_if(struct sk_parser *p);
static u32  parse_expr(struct sk_parser *p);
static void parse_body(struct sk_parser *p, u32 *first_child);
static u32  parse_fn_call(struct sk_parser *p);

static void syntax_error(struct sk_parser *p, const char *msg);

//----------------------------------------------------------------------------------------------------

vx_status sk_parser_init(struct sk_ctx *ctx, struct sk_parser *p)
{
    if (ctx == nullptr || p == nullptr)
    {
        return VX_ERROR;
    }

    ctx->nodes = mem_arena_alloc(g_sk_global_arena, sizeof(struct sk_ast_nodes));

    if (ctx->nodes == nullptr)
    {
        VX_ASSERT_LOG("Failed to allocate sk_ast_nodes");
        return VX_ERROR;
    }

    size_t arr_size =
        (5 * sizeof(u32) * SK_AST_MAX_NODES) + (sizeof(sk_ast_node_kind) * SK_AST_MAX_NODES);
    void *block = mem_arena_alloc(g_sk_global_arena, arr_size);

    if (block == nullptr)
    {
        VX_ASSERT_LOG("Failed to allocate ast node block");
        return VX_FATAL;
    }

    ctx->nodes->token_idxs = (u32 *) block;
    ctx->nodes->nexts      = ctx->nodes->token_idxs + SK_AST_MAX_NODES;
    ctx->nodes->data_a     = ctx->nodes->nexts + SK_AST_MAX_NODES;
    ctx->nodes->data_b     = ctx->nodes->data_a + SK_AST_MAX_NODES;
    ctx->nodes->data_c     = ctx->nodes->data_b + SK_AST_MAX_NODES;
    ctx->nodes->kinds      = (sk_ast_node_kind *) (ctx->nodes->data_c + SK_AST_MAX_NODES);
    ctx->nodes->count      = 1;
    ctx->nodes->err_count  = 0;

    p->tokens  = ctx->tokens;
    p->nodes   = ctx->nodes;
    p->current = 1;

    return VX_OK;
}

//----------------------------------------------------------------------------------------------------

vx_status sk_top_level_parse(struct sk_parser *p)
{
    if (p == nullptr)
    {
        return VX_ERROR;
    }

    u32 program = emit(SK_NODE_PROGRAM, 0);
    u32 last    = SK_NODE_INVALID;

    while (!is_at_end(p))
    {
        u32 pos_before = p->current;

        u32 node = SK_NODE_INVALID;

        sk_token_kind t1 = peek(p);
        sk_token_kind t2 = peek_at(p, 1);

        switch (t1)
        {
            case SK_TOKEN_KWORD_TARGET:
            {
                node = parse_target(p);
                break;
            }

            case SK_TOKEN_BUILTIN:
            {
                node = parse_fn_call(p);
                break;
            }

            case SK_TOKEN_IDENT:
            {
                if (t2 == SK_TOKEN_COLON || t2 == SK_TOKEN_DOUBLE_COLON)
                {
                    node = parse_global(p);
                }
                else
                {
                    p->nodes->err_count++;
                    advance(p);
                }

                break;
            }

            case SK_TOKEN_KWORD_CC:
            case SK_TOKEN_KWORD_COMPILER:
            case SK_TOKEN_KWORD_LINKER:
            case SK_TOKEN_KWORD_CFLAGS:
            case SK_TOKEN_KWORD_LFLAGS:
            case SK_TOKEN_KWORD_DEFINES:
            case SK_TOKEN_KWORD_MODE:
            {
                node = parse_global(p);
                break;
            }

            default:
            {
                // vx_dbglog("Unexpected token at top level: %s line %u",
                //           sk_token_tostr(t1),
                //           p->tokens->lines[p->current]);
                p->nodes->err_count++;
                advance(p);
                break;
            }
        }

        if (p->current == pos_before)
        {
            vx_errlog("INFINITE LOOP at token %u: %s line %u",
                      p->current,
                      sk_token_tostr(peek(p)),
                      p->tokens->lines[p->current]);
            return VX_ERROR;
        }

        // we might need re-peek

        if (node != SK_NODE_INVALID)
        {
            if (last == SK_NODE_INVALID)
            {
                p->nodes->data_a[program] = node;
            }
            else
            {
                p->nodes->nexts[last] = node;
            }
            last = node;
        }
    }

    if (p->nodes->err_count > 0)
    {
        vx_warn("Parse completed with %u errors", p->nodes->err_count);
    }

    // return p->nodes->err_count == 0 ? VX_OK : VX_ERROR;
    return VX_OK;
}

//----------------------------------------------------------------------------------------------------

static sk_ast_node_kind literal_kind(sk_token_kind t)
{
    switch (t)
    {
        case SK_TOKEN_IDENT:
        {
            return SK_NODE_IDENT;
        }

        case SK_TOKEN_FLAG:
        {
            return SK_NODE_FLAG;
        }

        case SK_TOKEN_PATH:
        {
            return SK_NODE_PATH;
        }

        case SK_TOKEN_LIT_STRING:
        {
            return SK_NODE_LIT_STRING;
        }

        case SK_TOKEN_LIT_INT:
        case SK_TOKEN_LIT_FLOAT:
        {
            return SK_NODE_LIT_NUMBER;
        }

        case SK_TOKEN_LIT_FALSE:
        case SK_TOKEN_LIT_TRUE:
        {
            return SK_NODE_LIT_BOOL;
        }

        default:
        {
            return SK_NODE_INVALID;
        }
    }
}

//----------------------------------------------------------------------------------------------------

static void parse_body(struct sk_parser *p, u32 *fist_child)
{
    u32 last = SK_NODE_INVALID;

    while (!is_at_end(p) && peek(p) != SK_TOKEN_RBRACE)
    {
        u32 pos_before = p->current;

        u32 child = SK_NODE_INVALID;

        sk_token_kind t1 = peek(p);
        sk_token_kind t2 = peek_at(p, 1);

        switch (t1)
        {
            case SK_TOKEN_KWORD_CC:
            case SK_TOKEN_KWORD_COMPILER:
            case SK_TOKEN_KWORD_LINKER:
            case SK_TOKEN_KWORD_CFLAGS:
            case SK_TOKEN_KWORD_LFLAGS:
            case SK_TOKEN_KWORD_SOURCES:
            case SK_TOKEN_KWORD_INCLUDES:
            case SK_TOKEN_KWORD_OUT:
            case SK_TOKEN_KWORD_KIND:
            case SK_TOKEN_KWORD_MODE:
            case SK_TOKEN_KWORD_DEPENDS:
            case SK_TOKEN_KWORD_DEFINES:
            case SK_TOKEN_IDENT:
            {
                if (t2 == SK_TOKEN_COLON || t2 == SK_TOKEN_DOUBLE_COLON)
                {
                    child = parse_global(p);
                }
                break;
            }

            case SK_TOKEN_BUILTIN:
            {
                child = parse_fn_call(p);
                break;
            }

            case SK_TOKEN_KWORD_IF:
            {
                child = parse_if(p);
                break;
            }

            default:
            {
                p->nodes->err_count++;
                advance(p);
                break;
            }
        }

        if (p->current == pos_before)
        {
            vx_errlog("STUCK in parse_body at token %u: %s line %u",
                      p->current,
                      sk_token_tostr(peek(p)),
                      p->tokens->lines[p->current]);
            return;
        }

        if (child != SK_NODE_INVALID)
        {
            if (last == SK_NODE_INVALID)
            {
                *fist_child = child;
            }
            else
            {
                p->nodes->nexts[last] = child;
            }
            last = child;
        }
    }
}

static u32 parse_fn_call(struct sk_parser *p)
{
    u32 tok_idx = advance(p);  // consume fn name
    u32 node    = emit(SK_NODE_FN_CALL, tok_idx);

    if (!expect(p, SK_TOKEN_LPAREN))
    {
        return SK_NODE_INVALID;
    }

    u32 first = SK_NODE_INVALID;
    u32 last  = SK_NODE_INVALID;

    while (!is_at_end(p) && peek(p) != SK_TOKEN_RPAREN)
    {
        sk_ast_node_kind nkind = literal_kind(peek(p));

        u32 arg = SK_NODE_INVALID;

        if (nkind != SK_NODE_INVALID)
        {
            u32 tok               = advance(p);
            u32 val               = emit(nkind, tok);
            arg                   = emit(SK_NODE_FN_ARG, tok);
            p->nodes->data_a[arg] = val;
        }
        else if (peek(p) == SK_TOKEN_IDENT)
        {
            // path like tool/src
            u32 val = emit(SK_NODE_IDENT, advance(p));

            arg = emit(SK_NODE_FN_ARG, val);
        }
        else
        {
            syntax_error(p, "expected argument");
            advance(p);
            break;
        }

        if (arg != SK_NODE_INVALID)
        {
            if (first == SK_NODE_INVALID)
            {
                first                  = arg;
                p->nodes->data_a[node] = first;
            }
            else
            {
                p->nodes->nexts[last] = arg;
            }
            last = arg;
        }

        if (peek(p) == SK_TOKEN_COMMA)
        {
            advance(p);
        }
    }

    if (!expect(p, SK_TOKEN_RPAREN))
    {
        return SK_NODE_INVALID;
    }

    return node;
}

static u32 parse_expr(struct sk_parser *p)
{
    if (peek(p) != SK_TOKEN_IDENT)
    {
        syntax_error(p, "Expected identifier");
        return SK_NODE_INVALID;
    }

    u32 left = emit(SK_NODE_IDENT, advance(p));

    sk_token_kind op = peek(p);
    if (op != SK_TOKEN_DOUBLE_EQUAL && op != SK_TOKEN_NOT_EQUAL && op != SK_TOKEN_LT &&
        op != SK_TOKEN_GT && op != SK_TOKEN_LE && op != SK_TOKEN_GE)
    {
        syntax_error(p, "Expected operator after 'if' condition");
        return SK_NODE_INVALID;
    }

    u32 op_tok = advance(p);  // consume op
    u32 node   = emit(SK_NODE_EXPR, op_tok);

    p->nodes->data_a[node] = left;

    // right side, ident or lit
    sk_ast_node_kind rkind = literal_kind(peek(p));

    if (rkind == SK_NODE_INVALID)
    {
        p->nodes->err_count++;
        return SK_NODE_INVALID;
    }

    u32 right = emit(rkind, advance(p));

    p->nodes->data_b[node] = right;

    return node;
}

static u32 parse_if(struct sk_parser *p)
{
    u32 tok_idx = advance(p);  // consume 'if'

    u32 node = emit(SK_NODE_IF, tok_idx);

    if (!expect(p, SK_TOKEN_LPAREN))
    {
        return SK_NODE_INVALID;
    }

    u32 cond = parse_expr(p);

    if (cond == SK_NODE_INVALID)
    {
        p->nodes->err_count++;
        return SK_NODE_INVALID;
    }
    p->nodes->data_a[node] = cond;

    if (!expect(p, SK_TOKEN_RPAREN))
    {
        return SK_NODE_INVALID;
    }
    if (!expect(p, SK_TOKEN_LBRACE))
    {
        return SK_NODE_INVALID;
    }

    parse_body(p, &p->nodes->data_b[node]);

    if (!expect(p, SK_TOKEN_RBRACE))
    {
        return SK_NODE_INVALID;
    }

    // optional else
    if (peek(p) == SK_TOKEN_KWORD_ELSE)
    {
        advance(p);  // consume 'else'

        if (peek(p) == SK_TOKEN_KWORD_IF)
        {
            p->nodes->data_c[node] = parse_if(p);
        }
        else
        {
            if (!expect(p, SK_TOKEN_LBRACE))
            {
                return SK_NODE_INVALID;
            }

            parse_body(p, &p->nodes->data_c[node]);

            if (!expect(p, SK_TOKEN_RBRACE))
            {
                return SK_NODE_INVALID;
            }
        }
    }

    return node;
}

static u32 parse_target(struct sk_parser *p)
{
    u32 tok_idx = advance(p);  // consume target keyword

    sk_token_kind name_kind = peek(p);

    if (name_kind != SK_TOKEN_IDENT)
    {
        syntax_error(p, "Expected identifier for target name");
        return SK_NODE_INVALID;
    }

    u32 name_tok = advance(p);
    u32 node     = emit(SK_NODE_TARGET, tok_idx);

    p->nodes->data_a[node] = name_tok;

    // expect {
    if (!expect(p, SK_TOKEN_LBRACE))
    {
        return SK_NODE_INVALID;
    }

    parse_body(p, &p->nodes->data_b[node]);

    // expect }
    if (!expect(p, SK_TOKEN_RBRACE))
    {
        syntax_error(p, "Expected closing brace");
    }

    return node;
}

static void parse_value_list(struct sk_parser *p, u32 node)
{
    u32 first = SK_NODE_INVALID;
    u32 last  = SK_NODE_INVALID;

    while (!is_at_end(p))
    {
        sk_token_kind t  = peek(p);
        sk_token_kind t2 = peek_at(p, 1);

        if (t == SK_TOKEN_LBRACE || t == SK_TOKEN_RBRACE || t == SK_TOKEN_LPAREN ||
            t == SK_TOKEN_RPAREN || t == SK_TOKEN_COLON || t == SK_TOKEN_DOUBLE_COLON)
        {
            break;
        }

        if (t == SK_TOKEN_IDENT && (t2 == SK_TOKEN_COLON || t2 == SK_TOKEN_DOUBLE_COLON))
        {
            break;
        }

        sk_ast_node_kind nkind = literal_kind(peek(p));

        if (nkind == SK_NODE_INVALID)
        {
            break;
        }

        u32 val = emit(nkind, advance(p));

        if (first == SK_NODE_INVALID)
        {
            first                  = val;
            p->nodes->data_a[node] = val;
        }
        else
        {
            p->nodes->nexts[last] = val;
        }
        last = val;
    }
}

static u32 parse_global(struct sk_parser *p)
{
    u32 tok_idx = advance(p);

    sk_ast_node_kind node_kind = SK_NODE_INVALID;

    if (peek(p) == SK_TOKEN_COLON)
    {
        advance(p);
        node_kind = SK_NODE_ASSIGN;
    }
    else if (peek(p) == SK_TOKEN_DOUBLE_COLON)
    {
        advance(p);
        node_kind = SK_NODE_APPEND;
    }
    else
    {
        p->nodes->err_count++;
        return SK_NODE_INVALID;
    }

    u32 node = emit(node_kind, tok_idx);

    parse_value_list(p, node);

    return node;
}

//----------------------------------------------------------------------------------------------------

static inline bool is_at_end(struct sk_parser *p)
{
    return p->current >= p->tokens->count || p->tokens->kinds[p->current] == SK_TOKEN_EOF;
}

static inline sk_token_kind peek(struct sk_parser *p)
{
    if (is_at_end(p))
    {
        return SK_TOKEN_EOF;
    }

    return p->tokens->kinds[p->current];
}

static inline sk_token_kind peek_at(struct sk_parser *p, u32 dist)
{
    u32 target = p->current + dist;
    if (target >= p->tokens->count)
    {
        return SK_TOKEN_EOF;
    }

    return p->tokens->kinds[target];
}

static inline u32 advance(struct sk_parser *p)
{
    u32 prev = p->current;

    if (peek(p) != SK_TOKEN_EOF)
    {
        p->current++;
    }

    return prev;
}

static inline bool expect(struct sk_parser *p, sk_token_kind kind)
{
    if (peek(p) == kind)
    {
        advance(p);
        return true;
    }

    g_sk_global_ctx.nodes->err_count++;
    return false;
}

static u32 emit(sk_ast_node_kind kind, u32 token_idx)
{
    struct sk_ast_nodes *nodes = g_sk_global_ctx.nodes;

    u32 i = nodes->count++;

    nodes->kinds[i]      = kind;
    nodes->token_idxs[i] = token_idx;
    nodes->nexts[i]      = 0;
    nodes->data_a[i]     = 0;
    nodes->data_b[i]     = 0;
    nodes->data_c[i]     = 0;

    return i;
}

static void syntax_error(struct sk_parser *p, const char *msg)
{
    u32 tok = p->current;

    vx_errlog("Syntax error at line %u col %u: %s (got %s)",
              p->tokens->lines[tok],
              p->tokens->cols[tok],
              msg,
              sk_token_tostr(p->tokens->kinds[tok]));

    p->nodes->err_count++;
}

//----------------------------------------------------------------------------------------------------
// debug

static void dump_node(struct sk_parser *p, u32 idx, u32 depth)
{
    if (idx == 0 || idx >= p->nodes->count)
    {
        return;
    }
    struct sk_ast_nodes *n = p->nodes;

    for (u32 i = 0; i < depth; i++)
    {
        vx_printf("  │");
    }
    vx_printf("  ├─ [%u] %s\n", idx, sk_node_tostr(n->kinds[idx]));

    for (u32 i = 0; i < depth; i++)
    {
        vx_printf("  │");
    }
    printf("  │   tok_idx: %u  tok: %s  a:%u  b:%u  c:%u  next:%u\n",
           n->token_idxs[idx],
           sk_token_tostr(p->tokens->kinds[n->token_idxs[idx]]),
           n->data_a[idx],
           n->data_b[idx],
           n->data_c[idx],
           n->nexts[idx]);

    if (n->kinds[idx] == SK_NODE_TARGET)
    {
        vx_printf("  │   name_tok: %u\n", n->data_a[idx]);
        dump_node(p, n->data_b[idx], depth + 1);  // body
        dump_node(p, n->data_c[idx], depth + 1);  // else
    }
    else
    {
        dump_node(p, n->data_a[idx], depth + 1);
        dump_node(p, n->data_b[idx], depth + 1);
        dump_node(p, n->data_c[idx], depth + 1);
        dump_node(p, n->nexts[idx], depth);
    }
}

void sk_parser_dbg_dump_ast(struct sk_parser *p)
{
    if (p == nullptr || p->nodes == nullptr || p->nodes->count == 0)
    {
        return;
    }

    vx_printf("=== AST DUMP (%u nodes, %u errors) ===\n", p->nodes->count, p->nodes->err_count);
    dump_node(p, 1, 0);
    vx_printf("=====================================\n");
}

//----------------------------------------------------------------------------------------------------
