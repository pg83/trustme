#include "chaos_monkey.h"

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    struct IdleChaosMonkey final: public ChaosMonkey {
        bool fail(ChaosFault fault) override;
    };
}

bool IdleChaosMonkey::fail(ChaosFault) {
    return false;
}

ChaosMonkey* ChaosMonkey::create(ObjPool& pool) {
#ifdef TRUSTME_FOR_TESTS
    if (auto* monkey = createTestChaosMonkey(pool)) {
        return monkey;
    }
#endif
    return pool.make<IdleChaosMonkey>();
}
