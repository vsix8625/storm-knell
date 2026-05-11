#ifndef SK_ARRAY_H_
#define SK_ARRAY_H_

#include <stddef.h>

struct mem_arena;

struct sk_arena_array
{
    void **items;

    size_t count;
    size_t cap;
};

struct sk_arena_array *sk_arena_array_create(struct mem_arena *arena, size_t capacity);

void sk_arena_array_push(struct sk_arena_array *arr, void *item);

bool sk_arena_array_contains(struct sk_arena_array *arr, const char *path);

#endif  // SK_ARRAY_H_
