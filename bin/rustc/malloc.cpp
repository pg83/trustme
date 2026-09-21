#include "malloc.h"

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>  // escape: std::nothrow_t is the signature of the nothrow operator new the compiler imports

#if !TRUSTME_SANITIZER_BUILD && !defined(__SANITIZE_ADDRESS__)

namespace {
    constexpr uintptr_t REGION_BASE = uintptr_t(0x2000) << 32;
    constexpr size_t SEGMENT_SHIFT = 21;
    constexpr size_t SEGMENT = size_t(1) << SEGMENT_SHIFT;
    constexpr size_t PAGE_SHIFT = 16;
    constexpr size_t PAGE = size_t(1) << PAGE_SHIFT;
    constexpr size_t PAGES_PER_SEGMENT = SEGMENT / PAGE;
    constexpr size_t REGION_SEGMENTS = size_t(1) << 15;
    constexpr size_t META_SEGMENTS = 17;
    constexpr size_t HEAP_SEGMENTS = REGION_SEGMENTS - META_SEGMENTS;
    constexpr uintptr_t HEAP_BASE = REGION_BASE + (META_SEGMENTS << SEGMENT_SHIFT);
    constexpr size_t HEAP_BYTES = HEAP_SEGMENTS << SEGMENT_SHIFT;
    constexpr size_t SMALL_LIMIT = 16384;
    constexpr size_t RUN_LIMIT = size_t(1) << 20;
    constexpr size_t CLASSES = 40;
    constexpr uint16_t FREE_PAGE = 0xFFFF;
    constexpr uint16_t RUN_PAGE = 0xFFFE;
    constexpr uint32_t NO_SEGMENT = 0;
    constexpr size_t LARGE_HEADER = 16;
    constexpr size_t OS_PAGE = 4096;

    struct FreeSlot {
        FreeSlot* next;
    };

    struct Page {
        FreeSlot* freeList;
        Page* prev;
        Page* next;
        uint32_t live;
        uint16_t cls;
        uint16_t run;
    };

    struct Segment {
        uint32_t freeMask;
        uint32_t prev;
        uint32_t next;
        uint32_t linked;
    };

    struct ClassState {
        FreeSlot* freeList;
        uintptr_t bump;
        uintptr_t end;
        size_t size;
        Page* current;
        Page* partial;
    };

    struct LargeHeader {
        void* map;
        size_t length;
    };

    struct State {
        uint32_t mapped;
        uint32_t metaMapped;
        uint32_t freeSegments;
    };

    constexpr uintptr_t SEGMENT_TABLE = REGION_BASE;
    constexpr size_t SEGMENT_TABLE_BYTES = REGION_SEGMENTS * sizeof(Segment);
    constexpr uintptr_t PAGE_TABLE = SEGMENT_TABLE + SEGMENT_TABLE_BYTES;
    constexpr size_t PAGE_TABLE_BYTES = HEAP_SEGMENTS * PAGES_PER_SEGMENT * sizeof(Page);
    static_assert(PAGE_TABLE + PAGE_TABLE_BYTES <= HEAP_BASE);

