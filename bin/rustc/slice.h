#pragma once

#include <stddef.h>

template <typename T>
class slice {
    T* ptr;
    size_t len;

public:
    slice(T* ptr, size_t len)
        : ptr(ptr)
        , len(len)
    {
    }

    T* begin() {
        return ptr;
    }

    T* end() {
        return ptr + len;
    }

    const T* begin() const {
        return ptr;
    }

    const T* end() const {
        return ptr + len;
    }
};
