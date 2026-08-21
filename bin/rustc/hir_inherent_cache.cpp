#include "hir_inherent_cache.h"

#include "hir_hir.h"
#include "hir_type.h"

#include <std/mem/obj_pool.h>

#include <map>
#include <memory>
#include <vector>

namespace {
    /// Cached lookup logic for inherent (non-trait) methods on types
    class InherentCacheImpl final: public HIRInherentCache {
    public:
        typedef ::std::function<void(const HIRTypeData* selfTy, const HIRTypeImpl& impl)> innerCallbackT;

        struct Lowest {
            // Same as HIR::Crate::ImplGroup
            typedef ::std::vector<const HIRTypeImpl*> listT;
            ::std::map<HIRSimplePath, listT> named;
            listT nonNamed; // TODO: use a map of HIR::ASTType*::Data::Tag
            listT generic;

            void insert(const Span& sp, const HIRTypeImpl& impl);
            void iterate(const HIRTypeData* ty, innerCallbackT& cb) const;
        };

        /// A layer of the cache
        struct Inner {
            /// Cache content used for just `Self`
            Lowest byvalue;
            // Sub-caches for different wrappers around `Self` (can recurse)
            std::unique_ptr<Inner> ref;
            std::unique_ptr<Inner> refMut;
            std::unique_ptr<Inner> refMove;
            std::unique_ptr<Inner> ptr;
            std::unique_ptr<Inner> ptrMut;
            std::unique_ptr<Inner> ptrMove;
            std::map<HIRSimplePath, Inner> path;
            /// Receivers that name no `Self` at all (`fn f(self: Bar)`), which
            /// reach it through their own `Receiver`/`Deref` impl.
            std::map<HIRSimplePath, Lowest::listT> concrete;

            void insert(const Span& sp, const HIRTypeData* receiver, const HIRTypeImpl& impl);
            void find(const Span& sp, const HIRTypeData* curTy, tCbResolveType tyRes, innerCallbackT& cb) const;
        };

        std::map<RcString, Inner> items;

        void insertAll(const Span& sp, const HIRTypeImpl& impl, const HIRSimplePath& langBox) override;
        void find(const Span& sp, const RcString& name, const HIRTypeData* ty, tCbResolveType tyRes, callbackT cb) const override;
    };
}

void InherentCacheImpl::Lowest::insert(const Span& sp, const HIRTypeImpl& impl) {
    const auto& type = impl.type;
    if (const auto* path = type->getSortPath()) {
        this->named[*path].push_back(&impl);
    } else if (type->is_Path() || type->is_Generic()) {
        this->generic.push_back(&impl);
    } else {
        this->nonNamed.push_back(&impl);
    }
}

void InherentCacheImpl::Lowest::iterate(const HIRTypeData* type, InherentCacheImpl::innerCallbackT& cb) const {
    auto visit = [&](const listT& l) {
        for (const HIRTypeImpl* implPtr : l) {
            cb(type, *implPtr);
        }
    };

    visit(this->generic);

    if (type->is_Infer() && !type->as_Infer().isLit()) {
        // An unbound `Self` cannot select a named bucket yet. The full custom
        // receiver shape is checked before any of these candidates is used.
        for (const auto& entry : this->named) {
            visit(entry.second);
        }
        visit(this->nonNamed);
    } else if (const auto* path = type->getSortPath()) {
        auto it = this->named.find(*path);
        if (it != this->named.end()) {
            visit(it->second);
        }
    } else if (type->is_Path() || type->is_Generic()) {
        // Already handled by the unconditional generic
    } else {
        visit(this->nonNamed);
    }
}

