#include "storm-knell.h"
#include "sk_paths.h"
#include "sk_globals.h"
#include "sk_util.h"
#include "cli/sk_cli.h"
#include "vx.h"
#include "mem.h"

// ----------------------------------------------------------------------------------------------------

sk_ctx            g_sk_global_ctx   = {0};
struct mem_arena *g_sk_global_arena = nullptr;

// ----------------------------------------------------------------------------------------------------

static bool      sk_is_debug(void);
static vx_status sk_init(i32 argc, char **argv);
static void      sk_shutdown(void);

// ----------------------------------------------------------------------------------------------------

i32 main(i32 argc, char **argv)
{
    vx_status init_result = sk_init(argc, argv);

    switch (init_result)
    {
        case VX_OK:
        {
            break;
        }

        case VX_ERROR:
        case VX_FATAL:
        case VX_LIB_NOT_INITIALIZED:
        {
            sk_shutdown();
            return VX_EXIT_FAILURE;
        }
    }
    sk_cli_driver(&g_sk_global_ctx, argc, argv);

    sk_shutdown();
    return VX_EXIT_SUCCESS;
}

// ----------------------------------------------------------------------------------------------------

static vx_status sk_init(i32 argc, char **argv)
{
    (void) argc;
    (void) argv;

    if (vx_init() != VX_OK)
    {
        return VX_FATAL;
    }
    vx_set_debug(sk_is_debug());

    vx_dbglog("Using VX library version: (%s-[%s])", VX_VERSION_STRING, VX_VERSION_STRING);

    if (!mem_init())
    {
        return VX_FATAL;
    }

    g_sk_global_arena = mem_arena_create("global-arena", VX_BUF_SIZE_64K);

    if (g_sk_global_arena == nullptr)
    {
        return VX_FATAL;
    }

    vx_dbglog("Memory system initialized");

    return VX_OK;
}

static void sk_shutdown(void)
{
    vx_dbglog("Shutting down");

    mem_shutdown();
    vx_shutdown();
}

static bool sk_is_debug(void)
{
#if defined(DEBUG) || defined(_DEBUG)
    return true;
#elif defined(NDEBUG) || defined(_NDEBUG) || defined(RELEASE)
    return false;
#endif
}
