#include "mem_heap.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline void spinlock_lock(atomic_flag *lock)
{
    while (atomic_flag_test_and_set_explicit(lock, memory_order_acquire))
    {
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
#endif
    }
}

static inline void spinlock_unlock(atomic_flag *lock)
{
    atomic_flag_clear_explicit(lock, memory_order_release);
}

/* -------------------------
 * Internal types
 * ------------------------- */
typedef struct mem_heap_alloc_info
{
    void       *ptr;
    size_t      size;
    const char *tag;
} mem_heap_alloc_info;

struct mem_heap_ctx
{
    atomic_flag lock;

    mem_heap_alloc_info *items;
    size_t               count;
    size_t               capacity;
    const char          *name;

    size_t total_bytes;
    size_t peak_bytes;
    size_t total_allocs;
    size_t total_frees;

    bool thread_safe;
    char padding[7];
};

/* -------------------------
 * Helpers
 * ------------------------- */
static void ctx_lock(struct mem_heap_ctx *ctx)
{
    if (ctx->thread_safe)
    {
        spinlock_lock(&ctx->lock);
    }
}

static void ctx_unlock(struct mem_heap_ctx *ctx)
{
    if (ctx->thread_safe)
    {
        spinlock_unlock(&ctx->lock);
    }
}

static bool heap_grow(struct mem_heap_ctx *ctx)
{
    size_t               new_cap   = ctx->capacity ? ctx->capacity * 2 : 256;
    mem_heap_alloc_info *new_items = realloc(ctx->items, new_cap * sizeof(mem_heap_alloc_info));

    if (new_items == NULL)
    {
        return false;
    }

    ctx->items    = new_items;
    ctx->capacity = new_cap;
    return true;
}

static void track(struct mem_heap_ctx *ctx, void *ptr, size_t size, const char *tag)
{
    if (ptr == NULL)
    {
        return;
    }

    ctx_lock(ctx);

    if (ctx->count >= ctx->capacity && !heap_grow(ctx))
    {
        ctx_unlock(ctx);
        return;
    }

    ctx->items[ctx->count++] = (mem_heap_alloc_info) {.ptr = ptr, .size = size, .tag = tag};

    ctx->total_bytes += size;
    if (ctx->total_bytes > ctx->peak_bytes)
    {
        ctx->peak_bytes = ctx->total_bytes;
    }
    ctx->total_allocs++;

    ctx_unlock(ctx);
}

static void untrack(struct mem_heap_ctx *ctx, void *ptr)
{
    if (ctx == NULL || ptr == NULL)
    {
        return;
    }

    ctx_lock(ctx);

    for (size_t i = ctx->count; i-- > 0;)
    {
        if (ctx->items[i].ptr == ptr)
        {
            ctx->total_bytes -= ctx->items[i].size;
            ctx->total_frees++;
            /* swap-remove — O(1), order not significant */
            ctx->items[i] = ctx->items[--ctx->count];
            break;
        }
    }

    ctx_unlock(ctx);
}

/* -------------------------
 * Context management
 * ------------------------- */
