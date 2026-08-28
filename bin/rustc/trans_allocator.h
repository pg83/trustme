#pragma once

#include <cstddef>

class HIRCrate;
class HIRPath;
class HIRSimplePath;
class HIRTypeData;

enum class AllocatorDataTy {
    Unit,
    ResultPtr,

    Layout,
    Ptr,
    Usize,
};

struct AllocatorMethod {
    const char* name;
    AllocatorDataTy ret;
    size_t nArgs;
    const AllocatorDataTy* args;
};
enum class AllocatorKind {
    Global,
    DefaultLib,
    DefaultExe,
};

extern const AllocatorMethod ALLOCATOR_METHODS[];
inline constexpr size_t NUM_ALLOCATOR_METHODS = 4;

extern const char GLOBAL_ALLOCATOR_LANG_ITEM[];
HIRSimplePath TransAllocatorTraitPath(const HIRCrate& crate);
const HIRSimplePath& TransAllocatorLayoutPath(const HIRCrate& crate);
HIRPath TransAllocatorLayoutCtorPath(const HIRCrate& crate);
HIRPath TransAllocatorMethodPath(const HIRCrate& crate, const HIRTypeData* allocatorType, const AllocatorMethod& method);
