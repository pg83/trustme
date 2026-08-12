// Driver for the compiler's C++ unit tests: every bin/rustc/*_ut.cpp is
// linked into this binary and registers its STD_TEST cases with the libstd
// test framework.
#include <std/tst/ctx.h>

namespace {
    struct RunnerCtx: public stl::Ctx {
        RunnerCtx(int c, char** v) {
            argc = c;
            argv = v;
        }
    };
}

int main(int argc, char** argv) {
    RunnerCtx(argc, argv).run();
}
