#include "hir_inherent_cache.h"

#include "hir_hir.h"
#include "hir_type.h"

#include <std/lib/vector.h>
#include <std/mem/obj_pool.h>

#include <map>
#include <memory>
#include <vector>

using namespace stl;

namespace {
    struct InherentCacheImpl final: public HIRInherentCache {
        struct Lowest {
            typedef Vector<const HIRTypeImpl*> listT;
            std::map<HIRSimplePath, listT> named;
            listT nonNamed; // TODO: use a map of HIR::ASTType*::Data::Tag
            listT generic;

            void insert(const Span& sp, const HIRTypeImpl& impl);
            void iterate(const HIRType* ty, Callback& cb) const;
        };

        struct Inner {
            Lowest byvalue;
            Lowest::listT wildcard;
            std::unique_ptr<Inner> ref;
            std::unique_ptr<Inner> refMut;
            std::unique_ptr<Inner> refMove;
            std::unique_ptr<Inner> ptr;
            std::unique_ptr<Inner> ptrMut;
            std::unique_ptr<Inner> ptrMove;
            std::map<HIRSimplePath, Inner> path;
            std::map<HIRSimplePath, Lowest::listT> concrete;

            void insert(const Span& sp, const HIRType* receiver, const HIRTypeImpl& impl);
            void find(const Span& sp, const HIRType* curTy, tCbResolveType tyRes, Callback& cb) const;
        };

        std::map<RcString, Inner> items;

        void insertAll(const Span& sp, const HIRTypeImpl& impl, const HIRSimplePath& langBox) override;
        void findWith(const Span& sp, const RcString& name, const HIRType* ty, tCbResolveType tyRes, Callback& cb) const override;
    };
}

void InherentCacheImpl::Lowest::insert(const Span& sp, const HIRTypeImpl& impl) {
    const auto& type = impl.type;
    if (const auto* path = type->getSortPath()) {
        this->named[*path].pushBack(&impl);
    } else if (type->is_Path() || type->is_Generic()) {
        this->generic.pushBack(&impl);
    } else {
        this->nonNamed.pushBack(&impl);
    }
}

void InherentCacheImpl::Lowest::iterate(const HIRType* type, Callback& cb) const {
    auto visit = [&](const listT& l) {
        for (const HIRTypeImpl* implPtr : l) {
            cb.visit(type, *implPtr);
        }
    };

    visit(this->generic);

    if (type->is_Infer() && !type->as_Infer().isLit()) {
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
    } else {
        visit(this->nonNamed);
    }
}

void InherentCacheImpl::Inner::insert(const Span& sp, const HIRType* curTy, const HIRTypeImpl& impl) {
    struct H {
        static void insertInner(const Span& sp, const HIRType* innerTy, const HIRTypeImpl& impl, std::unique_ptr<Inner>& slot) {
            if (!slot) {
                slot = std::make_unique<Inner>();
            }
            slot->insert(sp, innerTy, impl);
        }
    };

    switch ((*curTy).tag()) {
        default: {
            wildcard.pushBack(&impl);
            break;
        }
        case HIRType::TAG_Generic: {
            wildcard.pushBack(&impl);
            break;
        }
        case HIRType::TAG_Borrow: {
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
        case HIRType::TAG_Pointer: {
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
        case HIRType::TAG_Path: {
            auto& te = (*curTy).as_Path();
            if (const auto* gp = te.path.data.opt_Generic()) {
                DEBUG(StringView("m_concrete[") << gp->path << StringView("] += impl") << impl.params.fmtArgs() << StringView(" ") << impl.type);
                concrete[gp->path].pushBack(&impl);
            } else {
                wildcard.pushBack(&impl);
            }
            break;
        }
    }
}

void InherentCacheImpl::Inner::find(const Span& sp, const HIRType* curTyAct, tCbResolveType tyRes, Callback& cb) const {
    const auto& curTy = tyRes.getType(sp, curTyAct);
    TRACE_FUNCTION_F(StringView("[Inner] ") << curTy);
    for (const auto* impl : wildcard) {
        cb.visit(curTy, *impl);
    }
    byvalue.iterate(curTy, cb);

    const Inner* inner = nullptr;
    const HIRType* innerTy = nullptr;
    switch ((*curTy).tag()) {
        default:
            break;
        case HIRType::TAG_Borrow: {
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
        case HIRType::TAG_Pointer: {
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
        case HIRType::TAG_Path: {
            auto& te = (*curTy).as_Path();
            if (te.path.data.is_Generic()) {
                const auto& gp = te.path.data.as_Generic();
                auto ci = concrete.find(gp.path);
                if (ci != concrete.end()) {
                    for (const HIRTypeImpl* implPtr : ci->second) {
                        cb.visit(curTy, *implPtr);
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

    if (inner) {
        BUG_ASSERT(innerTy);
        DEBUG(StringView("inner_ty = ") << innerTy);
        inner->find(sp, innerTy, tyRes, cb);
        DEBUG(StringView("no wrapper"));
    }
}

void InherentCacheImpl::insertAll(const Span& sp, const HIRTypeImpl& impl, const HIRSimplePath& langBox) {
    for (const auto& m : impl.methods) {
        const auto& name = m.first;
        const auto& fcn = m.second.data;

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
                ASSERT_BUG(sp, fcn.receiverType, StringView("Custom receiver without a receiver type"));
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

void InherentCacheImpl::findWith(const Span& sp, const RcString& name, const HIRType* ty, tCbResolveType tyRes, Callback& cb) const {
    TRACE_FUNCTION_F(name << StringView(", ") << ty);

    auto it = items.find(name);
    if (it != items.end()) {
        it->second.find(sp, ty, tyRes, cb);
    }
}

HIRInherentCache* HIRInherentCache::create(ObjPool& pool) {
    return pool.make<InherentCacheImpl>();
}
