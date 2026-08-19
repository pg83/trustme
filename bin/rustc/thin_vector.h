#pragma once

// A thin vector type (single-pointer) that cannot resize past its capacity
// without reallocating, and stores the length/capacity in the pointed-to
// memory. Used for HIR structures to save significant amounts of memory.
//
// NOTE: `common.h` must NOT include this header; the dependency is one-way
// (`thin_vector.h` -> `common.h`, for `Ordering`/`ord`). Include this header
// directly from the (few) translation units that need `ThinVector`.

#include "common.h" // Ordering, ::ord

#include <vector>
#include <ostream>
#include <stdexcept>
#include <cstdlib>
#include <cassert>
#include <new>
#include <utility>

template <typename T>
class ThinVector {
    struct Meta {
        size_t len;
        size_t cap;
    };

    T* ptr;

public:
    ~ThinVector() {
        if (ptr) {
            auto* m = meta();
            auto len = m->len;
            m->len = 0;
            for (size_t i = 0; i < len; i++) {
                ptr[i].~T();
            }
            free(m);
            ptr = nullptr;
        }
    }

    ThinVector()
        : ptr(nullptr)
    {
    }

    explicit ThinVector(size_t len)
        : ptr(nullptr)
    {
        if (len > 0) {
            this->reserveInit(len);
            auto* meta = this->meta();
            for (size_t i = 0; i < len; i++) {
                new (&ptr[i]) T;
                meta->len++;
            }
        }
    }

    ThinVector(const T* begin, const T* end)
        : ptr(nullptr)
    {
        if (begin != end) {
            this->reserveInit(end - begin);
            auto* meta = this->meta();
            for (auto it = begin; it != end; ++it) {
                new (&ptr[meta->len]) T(*it);
                meta->len++;
            }
        }
    }

    explicit ThinVector(const std::vector<T>& x)
        : ThinVector(x.data(), x.data() + x.size())
    {
    }

    ThinVector(const ThinVector& x)
        : ThinVector(x.data(), x.data() + x.size())
    {
    }

    ThinVector(ThinVector&& x)
        : ptr(x.ptr)
    {
        x.ptr = nullptr;
    }

    ThinVector& operator=(const ThinVector& x) {
        this->~ThinVector();
        new (this) ThinVector(x);
        return *this;
    }

    ThinVector& operator=(ThinVector&& x) {
        this->~ThinVector();
        this->ptr = x.ptr;
        x.ptr = nullptr;
        return *this;
    }

    void reserve(size_t newCap) {
        if (newCap > this->capacity()) {
            auto saved = std::move(*this);
            this->reserveInit(newCap);
            for (auto& v : saved) {
                this->push_back(std::move(v));
            }
        }
    }

    void resize(size_t len) {
        this->reserve(len);
        auto* m = this->meta();
        if (m) {
            while (m->len > len) {
                m->len--;
                ptr[m->len].~T();
            }
            while (m->len < len) {
                assert(meta() == m);
                this->push_back(T());
            }
        }
    }

    void reserveInit(size_t cap) {
        if (ptr) {
            throw std::runtime_error("Initialising an initialised ThinVector");
        }
        if (cap > 0) {
            auto* p = static_cast<T*>(malloc(sizeof(T) * (cap + metadataLen())));
            if (!p) {
                throw ::std::bad_alloc();
            }
            auto* meta = (Meta*)p;
            ptr = p + metadataLen();
            meta->cap = cap;
            meta->len = 0;
        }
    }

    template <typename... Args>
    void emplace_back(Args&&... v) {
        auto len = size();
        if (!meta() || meta()->cap == 0) {
            this->reserve(2);
        } else if (len >= meta()->cap) {
            this->reserve((len + 1) * 3 / 2);
        }
        new (&ptr[len]) T(std::move(v)...);
        this->meta()->len++;
    }

    void push_back(T v) {
        this->emplace_back(std::move(v));
    }

    void pop_back() {
        auto* m = this->meta();
        if (m && m->len > 0) {
            m->len--;
            ptr[m->len].~T();
        }
    }

    /// Destroys the elements but keeps the capacity.
    void clear() {
        auto* m = this->meta();
        if (m) {
            for (size_t i = 0; i < m->len; i++) {
                ptr[i].~T();
            }
            m->len = 0;
        }
    }

    const T& front() const {
        if (this->size() == 0) {
            throw std::out_of_range("ThinVector::front");
        }
        return *ptr;
    }

    T& front() {
        if (this->size() == 0) {
            throw std::out_of_range("ThinVector::front");
        }
        return *ptr;
    }

    const T& back() const {
        if (this->size() == 0) {
            throw std::out_of_range("ThinVector::back");
        }
        return ptr[size() - 1];
    }

    T& back() {
        if (this->size() == 0) {
            throw std::out_of_range("ThinVector::back");
        }
        return ptr[size() - 1];
    }

    const T* begin() const {
        return ptr;
    }

    T* begin() {
        return ptr;
    }

    const T* end() const {
        return ptr + size();
    }

    T* end() {
        return ptr + size();
    }

    const T& operator[](size_t i) const {
        return ptr[i];
    }

    T& operator[](size_t i) {
        return ptr[i];
    }

    const T& at(size_t i) const {
        if (i >= this->size()) {
            throw std::out_of_range("ThinVector::at");
        }
        return ptr[i];
    }

    T& at(size_t i) {
        if (i >= this->size()) {
            throw std::out_of_range("ThinVector::at");
        }
        return ptr[i];
    }

    const T* data() const {
        return ptr;
    }

    T* data() {
        return ptr;
    }

    size_t size() const {
        if (ptr) {
            return meta()->len;
        } else {
            return 0;
        }
    }

    size_t capacity() const {
        if (ptr) {
            return meta()->cap;
        } else {
            return 0;
        }
    }

    bool empty() const {
        return ptr == nullptr;
    }

    Ordering ord(const ThinVector<T>& x) const {
        size_t cmpLen = this->size();
        if (cmpLen > x.size()) {
            cmpLen = x.size();
        }
        for (size_t i = 0; i < cmpLen; i++) {
            auto rv = ::ord((*this)[i], x[i]);
            if (rv != OrdEqual) {
                return rv;
            }
        }

        // Longer lists sort afer shorter ones
        if (this->size() < x.size()) {
            return OrdLess;
        } else if (this->size() > x.size()) {
            return OrdGreater;
        } else {
            return OrdEqual;
        }
    }

private:
    static size_t metadataLen() {
        return (sizeof(Meta) + sizeof(T) - 1) / sizeof(T);
    }

    const Meta* meta() const {
        return ptr ? (const Meta*)(ptr - metadataLen()) : nullptr;
    }

    Meta* meta() {
        return ptr ? (Meta*)(ptr - metadataLen()) : nullptr;
    }
};

template <typename T>
inline ::std::ostream& operator<<(::std::ostream& os, const ThinVector<T>& v) {
    if (v.size() > 0) {
        bool isFirst = true;
        for (const auto& i : v) {
            if (!isFirst) {
                os << ", ";
            }
            isFirst = false;
            os << i;
        }
    }
    return os;
}
