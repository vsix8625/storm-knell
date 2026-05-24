#include "sk_array.h"
#include "vx_platform.h"
#include "vx_time.h"
#include "vx_util.h"
#include "vx_io.h"
#include "vx_fs.h"
#include "vx_cpu.h"

#include "sk_paths.h"
#include "sk_commands.h"
#include "sk_globals.h"
#include "sk_config.h"
#include "sk_cli.h"
#include "sk_util.h"
#include "sk_cmd_strike.h"
#include "storm-knell.h"

#include "mem.h"
#include <stdlib.h>
#include <string.h>

static const char *g_sk_template_c;
static const char *g_sk_template_cpp;

// ----------------------------------------------------------------------------------------------------

// static bool is_subcmd(const char *arg);

// ----------------------------------------------------------------------------------------------------

static inline vx_status
opt_config_add_cc(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv);

static inline vx_status
opt_set_var(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv);

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

static inline vx_status
subcmd_surge_handler(struct sk_ctx *ctx, sk_cmd id, i32 *i, i32 argc, char **argv);

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
    {"strike", SK_CMD_STRIKE, subcmd_handler, "Parse Stormfile and build project (alias: build)"},
    {"surge", SK_CMD_SURGE, subcmd_surge_handler, "Run target (alias: run & manifest-aware)"},
    {"clean", SK_CMD_CLEAN, subcmd_handler, "Clean artifacts (manifest-aware)"},
    {"init", SK_CMD_INIT, subcmd_handler, "Initialize Storm-Knell in working directory"},
    {"purge", SK_CMD_PURGE, subcmd_handler, "De-initialize Storm-Knell from working directory"},
    {"cache", SK_CMD_CACHE, subcmd_handler, "View global cache size, or nuke"},
    {"status", SK_CMD_STATUS, subcmd_handler, "View status"},
    {"config", SK_CMD_CONFIG, subcmd_handler, "Config"},
    {nullptr, SK_CMD_NONE, nullptr, nullptr},
};

static struct sk_opt_entry g_sk_opts[] = {
    // globals - owner = SK_CMD_NONE
    {"--verbose", SK_CMD_NONE, SK_OPT_VERBOSE, opt_toggle_logging, "Verbose output"},
    {"--silent", SK_CMD_NONE, SK_OPT_SILENT, opt_toggle_logging, "No output"},
    {"--version", SK_CMD_NONE, SK_OPT_VERSION, opt_set_bit, "Show version information and exit"},
    {"--help", SK_CMD_NONE, SK_OPT_HELP, opt_help, "Show help information and exit"},
    {"--force", SK_CMD_NONE, SK_OPT_FORCE, opt_set_bit, "Force action"},
    {"--profile", SK_CMD_NONE, SK_OPT_PROFILE, opt_set_bit, "Enable profiling"},
    {"--memstat", SK_CMD_NONE, SK_OPT_MEMSTAT, opt_set_bit, "Show memory information"},
    {"--main-c", SK_CMD_NONE, SK_OPT_MAIN_C, opt_set_bit, "Generate 'Hello, from sk' main.c"},
    {"--main-cpp", SK_CMD_NONE, SK_OPT_MAIN_CPP, opt_set_bit, "Generate 'Hello, from sk' main.cpp"},
    {"--token-dump", SK_CMD_NONE, SK_OPT_TOK_DUMP, opt_set_bit, "Show tokens"},
    {"--node-dump", SK_CMD_NONE, SK_OPT_NODE_DUMP, opt_set_bit, "Show nodes"},
    {"--eval-dump", SK_CMD_NONE, SK_OPT_EVAL_DUMP, opt_set_bit, "Show eval"},
    {"--set", SK_CMD_NONE, SK_OPT_SETVAR, opt_set_var, "Inject boolean variable into eval"},

    {"--add-cc",
     SK_CMD_CONFIG,
     SK_OPT_CONFIG_ADD_CC,
     opt_config_add_cc,
     "Add compiler path to config"},

    {"-C", SK_CMD_NONE, SK_OPT_RUN_FROM_PATH, opt_set_rpath, "Run from path"},
    {"-j", SK_CMD_NONE, SK_OPT_THREADS, opt_set_jobs, "Allow N jobs at once"},
    {"-h", SK_CMD_NONE, SK_OPT_HELP, opt_help, "Show help information and exit"},
    // ----------------------------------------------------------------------------------------------------

