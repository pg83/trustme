#include "hir_inherent_cache.h"
#include "hir_type.h"
#include "hir_hir.h"

void HIR::InherentCache::Lowest::insert(const Span& sp, const HIR::TypeImpl& impl) {
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

void HIR::InherentCache::Lowest::iterate(const HIR::TypeData* type, InherentCache::innerCallbackT& cb) const {
    auto visit = [&](const listT& l) {
        for (const HIR::TypeImpl* implPtr : l) {
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

void HIR::InherentCache::Inner::insert(const Span& sp, const HIR::TypeData* curTy, const HIR::TypeImpl& impl) {
    struct H {
        static void insertInner(const Span& sp, const HIR::TypeData* innerTy, const HIR::TypeImpl& impl, std::unique_ptr<Inner>& slot) {
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
                case ::HIR::BorrowType::Shared:
                    H::insertInner(sp, te.inner, impl, ref);
                    break;
                case ::HIR::BorrowType::Unique:
                    H::insertInner(sp, te.inner, impl, refMut);
                    break;
                case ::HIR::BorrowType::Owned:
                    H::insertInner(sp, te.inner, impl, refMove);
                    break;
            }
        }
        TU_ARMA(Pointer, te) {
            switch (te.type) {
                case ::HIR::BorrowType::Shared:
                    H::insertInner(sp, te.inner, impl, ptr);
                    break;
                case ::HIR::BorrowType::Unique:
                    H::insertInner(sp, te.inner, impl, ptrMut);
                    break;
                case ::HIR::BorrowType::Owned:
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

void HIR::InherentCache::Inner::find(const Span& sp, const HIR::TypeData* curTyAct, tCbResolveType ty_res, InherentCache::innerCallbackT& cb) const {
    const auto& curTy = ty_res.getType(sp, curTyAct);
    TRACE_FUNCTION_F("[Inner] " << curTy);
    byvalue.iterate(curTy, cb);

    const Inner* inner = nullptr;
    const HIR::TypeData* innerTy = nullptr;
    TU_MATCH_HDRA( ((*curTy)), { )
    default:
        // No recursion possible
        break;
        TU_ARMA(Borrow, te) {
            innerTy = te.inner;
            switch (te.type) {
                case ::HIR::BorrowType::Shared:
                    inner = ref.get();
                    break;
                case ::HIR::BorrowType::Unique:
                    inner = refMut.get();
                    break;
                case ::HIR::BorrowType::Owned:
                    inner = refMove.get();
                    break;
            }
        }
        TU_ARMA(Pointer, te) {
            innerTy = te.inner;
            switch (te.type) {
                case ::HIR::BorrowType::Shared:
                    inner = ptr.get();
                    break;
                case ::HIR::BorrowType::Unique:
                    inner = ptrMut.get();
                    break;
                case ::HIR::BorrowType::Owned:
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
        inner->find(sp, innerTy, ty_res, cb);
    }
    else {
        DEBUG("no wrapper");
    }
}

void HIR::InherentCache::insertAll(const Span& sp, const HIR::TypeImpl& impl, const HIR::SimplePath& langBox) {
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
            case HIR::Function::Receiver::Free:
                break;
            case HIR::Function::Receiver::Custom:
                ASSERT_BUG(sp, fcn.receiverType, "Custom receiver without a receiver type");
                items[name].insert(sp, *fcn.receiverType, impl);
                break;
            case HIR::Function::Receiver::Box:
                // TODO: 1.54+ has an allocator param here.
                items[name].mPath[langBox].byvalue.insert(sp, impl);
                break;
            case HIR::Function::Receiver::Value:
                items[name].byvalue.insert(sp, impl);
                break;
            case HIR::Function::Receiver::BorrowOwned:
                H::g(items[name].refMove).byvalue.insert(sp, impl);
                break;
            case HIR::Function::Receiver::BorrowUnique:
                H::g(items[name].refMut).byvalue.insert(sp, impl);
                break;
            case HIR::Function::Receiver::BorrowShared:
                H::g(items[name].ref).byvalue.insert(sp, impl);
                break;
        }
    }
}

void HIR::InherentCache::find(const Span& sp, const RcString& name, const HIR::TypeData* ty, tCbResolveType ty_res, callbackT cb) const {
    TRACE_FUNCTION_F(name << ", " << ty);
    // Callback that ensures that a potential impl fully matches the required receiver type
    innerCallbackT innerCb = [&](const HIR::TypeData* roughSelfTy, const HIR::TypeImpl& impl) {
        DEBUG("- " << roughSelfTy);
        const HIR::Function& fcn = impl.methods.at(name).data;
        struct GetSelf: public ::HIR::MatchGenerics {
            ::std::optional<::HIR::TypeRef> detectedSelfTy;
            ::HIR::Compare matchTy(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, ::HIR::tCbResolveType _resolve_cb) override {
                if (g.isSelf()) {
                    detectedSelfTy = ty;
                }
                return ::HIR::Compare::Equal;
            }
            ::HIR::Compare matchVal(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) override {
                TODO(Span(), "GetSelf::match_val " << g << " with " << sz);
            }
        } getself;

        if (fcn.receiver == HIR::Function::Receiver::Custom) {
            ASSERT_BUG(sp, fcn.receiverType, "Custom receiver without a receiver type");
            if ((*fcn.receiverType)->matchTestGenerics(sp, ty, ResolvePlaceholdersNop(), getself)) {
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
        it->second.find(sp, ty, ty_res, innerCb);
    }
}
