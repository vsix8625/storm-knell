#include "os_layer.h"

size_t mem_sys_pagesize(void)
{
#if defined(MEM_OS_WIN32)
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (size_t) si.dwPageSize;
#else
    return (size_t) sysconf(_SC_PAGESIZE);
#endif
}

void *mem_os_map(uintptr_t hint, size_t capacity)
{
#if defined(MEM_OS_WIN32)
    void *ptr = VirtualAlloc((LPVOID) hint, capacity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return ptr ? ptr : NULL;
#else
    void *ptr =
        mmap((void *) hint, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (ptr == MAP_FAILED) ? NULL : ptr;
#endif
}

bool mem_os_unmap(void *ptr, size_t capacity)
{
    if (ptr == NULL)
    {
        return true;
    }
#if defined(MEM_OS_WIN32)
    (void) capacity;
    return VirtualFree(ptr, 0, MEM_RELEASE) != 0;
#else
    return munmap(ptr, capacity) == 0;
#endif
}

void mem_os_release_pages(void *ptr, size_t size)
{
    if (ptr == NULL || size == 0)
    {
        return;
    }
#if defined(MEM_OS_WIN32)
    /* MEM_RESET keeps the range committed but lets the OS reclaim physical pages */
    VirtualAlloc(ptr, size, MEM_RESET, PAGE_READWRITE);
#elif defined(MEM_OS_MACOS)
    madvise(ptr, size, MADV_FREE);
#else
    madvise(ptr, size, MADV_DONTNEED);
#endif
}
