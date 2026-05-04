#ifndef STORM_KNELL_H_
#define STORM_KNELL_H_

#include "cli/sk_cli.h"
#include "vx_defs.h"

#include "sk_globals.h"

typedef enum sk_cli_flags : u32
{
    SK_CLI_FLAGS_NONE = 0,

    SK_CLI_FLAGS_VERBOSE = 1 << 0,
    SK_CLI_FLAGS_SILENT  = 1 << 1,
    SK_CLI_FLAGS_ERROR   = 1 << 2,
} sk_cli_flags;

typedef struct sk_ctx
{
    sk_cmd cli_cmd;

    sk_cli_flags cli_flags;
} sk_ctx;

#endif  // STORM-KNELL_H_
