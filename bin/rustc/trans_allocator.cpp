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

const char GLOBAL_ALLOCATOR_LANG_ITEM[] = "mrustc-global_allocator";

HIR::SimplePath TransAllocatorTraitPath(const HIR::Crate& crate) {
    const auto& layout_path = TransAllocatorLayoutPath(crate);
    return HIR::SimplePath(layout_path.crate_name(), {"alloc", "global", "GlobalAlloc"});
}

const HIR::SimplePath& TransAllocatorLayoutPath(const HIR::Crate& crate) {
    return crate.get_lang_item_path(Span(), "alloc_layout");
}

HIR::Path TransAllocatorLayoutCtorPath(const HIR::Crate& crate) {
    const auto& layout_path = TransAllocatorLayoutPath(crate);
    const auto& layout_struct = crate.get_struct_by_path(Span(), layout_path);
    const auto layout_type = crate.m_types.path(
        HIR::GenericPath(layout_path),
        HIR::TypePathBinding(&layout_struct)
    );
    return HIR::Path(layout_type, RcString::new_interned("from_size_align_unchecked"));
}

HIR::Path TransAllocatorMethodPath(const HIR::Crate& crate, const HIR::TypeData* allocator_type, const AllocatorMethod& method) {
    const auto trait_path = TransAllocatorTraitPath(crate);
    const auto& trait = crate.get_trait_by_path(Span(), trait_path);
    const auto& function = trait.m_values.at(method.name).as_Function();
    return HIR::Path(
        allocator_type,
        HIR::GenericPath(trait_path),
        RcString::new_interned(method.name),
        function.m_params.make_empty_params(true)
    );
}
