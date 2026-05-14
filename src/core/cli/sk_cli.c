#include "vx_platform.h"
#include "vx_util.h"
#include "vx_io.h"
#include "vx_fs.h"
#include "vx_cpu.h"

#include "sk_cmd_new.h"

#include "sk_config.h"
#include "sk_cli.h"
#include "sk_util.h"
#include "sk_cmd_strike.h"
#include "storm-knell.h"

#include "mem.h"
#include <stdlib.h>
#include <string.h>

// ----------------------------------------------------------------------------------------------------

static bool is_subcmd(const char *arg);

// ----------------------------------------------------------------------------------------------------

static inline vx_status
opt_set_jobs(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv);

static inline vx_status
opt_set_rpath(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv);

static inline vx_status
opt_toggle_logging(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv);

static inline vx_status
opt_set_bit(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv);

static vx_status
opt_help(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv);

static vx_status init_handler(struct sk_ctx *ctx, sk_cmd id, i32 *i, i32 argc, char **argv);

// ----------------------------------------------------------------------------------------------------

static inline vx_status subcmd_handler(struct sk_ctx *ctx, sk_cmd id, i32 *i, i32 argc, char **argv)
{
    (void) argc;
    (void) argv;

    ctx->active_cmd |= id;

    (*i)++;
    return VX_OK;
}

static struct sk_subcmd_entry g_sk_subcmds[] = {
    {"new", SK_CMD_NEW, subcmd_handler, "Scaffold a new porject or file"},
    {"strike", SK_CMD_STRIKE, subcmd_handler, "Build"},
    {"surge", SK_CMD_SURGE, subcmd_handler, "Run binary"},
    {"clean", SK_CMD_CLEAN, subcmd_handler, "Clean artifacts"},
    {"init", SK_CMD_INIT, init_handler, "Initialize Storm-Knell in working directory"},
    {"purge", SK_CMD_PURGE, subcmd_handler, "Nuke .storm and artifacts"},
    {nullptr, SK_CMD_NONE, nullptr, nullptr},
};

static struct sk_opt_entry g_sk_opts[] = {
    // globals - owner = SK_CMD_NONE
    {"--verbose", SK_CMD_NONE, SK_OPT_VERBOSE, opt_toggle_logging, "Verbosity levels"},
    {"--silent", SK_CMD_NONE, SK_OPT_SILENT, opt_toggle_logging, "No output"},
    {"--version", SK_CMD_NONE, SK_OPT_VERSION, opt_set_bit, "Show version information and exit"},
    {"--help", SK_CMD_NONE, SK_OPT_HELP, opt_help, "Show help information and exit"},
    {"--force", SK_CMD_NONE, SK_OPT_FORCE, opt_set_bit, "Force action"},
    {"--profile", SK_CMD_NONE, SK_OPT_PROFILE, opt_set_bit, "Enable profiling"},
    {"--memstat", SK_CMD_NONE, SK_OPT_MEMSTAT, opt_set_bit, "Show memory information"},
    {"--token-dump", SK_CMD_NONE, SK_OPT_TOK_DUMP, opt_set_bit, "Show tokens"},
    {"--node-dump", SK_CMD_NONE, SK_OPT_NODE_DUMP, opt_set_bit, "Show nodes"},
    {"--eval-dump", SK_CMD_NONE, SK_OPT_EVAL_DUMP, opt_set_bit, "Show eval"},
    {"-h", SK_CMD_NONE, SK_OPT_HELP, opt_help, "Show help information and exit"},
    {"-C", SK_CMD_NONE, SK_OPT_RUN_FROM_PATH, opt_set_rpath, "Run from path"},
    {"-j", SK_CMD_NONE, SK_OPT_THREADS, opt_set_jobs, "Allow N jobs at once"},
    // ----------------------------------------------------------------------------------------------------

