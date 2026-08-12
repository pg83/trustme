#include "hir_inherent_cache.h"
#include "hir_type.h"
#include "hir_hir.h"

void HIR::InherentCache::Lowest::insert(const Span& sp, const HIR::TypeImpl& impl) {
    const auto& type = impl.mType;
    if (const auto* path = type->get_sort_path()) {
        //DEBUG(this->name << " named[" << *path << "] += impl" << impl.m_params.fmt_args() << " " << impl.m_type);
        this->named[*path].push_back(&impl);
    } else if (type->is_Path() || type->is_Generic()) {
        //DEBUG(this->name << " generic += impl" << impl.m_params.fmt_args() << " " << impl.m_type);
        this->generic.push_back(&impl);
    } else {
        //DEBUG(this->name << " non_named += impl" << impl.m_params.fmt_args() << " " << impl.m_type);
        this->non_named.push_back(&impl);
    }
}

void HIR::InherentCache::Lowest::iterate(const HIR::TypeData* type, InherentCache::inner_callback_t& cb) const {
    auto visit = [&](const list_t& l) {
        for (const HIR::TypeImpl* impl_ptr : l) {
            cb(type, *impl_ptr);
        }
    };

    visit(this->generic);

    if (const auto* path = type->get_sort_path()) {
        auto it = this->named.find(*path);
        if (it != this->named.end()) {
            visit(it->second);
        }
    } else if (type->is_Path() || type->is_Generic()) {
        // Already handled by the unconditional generic
    } else {
        visit(this->non_named);
    }
}

void HIR::InherentCache::Inner::insert(const Span& sp, const HIR::TypeData* curTy, const HIR::TypeImpl& impl) {
    struct H {
        static void insert_inner(const Span& sp, const HIR::TypeData* inner_ty, const HIR::TypeImpl& impl, std::unique_ptr<Inner>& slot) {
            if (!slot) {
                slot = ::std::make_unique<Inner>();
            }
            slot->insert(sp, inner_ty, impl);
        }
    };

    TU_MATCH_HDRA( ((*curTy)), { )
    default:
        BUG(sp, "Unknown receiver type - " << curTy);
        TU_ARMA(Generic, te) {
            if (te.is_self()) {
                byvalue.insert(sp, impl);
            } else {
                BUG(sp, "Receiver generic not `Self` - " << curTy);
            }
        }
        TU_ARMA(Borrow, te) {
            switch (te.type) {
                case ::HIR::BorrowType::Shared:
                    H::insert_inner(sp, te.inner, impl, ref);
                    break;
                case ::HIR::BorrowType::Unique:
                    H::insert_inner(sp, te.inner, impl, refMut);
                    break;
                case ::HIR::BorrowType::Owned:
                    H::insert_inner(sp, te.inner, impl, refMove);
                    break;
            }
        }
        TU_ARMA(Pointer, te) {
            switch (te.type) {
                case ::HIR::BorrowType::Shared:
                    H::insert_inner(sp, te.inner, impl, ptr);
                    break;
                case ::HIR::BorrowType::Unique:
                    H::insert_inner(sp, te.inner, impl, ptrMut);
                    break;
                case ::HIR::BorrowType::Owned:
                    H::insert_inner(sp, te.inner, impl, ptrMove);
                    break;
            }
        }
        TU_ARMA(Path, te) {
            ASSERT_BUG(sp, te.path.mData.is_Generic(), "Receiver path not a generic path - " << curTy);
            const auto& gp = te.path.mData.as_Generic();
            ASSERT_BUG(sp, gp.mParams.types.size() > 0, "Receiver path has no type params (needs at least one) - " << curTy);
            DEBUG("m_path[" << gp.mPath << "] += " << gp.mParams.types.at(0) << " impl" << impl.mParams.fmt_args() << " " << impl.mType);
            mPath[gp.mPath].insert(sp, gp.mParams.types.at(0), impl);
        }
    }
}

void HIR::InherentCache::Inner::find(const Span& sp, const HIR::TypeData* curTyAct, t_cb_resolve_type ty_res, InherentCache::inner_callback_t& cb) const {
    const auto& curTy = ty_res.get_type(sp, curTyAct);
    TRACE_FUNCTION_F("[Inner] " << curTy);
    byvalue.iterate(curTy, cb);

    const Inner* inner = nullptr;
    const HIR::TypeData* inner_ty = nullptr;
    TU_MATCH_HDRA( ((*curTy)), { )
    default:
        // No recursion possible
        break;
        TU_ARMA(Borrow, te) {
            inner_ty = te.inner;
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
            inner_ty = te.inner;
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
                        inner_ty = gp.mParams.types.at(0);
                        inner = &it->second;
                    }
                }
            }
        }
    }

    if(inner) {
        assert(inner_ty);
        DEBUG("inner_ty = " << inner_ty);
        inner->find(sp, inner_ty, ty_res, cb);
    }
    else {
        DEBUG("no wrapper");
    }
}

void HIR::InherentCache::insert_all(const Span& sp, const HIR::TypeImpl& impl, const HIR::SimplePath& langBox) {
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

void HIR::InherentCache::find(const Span& sp, const RcString& name, const HIR::TypeData* ty, t_cb_resolve_type ty_res, callbackT cb) const {
    TRACE_FUNCTION_F(name << ", " << ty);
    // Callback that ensures that a potential impl fully matches the required receiver type
    inner_callback_t inner_cb = [&](const HIR::TypeData* rough_self_ty, const HIR::TypeImpl& impl) {
        DEBUG("- " << rough_self_ty);
        const HIR::Function& fcn = impl.methods.at(name).data;
        struct GetSelf: public ::HIR::MatchGenerics {
            ::std::optional<::HIR::TypeRef> detectedSelfTy;
            ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, ::HIR::t_cb_resolve_type _resolve_cb) override {
                if (g.is_self()) {
                    detectedSelfTy = ty;
                }
                return ::HIR::Compare::Equal;
            }
            ::HIR::Compare match_val(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) override {
                TODO(Span(), "GetSelf::match_val " << g << " with " << sz);
            }
        } getself;

        if (fcn.receiver == HIR::Function::Receiver::Custom) {
            ASSERT_BUG(sp, fcn.receiverType, "Custom receiver without a receiver type");
            if ((*fcn.receiverType)->match_test_generics(sp, ty, ResolvePlaceholdersNop(), getself)) {
                ASSERT_BUG(sp, getself.detectedSelfTy, "Unable to determine receiver type when matching " << *fcn.receiverType << " and " << ty);
                cb(*getself.detectedSelfTy, impl);
            }
        } else {
            // No extra checks required?
            cb(rough_self_ty, impl);
        }
    };
    auto it = items.find(name);
    if (it != items.end()) {
        it->second.find(sp, ty, ty_res, inner_cb);
    }
}
