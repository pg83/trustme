#pragma once

#include <cstddef>

namespace HIR {
    class Crate;
    class Path;
    class SimplePath;
    class TypeData;
}

enum class AllocatorDataTy {
    // - Return
    Unit,      // ()
    ResultPtr, // (..., *mut i8) + *mut u8
    // - Args
    Layout, // usize, usize
    Ptr,    // *mut u8
    Usize,  // usize
};

struct AllocatorMethod {
    const char* name;
    AllocatorDataTy ret;
    size_t nArgs;
    const AllocatorDataTy* args; // Terminated by Never
};
enum class AllocatorKind {
    Global,
    DefaultLib,
    DefaultExe,
};

extern const AllocatorMethod ALLOCATOR_METHODS[];
extern const size_t NUM_ALLOCATOR_METHODS;

extern const char GLOBAL_ALLOCATOR_LANG_ITEM[];
HIR::SimplePath TransAllocatorTraitPath(const HIR::Crate& crate);
const HIR::SimplePath& TransAllocatorLayoutPath(const HIR::Crate& crate);
HIR::Path TransAllocatorLayoutCtorPath(const HIR::Crate& crate);
HIR::Path TransAllocatorMethodPath(const HIR::Crate& crate, const HIR::TypeData* allocatorType, const AllocatorMethod& method);
