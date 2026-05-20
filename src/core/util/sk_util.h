#ifndef SK_UTIL_H_
#define SK_UTIL_H_

#include "vx_defs.h"
#include "vx_string.h"
#include "sk_version.h"
#include "storm-knell.h"

#define CHAR_LPAREN   '('
#define CHAR_RPAREN   ')'
#define CHAR_LBRACE   '{'
#define CHAR_RBRACE   '}'
#define CHAR_LBRACKET '['
#define CHAR_RBRACKET ']'

//------------------------
// Separators / punctuation
//------------------------
#define CHAR_COMMA     ','
#define CHAR_COLON     ':'
#define CHAR_SEMICOLON ';'
#define CHAR_DOT       '.'
#define CHAR_QUESTION  '?'

//------------------------
// Arithmetic operators
//------------------------
#define CHAR_PLUS    '+'
#define CHAR_MINUS   '-'
#define CHAR_STAR    '*'
#define CHAR_SLASH   '/'
#define CHAR_PERCENT '%'

//------------------------
// Assignment / comparison
//------------------------
#define CHAR_EQUAL '='
#define CHAR_LT    '<'
#define CHAR_GT    '>'
#define CHAR_BANG  '!'  // exclamation mark

//------------------------
// Bitwise / logical operators
//------------------------
#define CHAR_AMPERSAND '&'
#define CHAR_PIPE      '|'
#define CHAR_CARET     '^'
#define CHAR_TILDE     '~'

//------------------------
// Quotes / escape
//------------------------
#define CHAR_SINGLE_QUOTE '\''
#define CHAR_DOUBLE_QUOTE '"'
#define CHAR_BACKSLASH    '\\'

//------------------------
// Whitespace
//------------------------
#define CHAR_NULTERM  '\0'
#define CHAR_SPACE    ' '
#define CHAR_TAB      '\t'
#define CHAR_CARRIAGE '\r'
#define CHAR_NEWLINE  '\n'

//------------------------
// Miscellaneous symbols
//------------------------
#define CHAR_AT         '@'
#define CHAR_HASH       '#'
#define CHAR_DOLLAR     '$'
#define CHAR_UNDERSCORE '_'
#define CHAR_SEMICOLON  ';'
#define CHAR_BACKTICK   '`'

static inline bool sk_util_is_ident(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static inline bool sk_util_is_digit(char c)
{
    return c >= '0' && c <= '9';
}

bool sk_is_initialized_at(const char *dir);
bool sk_discover_root(char *out_path, size_t size);

vx_status sk_resolve_project_root(struct sk_ctx *ctx);

void *sk_arena_alloc(void *user, size_t size);

static inline bool sk_has_ext(const char *name, size_t name_len, const char *ext)
{
    if (name == nullptr || name_len == 0 || ext == nullptr)
    {
        return false;
    }

    size_t ext_len = strlen(ext);

    if (name_len < ext_len)
    {
        return false;
    }

    return vx_strncmplit(name + (name_len - ext_len), ext_len, ext, ext_len);
}

static inline void sk_strncpy_safe(char *dest, const char *src, size_t dest_size)
{
    if (dest == nullptr || dest_size == 0)
    {
        return;
    }

    size_t i = 0;

    while (src[i] != CHAR_NULTERM && i < (dest_size - 1))
    {
        dest[i] = src[i];
        i++;
    }

    dest[i] = CHAR_NULTERM;
}

void sk_fmt_relative_time(u64 target_epoch, char *out_buf, size_t buf_size);

#define SK_MAX_TARGETS 32
#define SK_MAX_FLAGS   256
#define SK_MAX_DEFINES 512
#define SK_MAX_LIBS    128
#define SK_MAX_VARS    8192

#define SK_MAX_DEPS     16
#define SK_MAX_EXCLUDES 32
#define SK_MAX_INCLUDES 64

#define SK_FLAG_LEN 64
#define SK_NAME_LEN 64
#define SK_PATH_LEN 256  // mostly for relative paths

#endif  // SK_UTIL_H_