    // ----------------------------------------------------------------------------------------------------
    // owner = SK_CMD_STRIKE
    {"--dry", SK_CMD_STRIKE, SK_OPT_STRIKE_DRY, opt_set_bit, "Dry run"},
    {"--release", SK_CMD_STRIKE, SK_OPT_STRIKE_REL, opt_set_bit, "Release build"},
    {"-r", SK_CMD_STRIKE, SK_OPT_STRIKE_REL, opt_set_bit, "Release build"},
    // ----------------------------------------------------------------------------------------------------

    // ----------------------------------------------------------------------------------------------------
    // owner = SK_CMD_SURGE
    {"--with", SK_CMD_SURGE, SK_OPT_SURGE_WITH, opt_set_bit, "Run under tool"},
    // ----------------------------------------------------------------------------------------------------

    // ----------------------------------------------------------------------------------------------------
    // owner = SK_CMD_NEW
    {"--file", SK_CMD_NEW, SK_OPT_NEW_FILE, opt_set_bit, "Creates a new file"},
    {"-f", SK_CMD_NEW, SK_OPT_NEW_FILE, opt_set_bit, "Creates a new file"},

    {"--dir", SK_CMD_NEW, SK_OPT_NEW_DIR, opt_set_bit, "Creates a new directory"},
    {"-d", SK_CMD_NEW, SK_OPT_NEW_DIR, opt_set_bit, "Creates a new directory"},

    {"--pair", SK_CMD_NEW, SK_OPT_NEW_PAIR, opt_set_bit, "Creates pair of source and header"},
    {"-p", SK_CMD_NEW, SK_OPT_NEW_PAIR, opt_set_bit, "Creates pair of source and header"},

    // ----------------------------------------------------------------------------------------------------
    {"(init)", SK_CMD_INIT, SK_OPT_NONE, nullptr, "sk init [path]"},

    {nullptr, SK_CMD_NONE, SK_OPT_NONE, nullptr, nullptr},
};

//----------------------------------------------------------------------------------------------------

static bool is_subcmd(const char *arg)
{
    for (size_t i = 0; g_sk_subcmds[i].name; i++)
    {
        if (strcmp(arg, g_sk_subcmds[i].name) == 0)
        {
            return true;
        }
    }

    return false;
}

static void strip_trailing_sep(char *path)
{
    size_t len = strlen(path);
    while (len > 1 && (path[len - 1] == VX_PATH_SEP))
    {
        path[--len] = CHAR_NULTERM;
    }
}

static vx_status init_handler(struct sk_ctx *ctx, sk_cmd id, i32 *i, i32 argc, char **argv)
{
    ctx->active_cmd |= id;

    (*i)++;

    if (*i < argc && argv[*i][0] != CHAR_MINUS && !is_subcmd(argv[*i]))
    {
        strip_trailing_sep(argv[*i]);
        char resovled[VX_PATH_MAX];
        vx_fs_realpath(argv[*i], resovled);
        ctx->init_dir = resovled;
        (*i)++;
    }

    return VX_OK;
}

//----------------------------------------------------------------------------------------------------

static vx_status parse_subcmds(struct sk_ctx *ctx, i32 argc, char **argv)
{
    if (ctx == nullptr || argv == nullptr)
    {
        return VX_ERROR;
    }

    if (argc == 1)
    {
        vx_log("Usage: sk <subcmd> [opt] (sk --help for more information)");
    }

    for (i32 i = 1; i < argc;)
    {
        const char *arg = argv[i];

        bool matched = false;

        if (arg[0] == CHAR_MINUS)
        {
            i++;
            continue;
        }

        // aliases
        const char *resolved_cmd = arg;
        if (strcmp(arg, "build") == 0)
        {
            resolved_cmd = "strike";
        }
        else if (strcmp(arg, "run") == 0)
        {
            resolved_cmd = "surge";
        }

        for (size_t j = 0; g_sk_subcmds[j].name; j++)
        {
            if (strcmp(resolved_cmd, g_sk_subcmds[j].name) == 0)
            {
                if (g_sk_subcmds[j].fn == nullptr ||
                    g_sk_subcmds[j].fn(ctx, g_sk_subcmds[j].id, &i, argc, argv) != VX_OK)
                {
                    VX_ASSERT_LOG("Subcommand: %s exit abnormally", arg);
                    return VX_ERROR;
                }

                matched = true;
                break;
            }
        }

        if (!matched)
        {
            i++;
            continue;
        }
    }

    return VX_OK;
}

