#pragma GCC system_header
#ifndef OS_LAYER_H_
#define OS_LAYER_H_

#if !defined(_WIN32) && !defined(_WIN64) && !defined(__APPLE__)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(_WIN32) || defined(_WIN64)
#define MEM_OS_WIN32
#include <windows.h>
#elif defined(__APPLE__)
#define MEM_OS_MACOS
#include <sys/mman.h>
#include <unistd.h>
#else
#define MEM_OS_LINUX
#include <sys/mman.h>
#include <unistd.h>
#endif

/* Virtual memory */
size_t mem_sys_pagesize(void);
void  *mem_os_map(uintptr_t hint, size_t capacity);
bool   mem_os_unmap(void *ptr, size_t capacity);

/*
 * mem_os_release_pages — hint to the OS that a range of memory is no longer
 * needed, without unmapping it. Used by arena hard-reset.
 *
 * Linux  : MADV_DONTNEED — pages returned to OS, re-faulted on next access
 * macOS  : MADV_FREE     — pages lazily reclaimed (lower overhead)
 * Windows: MEM_RESET     — analogous lazy reclaim via VirtualAlloc
 */
void mem_os_release_pages(void *ptr, size_t size);

#endif /* OS_LAYER_H_ */
