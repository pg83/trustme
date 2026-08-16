#include "trans_allocator.h"

#include "hir_hir.h"

#define DEF_METHOD_ARGS(name, ...) const AllocatorDataTy ALLOCATOR_METHODS_ARGS_##name[] = {__VA_ARGS__};
#define DEF_METHOD(name, ret) {#name, AllocatorDataTy::ret, sizeof(ALLOCATOR_METHODS_ARGS_##name) / sizeof(AllocatorDataTy), ALLOCATOR_METHODS_ARGS_##name}

DEF_METHOD_ARGS(alloc, AllocatorDataTy::Layout)
DEF_METHOD_ARGS(dealloc, AllocatorDataTy::Ptr, AllocatorDataTy::Layout)
DEF_METHOD_ARGS(realloc, AllocatorDataTy::Ptr, AllocatorDataTy::Layout, AllocatorDataTy::Usize)
DEF_METHOD_ARGS(alloc_zeroed, AllocatorDataTy::Layout)

const AllocatorMethod ALLOCATOR_METHODS[4] = {DEF_METHOD(alloc, ResultPtr), DEF_METHOD(dealloc, Unit), DEF_METHOD(realloc, ResultPtr), DEF_METHOD(alloc_zeroed, ResultPtr)};
const size_t NUM_ALLOCATOR_METHODS = sizeof(ALLOCATOR_METHODS) / sizeof(ALLOCATOR_METHODS[0]);

const char GLOBAL_ALLOCATOR_LANG_ITEM[] = "trustme-global_allocator";

HIRSimplePath TransAllocatorTraitPath(const HIRCrate& crate) {
    const auto& layoutPath = TransAllocatorLayoutPath(crate);
    return HIRSimplePath(layoutPath.crateName(), {"alloc", "global", "GlobalAlloc"});
}

const HIRSimplePath& TransAllocatorLayoutPath(const HIRCrate& crate) {
    return crate.getLangItemPath(Span(), "alloc_layout");
}

HIRPath TransAllocatorLayoutCtorPath(const HIRCrate& crate) {
    const auto& layoutPath = TransAllocatorLayoutPath(crate);
    const auto& layoutStruct = crate.getStructByPath(Span(), layoutPath);
    const auto layoutType = crate.types.path(HIRGenericPath(layoutPath), HIRTypePathBinding(&layoutStruct));
    return HIRPath(layoutType, RcString::newInterned("from_size_align_unchecked"));
}

HIRPath TransAllocatorMethodPath(const HIRCrate& crate, const HIRTypeData* allocatorType, const AllocatorMethod& method) {
    const auto traitPath = TransAllocatorTraitPath(crate);
    const auto& trait = crate.getTraitByPath(Span(), traitPath);
    const auto& function = trait.values.at(method.name).as_Function();
    return HIRPath(allocatorType, HIRGenericPath(traitPath), RcString::newInterned(method.name), HIRPathParams());
}
