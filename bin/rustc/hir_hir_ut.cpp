#include "output.h"
#include "hir_hir.h"

#include <std/tst/ut.h>
#include <std/sym/h_map.h>
#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct Item {
        int tag;
    };

    /* Two addresses a live compiler actually produced, from a run where the
       mutable-owner cache handed a lookup the wrong item.  They differ only in
       bit 29, which is exactly the kind of pair a 2 MiB-arena allocator hands
       out for the same offset in two arenas. */
    const u64 collidingLeft = 0x7fe7d17cb490ULL;
    const u64 collidingRight = 0x7fe7f17cb490ULL;
}

STD_TEST_SUITE(HirPointerHasher) {
    STD_TEST(DistinguishesAddressesThatDifferInOneBit) {
        const auto* left = reinterpret_cast<const Item*>(collidingLeft);
        const auto* right = reinterpret_cast<const Item*>(collidingRight);

        STD_INSIST(left != right);
        STD_INSIST(HIRPointerHasher::hash(left) != HIRPointerHasher::hash(right));
    }

    STD_TEST(DistinguishesEveryBitOfAnAddress) {
        const u64 base = 0x7f0000001000ULL;
        for (unsigned bit = 3; bit < 48; bit++) {
            const auto* left = reinterpret_cast<const Item*>(base);
            const auto* right = reinterpret_cast<const Item*>(base ^ (1ULL << bit));
            STD_INSIST(HIRPointerHasher::hash(left) != HIRPointerHasher::hash(right));
        }
    }

    /* The cache this hasher serves keys its nodes by the hash alone, so a
       collision is not a slow lookup - it is one entry standing for two items,
       and the second insert replacing the first. */
    STD_TEST(AMapKeyedByTheHasherKeepsBothCollidingItems) {
        auto poolRef = ObjPool::fromMemory();
        HashMap<const Item*, const Item*, HIRPointerHasher> map(poolRef.mutPtr());

        const auto* left = reinterpret_cast<const Item*>(collidingLeft);
        const auto* right = reinterpret_cast<const Item*>(collidingRight);
        map.insert(left, left);
        map.insert(right, right);

        const auto* foundLeft = map.find(left);
        const auto* foundRight = map.find(right);
        STD_INSIST(foundLeft != nullptr);
        STD_INSIST(foundRight != nullptr);
        STD_INSIST(*foundLeft == left);
        STD_INSIST(*foundRight == right);
    }
}
