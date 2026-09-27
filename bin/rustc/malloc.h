#pragma once

#include <cstddef>
#include <cstdlib>

/* Declared exactly as the libc's <stdlib.h> and <malloc.h> declare them: a
   libc built with _BSD_SOURCE on by default declares valloc itself, and the
   two declarations must agree. glibc marks them __THROW, noexcept in C++;
   musl has no __THROW and no exception specification. */
#ifndef __THROW
#define __THROW
#endif

extern "C" {
    void* memalign(size_t align, size_t n) __THROW;
    void* valloc(size_t n) __THROW;
    void* pvalloc(size_t n) __THROW;
    size_t malloc_usable_size(void* p) __THROW;
}
