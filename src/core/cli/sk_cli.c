#include "vx_io.h"

#include "sk_cli.h"
#include "storm-knell.h"
#include <string.h>

// ----------------------------------------------------------------------------------------------------

static inline vx_status opt_verbose(sk_ctx *ctx, i32 *i, i32 argc, char **argv);
static inline vx_status opt_silent(sk_ctx *ctx, i32 *i, i32 argc, char **argv);
static inline vx_status opt_version(sk_ctx *ctx, i32 *i, i32 argc, char **argv);

// ----------------------------------------------------------------------------------------------------

static inline vx_status test_strike(sk_ctx *ctx, i32 *i, i32 argc, char **argv)
{
    (void) ctx;
    (void) argc;
    (void) argv;
    vx_log("STRIKE");

    (*i)++;
    return VX_OK;
}

static sk_subcmd_entry g_sk_subcmds[] = {
    {"new", SK_CMD_NEW, nullptr, "Scaffold a new porject or file"},
    {"strike", SK_CMD_STRIKE, test_strike, "Build"},
    {"surge", SK_CMD_SURGE, nullptr, "Run binary"},
    {"clear", SK_CMD_CLEAR, nullptr, "Clean artifacts"},
    {"purge", SK_CMD_PURGE, nullptr, "Nuke .storm and artifacts"},
    {nullptr, SK_CMD_NONE, nullptr, nullptr},
};

static sk_opt_entry g_sk_opts[] = {
    // globals - owner = SK_CMD_NONE
    {"--verbose", SK_CMD_NONE, opt_verbose, "Verbosity levels"},
    {"--silent", SK_CMD_NONE, opt_silent, "No output"},
    {"--version", SK_CMD_NONE, opt_version, "Show version information"},
    {"-C", SK_CMD_NONE, nullptr, "Run from path"},

    // strike - owner = SK_CMD_STRIKE
    {"--dry", SK_CMD_STRIKE, nullptr, "Dry run"},
    {"-d", SK_CMD_STRIKE, nullptr, "Dry run"},
    {"--release", SK_CMD_STRIKE, nullptr, "Release build"},
    {"-r", SK_CMD_STRIKE, nullptr, "Release build"},

    // surge - owner = SK_CMD_SURGE
    {"--with", SK_CMD_SURGE, nullptr, "Run under tool"},

    {nullptr, SK_CMD_NONE, nullptr, nullptr},
};

//----------------------------------------------------------------------------------------------------

static vx_status parse_subcmds(sk_ctx *ctx, i32 argc, char **argv)
{
    if (ctx == nullptr || argv == nullptr)
    {
        VX_ASSERT_LOG("nullptr args");
        return VX_ERROR;
    }

    if (argc == 0)
    {
        vx_log("Usage: bla bla");
    }

    for (i32 i = 1; i < argc;)
    {
        const char *arg = argv[i];

        bool matched = false;

        for (size_t j = 0; g_sk_subcmds[j].name; j++)
        {
            size_t cmd_len = strlen(g_sk_subcmds[j].name);

            if (strncmp(arg, g_sk_subcmds[j].name, cmd_len) == 0)
            {
                if (g_sk_subcmds[j].fn == nullptr ||
                    g_sk_subcmds[j].fn(ctx, &i, argc, argv) != VX_OK)
                {
                    VX_ASSERT_LOG("Subcmd: %s exit abnormally", arg);
                    return VX_ERROR;
                }

                matched = true;
                break;
            }
        }

        if (!matched)
        {
            vx_errlog("Unknown subcommand: %s", arg);
            i++;
            continue;
        }

        i++;
        continue;
    }

    return VX_OK;
}

//----------------------------------------------------------------------------------------------------

vx_status sk_cli_driver(sk_ctx *ctx, i32 argc, char **argv)
{
    if (ctx == nullptr || argv == nullptr)
    {
        VX_ASSERT_LOG("nullptr args");
        return VX_ERROR;
    }

    if (parse_subcmds(ctx, argc, argv) != VX_OK)
    {
        return VX_ERROR;
    }

    return VX_OK;
}

//----------------------------------------------------------------------------------------------------

static inline vx_status opt_verbose(sk_ctx *ctx, i32 *i, i32 argc, char **argv)
{
    (void) argc;
    (void) argv;

    ctx->cli_flags &= ~SK_CLI_FLAGS_SILENT;
    ctx->cli_flags |= SK_CLI_FLAGS_VERBOSE;

    (*i)++;

    return VX_OK;
}

static inline vx_status opt_silent(sk_ctx *ctx, i32 *i, i32 argc, char **argv)
{
    (void) argc;
    (void) argv;

    ctx->cli_flags &= ~SK_CLI_FLAGS_VERBOSE;
    ctx->cli_flags |= SK_CLI_FLAGS_SILENT;

    (*i)++;

    return VX_OK;
}

static inline vx_status opt_version(sk_ctx *ctx, i32 *i, i32 argc, char **argv)
{
    (void) argc;
    (void) argv;

    ctx->cli_cmd |= SK_CLI_FLAGS_SILENT;

    (*i)++;

    // TODO: REMOVE
    (void) g_sk_opts;
    return VX_OK;
}
