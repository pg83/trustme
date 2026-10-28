#include "chaos_monkey.h"

#include <std/mem/obj_pool.h>

#include <errno.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

using namespace stl;

namespace {
    struct TestChaosMonkey final: public ChaosMonkey {
        long remaining[static_cast<unsigned>(ChaosFault::Count)];

        explicit TestChaosMonkey(const char* script);
        bool fail(ChaosFault fault) override;
    };

    static const char* faultName(ChaosFault fault) {
        switch (fault) {
            case ChaosFault::BackendSpawn: {
                return "backend-spawn";
            }
            case ChaosFault::BackendExit: {
                return "backend-exit";
            }
            case ChaosFault::LiteralOpen: {
                return "literal-open";
            }
            case ChaosFault::LiteralWrite: {
                return "literal-write";
            }
            case ChaosFault::LiteralClose: {
                return "literal-close";
            }
            case ChaosFault::ProcMacroPipe: {
                return "proc-macro-pipe";
            }
            case ChaosFault::ProcMacroSpawn: {
                return "proc-macro-spawn";
            }
            case ChaosFault::ProcMacroRead: {
                return "proc-macro-read";
            }
            case ChaosFault::ProcMacroEof: {
                return "proc-macro-eof";
            }
            case ChaosFault::ProcMacroWrite: {
                return "proc-macro-write";
            }
            case ChaosFault::Count: {
                break;
            }
        }
        abort();
    }

    [[noreturn]] static void invalidScript(const char* script) {
        fprintf(stderr, "invalid TRUSTME_CHAOS rule: %s\n", script);
        exit(1);
    }
}

TestChaosMonkey::TestChaosMonkey(const char* script) {
    for (auto& value : remaining) {
        value = -1;
    }
    while (*script) {
        const char* at = strchr(script, '@');
        if (!at || at == script || at[1] < '0' || at[1] > '9') {
            invalidScript(script);
        }
        errno = 0;
        char* end = nullptr;
        const long skip = strtol(at + 1, &end, 10);
        if (errno == ERANGE || (*end && *end != ',')) {
            invalidScript(script);
        }
        unsigned index = 0;
        for (; index < static_cast<unsigned>(ChaosFault::Count); ++index) {
            const char* name = faultName(static_cast<ChaosFault>(index));
            if (strlen(name) == static_cast<size_t>(at - script) && memcmp(name, script, at - script) == 0) {
                break;
            }
        }
        if (index == static_cast<unsigned>(ChaosFault::Count) || remaining[index] != -1) {
            invalidScript(script);
        }
        remaining[index] = skip;
        script = end;
        if (*script == ',') {
            ++script;
            if (!*script) {
                invalidScript(script);
            }
        }
    }
}

bool TestChaosMonkey::fail(ChaosFault fault) {
    auto& skip = remaining[static_cast<unsigned>(fault)];
    if (skip < 0) {
        return false;
    }
    if (skip-- != 0) {
        return false;
    }
    fprintf(stderr, "CHAOS %s\n", faultName(fault));
    errno = EIO;
    return true;
}

ChaosMonkey* createTestChaosMonkey(ObjPool& pool) {
    const char* script = getenv("TRUSTME_CHAOS");
    return script && *script ? pool.make<TestChaosMonkey>(script) : nullptr;
}
