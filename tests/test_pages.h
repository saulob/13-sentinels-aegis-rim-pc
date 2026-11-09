/* Writable pages for the hook tests, executable when asked: VirtualAlloc on Windows, mmap elsewhere. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>

static uint8_t *test_pages(size_t size, int executable) {
    return VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT, executable ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
}
static void test_pages_free(uint8_t *pages, size_t size) {
    (void)size;
    VirtualFree(pages, 0, MEM_RELEASE);
}
#else
#include <sys/mman.h>

static uint8_t *test_pages(size_t size, int executable) {
    void *pages = mmap(NULL, size, PROT_READ | PROT_WRITE | (executable ? PROT_EXEC : 0), MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return pages == MAP_FAILED ? NULL : pages;
}
static void test_pages_free(uint8_t *pages, size_t size) { munmap(pages, size); }
#endif
