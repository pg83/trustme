#include "lang_items.h"

#include "hir_hir.h"

#include <std/mem/obj_pool.h>

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
        explicit LangItemsImpl(const HIRCrate& crate)
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

        const HIRSimplePath& copy() const override {
            return copy_;
        }

        const HIRSimplePath& clone() const override {
            return clone_;
        }

        const HIRSimplePath& drop() const override {
            return drop_;
        }

        const HIRSimplePath& sized() const override {
            return sized_;
        }

        const HIRSimplePath& unsize() const override {
            return unsize_;
        }

        const HIRSimplePath& fn() const override {
            return fn_;
        }

        const HIRSimplePath& fnMut() const override {
            return fnMut_;
        }

        const HIRSimplePath& fnOnce() const override {
            return fnOnce_;
        }

        const HIRSimplePath& asyncFn() const override {
            return asyncFn_;
        }

        const HIRSimplePath& asyncFnMut() const override {
            return asyncFnMut_;
        }

        const HIRSimplePath& asyncFnOnce() const override {
            return asyncFnOnce_;
        }

        const HIRSimplePath& box() const override {
            return box_;
        }

        const HIRSimplePath& phantomData() const override {
            return phantomData_;
        }

        const HIRSimplePath& generator() const override {
            return generator_;
        }

        const HIRSimplePath& discriminantKind() const override {
            return discriminantKind_;
        }

        const HIRSimplePath& pointee() const override {
            return pointee_;
        }

        const HIRSimplePath& dynMetadata() const override {
            return dynMetadata_;
        }

        const HIRSimplePath& pointeeSized() const override {
            return pointeeSized_;
        }

        const HIRSimplePath& metaSized() const override {
            return metaSized_;
        }

        const HIRSimplePath& destruct() const override {
            return destruct_;
        }

        const HIRSimplePath& future() const override {
            return future_;
        }

        const HIRSimplePath& asyncIterator() const override {
            return asyncIterator_;
        }
    };
}

LangItems* LangItems::create(stl::ObjPool& pool, const HIRCrate& crate) {
    return pool.make<LangItemsImpl>(crate);
}