static vx_status parse_opts(struct sk_ctx *ctx, i32 argc, char **argv)
{
    if (ctx == nullptr || argv == nullptr)
    {
        VX_ASSERT_LOG("nullptr args");
        return VX_ERROR;
    }

    for (i32 i = 1; i < argc;)
    {
        const char *arg = argv[i];

        if (arg[0] == CHAR_MINUS)
        {
            bool match = false;

            for (size_t j = 0; g_sk_opts[j].name; j++)
            {
                const char *opt = g_sk_opts[j].name;

                size_t opt_len = strlen(opt);

                if (arg[1] == CHAR_MINUS)
                {
                    match = strcmp(arg, opt) == 0;
                }
                else
                {
                    match = strncmp(arg, opt, opt_len) == 0;
                }

                if (match)
                {
                    bool is_global = (g_sk_opts[j].owner == SK_CMD_NONE);
                    bool is_owned  = (ctx->active_cmd & g_sk_opts[j].owner);

                    if (!is_global && !is_owned)
                    {
                        vx_warn("Option '%s' has no effect here, skipping", arg);
                        i++;
                        match = true;
                        continue;
                    }

                    if (g_sk_opts[j].fn == nullptr ||
                        g_sk_opts[j].fn(ctx, g_sk_opts[j].owner, g_sk_opts[j].id, &i, argc, argv) !=
                            VX_OK)
                    {
                        VX_ASSERT_LOG("Option: %s failed", arg);
                        return VX_ERROR;
                    }

                    match = true;
                    break;
                }
            }

            if (!match)
            {
                vx_errlog("Unknown option: %s", arg);
                i++;
                continue;
            }
        }
        else
        {
            i++;
        }
    }

    return VX_OK;
}

//----------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------

static vx_status cli_execute(struct sk_ctx *ctx)
{
    if (ctx == nullptr)
    {
        return VX_ERROR;
    }

    // NOTE: order matters here

    if (ctx->active_opt & SK_OPT_SILENT)
    {
        FILE *nul_out = fopen(VX_DEVNUL, "w");

        if (nul_out)
        {
            fflush(stdout);
            fflush(stderr);
            dup2(VX_FILENO(nul_out), STDOUT_FILENO);
            dup2(VX_FILENO(nul_out), STDERR_FILENO);
            fclose(nul_out);
        }
    }

    if (ctx->active_opt & SK_OPT_VERBOSE)
    {
        vx_set_debug(true);
    }

    vx_dbglog("active_cmd: 0x%08lX", ctx->active_cmd);
    vx_dbglog("active_opt: 0x%08lX", ctx->active_opt);
    const char *log_path = ctx->rpath;
    vx_dbglog("rpath: %s |  fallback: %s",
              log_path ? log_path : "(not set)",
              log_path ? "" : vx_getcwd_fn());

    // version exits early
    if (ctx->active_opt & SK_OPT_VERSION)
    {
        vx_log("Storm-Knell version: (%s-[%s])", SK_VERSION_STRING, SK_BUILD_TYPE);

        sk_shutdown();
        exit(VX_EXIT_SUCCESS);
    }

    if (ctx->active_cmd & SK_CMD_INIT)
    {
        sk_cmd_init_fn(ctx);

        if (ctx->active_cmd & SK_CMD_PURGE)
        {
            vx_warn("init and purge used together, purge ignored");
            ctx->active_cmd &= ~SK_CMD_PURGE;
        }
    }

    if (ctx->active_cmd & SK_CMD_PURGE)
    {
        if ((ctx->active_cmd & SK_CMD_STRIKE) || (ctx->active_cmd & SK_CMD_SURGE))
        {
            vx_warn("Aborted purge: cannot nuke a project while building or running");
            ctx->active_cmd &= ~SK_CMD_PURGE;
        }
        else
        {
            sk_cmd_purge_fn(ctx);
        }
    }

    if (ctx->active_cmd & SK_CMD_NEW)
    {
        // if (sk_cmd_new_file(ctx) != VX_OK)
        // {
        //     return VX_ERROR;
        // }
    }

    if (ctx->active_cmd & SK_CMD_STRIKE)
    {
        if (sk_cmd_strike_fn(ctx) != VX_OK)
        {
            return VX_ERROR;
        }
    }

    if (ctx->active_cmd & SK_CMD_SURGE)
    {
        vx_warn("SURGING");
    }

    return VX_OK;
}

