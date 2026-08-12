#include "hir_inherent_cache.h"

#include "hir_hir.h"
#include "hir_type.h"

void HIRInherentCache::Lowest::insert(const Span& sp, const HIRTypeImpl& impl) {
    const auto& type = impl.mType;
    if (const auto* path = type->getSortPath()) {
        //DEBUG(this->name << " named[" << *path << "] += impl" << impl.m_params.fmt_args() << " " << impl.m_type);
        this->named[*path].push_back(&impl);
    } else if (type->is_Path() || type->is_Generic()) {
        //DEBUG(this->name << " generic += impl" << impl.m_params.fmt_args() << " " << impl.m_type);
        this->generic.push_back(&impl);
    } else {
        //DEBUG(this->name << " non_named += impl" << impl.m_params.fmt_args() << " " << impl.m_type);
        this->nonNamed.push_back(&impl);
    }
}

void HIRInherentCache::Lowest::iterate(const HIRTypeData* type, HIRInherentCache::innerCallbackT& cb) const {
    auto visit = [&](const listT& l) {
        for (const HIRTypeImpl* implPtr : l) {
            cb(type, *implPtr);
        }
    };

    visit(this->generic);

    if (const auto* path = type->getSortPath()) {
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

void HIRInherentCache::Inner::insert(const Span& sp, const HIRTypeData* curTy, const HIRTypeImpl& impl) {
    struct H {
        static void insertInner(const Span& sp, const HIRTypeData* innerTy, const HIRTypeImpl& impl, std::unique_ptr<Inner>& slot) {
            if (!slot) {
                slot = ::std::make_unique<Inner>();
            }
            slot->insert(sp, innerTy, impl);
        }
    };

    TU_MATCH_HDRA( ((*curTy)), { )
    default:
        BUG(sp, "Unknown receiver type - " << curTy);
        TU_ARMA(Generic, te) {
            if (te.isSelf()) {
                byvalue.insert(sp, impl);
            } else {
                BUG(sp, "Receiver generic not `Self` - " << curTy);
            }
        }
        TU_ARMA(Borrow, te) {
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
        }
        TU_ARMA(Pointer, te) {
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
        }
        TU_ARMA(Path, te) {
            ASSERT_BUG(sp, te.path.mData.is_Generic(), "Receiver path not a generic path - " << curTy);
            const auto& gp = te.path.mData.as_Generic();
            ASSERT_BUG(sp, gp.mParams.types.size() > 0, "Receiver path has no type params (needs at least one) - " << curTy);
            DEBUG("m_path[" << gp.mPath << "] += " << gp.mParams.types.at(0) << " impl" << impl.mParams.fmtArgs() << " " << impl.mType);
            mPath[gp.mPath].insert(sp, gp.mParams.types.at(0), impl);
        }
    }
}

void HIRInherentCache::Inner::find(const Span& sp, const HIRTypeData* curTyAct, tCbResolveType tyRes, HIRInherentCache::innerCallbackT& cb) const {
    const auto& curTy = tyRes.getType(sp, curTyAct);
    TRACE_FUNCTION_F("[Inner] " << curTy);
    byvalue.iterate(curTy, cb);

    const Inner* inner = nullptr;
    const HIRTypeData* innerTy = nullptr;
    TU_MATCH_HDRA( ((*curTy)), { )
    default:
        // No recursion possible
        break;
        TU_ARMA(Borrow, te) {
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
        }
        TU_ARMA(Pointer, te) {
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
        }
        TU_ARMA(Path, te) {
            if (te.path.mData.is_Generic()) {
                const auto& gp = te.path.mData.as_Generic();
                if (gp.mParams.types.size() > 0) {
                    auto it = mPath.find(gp.mPath);
                    if (it != mPath.end()) {
                        innerTy = gp.mParams.types.at(0);
                        inner = &it->second;
                    }
                }
            }
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

void HIRInherentCache::insertAll(const Span& sp, const HIRTypeImpl& impl, const HIRSimplePath& langBox) {
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
                items[name].mPath[langBox].byvalue.insert(sp, impl);
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

void HIRInherentCache::find(const Span& sp, const RcString& name, const HIRTypeData* ty, tCbResolveType tyRes, callbackT cb) const {
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
                ASSERT_BUG(sp, getself.detectedSelfTy, "Unable to determine receiver type when matching " << *fcn.receiverType << " and " << ty);
                cb(*getself.detectedSelfTy, impl);
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
