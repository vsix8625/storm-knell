#ifndef MEM_ARENA_H_
#define MEM_ARENA_H_

#include "os_layer.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* -------------------------
     * Lifecycle
     * ------------------------- */
    bool mem_arena_init(void);
    void mem_arena_shutdown(void); /* destroys ALL registered arenas + internal heap */

    /* -------------------------
     * Arena
     * ------------------------- */
    struct mem_arena *mem_arena_create(const char *name, size_t capacity);
    void              mem_arena_destroy(struct mem_arena *arena);

    /* Allocation — bump pointer, no individual frees */
    void *mem_arena_alloc(struct mem_arena *arena, size_t size);
    void *mem_arena_alloc_aligned(struct mem_arena *arena, size_t size, size_t align);

    /*
     * soft reset — zero used counter, keep backing memory hot.
     *   Use between frames / operations that reuse the same arena.
     * hard reset — soft reset + hint to OS to reclaim physical pages.
     *   Use when the arena won't be touched for a while.
     */
    void mem_arena_soft_reset(struct mem_arena *arena);
    void mem_arena_hard_reset(struct mem_arena *arena);

    /* -------------------------
     * Getters
     * ------------------------- */
    void       *mem_arena_get_base(const struct mem_arena *arena);
    const char *mem_arena_get_name(const struct mem_arena *arena);
    size_t      mem_arena_get_used(const struct mem_arena *arena);
    size_t      mem_arena_get_capacity(const struct mem_arena *arena);

    /* -------------------------
     * Registry
     * ------------------------- */
    size_t            mem_arena_registry_count(void);
    struct mem_arena *mem_arena_registry_get(size_t i);
    void              mem_arena_log_all_stats(void);
    void              mem_arena_log_stats(const struct mem_arena *arena);

/* -------------------------
 * Debug — only available in DEBUG builds
 * ------------------------- */
#if defined(DEBUG)
    size_t mem_arena_get_peak(const struct mem_arena *arena);
    size_t mem_arena_get_alloc_count(const struct mem_arena *arena);
#endif

    /* -------------------------
     * Utilities
     * ------------------------- */
    static inline size_t mem_arena_nxtpow2(size_t v)
    {
        if (v == 0)
        {
            return 1;
        }
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v |= v >> 32;
        v++;
        return v;
    }

    static inline size_t mem_arena_align_up(size_t v, size_t align)
    {
        return (v + align - 1) & ~(align - 1);
    }

#ifdef __cplusplus
}
#endif

#endif /* MEM_ARENA_H_ */
