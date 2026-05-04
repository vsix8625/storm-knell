#ifndef SK_CLI_H_
#define SK_CLI_H_

#include "vx_defs.h"

#define SK_STREQ_LIT(s, len, lit) vx_strncmplit(s, len, lit, sizeof(lit) - 1)

typedef struct sk_ctx sk_ctx;

typedef enum sk_cmd : u64
{
    SK_CMD_NONE = 0,

    SK_CMD_NEW    = 1 << 0,
    SK_CMD_STRIKE = 1 << 1,
    SK_CMD_SURGE  = 1 << 2,
    SK_CMD_CLEAR  = 1 << 3,
    SK_CMD_PURGE  = 1 << 4,
} sk_cmd;

typedef vx_status (*sk_cmd_fn)(sk_ctx *ctx, i32 *i, i32 argc, char **argv);

typedef struct sk_subcmd_entry
{
    const char *name;

    sk_cmd cmd;

    sk_cmd_fn fn;

    const char *desc;
} sk_subcmd_entry;

typedef struct sk_opt_entry
{
    const char *name;

    sk_cmd owner;

    sk_cmd_fn fn;

    const char *desc;
} sk_opt_entry;

vx_status sk_cli_driver(sk_ctx *ctx, i32 argc, char **argv);

#endif  // SK_CLI_H_
