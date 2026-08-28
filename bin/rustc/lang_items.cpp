#include "lang_items.h"

#include "hir_hir.h"

#include <std/mem/obj_pool.h>

using namespace stl;

namespace {
    class LangItemsImpl final: public LangItems {
        HIRSimplePath copy_;
        HIRSimplePath clone_;
        HIRSimplePath drop_;
        HIRSimplePath sized_;
        HIRSimplePath unsize_;
        HIRSimplePath fn_;
        HIRSimplePath fnMut_;
        HIRSimplePath fnOnce_;
        HIRSimplePath asyncFn_;
        HIRSimplePath asyncFnMut_;
        HIRSimplePath asyncFnOnce_;
        HIRSimplePath box_;
        HIRSimplePath phantomData_;
        HIRSimplePath generator_;
        HIRSimplePath discriminantKind_;
        HIRSimplePath pointee_;
        HIRSimplePath dynMetadata_;
        HIRSimplePath pointeeSized_;
        HIRSimplePath metaSized_;
        HIRSimplePath destruct_;
        HIRSimplePath future_;
        HIRSimplePath asyncIterator_;

    public:
        explicit LangItemsImpl(const HIRCrate& crate);

        const HIRSimplePath& copy() const override;

        const HIRSimplePath& clone() const override;

        const HIRSimplePath& drop() const override;

        const HIRSimplePath& sized() const override;

        const HIRSimplePath& unsize() const override;

        const HIRSimplePath& fn() const override;

        const HIRSimplePath& fnMut() const override;

        const HIRSimplePath& fnOnce() const override;

        const HIRSimplePath& asyncFn() const override;

        const HIRSimplePath& asyncFnMut() const override;

        const HIRSimplePath& asyncFnOnce() const override;

        const HIRSimplePath& box() const override;

        const HIRSimplePath& phantomData() const override;

        const HIRSimplePath& generator() const override;

        const HIRSimplePath& discriminantKind() const override;

        const HIRSimplePath& pointee() const override;

        const HIRSimplePath& dynMetadata() const override;

        const HIRSimplePath& pointeeSized() const override;

        const HIRSimplePath& metaSized() const override;

        const HIRSimplePath& destruct() const override;

        const HIRSimplePath& future() const override;

        const HIRSimplePath& asyncIterator() const override;
    };
}

LangItems* LangItems::create(ObjPool& pool, const HIRCrate& crate) {
    return pool.make<LangItemsImpl>(crate);
}

LangItemsImpl::LangItemsImpl(const HIRCrate& crate)
    : copy_(crate.getLangItemPathOpt("copy"))
    , clone_(crate.getLangItemPathOpt("clone"))
    , drop_(crate.getLangItemPathOpt("drop"))
    , sized_(crate.getLangItemPathOpt("sized"))
    , unsize_(crate.getLangItemPathOpt("unsize"))
    , fn_(crate.getLangItemPathOpt("fn"))
    , fnMut_(crate.getLangItemPathOpt("fn_mut"))
    , fnOnce_(crate.getLangItemPathOpt("fn_once"))
    , asyncFn_(crate.getLangItemPathOpt("async_fn"))
    , asyncFnMut_(crate.getLangItemPathOpt("async_fn_mut"))
    , asyncFnOnce_(crate.getLangItemPathOpt("async_fn_once"))
    , box_(crate.getLangItemPathOpt("owned_box"))
    , phantomData_(crate.getLangItemPathOpt("phantom_data"))
    , generator_(crate.getLangItemPathOpt("coroutine"))
    , discriminantKind_(crate.getLangItemPathOpt("discriminant_kind"))
    , pointee_(crate.getLangItemPathOpt("pointee_trait"))
    , dynMetadata_(crate.getLangItemPathOpt("dyn_metadata"))
    , pointeeSized_(crate.getLangItemPathOpt("pointee_sized"))
    , metaSized_(crate.getLangItemPathOpt("meta_sized"))
    , destruct_(crate.getLangItemPathOpt("destruct"))
    , future_(crate.getLangItemPathOpt("future_trait"))
    , asyncIterator_(crate.getLangItemPathOpt("async_iterator"))
{
}

auto LangItemsImpl::copy() const -> const HIRSimplePath& {
    return copy_;
}

auto LangItemsImpl::clone() const -> const HIRSimplePath& {
    return clone_;
}

auto LangItemsImpl::drop() const -> const HIRSimplePath& {
    return drop_;
}

auto LangItemsImpl::sized() const -> const HIRSimplePath& {
    return sized_;
}

auto LangItemsImpl::unsize() const -> const HIRSimplePath& {
    return unsize_;
}

auto LangItemsImpl::fn() const -> const HIRSimplePath& {
    return fn_;
}

auto LangItemsImpl::fnMut() const -> const HIRSimplePath& {
    return fnMut_;
}

auto LangItemsImpl::fnOnce() const -> const HIRSimplePath& {
    return fnOnce_;
}

auto LangItemsImpl::asyncFn() const -> const HIRSimplePath& {
    return asyncFn_;
}

auto LangItemsImpl::asyncFnMut() const -> const HIRSimplePath& {
    return asyncFnMut_;
}

auto LangItemsImpl::asyncFnOnce() const -> const HIRSimplePath& {
    return asyncFnOnce_;
}

auto LangItemsImpl::box() const -> const HIRSimplePath& {
    return box_;
}

auto LangItemsImpl::phantomData() const -> const HIRSimplePath& {
    return phantomData_;
}

auto LangItemsImpl::generator() const -> const HIRSimplePath& {
    return generator_;
}

auto LangItemsImpl::discriminantKind() const -> const HIRSimplePath& {
    return discriminantKind_;
}

auto LangItemsImpl::pointee() const -> const HIRSimplePath& {
    return pointee_;
}

auto LangItemsImpl::dynMetadata() const -> const HIRSimplePath& {
    return dynMetadata_;
}

auto LangItemsImpl::pointeeSized() const -> const HIRSimplePath& {
    return pointeeSized_;
}

auto LangItemsImpl::metaSized() const -> const HIRSimplePath& {
    return metaSized_;
}

auto LangItemsImpl::destruct() const -> const HIRSimplePath& {
    return destruct_;
}

auto LangItemsImpl::future() const -> const HIRSimplePath& {
    return future_;
}

auto LangItemsImpl::asyncIterator() const -> const HIRSimplePath& {
    return asyncIterator_;
}