    ClassState classes[CLASSES];
    State state;

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
        return 16 + (exponent - 8) * 4 + (((n - 1) >> (exponent - 2)) & 3);
    }

    size_t classSize(size_t index) {
        if (index < 16) {
            return (index + 1) * 16;
        }
        const size_t base = size_t(1) << (8 + (index - 16) / 4);
        return base + (base / 4) * ((index - 16) % 4 + 1);
    }

    bool owns(const void* p) {
        return reinterpret_cast<uintptr_t>(p) - HEAP_BASE < HEAP_BYTES;
    }

    Segment* segmentAt(uint32_t index) {
        return reinterpret_cast<Segment*>(SEGMENT_TABLE) + index;
    }

    Page* pageAt(size_t index) {
        return reinterpret_cast<Page*>(PAGE_TABLE) + index;
    }

    Page* pageOf(const void* p) {
        return pageAt((reinterpret_cast<uintptr_t>(p) - HEAP_BASE) >> PAGE_SHIFT);
    }

    uintptr_t pageAddress(const Page* page) {
        return HEAP_BASE + (uintptr_t(page - pageAt(0)) << PAGE_SHIFT);
    }

    void linkSegment(uint32_t index) {
        auto* segment = segmentAt(index);
        segment->linked = 1;
        segment->prev = NO_SEGMENT;
        segment->next = state.freeSegments;
        if (state.freeSegments != NO_SEGMENT) {
            segmentAt(state.freeSegments)->prev = index;
        }
        state.freeSegments = index;
    }

    void unlinkSegment(uint32_t index) {
        auto* segment = segmentAt(index);
        segment->linked = 0;
        if (segment->prev != NO_SEGMENT) {
            segmentAt(segment->prev)->next = segment->next;
        } else {
            state.freeSegments = segment->next;
        }
        if (segment->next != NO_SEGMENT) {
            segmentAt(segment->next)->prev = segment->prev;
        }
    }

    void mapFixed(uintptr_t address) {
        void* wanted = reinterpret_cast<void*>(address);
        void* got = mmap(wanted, SEGMENT, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
        if (got != wanted) {
            refuse("fixed mapping refused");
        }
        madvise(got, SEGMENT, MADV_HUGEPAGE);
    }

    void mapSegment() {
        if (state.mapped == HEAP_SEGMENTS) {
            refuse("region exhausted");
        }
        const size_t tableEnd = PAGE_TABLE + size_t(state.mapped + 1) * PAGES_PER_SEGMENT * sizeof(Page) - 1;
        const uint32_t metaNeeded = uint32_t((tableEnd - REGION_BASE) >> SEGMENT_SHIFT);
        while (state.metaMapped <= metaNeeded) {
            mapFixed(REGION_BASE + (uintptr_t(state.metaMapped) << SEGMENT_SHIFT));
            state.metaMapped++;
        }
        const uint32_t index = uint32_t(META_SEGMENTS + state.mapped);
        mapFixed(REGION_BASE + (uintptr_t(index) << SEGMENT_SHIFT));
        segmentAt(index)->freeMask = 0xFFFFFFFF;
        linkSegment(index);
        state.mapped++;
    }

    Page* takePages(size_t count) {
        const uint32_t needed = count == 32 ? 0xFFFFFFFF : (uint32_t(1) << count) - 1;
        for (;;) {
            unsigned hops = 0;
            for (uint32_t index = state.freeSegments; index != NO_SEGMENT && hops < 8; index = segmentAt(index)->next, hops++) {
                auto* segment = segmentAt(index);
                uint32_t runs = segment->freeMask;
                for (size_t i = 1; i < count; i++) {
                    runs &= runs >> 1;
                }
                if (runs == 0) {
                    continue;
                }
                const unsigned start = __builtin_ctz(runs);
                segment->freeMask &= ~(needed << start);
                if (segment->freeMask == 0) {
                    unlinkSegment(index);
                }
                Page* page = pageAt(size_t(index - META_SEGMENTS) * PAGES_PER_SEGMENT + start);
                page->freeList = nullptr;
                page->live = 0;
                page->run = uint16_t(count);
                return page;
            }
            mapSegment();
        }
    }

    void releasePages(Page* page, size_t count) {
        const size_t pageIndex = size_t(page - pageAt(0));
        const uint32_t index = uint32_t(META_SEGMENTS + pageIndex / PAGES_PER_SEGMENT);
        const unsigned start = unsigned(pageIndex % PAGES_PER_SEGMENT);
        const uint32_t bits = count == 32 ? 0xFFFFFFFF : (uint32_t(1) << count) - 1;
        auto* segment = segmentAt(index);
        page->cls = FREE_PAGE;
        segment->freeMask |= bits << start;
        if (!segment->linked) {
            linkSegment(index);
        }
    }

    void* refillClass(size_t index) {
        auto& state = classes[index];
        const size_t size = classSize(index);
        if (auto* full = state.current) {
            full->live = uint32_t(PAGE / size);
        }
        if (auto* page = state.partial) {
            state.partial = page->next;
            if (page->next) {
                page->next->prev = nullptr;
            }
            state.current = page;
            auto* slot = page->freeList;
            page->freeList = nullptr;
            state.freeList = slot->next;
            state.bump = state.end = 0;
            return slot;
        }
        auto* page = takePages(1);
        page->cls = uint16_t(index);
        state.current = page;
        state.freeList = nullptr;
        state.size = size;
        state.bump = pageAddress(page) + size;
        state.end = pageAddress(page) + (PAGE / size) * size;
        return reinterpret_cast<void*>(pageAddress(page));
    }

    void* allocateRun(size_t n) {
        const size_t count = (n + PAGE - 1) >> PAGE_SHIFT;
        auto* page = takePages(count);
        page->cls = RUN_PAGE;
        return reinterpret_cast<void*>(pageAddress(page));
    }

    void* allocateLarge(size_t n, size_t align) {
        const size_t slack = align > LARGE_HEADER ? align : 0;
        const size_t length = (n + LARGE_HEADER + slack + OS_PAGE - 1) & ~(OS_PAGE - 1);
        void* map = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (map == MAP_FAILED) {
            errno = ENOMEM;
            return nullptr;
        }
        madvise(map, length, MADV_HUGEPAGE);
        uintptr_t block = reinterpret_cast<uintptr_t>(map) + LARGE_HEADER;
        if (align > LARGE_HEADER) {
            block = (block + align - 1) & ~(uintptr_t(align) - 1);
        }
        auto* header = reinterpret_cast<LargeHeader*>(block - LARGE_HEADER);
        header->map = map;
        header->length = length;
        return reinterpret_cast<void*>(block);
    }

    [[gnu::noinline]] void* allocateSlow(size_t n, size_t align) {
        if (align <= 16) {
            if (n <= SMALL_LIMIT) {
                return refillClass(classIndex(n));
            }
            if (n <= RUN_LIMIT) {
                return allocateRun(n);
            }
            return allocateLarge(n, align);
        }
        if (n <= SMALL_LIMIT && align <= SMALL_LIMIT) {
            size_t index = classIndex(n < align ? align : n);
            while (index < CLASSES && classSize(index) % align != 0) {
                index++;
            }
            if (index < CLASSES) {
                auto& state = classes[index];
                if (auto* slot = state.freeList) {
                    state.freeList = slot->next;
                    return slot;
                }
                if (state.bump < state.end) {
                    void* block = reinterpret_cast<void*>(state.bump);
                    state.bump += state.size;
                    return block;
                }
                return refillClass(index);
            }
        }
        if (align <= PAGE && n <= RUN_LIMIT) {
            return allocateRun(n);
        }
        return allocateLarge(n, align);
    }

    void* allocate(size_t n, size_t align) {
        if (n <= SMALL_LIMIT && align <= 16) {
            auto& state = classes[classIndex(n)];
            if (auto* slot = state.freeList) {
                state.freeList = slot->next;
                return slot;
            }
            if (state.bump < state.end) {
                void* block = reinterpret_cast<void*>(state.bump);
                state.bump += state.size;
                return block;
            }
        }
        return allocateSlow(n, align);
    }

    void* allocateOrDie(size_t n, size_t align) {
        void* p = allocate(n, align);
        if (!p) {
            refuse("out of memory");
        }
        return p;
    }

    [[gnu::noinline]] void releaseSlow(void* p, Page* page) {
        if (!page) {
            auto* header = reinterpret_cast<LargeHeader*>(static_cast<char*>(p) - LARGE_HEADER);
            munmap(header->map, header->length);
            return;
        }
        const size_t cls = page->cls;
        if (cls == RUN_PAGE) {
            releasePages(page, page->run);
            return;
        }
        if (cls >= CLASSES) {
            refuse("free of a block that is not allocated");
        }
        auto& state = classes[cls];
        auto* slot = static_cast<FreeSlot*>(p);
        if (--page->live == 0) {
            if (page->prev) {
                page->prev->next = page->next;
            } else {
                state.partial = page->next;
            }
            if (page->next) {
                page->next->prev = page->prev;
            }
            releasePages(page, 1);
            return;
        }
        slot->next = page->freeList;
        page->freeList = slot;
        if (!slot->next) {
            page->prev = nullptr;
            page->next = state.partial;
            if (state.partial) {
                state.partial->prev = page;
            }
            state.partial = page;
        }
    }

    void release(void* p) {
        if (!owns(p)) {
            releaseSlow(p, nullptr);
            return;
        }
        auto* page = pageOf(p);
        const size_t cls = page->cls;
        if (cls < CLASSES) {
            auto& state = classes[cls];
            if (page == state.current) {
                auto* slot = static_cast<FreeSlot*>(p);
                slot->next = state.freeList;
                state.freeList = slot;
                return;
            }
        }
        releaseSlow(p, page);
    }

    size_t usableSize(const void* p) {
        if (!owns(p)) {
            const auto* header = reinterpret_cast<const LargeHeader*>(static_cast<const char*>(p) - LARGE_HEADER);
            return header->length - (reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(header->map));
        }
        const auto* page = pageOf(p);
        if (page->cls == RUN_PAGE) {
            return size_t(page->run) << PAGE_SHIFT;
        }
        return classSize(page->cls);
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
        return memalign(OS_PAGE, n);
    }

    void* pvalloc(size_t n) noexcept {
        return memalign(OS_PAGE, (n + OS_PAGE - 1) & ~(OS_PAGE - 1));
    }

    size_t malloc_usable_size(void* p) noexcept {
        return p ? usableSize(p) : 0;
    }
}

void* operator new(size_t n) {
    return allocateOrDie(n, 16);
}

void* operator new(size_t n, const std::nothrow_t&) noexcept {
    return allocate(n, 16);
}

void operator delete(void* p) noexcept {
    free(p);
}

void operator delete(void* p, size_t) noexcept {
    free(p);
}

#endif
