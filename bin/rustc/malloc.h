#pragma once

#include <cstddef>

extern "C" {
    void* memalign(size_t align, size_t n) noexcept;
    void* valloc(size_t n) noexcept;
    void* pvalloc(size_t n) noexcept;
    size_t malloc_usable_size(void* p) noexcept;
}