    // ----------------------------------------------------------------------------------------------------
    // owner = SK_CMD_STRIKE
    {"--dry", SK_CMD_STRIKE, SK_OPT_STRIKE_DRY, opt_set_bit, "Dry run"},
    {"--release", SK_CMD_STRIKE, SK_OPT_STRIKE_REL, opt_set_bit, "Release build"},
    {"-r", SK_CMD_STRIKE, SK_OPT_STRIKE_REL, opt_set_bit, "Release build"},
    {"--gen-ccmds", SK_CMD_STRIKE, SK_OPT_GEN_CCMDS, opt_set_bit, "Generate compile_commands.json"},
    // ----------------------------------------------------------------------------------------------------

    // ----------------------------------------------------------------------------------------------------
    // owner = SK_CMD_SURGE
    {"--with", SK_CMD_SURGE, SK_OPT_SURGE_WITH, opt_set_bit, "Run under tool"},
    // ----------------------------------------------------------------------------------------------------

    // ----------------------------------------------------------------------------------------------------
    // owner = SK_CMD_CACHE
    {"--nuke", SK_CMD_CACHE, SK_OPT_CACHE_NUKE, opt_set_bit, "Clean cache"},
    // ----------------------------------------------------------------------------------------------------

    // owner = SK_CMD_CLEAN
    {"--full", SK_CMD_CLEAN, SK_OPT_CLEAN_FULL, opt_set_bit, "Clean also cached project objects"},

    // ----------------------------------------------------------------------------------------------------
    {"(init)", SK_CMD_INIT, SK_OPT_NONE, nullptr, "sk init"},

    {nullptr, SK_CMD_NONE, SK_OPT_NONE, nullptr, nullptr},
};

//----------------------------------------------------------------------------------------------------

// static bool is_subcmd(const char *arg)
// {
//     for (size_t i = 0; g_sk_subcmds[i].name; i++)
//     {
//         if (strcmp(arg, g_sk_subcmds[i].name) == 0)
//         {
//             return true;
//         }
//     }
//
//     return false;
// }

