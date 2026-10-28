#include <stdio.h>

extern "C" int __llvm_profile_write_file();
extern "C" [[noreturn]] void __real_abort();

// Only the coverage executable links this wrapper. Compiler diagnostics end
// with abort(), which bypasses LLVM's at-exit hook. Preserve that termination
// and save the path that led to it before invoking the real abort.
extern "C" [[noreturn]] void __wrap_abort() {
    if (__llvm_profile_write_file() != 0) {
        fprintf(stderr, "unable to write compiler coverage before abort\n");
    }
    __real_abort();
}
