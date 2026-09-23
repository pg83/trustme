#pragma once

#include <cstddef>

/* Declared exactly as musl's <stdlib.h> and <malloc.h> declare them, without
   an exception specification: a libc built with _BSD_SOURCE on by default
   declares valloc itself, and the two declarations must agree. */
extern "C" {
    void* memalign(size_t align, size_t n);
    void* valloc(size_t n);
    void* pvalloc(size_t n);
    size_t malloc_usable_size(void* p);
}
