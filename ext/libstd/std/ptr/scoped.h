#pragma once

namespace stl {
    template <typename T>
    struct ScopedPtr {
        T* ptr;

        void drop() noexcept {
            ptr = 0;
        }

        ~ScopedPtr() {
            delete ptr;
        }

        auto operator->() noexcept {
            return ptr;
        }

        auto operator->() const noexcept {
            return ptr;
        }
    };
}