//----------------------------------------------------------------------------------------------------

vx_status sk_cli_driver(struct sk_ctx *ctx, i32 argc, char **argv)
{
    if (ctx == nullptr || argv == nullptr)
    {
        return VX_ERROR;
    }

    if (parse_subcmds(ctx, argc, argv) != VX_OK)
    {
        return VX_ERROR;
    }

    if (parse_opts(ctx, argc, argv) != VX_OK)
    {
        return VX_ERROR;
    }

    if (cli_execute(ctx) != VX_OK)
    {
        return VX_ERROR;
    }

    if (ctx->active_opt & SK_OPT_MEMSTAT)
    {
        mem_arena_log_all_stats();
        mem_heap_print_stats(nullptr);
    }

    return VX_OK;
}

//----------------------------------------------------------------------------------------------------

static inline vx_status
opt_set_jobs(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv)
{
    VX_CAST(void, owner);

    ctx->active_opt |= opt;

    char *arg = argv[*i];

    u32 nproc   = vx_cpu_get_nproc();
    u32 threads = 1;

    if (strncmp(arg, "-j", 2) == 0)
    {
        if (arg[2] != CHAR_NULTERM)
        {
            threads = (u32) atoi(&arg[2]);

            *i += 1;
        }
        else if (*i + 1 < argc && sk_util_is_digit(argv[*i + 1][0]))
        {
            threads = (u32) atoi(argv[*i + 1]);

            *i += 2;
        }
        else
        {
            threads = 0;

            *i += 1;
        }

        ctx->cores   = nproc;
        ctx->threads = threads;

        vx_dbglog("Cores: %u, Threads: %u", nproc, threads);
        return VX_OK;
    }

    return VX_ERROR;
}

//----------------------------------------------------------------------------------------------------

static inline bool sk_is_path_valid(const char *path)
{
    if (path == nullptr || path[0] == CHAR_NULTERM)
    {
        return false;
    }

    if (path[0] == CHAR_MINUS)
    {
        return false;
    }

    return true;
}

static inline vx_status
opt_set_rpath(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv)
{
    VX_CAST(void, owner);
    ctx->active_opt |= opt;

    char *arg      = argv[*i];
    char *tmp_path = nullptr;

    if (arg[2] != CHAR_NULTERM)
    {
        tmp_path = &arg[2];
        (*i)++;
    }
    else if (*i + 1 < argc)
    {
        (*i)++;
        tmp_path = argv[*i];
    }

    if (!sk_is_path_valid(tmp_path))
    {
        vx_errlog("Invalid or missing directory path for -C: '%s'", tmp_path);
        return VX_ERROR;
    }

    strip_trailing_sep(tmp_path);
    ctx->rpath = tmp_path;

    return VX_OK;
}

static inline vx_status
opt_toggle_logging(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv)
{
    VX_CAST(void, argc);
    VX_CAST(void, argv);
    VX_CAST(void, owner);

    if (opt == SK_OPT_VERBOSE)
    {
        ctx->active_opt &= ~SK_OPT_SILENT;
        ctx->active_opt |= SK_OPT_VERBOSE;
    }
    else if (opt == SK_OPT_SILENT)
    {
        ctx->active_opt &= ~SK_OPT_VERBOSE;
        ctx->active_opt |= SK_OPT_SILENT;
    }

    (*i)++;
    return VX_OK;
}

