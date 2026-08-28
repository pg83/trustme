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
