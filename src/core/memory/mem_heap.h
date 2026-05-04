#ifndef MEM_HEAP_H_
#define MEM_HEAP_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct mem_heap_ctx mem_heap_ctx;

    typedef struct mem_heap_stats
    {
        size_t total_bytes;
        size_t peak_bytes;
        size_t active_count;
        size_t total_allocs;
        size_t total_frees;
        size_t largest_alloc;
    } mem_heap_stats;

    /* -------------------------
     * Context management
     * ------------------------- */
    mem_heap_ctx *mem_heap_create_ctx(const char *name, bool thread_safe);
    void          mem_heap_destroy_ctx(mem_heap_ctx *ctx);
    size_t        mem_heap_ctx_total_bytes(const mem_heap_ctx *ctx);
    size_t        mem_heap_ctx_peak_bytes(const mem_heap_ctx *ctx);
    const char   *mem_heap_ctx_name(const mem_heap_ctx *ctx);

    /* -------------------------
     * Global ctx
     * ------------------------- */
    bool mem_heap_init(void);
    void mem_heap_shutdown(void);
    /* free all global-ctx allocations matching tag */
    void mem_heap_destroy_tag(const char *tag);

    /* -------------------------
     * Allocations — global ctx
     * ------------------------- */
    void *mem_heap_malloc(size_t size);
    void *mem_heap_calloc(size_t n, size_t size);
    void *mem_heap_realloc(void *ptr, size_t size);
    void  mem_heap_free(void *ptr);
    char *mem_heap_strdup(const char *s);

    /* -------------------------
     * Allocations — scoped ctx
     * ------------------------- */
    void *mem_heap_malloc_ctx(mem_heap_ctx *ctx, size_t size, const char *tag);
    void *mem_heap_calloc_ctx(mem_heap_ctx *ctx, size_t n, size_t size, const char *tag);
    void *mem_heap_realloc_ctx(mem_heap_ctx *ctx, void *ptr, size_t size);
    void  mem_heap_free_ctx(mem_heap_ctx *ctx, void *ptr);
    char *mem_heap_strdup_ctx(mem_heap_ctx *ctx, const char *s, const char *tag);

    /* -------------------------
     * Collection
     * ------------------------- */
    void mem_heap_collect_all(mem_heap_ctx *ctx);
    void mem_heap_collect_tag(mem_heap_ctx *ctx, const char *tag);

    /* -------------------------
     * Debug / Stats
     * ------------------------- */
    mem_heap_stats mem_heap_get_stats(mem_heap_ctx *ctx);
    void           mem_heap_print_stats(const mem_heap_ctx *ctx);
    void           mem_heap_dump(const mem_heap_ctx *ctx);
    void           mem_heap_dump_file(const mem_heap_ctx *ctx, const char *filename);
    /* dump global ctx to "mem_heap_global_dump.txt" */
    void mem_heap_dump_global_file(void);

#ifdef __cplusplus
}
#endif

#endif /* MEM_HEAP_H_ */