static inline vx_status
opt_set_bit(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv)
{
    VX_CAST(void, argc);
    VX_CAST(void, argv);
    VX_CAST(void, owner);

    ctx->active_opt |= opt;

    (*i)++;
    return VX_OK;
}

//----------------------------------------------------------------------------------------------------

static vx_status
opt_help(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv)
{
    VX_CAST(void, owner);
    VX_CAST(void, opt);

    sk_cmd focus = ctx->active_cmd;

    for (i32 k = *i + 1; k < argc; k++)
    {
        if (argv[k][0] == '-')
        {
            continue;
        }
        for (i32 m = 0; g_sk_subcmds[m].name; m++)
        {
            if (strcmp(argv[k], g_sk_subcmds[m].name) == 0)
            {
                focus |= g_sk_subcmds[m].id;
            }
        }
    }

    vx_printf("==================================================\n");
    vx_printf("Storm-Knell Help: v.%s\n", SK_VERSION_STRING);
    vx_printf("==================================================\n");

    if (focus == SK_CMD_NONE)
    {
        vx_printf("Commands:\n");
        for (i32 k = 0; g_sk_subcmds[k].name; k++)
        {
            vx_printf("  %-10s %s\n", g_sk_subcmds[k].name, g_sk_subcmds[k].desc);
        }
        vx_printf("\nGlobal options:\n");
    }
    else
    {
        vx_printf("Command: ");
        i32 found_count = 0;
        for (i32 k = 0; g_sk_subcmds[k].name; k++)
        {
            if (focus & g_sk_subcmds[k].id)
            {
                vx_printf("%s%s", found_count > 0 ? " + " : "", g_sk_subcmds[k].name);
                found_count++;
            }
        }
        vx_printf("\n\nOptions:\n");
    }

    u64 printed_mask = 0;

    for (i32 j = 0; g_sk_opts[j].name; j++)
    {
        sk_opt current_id = g_sk_opts[j].id;

        if (printed_mask & current_id)
        {
            continue;
        }

        bool is_global     = (g_sk_opts[j].owner == SK_CMD_NONE);
        bool matches_focus = (g_sk_opts[j].owner & focus);

        if ((focus == SK_CMD_NONE && is_global) || (focus != SK_CMD_NONE && matches_focus))
        {
            // Collect all names for this ID (e.g., "-r, --release")
            char name_buf[64] = {0};
            strcat(name_buf, g_sk_opts[j].name);

            for (i32 alias = j + 1; g_sk_opts[alias].name; alias++)
            {
                if (g_sk_opts[alias].id == current_id)
                {
                    strcat(name_buf, ", ");
                    strcat(name_buf, g_sk_opts[alias].name);
                }
            }

            vx_printf("  %-20s %s\n", name_buf, g_sk_opts[j].desc);
            printed_mask |= current_id;  // Mark as done
        }
    }

    sk_shutdown();
    exit(0);
}

//----------------------------------------------------------------------------------------------------
/*
 * STORM-KNELL CLI PARSER ARCHITECTURE
 * -----------------------------------
 * The CLI uses a three-phase state-machine approach:
 *
 * 1. SUBCMD PASS:
 *    Iterates through g_sk_subcmds. If a match is found, the handler sets a
 *    bit in ctx->active_cmd. Subcommands are identified as strings NOT
 *    starting with '-'.
 *
 * 2. OPTION PASS:
 *    Iterates through g_sk_opts. Validates that the current 'arg' starts with '-'
 *    and that ctx->active_cmd matches g_sk_opts[j].owner (unless global).
 *
 * 3. EXECUTION (PIPELINE):
 *    cli_execute() reads the bitmasks in ctx->active_cmd and ctx->active_opts
 *    to run the logic in a deterministic order.
 *
 * HANDLER RESPONSIBILITIES:
 * - Handlers must manually advance the index pointer (*i) to "consume" arguments.
 * - Flag handlers (no value) consume 1: (*i)++
 * - Value handlers (e.g. --path <p>) consume 2: (*i) += 2
 * - This allows handlers full control over the argument stream.
 */
