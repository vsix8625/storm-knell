#include "mem_arena.h"
#include "mem_heap.h"

#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

/* -------------------------
 * Arena struct
 * ------------------------- */
struct mem_arena
{
    void  *base;
    size_t capacity;
    size_t used;

#if defined(DEBUG)
    size_t total_allocs;
    size_t largest_alloc;
    size_t peak;
    size_t page_size;
#endif

    const char *name;
};

/* -------------------------
 * State
 * ------------------------- */
#define ARENA_ADDR_HINT      0x700000000000ULL
#define ARENA_PAGE_SIZE_HUGE 0x00200000 /* 2MB huge pages (Linux only) */

typedef enum mem_arena_state : uint32_t
{
    ARENA_STATE_OFF           = 0,
    ARENA_STATE_RUNNING       = 1,
    ARENA_STATE_SHUTTING_DOWN = 2,
} mem_arena_state;

static _Atomic mem_arena_state g_arena_state  = ARENA_STATE_OFF;
static _Atomic uintptr_t       g_arena_cursor = 0;
static size_t                  g_sys_pagesize = 0;

/* -------------------------
 * Registry — spinlock protected
 * ------------------------- */
struct arena_registry
{
    struct mem_arena **arenas;
    size_t             count;
    size_t             capacity;
    atomic_flag        lock;
    char               padding[7];
};

static struct arena_registry g_registry = {0};

static void registry_lock(void)
{
    while (atomic_flag_test_and_set_explicit(&g_registry.lock, memory_order_acquire))
    {
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
#endif
    }
}

static void registry_unlock(void)
{
    atomic_flag_clear_explicit(&g_registry.lock, memory_order_release);
}

static void registry_add(struct mem_arena *arena)
{
    if (arena == NULL)
    {
        return;
    }

    registry_lock();

    if (g_registry.count >= g_registry.capacity)
    {
        size_t new_cap = g_registry.capacity ? g_registry.capacity * 2 : 64;

        struct mem_arena **tmp =
            mem_heap_realloc(g_registry.arenas, new_cap * sizeof(struct mem_arena *));

        if (tmp == NULL)
        {
            fprintf(stderr, "%s: registry realloc failed\n", __func__);
            registry_unlock();
            return;
        }

        g_registry.arenas   = tmp;
        g_registry.capacity = new_cap;
    }

    g_registry.arenas[g_registry.count++] = arena;
    registry_unlock();
}

static void registry_remove(struct mem_arena *arena)
{
    if (arena == NULL)
    {
        return;
    }

    registry_lock();

    for (size_t i = 0; i < g_registry.count; i++)
    {
        if (g_registry.arenas[i] == arena)
        {
            /* swap-remove */
            g_registry.arenas[i] = g_registry.arenas[--g_registry.count];
            break;
        }
    }

    registry_unlock();
}

/* -------------------------
 * Lifecycle
 * ------------------------- */
bool mem_arena_init(void)
{
    if (!mem_heap_init())
    {
        return false;
    }

    mem_arena_state expected = ARENA_STATE_OFF;
    if (!atomic_compare_exchange_strong(&g_arena_state, &expected, ARENA_STATE_RUNNING))
    {
        return false;
    }

    g_sys_pagesize = mem_sys_pagesize();

    uintptr_t cursor_expected = 0;
    uintptr_t hint            = ARENA_ADDR_HINT;
    atomic_compare_exchange_strong(&g_arena_cursor, &cursor_expected, hint);

    return true;
}

void mem_arena_shutdown(void)
{
    mem_arena_state expected = ARENA_STATE_RUNNING;
    if (!atomic_compare_exchange_strong(&g_arena_state, &expected, ARENA_STATE_SHUTTING_DOWN))
    {
        return;
    }

    /*
     * Destroy every registered arena before tearing down the heap.
     * Iterate backwards so swap-remove inside mem_arena_destroy doesn't skip entries.
     * We snapshot count first since mem_arena_destroy calls registry_remove.
     */
    size_t n = g_registry.count;
    while (n--)
    {
        struct mem_arena *arena = g_registry.arenas[0];
        mem_arena_destroy(arena);
    }

    /* registry.arenas array itself was heap-allocated */
    mem_heap_free(g_registry.arenas);
    g_registry.arenas   = NULL;
    g_registry.count    = 0;
    g_registry.capacity = 0;

    mem_heap_shutdown();
}

/* -------------------------
 * Arena create / destroy
 * ------------------------- */
struct mem_arena *mem_arena_create(const char *name, size_t capacity)
{
    if (name == NULL || name[0] == '\0')
    {
        name = "arena";
    }

    if (capacity == 0)
    {
        fprintf(stderr, "%s: capacity must be > 0\n", __func__);
        return NULL;
    }

    capacity = mem_arena_nxtpow2(capacity);

    size_t alignment = g_sys_pagesize;

#if defined(MEM_OS_LINUX)
    if (capacity >= ARENA_PAGE_SIZE_HUGE)
    {
        alignment = ARENA_PAGE_SIZE_HUGE;
    }
#endif

    capacity = mem_arena_align_up(capacity, alignment);

    /* Bump cursor atomically so concurrent creates get non-overlapping hints */
    uintptr_t hint = atomic_fetch_add(&g_arena_cursor, capacity);

    struct mem_arena *arena = mem_heap_calloc(1, sizeof(struct mem_arena));

    if (arena == NULL)
    {
        fprintf(stderr, "%s: failed to allocate mem_arena struct\n", __func__);
        return NULL;
    }

    void *ptr = mem_os_map(hint, capacity);