void InherentCacheImpl::Inner::insert(const Span& sp, const HIRTypeData* curTy, const HIRTypeImpl& impl) {
    struct H {
        static void insertInner(const Span& sp, const HIRTypeData* innerTy, const HIRTypeImpl& impl, std::unique_ptr<Inner>& slot) {
            if (!slot) {
                slot = ::std::make_unique<Inner>();
            }
            slot->insert(sp, innerTy, impl);
        }
    };

    switch ((*curTy).tag()) {
default:
        BUG(sp, "Unknown receiver type - " << curTy);
        case HIRTypeData::TAG_Generic: {
            auto& te = (*curTy).as_Generic();
            if (te.isSelf()) {
                byvalue.insert(sp, impl);
            } else {
                BUG(sp, "Receiver generic not `Self` - " << curTy);
            }
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& te = (*curTy).as_Borrow();
            switch (te.type) {
                case HIRBorrowType::Shared:
                    H::insertInner(sp, te.inner, impl, ref);
                    break;
                case HIRBorrowType::Unique:
                    H::insertInner(sp, te.inner, impl, refMut);
                    break;
                case HIRBorrowType::Owned:
                    H::insertInner(sp, te.inner, impl, refMove);
                    break;
            }
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& te = (*curTy).as_Pointer();
            switch (te.type) {
                case HIRBorrowType::Shared:
                    H::insertInner(sp, te.inner, impl, ptr);
                    break;
                case HIRBorrowType::Unique:
                    H::insertInner(sp, te.inner, impl, ptrMut);
                    break;
                case HIRBorrowType::Owned:
                    H::insertInner(sp, te.inner, impl, ptrMove);
                    break;
            }
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& te = (*curTy).as_Path();
            ASSERT_BUG(sp, te.path.data.is_Generic(), "Receiver path not a generic path - " << curTy);
            const auto& gp = te.path.data.as_Generic();
            if (gp.params.types.empty()) {
                DEBUG("m_concrete[" << gp.path << "] += impl" << impl.params.fmtArgs() << " " << impl.type);
                concrete[gp.path].push_back(&impl);
                return;
            }
            DEBUG("m_path[" << gp.path << "] += " << gp.params.types.at(0) << " impl" << impl.params.fmtArgs() << " " << impl.type);
            path[gp.path].insert(sp, gp.params.types.at(0), impl);
            break;
        }
    }
}

void InherentCacheImpl::Inner::find(const Span& sp, const HIRTypeData* curTyAct, tCbResolveType tyRes, InherentCacheImpl::innerCallbackT& cb) const {
    const auto& curTy = tyRes.getType(sp, curTyAct);
    TRACE_FUNCTION_F("[Inner] " << curTy);
    byvalue.iterate(curTy, cb);

    const Inner* inner = nullptr;
    const HIRTypeData* innerTy = nullptr;
    switch ((*curTy).tag()) {
default:
        // No recursion possible
        break;
        case HIRTypeData::TAG_Borrow: {
            auto& te = (*curTy).as_Borrow();
            innerTy = te.inner;
            switch (te.type) {
                case HIRBorrowType::Shared:
                    inner = ref.get();
                    break;
                case HIRBorrowType::Unique:
                    inner = refMut.get();
                    break;
                case HIRBorrowType::Owned:
                    inner = refMove.get();
                    break;
            }
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& te = (*curTy).as_Pointer();
            innerTy = te.inner;
            switch (te.type) {
                case HIRBorrowType::Shared:
                    inner = ptr.get();
                    break;
                case HIRBorrowType::Unique:
                    inner = ptrMut.get();
                    break;
                case HIRBorrowType::Owned:
                    inner = ptrMove.get();
                    break;
            }
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& te = (*curTy).as_Path();
            if (te.path.data.is_Generic()) {
                const auto& gp = te.path.data.as_Generic();
                auto ci = concrete.find(gp.path);
                if (ci != concrete.end()) {
                    for (const HIRTypeImpl* implPtr : ci->second) {
                        cb(curTy, *implPtr);
                    }
                }
                if (gp.params.types.size() > 0) {
                    auto it = path.find(gp.path);
                    if (it != path.end()) {
                        innerTy = gp.params.types.at(0);
                        inner = &it->second;
                    }
                }
            }
            break;
        }
    }

    if(inner) {
        assert(innerTy);
        DEBUG("inner_ty = " << innerTy);
        inner->find(sp, innerTy, tyRes, cb);
    }
    else {
        DEBUG("no wrapper");
    }
}

