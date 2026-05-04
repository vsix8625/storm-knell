#pragma GCC system_header
#ifndef MEM_H_
#define MEM_H_

/*
 * mem — unified cross-platform memory library
 *
 * Two layers:
 *   mem_arena — mmap/VirtualAlloc backed bump-pointer arenas
 *   mem_heap  — tracked heap allocator with scoped contexts and tags
 *
 * Lifecycle:
 *   mem_init()     — initialises both layers (heap first, then arena)
 *   mem_shutdown() — destroys all arenas, then the heap
 *
 * Usage:
 *   #include "mem.h"   <- this is all you need
 */

#include "mem_arena.h"
#include "mem_heap.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
     * `mem_arena_init` calls `mem_heap_init` internally.
     * Call this one entry point for both.
     */
    static inline bool mem_init(void)
    {
        return mem_arena_init();
    }

    /*
     * `mem_arena_shutdown` destroys all arenas then calls `mem_heap_shutdown`.
     */
    static inline void mem_shutdown(void)
    {
        mem_arena_shutdown();
    }

#ifdef __cplusplus
}
#endif

#endif /* MEM_H_ */
