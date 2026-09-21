#include <sys/mman.h>
#include <unistd.h>
#include <malloc.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#if !TRUSTME_SANITIZER_BUILD && !defined(__SANITIZE_ADDRESS__)

namespace {
    constexpr uintptr_t REGION_BASE = uintptr_t(0x2000) << 32;
    constexpr size_t SEGMENT_SHIFT = 34;
    constexpr size_t SEGMENT = size_t(1) << SEGMENT_SHIFT;
    constexpr size_t CHUNK = size_t(2) << 20;
    constexpr size_t SMALL_LIMIT = 262144;
    constexpr size_t CLASSES = 56;
    constexpr size_t LARGE_HEADER = 16;
    constexpr size_t PAGE = 4096;

    struct FreeBlock {
        FreeBlock* next;
    };

    struct ClassState {
        uintptr_t cursor;
        uintptr_t mapped;
        FreeBlock* freeList;
    };

    struct LargeHeader {
        void* map;
        size_t length;
    };

    ClassState classes[CLASSES];

    [[noreturn]] void refuse(const char* what) {
        const char* prefix = "trustme: allocator: ";
        (void)!write(2, prefix, strlen(prefix));
        (void)!write(2, what, strlen(what));
        (void)!write(2, "\n", 1);
        abort();
    }

    size_t classIndex(size_t n) {
        if (n <= 256) {
            return n == 0 ? 0 : (n + 15) / 16 - 1;
        }
        const unsigned exponent = 63 - __builtin_clzll(n - 1);
        const size_t base = size_t(1) << exponent;
        const size_t quarter = base / 4;
        const size_t step = (n - base + quarter - 1) / quarter - 1;
        return 16 + (exponent - 8) * 4 + step;
    }

    size_t classSize(size_t index) {
        if (index < 16) {
            return (index + 1) * 16;
        }
        const size_t base = size_t(1) << (8 + (index - 16) / 4);
        return base + (base / 4) * ((index - 16) % 4 + 1);
    }

    uintptr_t segmentBase(size_t index) {
        return REGION_BASE + (uintptr_t(index) << SEGMENT_SHIFT);
    }

    void* carve(size_t index) {
        auto& state = classes[index];
        const size_t size = classSize(index);
        if (state.cursor == 0) {
            state.cursor = segmentBase(index);
            state.mapped = state.cursor;
        }
        while (state.cursor + size > state.mapped) {
            if (state.mapped + CHUNK > segmentBase(index) + SEGMENT) {
                refuse("size class segment exhausted");
            }
            void* wanted = reinterpret_cast<void*>(state.mapped);
            void* got = mmap(wanted, CHUNK, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            if (got != wanted) {
                refuse("fixed mapping refused");
            }
            state.mapped += CHUNK;
        }
        void* block = reinterpret_cast<void*>(state.cursor);
        state.cursor += size;
        return block;
    }

    void* allocateSmall(size_t index) {
        auto& state = classes[index];
        if (auto* block = state.freeList) {
            state.freeList = block->next;
            return block;
        }
        return carve(index);
    }

    void* allocateLarge(size_t n, size_t align) {
        const size_t slack = align > LARGE_HEADER ? align : 0;
        const size_t length = (n + LARGE_HEADER + slack + PAGE - 1) & ~(PAGE - 1);
        void* map = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (map == MAP_FAILED) {
            errno = ENOMEM;
            return nullptr;
        }
        uintptr_t block = reinterpret_cast<uintptr_t>(map) + LARGE_HEADER;
        if (align > LARGE_HEADER) {
            block = (block + align - 1) & ~(uintptr_t(align) - 1);
        }
        auto* header = reinterpret_cast<LargeHeader*>(block - LARGE_HEADER);
        header->map = map;
        header->length = length;
        return reinterpret_cast<void*>(block);
    }

    bool inRegion(const void* p) {
        const auto address = reinterpret_cast<uintptr_t>(p);
        return address >= REGION_BASE && address < REGION_BASE + (uintptr_t(CLASSES) << SEGMENT_SHIFT);
    }

    size_t classOf(const void* p) {
        return (reinterpret_cast<uintptr_t>(p) - REGION_BASE) >> SEGMENT_SHIFT;
    }

    size_t usableSize(const void* p) {
        if (inRegion(p)) {
            return classSize(classOf(p));
        }
        const auto* header = reinterpret_cast<const LargeHeader*>(static_cast<const char*>(p) - LARGE_HEADER);
        return header->length - (reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(header->map));
    }

    void* allocate(size_t n, size_t align) {
        if (n <= SMALL_LIMIT) {
            if (align <= 16) {
                return allocateSmall(classIndex(n));
            }
            if (align <= SMALL_LIMIT) {
                size_t index = classIndex(n < align ? align : n);
                while (index < CLASSES && classSize(index) % align != 0) {
                    index++;
                }
                if (index < CLASSES) {
                    return allocateSmall(index);
                }
            }
        }
        return allocateLarge(n, align);
    }

    void release(void* p) {
        if (inRegion(p)) {
            auto& state = classes[classOf(p)];
            auto* block = static_cast<FreeBlock*>(p);
            block->next = state.freeList;
            state.freeList = block;
            return;
        }
        auto* header = reinterpret_cast<LargeHeader*>(static_cast<char*>(p) - LARGE_HEADER);
        munmap(header->map, header->length);
    }
}

extern "C" {
    void* malloc(size_t n) noexcept {
        return allocate(n, 16);
    }

    void free(void* p) noexcept {
        if (p) {
            release(p);
        }
    }

    void* calloc(size_t count, size_t size) noexcept {
        size_t total;
        if (__builtin_mul_overflow(count, size, &total)) {
            errno = ENOMEM;
            return nullptr;
        }
        void* p = allocate(total, 16);
        if (p) {
            memset(p, 0, total);
        }
        return p;
    }

    void* realloc(void* p, size_t n) noexcept {
        if (!p) {
            return allocate(n, 16);
        }
        if (n == 0) {
            release(p);
            return nullptr;
        }
        const size_t old = usableSize(p);
        if (n <= old) {
            return p;
        }
        void* fresh = allocate(n, 16);
        if (!fresh) {
            return nullptr;
        }
        memcpy(fresh, p, old);
        release(p);
        return fresh;
    }

    void* memalign(size_t align, size_t n) noexcept {
        return allocate(n, align < 16 ? 16 : align);
    }

    void* aligned_alloc(size_t align, size_t n) noexcept {
        return memalign(align, n);
    }

    int posix_memalign(void** out, size_t align, size_t n) noexcept {
        void* p = memalign(align, n);
        if (!p) {
            return ENOMEM;
        }
        *out = p;
        return 0;
    }

    void* valloc(size_t n) noexcept {
        return memalign(PAGE, n);
    }

    void* pvalloc(size_t n) noexcept {
        return memalign(PAGE, (n + PAGE - 1) & ~(PAGE - 1));
    }

    size_t malloc_usable_size(void* p) noexcept {
        return p ? usableSize(p) : 0;
    }
}

#endif