static bool init_ctx(struct mem_heap_ctx *ctx, const char *name, bool thread_safe)
{
    if (ctx == NULL)
    {
        return false;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx->name        = name ? name : "mem_heap";
    ctx->thread_safe = thread_safe;

    return true;
}

static void reset_ctx(struct mem_heap_ctx *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    mem_heap_collect_all(ctx);
    free(ctx->items);
    ctx->items    = NULL;
    ctx->count    = 0;
    ctx->capacity = 0;
}

mem_heap_ctx *mem_heap_create_ctx(const char *name, bool thread_safe)
{
    struct mem_heap_ctx *ctx = malloc(sizeof(struct mem_heap_ctx));

    if (ctx == NULL)
    {
        return NULL;
    }

    if (!init_ctx(ctx, name, thread_safe))
    {
        free(ctx);
        return NULL;
    }

    return ctx;
}

void mem_heap_destroy_ctx(mem_heap_ctx *ctx)
{
    if (ctx == NULL)
    {
        return;
    }

    reset_ctx(ctx);
    free(ctx);
}

size_t mem_heap_ctx_total_bytes(const mem_heap_ctx *ctx)
{
    return ctx ? ctx->total_bytes : 0;
}

size_t mem_heap_ctx_peak_bytes(const mem_heap_ctx *ctx)
{
    return ctx ? ctx->peak_bytes : 0;
}

const char *mem_heap_ctx_name(const mem_heap_ctx *ctx)
{
    return ctx ? ctx->name : NULL;
}

/* -------------------------
 * Global ctx
 * ------------------------- */
static struct mem_heap_ctx g_heap_global;

bool mem_heap_init(void)
{
    return init_ctx(&g_heap_global, "global", true);
}

void mem_heap_shutdown(void)
{
    reset_ctx(&g_heap_global);
}

void mem_heap_destroy_tag(const char *tag)
{
    if (tag == NULL)
    {
        return;
    }
    mem_heap_collect_tag(&g_heap_global, tag);
}

/* -------------------------
 * Allocations — scoped ctx
 * ------------------------- */
void *mem_heap_malloc_ctx(mem_heap_ctx *ctx, size_t size, const char *tag)
{
    if (ctx == NULL)
    {
        return NULL;
    }
    if (size == 0)
    {
        size = 1;
    }

    void *p = malloc(size);

    if (p == NULL)
    {
        fprintf(stderr, "%s: failed to malloc %zu bytes\n", __func__, size);
        return NULL;
    }

    track(ctx, p, size, tag);
    return p;
}

void *mem_heap_calloc_ctx(mem_heap_ctx *ctx, size_t n, size_t size, const char *tag)
{
    if (ctx == NULL)
    {
        return NULL;
    }

    if (n == 0)
    {
        n = 1;
    }

    if (size == 0)
    {
        size = 1;
    }

    void *p = calloc(n, size);

    if (p == NULL)
    {
        fprintf(stderr, "%s: failed to calloc %zu bytes\n", __func__, n * size);
        return NULL;
    }

    track(ctx, p, n * size, tag);
    return p;
}

void *mem_heap_realloc_ctx(mem_heap_ctx *ctx, void *ptr, size_t size)
{
    if (ctx == NULL)
    {
        return NULL;
    }
    if (size == 0)
    {
        size = 1;
    }

    const char *tag      = NULL;
    size_t      old_size = 0;

    ctx_lock(ctx);
    for (size_t i = ctx->count; i-- > 0;)
    {
        if (ctx->items[i].ptr == ptr)
        {
            old_size = ctx->items[i].size;
            tag      = ctx->items[i].tag;
            break;
        }
    }
    ctx_unlock(ctx);

    void *new_ptr = mem_heap_malloc_ctx(ctx, size, tag);

    if (new_ptr == NULL)
    {
        return NULL;
    }

    if (ptr)
    {
        memcpy(new_ptr, ptr, old_size < size ? old_size : size);
        mem_heap_free_ctx(ctx, ptr);
    }

    return new_ptr;
}

void mem_heap_free_ctx(mem_heap_ctx *ctx, void *ptr)
{
    if (ptr == NULL)
    {
        return;
    }
    untrack(ctx, ptr);
    free(ptr);
}

char *mem_heap_strdup_ctx(mem_heap_ctx *ctx, const char *s, const char *tag)
{
    if (s == NULL)
    {
        return NULL;
    }
    size_t len  = strlen(s) + 1;
    char  *copy = mem_heap_malloc_ctx(ctx, len, tag);
    if (copy)
    {
        memcpy(copy, s, len);
    }
    return copy;
}

/* -------------------------
 * Allocations — global ctx
 * ------------------------- */
void *mem_heap_malloc(size_t size)
{
    return mem_heap_malloc_ctx(&g_heap_global, size, "global");
}

void *mem_heap_calloc(size_t n, size_t size)
{
    return mem_heap_calloc_ctx(&g_heap_global, n, size, "global");
}

void *mem_heap_realloc(void *ptr, size_t size)
{
    return mem_heap_realloc_ctx(&g_heap_global, ptr, size);
}

void mem_heap_free(void *ptr)
{
    mem_heap_free_ctx(&g_heap_global, ptr);
}

char *mem_heap_strdup(const char *s)
{
    return mem_heap_strdup_ctx(&g_heap_global, s, "global");
}

/* -------------------------
 * Collection
 * ------------------------- */
void mem_heap_collect_all(mem_heap_ctx *ctx)
{
    if (ctx == NULL || ctx->items == NULL)
    {
        return;
    }

    ctx_lock(ctx);

    for (size_t i = ctx->count; i-- > 0;)
    {
        if (ctx->items[i].ptr)
        {
            free(ctx->items[i].ptr);
            ctx->total_frees++;
        }
    }
    ctx->count       = 0;
    ctx->total_bytes = 0;

    ctx_unlock(ctx);
}

void mem_heap_collect_tag(mem_heap_ctx *ctx, const char *tag)
{
    if (ctx == NULL || ctx->items == NULL || tag == NULL)
    {
        return;
    }

    ctx_lock(ctx);

    for (size_t i = 0; i < ctx->count;)
    {
        mem_heap_alloc_info *info = &ctx->items[i];

        if (info->tag && strcmp(info->tag, tag) == 0)
        {
            if (info->ptr)
            {
                free(info->ptr);
                ctx->total_frees++;
            }
            ctx->total_bytes -= info->size;
            ctx->items[i]     = ctx->items[--ctx->count];
            continue;
        }
        i++;
    }

    ctx_unlock(ctx);
}

/* -------------------------
 * Debug / Stats
 * ------------------------- */
mem_heap_stats mem_heap_get_stats(mem_heap_ctx *ctx)
{
    mem_heap_stats stats = {0};

    if (ctx == NULL)
    {
        return stats;
    }

    ctx_lock(ctx);

    stats.total_bytes  = ctx->total_bytes;
    stats.peak_bytes   = ctx->peak_bytes;
    stats.total_allocs = ctx->total_allocs;
    stats.total_frees  = ctx->total_frees;

    for (size_t i = 0; i < ctx->count; i++)
    {
        if (ctx->items[i].ptr)
        {
            stats.active_count++;
            if (ctx->items[i].size > stats.largest_alloc)
            {
                stats.largest_alloc = ctx->items[i].size;
            }
        }
    }

    ctx_unlock(ctx);
    return stats;
}

void mem_heap_print_stats(const mem_heap_ctx *ctx)
{
    if (ctx == NULL)
    {
        ctx = &g_heap_global;
    }

    ctx_lock((struct mem_heap_ctx *) ctx);

    printf("\n--------------------------------------------------------------------------------\n");
    printf("mem_heap stats — ctx: %s\n", ctx->name ? ctx->name : "global");
    printf("  Active allocations : %zu\n", ctx->count);
    printf("  Active bytes       : %zu\n", ctx->total_bytes);
    printf("  Peak bytes         : %zu\n", ctx->peak_bytes);
    printf("  Total allocs       : %zu\n", ctx->total_allocs);
    printf("  Total frees        : %zu\n", ctx->total_frees);
    printf("--------------------------------------------------------------------------------\n");

    ctx_unlock((struct mem_heap_ctx *) ctx);
}

void mem_heap_dump(const mem_heap_ctx *ctx)
{
    if (ctx == NULL)
    {
        ctx = &g_heap_global;
    }

    for (size_t i = 0; i < ctx->count; i++)
    {
        const mem_heap_alloc_info *info = &ctx->items[i];
        printf("[%zu] %p (%zu bytes) tag=%s\n",
               i,
               info->ptr,
               info->size,
               info->tag ? info->tag : "N/A");
    }
}

void mem_heap_dump_file(const mem_heap_ctx *ctx, const char *filename)
{
    if (ctx == NULL || filename == NULL)
    {
        return;
    }

    FILE *fp = fopen(filename, "w");

    if (fp == NULL)
    {
        return;
    }

    for (size_t i = 0; i < ctx->count; i++)
    {
        const mem_heap_alloc_info *info = &ctx->items[i];
        fprintf(fp,
                "[%zu] %p (%zu bytes) ctx=%s tag=%s\n",
                i,
                info->ptr,
                info->size,
                ctx->name ? ctx->name : "N/A",
                info->tag ? info->tag : "N/A");
    }

    fclose(fp);
}

void mem_heap_dump_global_file(void)
{
    mem_heap_dump_file(&g_heap_global, "mem_heap_global_dump.txt");
}
