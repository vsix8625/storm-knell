#ifndef SK_CLI_H_
#define SK_CLI_H_

#include "vx_defs.h"

#define SK_STREQ_LIT(s, len, lit) vx_strncmplit(s, len, lit, sizeof(lit) - 1)

struct sk_ctx;

typedef enum sk_cmd : u64
{
    SK_CMD_NONE = 0,

    SK_CMD_NEW    = 1 << 0,
    SK_CMD_STRIKE = 1 << 1,
    SK_CMD_SURGE  = 1 << 2,
    SK_CMD_CLEAN  = 1 << 3,
    SK_CMD_INIT   = 1 << 4,
    SK_CMD_PURGE  = 1 << 5,
    SK_CMD_CACHE  = 1 << 6,
} sk_cmd;

typedef enum sk_opt : u64
{
    SK_OPT_NONE = 0,

    SK_OPT_VERBOSE = 1 << 0,
    SK_OPT_SILENT  = 1 << 1,
    SK_OPT_VERSION = 1 << 2,
    SK_OPT_HELP    = 1 << 3,

    SK_OPT_RUN_FROM_PATH = 1 << 4,

    SK_OPT_STRIKE_DRY = 1 << 5,
    SK_OPT_STRIKE_REL = 1 << 6,

    SK_OPT_SURGE_WITH = 1 << 7,

    SK_OPT_NEW_FILE = 1 << 8,
    SK_OPT_NEW_DIR  = 1 << 9,
    SK_OPT_NEW_PAIR = 1 << 10,

    SK_OPT_FORCE   = 1 << 11,
    SK_OPT_THREADS = 1 << 12,

    SK_OPT_PROFILE   = 1 << 13,
    SK_OPT_MEMSTAT   = 1 << 14,
    SK_OPT_TOK_DUMP  = 1 << 15,
    SK_OPT_NODE_DUMP = 1 << 16,
    SK_OPT_EVAL_DUMP = 1 << 17,
    SK_OPT_GEN_CCMDS = 1 << 18,

    SK_OPT_CACHE_SIZE = 1 << 19,
    SK_OPT_CACHE_NUKE = 1 << 20,
} sk_opt;

typedef vx_status (*sk_cmd_fn)(struct sk_ctx *ctx, sk_cmd cmd_id, i32 *i, i32 argc, char **argv);
typedef vx_status (
    *sk_opt_fn)(struct sk_ctx *ctx, sk_cmd cmd_id, sk_opt opt_id, i32 *i, i32 argc, char **argv);

struct sk_subcmd_entry
{
    const char *name;

    sk_cmd id;

    sk_cmd_fn fn;

    const char *desc;
};

struct sk_opt_entry
{
    const char *name;

    sk_cmd owner;
    sk_opt id;

    sk_opt_fn fn;

    const char *desc;
};

vx_status sk_cli_driver(struct sk_ctx *ctx, i32 argc, char **argv);

#endif  // SK_CLI_H_
