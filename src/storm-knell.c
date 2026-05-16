#include "storm-knell.h"
#include "sk_paths.h"
#include "sk_globals.h"
#include "sk_util.h"
#include "sk_cli.h"
#include "vx.h"
#include "mem.h"

// ----------------------------------------------------------------------------------------------------

struct sk_ctx     g_sk_global_ctx   = {0};
struct mem_arena *g_sk_global_arena = nullptr;

// ----------------------------------------------------------------------------------------------------

static bool      sk_is_debug(void);
static vx_status sk_init(i32 argc, char **argv);

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

    sk_shutdown();
    return VX_EXIT_SUCCESS;
}

// ----------------------------------------------------------------------------------------------------

#define SK_GLOBAL_ARENA_SIZE (1024ULL * 1024ULL * 32)

static vx_status sk_init(i32 argc, char **argv)
{
    if (vx_init() != VX_OK)
    {
        return VX_LIB_NOT_INITIALIZED;
    }
    vx_set_debug(sk_is_debug());

    if (!mem_init())
    {
        return VX_FATAL;
    }

    g_sk_global_arena = mem_arena_create("global-arena", SK_GLOBAL_ARENA_SIZE);

    if (g_sk_global_arena == nullptr)
    {
        return VX_FATAL;
    }

    if (sk_cli_driver(&g_sk_global_ctx, argc, argv) != VX_OK)
    {
        return VX_FATAL;
    }

    vx_dbglog("Using VX library version: (%s)", VX_VERSION_STRING);
    vx_dbglog("Global arena: %zu KB", mem_arena_get_capacity(g_sk_global_arena) / 1024);
    return VX_OK;
}

void sk_shutdown(void)
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
