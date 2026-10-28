#pragma once

namespace stl {
    class ObjPool;
}

enum class ChaosFault {
    BackendSpawn,
    BackendExit,
    LiteralOpen,
    LiteralWrite,
    LiteralClose,
    ProcMacroPipe,
    ProcMacroSpawn,
    ProcMacroRead,
    ProcMacroEof,
    ProcMacroWrite,
    Count,
};

/* Faults are spent before an operation acquires a resource. A failed close
 * still closes its resource before reporting the injected error. */
struct ChaosMonkey {
    virtual bool fail(ChaosFault fault) = 0;

    static ChaosMonkey* create(stl::ObjPool& pool);
};

#ifdef TRUSTME_FOR_TESTS
ChaosMonkey* createTestChaosMonkey(stl::ObjPool& pool);
#endif