    if (ptr == NULL)
    {
        fprintf(stderr, "%s: mem_os_map failed, retrying with 0 hint \n", __func__);

        ptr = mem_os_map(0, capacity);

        if (ptr == NULL)
        {
            fprintf(stderr,
                    "%s: mem_os_map failed for '%s' (%zu bytes): %s\n",
                    __func__,
                    name,
                    capacity,
                    strerror(errno));
            mem_heap_free(arena);
            return NULL;
        }
    }

    arena->name     = name;
    arena->base     = ptr;
    arena->capacity = capacity;
    arena->used     = 0;

#if defined(MEM_OS_LINUX)
    if (alignment == ARENA_PAGE_SIZE_HUGE)
    {
        madvise(arena->base, arena->capacity, MADV_HUGEPAGE);
    }
#endif

#if defined(DEBUG)
    arena->page_size = alignment;
#endif

    registry_add(arena);
    return arena;
}

void mem_arena_destroy(struct mem_arena *arena)
{
    if (arena == NULL)
    {
        return;
    }

    registry_remove(arena);

    if (arena->base)
    {
        if (!mem_os_unmap(arena->base, arena->capacity))
        {
            fprintf(
                stderr, "%s: unmap failed for '%s': %s\n", __func__, arena->name, strerror(errno));
        }
        arena->base = NULL;
    }

    mem_heap_free(arena);
}

/* -------------------------
 * Allocation
 * ------------------------- */
void *mem_arena_alloc_aligned(struct mem_arena *arena, size_t size, size_t align)
{
    if (arena == NULL || size == 0)
    {
        return NULL;
    }

    assert((align & (align - 1)) == 0 && "align must be a power of two");

    if (align < 16)
    {
        align = 16;
    }

    size_t start = (arena->used + align - 1) & ~(align - 1);

    if (start + size > arena->capacity)
    {
        fprintf(stderr,
                "%s: arena '%s' OOM (requested %zu, used %zu/%zu)\n",
                __func__,
                arena->name,
                size,
                arena->used,
                arena->capacity);
        return NULL;
    }

    void *ptr   = (char *) arena->base + start;
    arena->used = start + size;

#if defined(DEBUG)
    arena->total_allocs++;
    if (size > arena->largest_alloc)
    {
        arena->largest_alloc = size;
    }
    if (arena->used > arena->peak)
    {
        arena->peak = arena->used;
    }
#endif

    return ptr;
}

void *mem_arena_alloc(struct mem_arena *arena, size_t size)
{
    return mem_arena_alloc_aligned(arena, size, 16);
}

void *mem_arena_zalloc(struct mem_arena *arena, size_t size)
{
    void *ptr = mem_arena_alloc_aligned(arena, size, 16);
    if (ptr != nullptr)
    {
        memset(ptr, 0, size);
    }
    return ptr;
}

/* -------------------------
 * Reset
 * ------------------------- */
void mem_arena_soft_reset(struct mem_arena *arena)
{
    if (arena == NULL)
    {
        return;
    }

    arena->used = 0;
#if defined(DEBUG)
    arena->total_allocs = 0;
#endif
}

void mem_arena_hard_reset(struct mem_arena *arena)
{
    if (arena == NULL || arena->used == 0)
    {
        return;
    }

    mem_os_release_pages(arena->base, arena->used);

    arena->used = 0;
#if defined(DEBUG)
    arena->total_allocs = 0;
#endif
}

/* -------------------------
 * Getters
 * ------------------------- */
void *mem_arena_get_base(const struct mem_arena *arena)
{
    return arena ? arena->base : NULL;
}

const char *mem_arena_get_name(const struct mem_arena *arena)
{
    return arena ? arena->name : NULL;
}

size_t mem_arena_get_used(const struct mem_arena *arena)
{
    return arena ? arena->used : 0;
}

size_t mem_arena_get_capacity(const struct mem_arena *arena)
{
    return arena ? arena->capacity : 0;
}

#if defined(DEBUG)
size_t mem_arena_get_peak(const struct mem_arena *arena)
{
    return arena ? arena->peak : 0;
}

size_t mem_arena_get_alloc_count(const struct mem_arena *arena)
{
    return arena ? arena->total_allocs : 0;
}
#endif

/* -------------------------
 * Registry
 * ------------------------- */
size_t mem_arena_registry_count(void)
{
    return g_registry.count;
}

struct mem_arena *mem_arena_registry_get(size_t i)
{
    return (i < g_registry.count) ? g_registry.arenas[i] : NULL;
}

/* -------------------------
 * Stats / logging
 * ------------------------- */
void mem_arena_log_stats(const struct mem_arena *arena)
{
    if (arena == NULL)
    {
        return;
    }

    double used_mb = (double) arena->used / (1024.0 * 1024.0);
    double cap_mb  = (double) arena->capacity / (1024.0 * 1024.0);

    printf("\n=== mem_arena [%s: %p] ===\n", arena->name, arena->base);
    printf("  Used     : %-10zu (%.2f MB / %.2f MB)\n", arena->used, used_mb, cap_mb);

#if defined(DEBUG)
    double peak_mb = (double) arena->peak / (1024.0 * 1024.0);
    size_t ps      = arena->page_size;

    printf("  Peak     : %-10zu (%.2f MB)\n", arena->peak, peak_mb);
    printf("  Allocs   : %-10zu\n", arena->total_allocs);
    printf("  Largest  : %-10zu bytes\n", arena->largest_alloc);
    printf("  Page size: %zu\n", ps);
    printf("  Pages    : %zu / %zu\n", (arena->used + ps - 1) / ps, arena->capacity / ps);
#endif

    printf("=================================================================\n");
}

void mem_arena_log_all_stats(void)
{
    registry_lock();
    for (size_t i = 0; i < g_registry.count; i++)
    {
        mem_arena_log_stats(g_registry.arenas[i]);
    }
    registry_unlock();
}
