#include "lang_items.h"

#include "hir_hir.h"

#include <std/mem/obj_pool.h>

namespace {
    class LangItemsImpl final: public LangItems {
        HIRSimplePath mCopy;
        HIRSimplePath mClone;
        HIRSimplePath mDrop;
        HIRSimplePath mSized;
        HIRSimplePath mUnsize;
        HIRSimplePath mFn;
        HIRSimplePath mFnMut;
        HIRSimplePath mFnOnce;
        HIRSimplePath mAsyncFn;
        HIRSimplePath mAsyncFnMut;
        HIRSimplePath mAsyncFnOnce;
        HIRSimplePath mBox;
        HIRSimplePath mPhantomData;
        HIRSimplePath mGenerator;
        HIRSimplePath mDiscriminantKind;
        HIRSimplePath mPointee;
        HIRSimplePath mDynMetadata;
        HIRSimplePath mPointeeSized;
        HIRSimplePath mMetaSized;
        HIRSimplePath mDestruct;
        HIRSimplePath mFuture;

    public:
        explicit LangItemsImpl(const HIRCrate& crate)
            : mCopy(crate.getLangItemPathOpt("copy"))
            , mClone(crate.getLangItemPathOpt("clone"))
            , mDrop(crate.getLangItemPathOpt("drop"))
            , mSized(crate.getLangItemPathOpt("sized"))
            , mUnsize(crate.getLangItemPathOpt("unsize"))
            , mFn(crate.getLangItemPathOpt("fn"))
            , mFnMut(crate.getLangItemPathOpt("fn_mut"))
            , mFnOnce(crate.getLangItemPathOpt("fn_once"))
            , mAsyncFn(crate.getLangItemPathOpt("async_fn"))
            , mAsyncFnMut(crate.getLangItemPathOpt("async_fn_mut"))
            , mAsyncFnOnce(crate.getLangItemPathOpt("async_fn_once"))
            , mBox(crate.getLangItemPathOpt("owned_box"))
            , mPhantomData(crate.getLangItemPathOpt("phantom_data"))
            , mGenerator(crate.getLangItemPathOpt("coroutine"))
            , mDiscriminantKind(crate.getLangItemPathOpt("discriminant_kind"))
            , mPointee(crate.getLangItemPathOpt("pointee_trait"))
            , mDynMetadata(crate.getLangItemPathOpt("dyn_metadata"))
            , mPointeeSized(crate.getLangItemPathOpt("pointee_sized"))
            , mMetaSized(crate.getLangItemPathOpt("meta_sized"))
            , mDestruct(crate.getLangItemPathOpt("destruct"))
            , mFuture(crate.getLangItemPathOpt("future_trait"))
        {
        }

        const HIRSimplePath& copy() const override {
            return mCopy;
        }

        const HIRSimplePath& clone() const override {
            return mClone;
        }

        const HIRSimplePath& drop() const override {
            return mDrop;
        }

        const HIRSimplePath& sized() const override {
            return mSized;
        }

        const HIRSimplePath& unsize() const override {
            return mUnsize;
        }

        const HIRSimplePath& fn() const override {
            return mFn;
        }

        const HIRSimplePath& fnMut() const override {
            return mFnMut;
        }

        const HIRSimplePath& fnOnce() const override {
            return mFnOnce;
        }

        const HIRSimplePath& asyncFn() const override {
            return mAsyncFn;
        }

        const HIRSimplePath& asyncFnMut() const override {
            return mAsyncFnMut;
        }

        const HIRSimplePath& asyncFnOnce() const override {
            return mAsyncFnOnce;
        }

        const HIRSimplePath& box() const override {
            return mBox;
        }

        const HIRSimplePath& phantomData() const override {
            return mPhantomData;
        }

        const HIRSimplePath& generator() const override {
            return mGenerator;
        }

        const HIRSimplePath& discriminantKind() const override {
            return mDiscriminantKind;
        }

        const HIRSimplePath& pointee() const override {
            return mPointee;
        }

        const HIRSimplePath& dynMetadata() const override {
            return mDynMetadata;
        }

        const HIRSimplePath& pointeeSized() const override {
            return mPointeeSized;
        }

        const HIRSimplePath& metaSized() const override {
            return mMetaSized;
        }

        const HIRSimplePath& destruct() const override {
            return mDestruct;
        }

        const HIRSimplePath& future() const override {
            return mFuture;
        }
    };
}

LangItems* LangItems::create(stl::ObjPool& pool, const HIRCrate& crate) {
    return pool.make<LangItemsImpl>(crate);
}
