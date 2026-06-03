#include "sk_array.h"
#include "mem_arena.h"

#include "vx_io.h"
#include <string.h>

bool sk_arena_array_contains(struct sk_arena_array *arr, const char *path)
{
    if (arr->count == 0)
    {
        return false;
    }

    size_t path_len = strlen(path);

    for (u32 i = 0; i < arr->count; i++)
    {
        const char *item = (const char *) arr->items[i];

        if (strlen(item) != path_len)
        {
            continue;
        }

        if (strcmp(item, path) == 0)
        {
            return true;
        }
    }

    return false;
}

struct sk_arena_array *sk_arena_array_create(struct mem_arena *arena, size_t capacity)
{
    if (capacity == 0 || capacity < 32)
    {
        capacity = 32;
    }

    struct sk_arena_array *arr = mem_arena_alloc(arena, sizeof(struct sk_arena_array));

    if (arr == nullptr)
    {
        return nullptr;
    }

    arr->items = mem_arena_alloc(arena, capacity * sizeof(void *));

    if (arr->items == nullptr)
    {
        return nullptr;
    }

    arr->count = 0;
    arr->cap   = capacity;

    return arr;
}

void sk_arena_array_push(struct sk_arena_array *arr, void *item)
{
    if (arr == nullptr || item == nullptr)
    {
        return;
    }

    if (arr->count >= arr->cap)
    {
        VX_ASSERT_LOG("Arena array limit reached");
        return;
    }

    arr->items[arr->count++] = item;
}