void InherentCacheImpl::insertAll(const Span& sp, const HIRTypeImpl& impl, const HIRSimplePath& langBox) {
    for (const auto& m : impl.methods) {
        const auto& name = m.first;
        const auto& fcn = m.second.data;
        if (fcn.receiverType) {
            DEBUG(name << " " << *fcn.receiverType);
        } else {
            DEBUG(name);
        }

        struct H {
            static Inner& g(std::unique_ptr<Inner>& slot) {
                if (!slot) {
                    slot = std::make_unique<Inner>();
                }
                return *slot;
            }
        };

        switch (fcn.receiver) {
            case HIRFunction::Receiver::Free:
                break;
            case HIRFunction::Receiver::Custom:
                ASSERT_BUG(sp, fcn.receiverType, "Custom receiver without a receiver type");
                items[name].insert(sp, *fcn.receiverType, impl);
                break;
            case HIRFunction::Receiver::Box:
                // TODO: 1.54+ has an allocator param here.
                items[name].path[langBox].byvalue.insert(sp, impl);
                break;
            case HIRFunction::Receiver::Value:
                items[name].byvalue.insert(sp, impl);
                break;
            case HIRFunction::Receiver::BorrowOwned:
                H::g(items[name].refMove).byvalue.insert(sp, impl);
                break;
            case HIRFunction::Receiver::BorrowUnique:
                H::g(items[name].refMut).byvalue.insert(sp, impl);
                break;
            case HIRFunction::Receiver::BorrowShared:
                H::g(items[name].ref).byvalue.insert(sp, impl);
                break;
        }
    }
}

void InherentCacheImpl::find(const Span& sp, const RcString& name, const HIRTypeData* ty, tCbResolveType tyRes, callbackT cb) const {
    TRACE_FUNCTION_F(name << ", " << ty);
    // Callback that ensures that a potential impl fully matches the required receiver type
    innerCallbackT innerCb = [&](const HIRTypeData* roughSelfTy, const HIRTypeImpl& impl) {
        DEBUG("- " << roughSelfTy);
        const HIRFunction& fcn = impl.methods.at(name).data;
        struct GetSelf: public HIRMatchGenerics {
            ::std::optional<HIRTypeRef> detectedSelfTy;
            HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType _resolve_cb) override {
                if (g.isSelf()) {
                    detectedSelfTy = ty;
                }
                return HIRCompare::Equal;
            }
            HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
                TODO(Span(), "GetSelf::match_val " << g << " with " << sz);
            }
        } getself;

        if (fcn.receiver == HIRFunction::Receiver::Custom) {
            ASSERT_BUG(sp, fcn.receiverType, "Custom receiver without a receiver type");
            if ((*fcn.receiverType)->matchTestGenerics(sp, ty, HIRResolvePlaceholdersNop(), getself)) {
                // A receiver that names no `Self` says nothing about it, and the
                // impl it belongs to is the answer.
                auto selfTy = getself.detectedSelfTy ? *getself.detectedSelfTy : impl.type;
                const auto* resolvedSelfTy = tyRes.getType(sp, selfTy);
                // The matched concrete impl is enough to turn `<_>::method`
                // into a resolvable path. Keep generic impls on the old path:
                // their parameters have not been monomorphised here.
                if (resolvedSelfTy->is_Infer() && !resolvedSelfTy->as_Infer().isLit()
                    && !impl.type->needsMonomorphisation()) {
                    selfTy = impl.type;
                }
                cb(selfTy, impl);
            }
        } else {
            // No extra checks required?
            cb(roughSelfTy, impl);
        }
    };
    auto it = items.find(name);
    if (it != items.end()) {
        it->second.find(sp, ty, tyRes, innerCb);
    }
}

HIRInherentCache* HIRInherentCache::create(stl::ObjPool& pool) {
    return pool.make<InherentCacheImpl>();
}