static void strip_trailing_sep(char *path)
{
    size_t len = strlen(path);
    while (len > 1 && (path[len - 1] == VX_PATH_SEP))
    {
        path[--len] = CHAR_NULTERM;
    }
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
        if (ctx->surge_passthrough_argv != nullptr && &argv[i] >= ctx->surge_passthrough_argv)
        {
            break;
        }

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
                    if (!match)
                    {
                        match = (strncmp(arg, opt, opt_len) == 0 && arg[opt_len] == CHAR_EQUAL);
                    }
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

    // ----------------------------------------------------------------------------------------------------
    // global config

    sk_cache_config_init_global(&ctx->ccfg);

    // ----------------------------------------------------------------------------------------------------
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

    vx_ticks total_time = {0};
    if (ctx->active_opt & SK_OPT_PROFILE)
    {
        vx_sbuf_append(&g_sk_profile_sbuf, "====== Profiler ======\n");
        vx_ticks_start(&total_time);
    }

    if (ctx->active_cmd & SK_CMD_CONFIG)
    {
        if (sk_cmd_config_fn(ctx) != VX_OK)
        {
            vx_warn("Config function exited abnormally");
        }
    }

    vx_dbglog("active_cmd: 0x%08lX", ctx->active_cmd);
    vx_dbglog("active_opt: 0x%08lX", ctx->active_opt);

    // version and status exits early
    if (ctx->active_opt & SK_OPT_VERSION)
    {
        vx_log("Storm-Knell version: (%s)", SK_VERSION_STRING);

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

    if (ctx->active_cmd & SK_CMD_CLEAN)
    {
        if (sk_cmd_clean_fn(ctx) != VX_OK)
        {
            return VX_ERROR;
        }
    }

    if (ctx->active_opt & SK_OPT_MAIN_C)
    {
        const char *rpath = ctx->rpath ? ctx->rpath : vx_getcwd_fn();
        const char *path  = sk_path_join(g_sk_global_arena, rpath, "main.c");

        vx_dbglog("opt: --main-c -> %s", path);
        if (vx_mkdir_p(rpath) == VX_OK)
        {
            vx_fwrite(path, "%s", g_sk_template_c);
        }
    }

    if (ctx->active_opt & SK_OPT_MAIN_CPP)
    {
        const char *rpath = ctx->rpath ? ctx->rpath : vx_getcwd_fn();
        const char *path  = sk_path_join(g_sk_global_arena, rpath, "main.cpp");

        vx_dbglog("opt: --main-cpp -> %s", path);
        if (vx_mkdir_p(rpath) == VX_OK)
        {
            vx_fwrite(path, "%s", g_sk_template_cpp);
        }
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
        if (sk_cmd_surge_fn(ctx) != VX_OK)
        {
            return VX_ERROR;
        }
    }

    if (ctx->active_cmd & SK_CMD_STATUS)
    {
        sk_cmd_status_fn(ctx);
    }

    if (ctx->active_cmd & SK_CMD_CACHE)
    {
        return sk_cmd_cache_fn(ctx);
    }

    if (ctx->active_opt & SK_OPT_PROFILE)
    {
        vx_ticks_end(&total_time);
        char  elapsed[VX_BUF_SIZE_32];
        char *elapsed_fmt = vx_ticks_format(&total_time, elapsed, sizeof(elapsed));
        vx_sbuf_append(&g_sk_profile_sbuf,
                       "======================\n"
                       "Total: %s%s%s\n",
                       "\033[34m",
                       elapsed_fmt,
                       "\033[0m");

        vx_sbuf_append(&g_sk_profile_sbuf, "======================\n");
        vx_printf("%s", g_sk_profile_buf);
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

    ctx->cores   = vx_cpu_get_nproc();
    ctx->setvars = sk_arena_array_create(g_sk_global_arena, 16);

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
        mem_arena_log_stats(g_sk_global_arena);
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

    u32 threads = 0;

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

        ctx->threads = threads;

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

    char *abs_path = mem_arena_alloc(g_sk_global_arena, VX_PATH_MAX);

    if (vx_fs_is_abspath(tmp_path))
    {
        snprintf(abs_path, VX_PATH_MAX, "%s", tmp_path);
    }
    else
    {
        const char *cwd = vx_getcwd_fn();

        i32 len = snprintf(abs_path, VX_PATH_MAX, "%s%s%s", cwd, VX_PATH_SEP_STR, tmp_path);

        if (len < 0 || (size_t) len >= VX_PATH_MAX)
        {
            vx_errlog("Path resolution exceeded maximum path length!");
            return VX_ERROR;
        }
    }

    ctx->rpath = abs_path;
    vx_dbglog("rpath set to: %s", abs_path);

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

    ctx->active_cmd |= owner;
    ctx->active_opt |= opt;

    (*i)++;
    return VX_OK;
}

//----------------------------------------------------------------------------------------------------

static inline vx_status
subcmd_surge_handler(struct sk_ctx *ctx, sk_cmd id, i32 *i, i32 argc, char **argv)
{
    ctx->active_cmd |= id;
    (*i)++;

    if (*i < argc && argv[*i][0] != CHAR_MINUS && strcmp(argv[*i], ":::") != 0)
    {
        ctx->surge_target = argv[*i];
        (*i)++;
    }

    if (*i < argc && strcmp(argv[*i], ":::") == 0)
    {
        (*i)++;
        ctx->surge_passthrough_argv = &argv[*i];
        ctx->surge_passthrough_argc = argc - *i;

        *i = argc;
    }

    return VX_OK;
}

// help

struct sk_deep_help_entry
{
    sk_cmd      cmd_mask;
    const char *extended;
};

static const struct sk_deep_help_entry g_sk_deep_helps[] = {
    {SK_CMD_STRIKE,
     "Usage: sk strike [options]\n\n"
     "Detailed Information:\n"
     "  Parses the Stormfile in the current directory and executes the dependency graph\n"
     "  to compile targets.\n"
     "  \n\n"
     "Examples:\n"
     "  sk strike -r          Builds the current project with optimizations turned on.(NYI)\n"
     "  sk strike -jN         Global opt: Allow N jobs at onces.\n"
     "  sk strike --gen-ccmds Generate compile_commands.json in working directory.\n"
     "  sk strike --dry       Validates the Stormfile parsing without starting compilation."},

    {SK_CMD_SURGE,
     "Usage: sk surge [target] [::: arguments...]\n\n"
     "Detailed Information:\n"
     "  Executes a compiled binary target. It reads 'manifest.bin' (generated\n"
     "  by 'strike' inside the .storm directory) to locate valid outputs.\n\n"
     "  If no target is specified, surge spawns the most recently compiled executable.\n\n"
     "  Use the ':::' separator to pass raw flags or options to the target process.\n"
     "  Everything after ':::' bypasses Storm-Knell's parser and is forwarded directly\n"
     "  into the child process's argv array.\n\n"
     "Examples:\n"
     "  sk surge                          Spawns the last successfully built target.\n"
     "  sk surge foo                      Spawns the target named 'foo'.\n"
     "  sk surge foo ::: -arg1 --flags    Spawns 'foo' and forwards '-arg1 --flags' directly to "
     "it."},

    {SK_CMD_INIT,
     "Usage: sk init [options]\n\n"
     "Detailed Information:\n"
     "  Creates a base 'Stormfile' and the local '.storm/' tracking directory.\n"
     "  If the target directory path does not exist, Storm-Knell will create it.\n\n"
     "Examples:\n"
     "  sk init                          Initializes the current directory.\n"
     "  sk init --force                  Overwrites and resets existing Stormfile and .storm/\n"
     "  sk init -C <path>                Initializes inside the specified path."},

    {SK_CMD_PURGE,
     "Usage: sk purge [options]\n\n"
     "Detailed Information:\n"
     "  Removes 'Stormfile' and deletes the '.storm/' directory completely.\n"
     "  NOTE: This only removes files owned by sk; it will never delete your parent\n"
     "  project directories.\n\n"
     "Examples:\n"
     "  sk purge                         Purges sk files from the current directory.\n"
     "  sk purge -C <path>               Purges sk files from the specified path."},

    {SK_CMD_CACHE,
     "Usage: sk cache [options]\n\n"
     "Detailed Information:\n"
     "  Displays the total disk space used by the global object cache.\n"
     "  Use '--nuke' to clear out the cached objects and reset the directory size.\n\n"
     "Examples:\n"
     "  sk cache                         Show global cache size and object count.\n"
     "  sk cache --nuke                  Deletes all objects stored in the global cache."},

    {SK_CMD_STATUS,
     "Usage: sk status\n\n"
     "Detailed Information:\n"
     "  Reads 'manifest.bin' to show the state of the last build.\n"
     "  Displays which targets are compiled, up to date, or missing.\n\n"
     "Examples:\n"
     "  sk status"},

    {SK_CMD_CLEAN,
     "Usage: sk clean\n\n"
     "Detailed Information:\n"
     "  Deletes local build artifacts (object files and binaries) tracked in 'manifest.bin'.\n"
     "  Unlike 'cache --nuke', this only affects the current project's outputs.\n\n"
     "Examples:\n"
     "  sk clean"},

    {SK_CMD_NONE, nullptr}};

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

    if (focus != SK_CMD_NONE)
    {
        for (i32 d = 0; g_sk_deep_helps[d].extended; d++)
        {
            if (g_sk_deep_helps[d].cmd_mask & focus)
            {
                vx_printf("\n--------------------------------------------------\n");
                vx_printf("%s\n", g_sk_deep_helps[d].extended);
            }
        }
    }

    sk_shutdown();
    exit(0);
}

static bool ctx_var_is_set(struct sk_ctx *ctx, const char *name)
{
    return sk_arena_array_contains(ctx->setvars, name);
}

static inline vx_status
opt_set_var(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv)
{
    VX_CAST(void, argc);
    VX_CAST(void, owner);

    ctx->active_opt |= opt;

    char *arg = argv[*i];
    char *eq  = strchr(arg, CHAR_EQUAL);

    if (eq == nullptr || eq[1] == CHAR_NULTERM)
    {
        return VX_ERROR;
    }
    char *name = mem_arena_strdup(g_sk_global_arena, eq + 1);

    if (ctx_var_is_set(ctx, name))
    {
        vx_warn("--set=%s already set, ignored", name);
        (*i)++;
        return VX_OK;
    }

    sk_arena_array_push(ctx->setvars, name);

    (*i)++;
    return VX_OK;
}

static inline vx_status
opt_config_add_cc(struct sk_ctx *ctx, sk_cmd owner, sk_opt opt, i32 *i, i32 argc, char **argv)
{
    VX_CAST(void, argc);

    ctx->active_cmd |= owner;
    ctx->active_opt |= opt;

    char *arg = argv[*i];
    char *eq  = strchr(arg, CHAR_EQUAL);

    if (eq == nullptr || eq[1] == CHAR_NULTERM)
    {
        return VX_ERROR;
    }

    char *path_val = eq + 1;

    if (!vx_fs_is_abspath(path_val))
    {
        vx_warn("Option: '--add-cc' expects absolute path");
        return VX_ERROR;
    }

    if (sk_config_add_cc_path_b(path_val) != VX_OK)
    {
        return VX_ERROR;
    }

    (*i)++;
    return VX_OK;
}

static const char *g_sk_template_c = "#include <stdio.h>\n"
                                     "\n"
                                     "int main(int argc, char **argv)\n"
                                     "{\n"
                                     "    (void)argc;\n"
                                     "    (void)argv;\n"
                                     "\n"
                                     "    printf(\"Hello from Storm-Knell C project!\\n\");\n"
                                     "    return 0;\n"
                                     "}\n";

static const char *g_sk_template_cpp =
    "#include <iostream>\n"
    "\n"
    "int main(int argc, char *argv[])\n"
    "{\n"
    "    (void)argc;\n"
    "    (void)argv;\n"
    "\n"
    "    std::cout << \"Hello from Storm-Knell C++ project!\\n\";\n"
    "    return 0;\n"
    "}\n";

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
