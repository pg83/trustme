#pragma once

#include <std/sys/types.h>

namespace stl {
    class Buffer;

    class StringView {
        const u8* ptr_;
        size_t len_;

    public:
        StringView() noexcept
            : StringView(nullptr, (size_t)0) {
        }

        template <size_t N>
        StringView(const u8 (&str)[N]) noexcept
            : StringView(str, N - 1) {
        }

        StringView(const u8* ptr, size_t len) noexcept
            : ptr_(ptr)
            , len_(len) {
        }

        StringView(const u8* b, const u8* e) noexcept
            : StringView(b, e - b) {
        }

        StringView(const char* s) noexcept;
        StringView(const Buffer& b) noexcept;

        auto data() const noexcept {
            return ptr_;
        }

        auto length() const noexcept {
            return len_;
        }

        auto begin() const noexcept {
            return data();
        }

        auto end() const noexcept {
            return begin() + length();
        }

        const auto& operator[](size_t i) const noexcept {
            return *(begin() + i);
        }

        bool empty() const noexcept {
            return length() == 0;
        }

        const auto& back() const noexcept {
            return *(end() - 1);
        }

        StringView prefix(size_t len) const noexcept;
        StringView suffix(size_t len) const noexcept;

        bool endsWith(StringView suffix) const noexcept;
        bool startsWith(StringView prefix) const noexcept;

        const u8* memChr(u8 ch) const noexcept;
        const u8* search(StringView substr) const noexcept;

        u32 hash32() const noexcept;
        u64 hash64() const noexcept;

        StringView stripSpace() const noexcept;
        StringView stripCr() const noexcept;

        bool split(u8 delim, StringView& before, StringView& after) const noexcept;

        StringView lower(u8* buffer) const noexcept;
        StringView lower(Buffer& buffer) const noexcept;

        u64 stou() const noexcept;
        u64 stoh() const noexcept;
    };

    bool operator==(StringView l, StringView r) noexcept;
    bool operator!=(StringView l, StringView r) noexcept;
    bool operator<(StringView l, StringView r) noexcept;
}
