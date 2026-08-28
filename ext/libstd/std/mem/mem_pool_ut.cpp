#include "mem_pool.h"

#include <std/tst/ut.h>
#include <std/sys/types.h>

using namespace stl;

namespace {

    bool isAligned(void* ptr, size_t alignment) {
        return reinterpret_cast<uintptr_t>(ptr) % alignment == 0;
    }

    struct TestStruct {
        int a;
        int b;
        char c;
    };

    struct LargeStruct {
        char data[1024];
        int id;
    };
}

STD_TEST_SUITE(MemoryPool) {
    STD_TEST(allocate_returns_non_null) {
        MemoryPool pool;

        void* ptr = pool.allocate(64);

        STD_INSIST(ptr != nullptr);
    }

    STD_TEST(allocate_returns_aligned_pointers) {
        MemoryPool pool;

        void* ptr1 = pool.allocate(1);
        void* ptr2 = pool.allocate(16);
        void* ptr3 = pool.allocate(32);

        constexpr size_t alignment = alignof(max_align_t);
        STD_INSIST(isAligned(ptr1, alignment));
        STD_INSIST(isAligned(ptr2, alignment));
        STD_INSIST(isAligned(ptr3, alignment));
    }

    STD_TEST(allocate_different_sizes) {
        MemoryPool pool;

        void* ptr1 = pool.allocate(1);
        void* ptr2 = pool.allocate(8);
        void* ptr3 = pool.allocate(64);
        void* ptr4 = pool.allocate(1024);

        STD_INSIST(ptr1 != nullptr);
        STD_INSIST(ptr2 != nullptr);
        STD_INSIST(ptr3 != nullptr);
        STD_INSIST(ptr4 != nullptr);
    }

    STD_TEST(allocate_returns_different_addresses) {
        MemoryPool pool;

        void* ptr1 = pool.allocate(32);
        void* ptr2 = pool.allocate(32);

        STD_INSIST(ptr1 != ptr2);
    }

    STD_TEST(allocate_zero_size) {
        MemoryPool pool;

        void* ptr = pool.allocate(0);

        STD_INSIST(ptr != nullptr);
    }

    STD_TEST(allocate_multiple_then_access) {
        MemoryPool pool;

        int* intPtr = static_cast<int*>(pool.allocate(sizeof(int)));
        *intPtr = 42;

        double* doublePtr = static_cast<double*>(pool.allocate(sizeof(double)));
        *doublePtr = 3.14;

        TestStruct* structPtr = static_cast<TestStruct*>(pool.allocate(sizeof(TestStruct)));
        structPtr->a = 1;
        structPtr->b = 2;
        structPtr->c = 'x';

        STD_INSIST(*intPtr == 42);
        STD_INSIST(*doublePtr == 3.14);
        STD_INSIST(structPtr->a == 1);
        STD_INSIST(structPtr->b == 2);
        STD_INSIST(structPtr->c == 'x');
    }

    STD_TEST(allocate_large_chunks) {
        MemoryPool pool;

        void* ptr1 = pool.allocate(1024);
        STD_INSIST(ptr1 != nullptr);

        void* ptr2 = pool.allocate(2048);
        STD_INSIST(ptr2 != nullptr);

        STD_INSIST(ptr1 != ptr2);
    }

    STD_TEST(allocate_many_small_objects) {
        MemoryPool pool;

        constexpr size_t count = 100;
        int* pointers[count];

        for (size_t i = 0; i < count; ++i) {
            pointers[i] = static_cast<int*>(pool.allocate(sizeof(int)));
            *pointers[i] = static_cast<int>(i);
        }

        for (size_t i = 0; i < count; ++i) {
            STD_INSIST(*pointers[i] == static_cast<int>(i));
        }
    }

    STD_TEST(pool_destructor_runs_without_error) {
        {
            MemoryPool pool;
            [[maybe_unused]] void* ptr = pool.allocate(32);
        }

        STD_INSIST(true);
    }
}
