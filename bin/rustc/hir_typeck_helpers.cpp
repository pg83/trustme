#include "hir_typeck_helpers.h"

#include "settings.h"
#include "wire_board.h"
#include "hir_inherent_cache.h"
#include "hir_conv_main_bindings.h"

#include <std/mem/obj_list.h>
#include <std/mem/obj_pool.h>

#include <optional>
#include <algorithm>
#include <unordered_map>

namespace {
    // TODO: De-duplicate this with `static.cpp`
    const HIRGenericParams emptyParams;

    // Give every fresh placeholder in one active trait goal the same stable
    // spelling.  This makes a recurrence through independently-instantiated
    // blanket impls visible to the solver without changing the goal's actual
    // type data or inference state.
    class CanonicalizeTraitGoal final: public Monomorphiser {
        mutable ::std::vector<::std::pair<RcString, RcString>> mPlaceholderNames;

        RcString canonicalPlaceholderName(const RcString& name) const {
            for (const auto& entry : mPlaceholderNames) {
                if (entry.first == name) {
                    return entry.second;
                }
            }
            auto canonical = RcString::newInterned(FMT("#solver-placeholder-" << mPlaceholderNames.size()));
            mPlaceholderNames.push_back({name, canonical});
            return canonical;
        }

    public:
        explicit CanonicalizeTraitGoal(HIRTypeInterner& types)
            : Monomorphiser(types)
        {
        }

        HIRTypeRef getType(const Span&, const HIRGenericRef& generic) const override {
            return generic.isPlaceholder() ? types.generic(canonicalPlaceholderName(generic.name), generic.binding) : types.generic(generic.name, generic.binding);
        }

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override {
            return HIRConstGeneric(generic.isPlaceholder() ? HIRGenericRef(canonicalPlaceholderName(generic.name), generic.binding) : generic);
        }


        const ::std::vector<::std::pair<RcString, RcString>>& placeholderNames() const {
            return mPlaceholderNames;
        }
    };

    class InstantiateCanonicalTraitResponse final: public Monomorphiser {
        const ::std::vector<::std::pair<RcString, RcString>>& goalNames;
        const uint64_t instance;
        mutable ::std::vector<::std::pair<RcString, RcString>> freshNames;

        RcString instantiatePlaceholderName(const RcString& canonical) const {
            for (const auto& entry : goalNames) {
                if (entry.second == canonical) {
                    return entry.first;
                }
            }
            for (const auto& entry : freshNames) {
                if (entry.first == canonical) {
                    return entry.second;
                }
            }
            auto fresh = RcString(FMT("solver_response_" << instance << "_" << freshNames.size()));
            freshNames.push_back({canonical, fresh});
            return fresh;
        }

    public:
        InstantiateCanonicalTraitResponse(HIRTypeInterner& types, const ::std::vector<::std::pair<RcString, RcString>>& goalNames, uint64_t instance)
            : Monomorphiser(types)
            , goalNames(goalNames)
            , instance(instance)
        {
        }

        HIRTypeRef getType(const Span&, const HIRGenericRef& generic) const override {
            return types.generic(generic.isPlaceholder() ? instantiatePlaceholderName(generic.name) : generic.name, generic.binding);
        }

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override {
            return HIRConstGeneric(generic.isPlaceholder() ? HIRGenericRef(instantiatePlaceholderName(generic.name), generic.binding) : generic);
        }

    };

    // Canonical query variables created while evaluating a goal are
    // existential.  They must be instantiated as fresh variables in the
    // caller's inference table before a root response leaves the solver.
    // Placeholders already present in the input goal are universal and stay
    // unchanged.
    class InstantiateTraitResponseForCaller final: public Monomorphiser {
        HMTypeInferrence& ivars;
        const ::std::vector<::std::pair<RcString, RcString>>& goalNames;
        mutable ::std::vector<::std::pair<HIRGenericRef, HIRTypeRef>> typeValues;
        mutable ::std::vector<::std::pair<HIRGenericRef, HIRConstGeneric>> values;

        bool isGoalPlaceholder(const HIRGenericRef& generic) const {
            for (const auto& entry : goalNames) {
                if (entry.first == generic.name) {
                    return true;
                }
            }
            return false;
        }

    public:
        InstantiateTraitResponseForCaller(HIRTypeInterner& types, HMTypeInferrence& ivars, const ::std::vector<::std::pair<RcString, RcString>>& goalNames)
            : Monomorphiser(types)
            , ivars(ivars)
            , goalNames(goalNames)
        {
        }

        HIRTypeRef getType(const Span&, const HIRGenericRef& generic) const override {
            if (!generic.isPlaceholder() || isGoalPlaceholder(generic)) {
                return Monomorphiser::types.generic(generic.name, generic.binding);
            }
            for (const auto& entry : typeValues) {
                if (entry.first == generic) {
                    return entry.second;
                }
            }
            auto fresh = ivars.newIvarTr();
            typeValues.push_back({generic, fresh});
            return fresh;
        }

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override {
            if (!generic.isPlaceholder() || isGoalPlaceholder(generic)) {
                return HIRConstGeneric(generic);
            }
            for (const auto& entry : values) {
                if (entry.first == generic) {
                    return entry.second.clone();
                }
            }
            auto fresh = HIRConstGeneric::make_Infer({ivars.newIvarVal()});
            values.push_back({generic, fresh.clone()});
            return fresh;
        }

    };

}

// --------------------------------------------------------------------
// HMTypeInferrence
// --------------------------------------------------------------------
void HMTypeInferrence::dump() const {
    for (const auto& v : ivars) {
        auto i = &v - &ivars.front();
        if (v.isAlias()) {
        } else {
            DEBUG("#" << i << " = " << v.type << FMT_CB(os, bool open = false; unsigned int i2 = 0; for (const auto& v2 : ivars) {
                      if (v2.isAlias() && v2.alias == i) {
                          if (!open) {
                              os << " { ";
                          }
                          open = true;
                          os << "#" << i2 << " ";
                      }
                      i2++;
                  } if (open) os << "}";));
        }
    }
    for (const auto& v : values) {
        auto i = &v - &values.front();
        if (v.isAlias()) {
        } else {
            DEBUG("V#" << i << " = " << *v.val << FMT_CB(os, bool open = false; for (const auto& v2 : values) {
                      auto i2 = &v2 - &values.front();
                      if (v2.isAlias() && v2.alias == i) {
                          if (!open) {
                              os << " { ";
                          }
                          open = true;
                          os << "#" << i2 << " ";
                      }
                  } if (open) os << "}";));
        }
    }
}

void HMTypeInferrence::checkForLoops() {
    struct LoopChecker {
        ::std::vector<unsigned int> indexes;

        void checkTy(const HMTypeInferrence& ivars, const HIRTypeData* ty) {
            visitTyWith(ty, [&](const HIRTypeData* t) {
                if (const auto* ep = t->opt_Infer()) {
                    const auto& e = *ep;
                    for (auto idx : indexes) {
                        ASSERT_BUG(Span(), e.index != idx, "Recursion in ivar #" << indexes.front() << " " << ivars.ivars[indexes.front()].type << " - loop with " << idx << " " << ivars.ivars[idx].type);
                    }
                    const auto& ivd = ivars.getPointedIvar(e.index);
                    assert(!ivd.isAlias());
                    if (!ivd.type->is_Infer()) {
                        indexes.push_back(e.index);
                        this->checkTy(ivars, ivd.type);
                        indexes.pop_back();
                    }
                }
                return false;
            });
        }
    };

    unsigned int i = 0;
    for (const auto& v : ivars) {
        if (!v.isAlias() && !v.type->is_Infer()) {
            DEBUG("- " << i << " " << v.type);
            (LoopChecker{{i}}).checkTy(*this, v.type);
        }
        i++;
    }
}

void HMTypeInferrence::compactIvars() {
    this->checkForLoops();

    unsigned int i = 0;
    for (auto& v : ivars) {
        if (!v.isAlias()) {
            auto old = v.type;
            this->expandIvars(v.type);
            DEBUG("- " << i << " " << old << " -> " << v.type);
        } else {
            auto index = v.alias;
            unsigned int count = 0;
            assert(index < ivars.size());
            while (ivars.at(index).isAlias()) {
                index = ivars.at(index).alias;

                if (count >= ivars.size()) {
                    this->dump();
                    BUG(Span(), "Loop detected in ivar list when starting at " << v.alias << ", current is " << index);
                }
                count++;
            }
            v.alias = index;
        }
        i++;
    }
}

bool HMTypeInferrence::applyDefaults() {
    bool rv = false;
    for (auto& v : ivars) {
        if (!v.isAlias()) {
            if (const auto* e = v.type->opt_Infer()) {
                switch (e->tyClass) {
                    case HIRInferClass::None:
                        break;
                    case HIRInferClass::Integer:
                        rv = true;
                        DEBUG("- IVar " << e->index << " = i32");
                        v.type = types.primitive(HIRCoreType::I32);
                        break;
                    case HIRInferClass::Float:
                        rv = true;
                        DEBUG("- IVar " << e->index << " = f64");
                        v.type = types.primitive(HIRCoreType::F64);
                        break;
                }
            }
        }
    }
    return rv;
}

void HMTypeInferrence::printType(::std::ostream& os, const HIRTypeData* tr, LList<const HIRTypeData*> outerStack) const {
    const auto& ty = this->getType(tr);
    for (const auto* pty : outerStack) {
        if (pty) {
            if (pty == ty) {
                os << "/*RECURSE*/";
                return;
            }
        }
    }
    auto stack = LList<const HIRTypeData*>(&outerStack, ty);

    auto printTraitpath = [&](const HIRTraitPath& tp) {
        this->printGenericpath(os, tp.mPath, stack);
        // TODO: ATYs?
    };
    auto printPath = [&](const HIRPath& path) {
        TU_MATCH_HDRA( (path.mData), {)
        TU_ARMA(Generic, pe) {
                this->printGenericpath(os, pe, stack);
            }
            TU_ARMA(UfcsKnown, pe) {
                os << "<";
                this->printType(os, pe.type, stack);
                os << " as ";
                this->printGenericpath(os, pe.trait, stack);
                os << ">::" << pe.item;
                this->printPathparams(os, pe.params, stack);
            }
            TU_ARMA(UfcsInherent, pe) {
                os << "<";
                this->printType(os, pe.type, stack);
                os << ">::" << pe.item;
                this->printPathparams(os, pe.params, stack);
            }
            TU_ARMA(UfcsUnknown, pe) {
                BUG(Span(), "UfcsUnknown");
            }
        }
    };

    TU_MATCH_HDRA( (*ty), {)
    TU_ARMA(Infer, e) {
            os << ty;
        }
        TU_ARMA(Primitive, e) {
            os << ty;
        }
        TU_ARMA(Diverge, e) {
            os << ty;
        }
        TU_ARMA(Generic, e) {
            os << ty;
        }
        TU_ARMA(Path, e) {
            printPath(e.path);
        }
        TU_ARMA(Borrow, e) {
            os << "&";
            switch (e.type) {
                case HIRBorrowType::Shared:
                    os << "";
                    break;
                case HIRBorrowType::Unique:
                    os << "mut ";
                    break;
                case HIRBorrowType::Owned:
                    os << "move ";
                    break;
            }
            this->printType(os, e.inner, stack);
        }
        TU_ARMA(Pointer, e) {
            switch (e.type) {
                case HIRBorrowType::Shared:
                    os << "*const ";
                    break;
                case HIRBorrowType::Unique:
                    os << "*mut ";
                    break;
                case HIRBorrowType::Owned:
                    os << "*move ";
                    break;
            }
            this->printType(os, e.inner, stack);
        }
        TU_ARMA(Slice, e) {
            os << "[";
            this->printType(os, e.inner, stack);
            os << "]";
        }
        TU_ARMA(Array, e) {
            os << "[";
            this->printType(os, e.inner, stack);
            os << "; " << e.size << "]";
        }
        TU_ARMA(NodeType, e) {
            e.fmt(os);
        TU_MATCH_HDRA((e), {)
        TU_ARMA(Closure, nodeP) {
                    os << "(";
                    for (const auto& arg : nodeP->mArgs) {
                        this->printType(os, arg.second, stack);
                        os << ",";
                    }
                    os << ")->";
                    this->printType(os, nodeP->returnType, stack);
                }
                TU_ARMA(Generator, nodeP) {
                }
                TU_ARMA(Async, nodeP) {
                }
        }
        }
        TU_ARMA(NamedFunction, e) {
            os << "fn{";
            printPath(e.path);
            os << "}";
        }
        TU_ARMA(Function, e) {
            if (e.isUnsafe) {
                os << "unsafe ";
            }
            if (e.mAbi != "") {
                os << "extern \"" << e.mAbi << "\" ";
            }
            os << "fn(";
            for (const auto& arg : e.argTypes) {
                this->printType(os, arg, stack);
                os << ",";
            }
            os << ")->";
            this->printType(os, e.mRettype, stack);
        }
        TU_ARMA(TraitObject, e) {
            os << "dyn (";
            printTraitpath(e.mTrait);
            for (const auto& marker : e.markers) {
                os << "+";
                this->printGenericpath(os, marker, stack);
            }
            os << ")";
        }
        TU_ARMA(ErasedType, e) {
            os << "impl ";
            for (const auto& tr : e.traits) {
                if (&tr != &e.traits[0]) {
                    os << "+";
                }
                printTraitpath(tr);
            }
            os << "+use";
            this->printPathparams(os, e.use, outerStack);
            os << "/*";
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Fcn, ee) {
                    os << "fn ";
                    printPath(ee.origin);
                    os << "#" << ee.index;
                }
                TU_ARMA(Known, ee) {
                    printType(os, ee, stack);
                }
                TU_ARMA(Alias, ee) {
                }
        }
        os << "*/";
        }
        TU_ARMA(Tuple, e) {
            os << "(";
            for (const auto& st : e) {
                this->printType(os, st, stack);
                os << ",";
            }
            os << ")";
        }
    }
}

void HMTypeInferrence::printGenericpath(::std::ostream& os, const HIRGenericPath& gp, LList<const HIRTypeData*> stack) const {
    os << gp.mPath;
    this->printPathparams(os, gp.mParams, stack);
}

void HMTypeInferrence::printPathparams(::std::ostream& os, const HIRPathParams& pps, LList<const HIRTypeData*> stack) const {
    if (pps.hasParams()) {
        os << "<";
        for (const auto& ppT : pps.types) {
            this->printType(os, ppT, stack);
            os << ",";
        }
        for (const auto& ppV : pps.values) {
            os << ppV;
            os << ",";
        }
        os << ">";
    }
}

void HMTypeInferrence::expandIvars(HIRTypeRef& type) {
    if (!type->hasTypeInfer()) {
        return;
    }
    if (::std::find(expandStack.begin(), expandStack.end(), type) != expandStack.end()) {
        return;
    }
    expandStack.push_back(type);

    struct Guard {
        ::std::vector<HIRTypeRef>& stack;

        ~Guard() {
            stack.pop_back();
        }
    } guard{expandStack};

    if (type->is_Infer()) {
        const auto& resolved = this->getType(type);
        if (resolved != type) {
            type = resolved;
        }
        return;
    }

    auto data = type->cloneData();

    struct H {
        static void expandIvarsPath(/*const*/ HMTypeInferrence& self, HIRPath& path) {
            TU_MATCH(HIRPath::Data, (path.mData), (e2), (Generic, self.expandIvarsParams(e2.mParams);), (UfcsKnown, self.expandIvars(e2.type); self.expandIvarsParams(e2.trait.mParams); self.expandIvarsParams(e2.params);), (UfcsUnknown, self.expandIvars(e2.type); self.expandIvarsParams(e2.params);), (UfcsInherent, self.expandIvars(e2.type); self.expandIvarsParams(e2.params);))
        }
    };

    TU_MATCH_HDRA( (data), {)
    TU_ARMA(Infer, e) {
        }
        TU_ARMA(Diverge, e) {
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Path, e) {
            // Iterate all arguments
            H::expandIvarsPath(*this, e.path);
        }
        TU_ARMA(Generic, e) {
        }
        TU_ARMA(TraitObject, e) {
            this->expandIvarsParams(e.mTrait.mPath.mParams);
            for (auto& marker : e.markers) {
                this->expandIvarsParams(marker.mParams);
            }
            // TODO: Associated types
        }
        TU_ARMA(ErasedType, e) {
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Fcn, ee) {
                    H::expandIvarsPath(*this, ee.origin);
                }
                TU_ARMA(Known, ee) {
                    this->expandIvars(ee);
                }
                TU_ARMA(Alias, ee) {
                }
        }
        for(auto& trait : e.traits)
        {
                this->expandIvarsParams(trait.mPath.mParams);
                // TODO: Associated types
        }
        }
        TU_ARMA(Array, e) {
            this->expandIvars(e.inner);
        }
        TU_ARMA(Slice, e) {
            this->expandIvars(e.inner);
        }
        TU_ARMA(Tuple, e) {
            for (auto& ty : e) {
                this->expandIvars(ty);
            }
        }
        TU_ARMA(Borrow, e) {
            this->expandIvars(e.inner);
        }
        TU_ARMA(Pointer, e) {
            this->expandIvars(e.inner);
        }
        TU_ARMA(NamedFunction, e) {
            H::expandIvarsPath(*this, e.path);
        }
        TU_ARMA(Function, e) {
            this->expandIvars(e.mRettype);
            for (auto& ty : e.argTypes) {
                this->expandIvars(ty);
            }
        }
        TU_ARMA(NodeType, e) {
        }
    }
    type = types.intern(std::move(data));
}

void HMTypeInferrence::expandIvarsParams(HIRPathParams& params) {
    for (auto& arg : params.types) {
        expandIvars(arg);
    }
}

void HMTypeInferrence::addIvars(HIRTypeRef& type) {
    if (type->is_Infer() && type->as_Infer().index == ~0u) {
        type = newIvarTr(type->as_Infer().tyClass);
        this->markChange();
        DEBUG("New ivar " << type);
        return;
    }

    auto data = type->cloneData();
    TU_MATCH_HDRA( (data), {)
    TU_ARMA(Infer, e) {
        }
        TU_ARMA(Diverge, e) {
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Path, e) {
            // Iterate all arguments
            TU_MATCH(HIRPath::Data, (e.path.mData), (e2), (Generic, this->addIvarsParams(e2.mParams);), (UfcsKnown, this->addIvars(e2.type); this->addIvarsParams(e2.trait.mParams); this->addIvarsParams(e2.params);), (UfcsUnknown, this->addIvars(e2.type); this->addIvarsParams(e2.params);), (UfcsInherent, this->addIvars(e2.type); this->addIvarsParams(e2.params);))
        }
        TU_ARMA(Generic, e) {
        }
        TU_ARMA(TraitObject, e) {
            // Iterate all paths
            this->addIvarsParams(e.mTrait.mPath.mParams);
            for (auto& aty : e.mTrait.typeBounds) {
                this->addIvars(aty.second.type);
            }
            for (auto& marker : e.markers) {
                this->addIvarsParams(marker.mParams);
            }
        }
        TU_ARMA(ErasedType, e) {
            if (typeContainsIvars(type, /*only_unbound=*/true)) {
                BUG(Span(), "ErasedType getting ivars added - " << type);
            }
        }
        TU_ARMA(Array, e) {
            addIvars(e.inner);
            if (e.size.is_Unevaluated()) {
                addIvars(e.size.as_Unevaluated());
            }
        }
        TU_ARMA(Slice, e) {
            addIvars(e.inner);
        }
        TU_ARMA(Tuple, e) {
            for (auto& ty : e) {
                addIvars(ty);
            }
        }
        TU_ARMA(Borrow, e) {
            addIvars(e.inner);
        }
        TU_ARMA(Pointer, e) {
            addIvars(e.inner);
        }
        TU_ARMA(NamedFunction, e) {
            // Shouldn't be possible?
            // Even if it is seen, it shouldn't have any empty ivars
        }
        TU_ARMA(Function, e) {
            addIvars(e.mRettype);
            for (auto& ty : e.argTypes) {
                addIvars(ty);
            }
        }
        TU_ARMA(NodeType, e) {
            // Shouldn't be possible
        }
    }
    type = types.intern(std::move(data));
}

void HMTypeInferrence::addIvars(HIRConstGeneric& val) {
    if (val.is_Infer()) {
        if (val.as_Infer().index == ~0u) {
            val.as_Infer().index = newIvarVal();
            this->markChange();
            DEBUG("New ivar " << val);
        }
    }
}

void HMTypeInferrence::addIvarsParams(HIRPathParams& params) {
    for (auto& arg : params.types) {
        addIvars(arg);
    }
    for (auto& arg : params.values) {
        addIvars(arg);
    }
}

unsigned int HMTypeInferrence::newIvar(HIRInferClass ic /* = HIR::InferClass::None*/) {
    auto rv = ivars.size();
    ivars.emplace_back(types.infer(rv, ic));
    DEBUG("New type IVar " << rv);
    return rv;
}

HIRTypeRef HMTypeInferrence::newIvarTr(HIRInferClass ic /* = HIR::InferClass::None*/) {
    return ivars.at(this->newIvar(ic)).type;
}

unsigned int HMTypeInferrence::newIvarVal() {
    values.push_back(IVarValue());
    values.back().val->as_Infer().index = values.size() - 1;
    return values.size() - 1;
}

void HMTypeInferrence::setIvarValTo(unsigned int slot, HIRConstGeneric val) {
    ASSERT_BUG(Span(), slot < values.size(), "slot " << slot << " >= " << values.size());
    ASSERT_BUG(Span(), !values[slot].isAlias(), "slot " << slot);
    if (*values[slot].val == val) {
    } else {
        DEBUG("Set ValIVar " << slot << " = " << val);
        ASSERT_BUG(Span(), values[slot].val->is_Infer(), "slot " << slot << " - " << *values[slot].val);
        ASSERT_BUG(Span(), values[slot].val->as_Infer().index == slot, "slot " << slot << " - " << *values[slot].val);
        *values[slot].val = std::move(val);
    }
}

void HMTypeInferrence::ivarValUnify(unsigned int leftSlot, unsigned int rightSlot) {
    Span sp;
    ASSERT_BUG(sp, leftSlot < values.size(), "slot " << leftSlot << " >= " << values.size());
    ASSERT_BUG(sp, rightSlot < values.size(), "slot " << leftSlot << " >= " << values.size());
    ASSERT_BUG(sp, !values[leftSlot].isAlias(), "slot " << leftSlot);
    ASSERT_BUG(sp, !values[rightSlot].isAlias(), "slot " << rightSlot);

    if (/*const auto* re =*/values[rightSlot].val->opt_Infer()) {
        DEBUG("Set ValIVar " << rightSlot << " = @" << leftSlot);
        values[rightSlot].alias = leftSlot;
        values[rightSlot].val.reset();

        this->markChange();
    } else {
        BUG(sp, "Unifiying over a set value");
    }
}

//::HIR::TypeRef& HMTypeInferrence::get_type(::HIR::TypeRef& type)
//{
//    }
//    }
//}

const HIRTypeData* HMTypeInferrence::getType(const HIRTypeData* type) const {
    const auto* current = &type;
    for (size_t count = 0; count <= ivars.size(); count++) {
        const auto* e = (*current)->opt_Infer();
        if (!e) {
            return *current;
        }
        ASSERT_BUG(Span(), e->index != ~0u, "Encountered non-populated IVar");

        const auto* next = &getPointedIvar(e->index).type;
        if (*next == *current) {
            return *current;
        }
        current = next;
    }
    this->dump();
    BUG(Span(), "Loop detected while resolving type " << type);
}

HIRTypeRef& HMTypeInferrence::getType(unsigned idx) {
    assert(idx != ~0u);
    auto* current = &getPointedIvar(idx).type;
    for (size_t count = 0; count <= ivars.size(); count++) {
        const auto* e = (*current)->opt_Infer();
        if (!e) {
            return *current;
        }
        auto* next = &getPointedIvar(e->index).type;
        if (*next == *current) {
            return *current;
        }
        current = next;
    }
    this->dump();
    BUG(Span(), "Loop detected while resolving type ivar " << idx);
}

const HIRTypeData* HMTypeInferrence::getType(unsigned idx) const {
    assert(idx != ~0u);
    const auto* current = &getPointedIvar(idx).type;
    for (size_t count = 0; count <= ivars.size(); count++) {
        const auto* e = (*current)->opt_Infer();
        if (!e) {
            return *current;
        }
        const auto* next = &getPointedIvar(e->index).type;
        if (*next == *current) {
            return *current;
        }
        current = next;
    }
    this->dump();
    BUG(Span(), "Loop detected while resolving type ivar " << idx);
}

void HMTypeInferrence::setIvarTo(unsigned int slot, HIRTypeRef type) {
    auto sp = Span();
    auto& rootIvar = this->getPointedIvar(slot);
    DEBUG("set_ivar_to(" << slot << " { " << rootIvar.type << " }, " << type << ")");

    // If the left type was '_', alias the right to it
    if (const auto* lE = type->opt_Infer()) {
        assert(lE->index != slot);
        if (lE->tyClass != HIRInferClass::None) {
            TU_MATCH_DEF(
                HIRTypeData,
                ((*rootIvar.type)),
                (e),
                (ERROR(sp, E0000, "Type unificiation of literal with invalid type - " << rootIvar.type);),
                (Primitive, checkTypeClassPrimitive(sp, type, lE->tyClass, e);),
                (Infer,
                 // Check for right having a ty_class
                 if (e.tyClass != HIRInferClass::None && e.tyClass != lE->tyClass) { ERROR(sp, E0000, "Unifying types with mismatching literal classes - " << type << " := " << rootIvar.type); })
            )
        }

        // Alias `l_e.index` to this slot
        DEBUG("Set IVar " << lE->index << " = @" << slot);
        auto& rIvar = this->getPointedIvar(lE->index);
        rIvar.alias = slot;
        rIvar.type = nullptr;
    } else if (rootIvar.type == type) {
        return;
    } else {
        // Erase (replace with blank) lifetimes
        // TODO: Avoid needing to clone in all cases?
        struct MonomorphAddLifetimes: public Monomorphiser {
            explicit MonomorphAddLifetimes(HIRTypeInterner& types)
                : Monomorphiser(types)
            {
            }

            HIRTypeRef getType(const Span& sp, const HIRGenericRef& g) const override {
                return types.generic(g.name, g.binding);
            }

            HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override {
                return g;
            }

        };

        type = MonomorphAddLifetimes(types).monomorphType(sp, type, true);

        // Otherwise, store left in right's slot
        DEBUG("Set IVar " << slot << " = " << type);
        if (const auto* e = rootIvar.type->opt_Infer()) {
            switch (e->tyClass) {
                case HIRInferClass::None:
                    break;
                case HIRInferClass::Integer:
                case HIRInferClass::Float:
                    // `type` can't be an ivar, so it has to be a primitive (or an associated?)
                    if (const auto* lE = type->opt_Primitive()) {
                        checkTypeClassPrimitive(sp, type, e->tyClass, *lE);
                    } else if (type->is_Diverge()) {
                        // ... acceptable
                    } else {
                        BUG(sp, "Setting primitive to " << type);
                    }
                    break;
            }
        } else {
            BUG(sp, "Overwriting ivar " << slot << " (" << rootIvar.type << ") with " << type);
        }

        rootIvar.type = type;
    }

    this->markChange();
}

void HMTypeInferrence::ivarUnify(unsigned int leftSlot, unsigned int rightSlot) {
    auto sp = Span();
    if (leftSlot != rightSlot) {
        auto& leftIvar = this->getPointedIvar(leftSlot);

        // TODO: Assert that setting this won't cause a loop.
        auto& rootIvar = this->getPointedIvar(rightSlot);

        if (const auto* re = rootIvar.type->opt_Infer()) {
            DEBUG("Class unify " << leftIvar.type << " <- " << rootIvar.type);

            if (re->tyClass != HIRInferClass::None) {
                if (const auto* le = leftIvar.type->opt_Infer()) {
                    if (le->tyClass != HIRInferClass::None && le->tyClass != re->tyClass) {
                        ERROR(sp, E0000, "Unifying types with mismatching literal classes - " << leftIvar.type << " := " << rootIvar.type);
                    }
                    if (le->tyClass == HIRInferClass::None) {
                        leftIvar.type = types.infer(le->index, re->tyClass);
                    }
                } else if (const auto* le = leftIvar.type->opt_Primitive()) {
                    checkTypeClassPrimitive(sp, leftIvar.type, re->tyClass, *le);
                } else {
                    ERROR(sp, E0000, "Type unificiation of literal with invalid type - " << leftIvar.type);
                }
            } else {
            }
        } else {
            BUG(sp, "Unifying over a concrete type - " << rootIvar.type);
        }

        DEBUG("IVar " << rootIvar.type->as_Infer().index << " = @" << leftSlot);
        rootIvar.alias = leftSlot;
        rootIvar.type = nullptr;

        this->markChange();
    }
}

const HIRConstGeneric& HMTypeInferrence::getValue(const HIRConstGeneric& val) const {
    if (val.is_Infer()) {
        return getValue(val.as_Infer().index);
    } else {
        return val;
    }
}

const HIRConstGeneric& HMTypeInferrence::getValue(unsigned slot) const {
    ASSERT_BUG(Span(), slot != ~0u, "HMTypeInferrence::get_value: Value generic ivar index not assigned");
    auto index = slot;
    // Limit the iteration count to the number of ivars
    for (unsigned int count = 0; count < values.size(); count++) {
        ASSERT_BUG(Span(), index < values.size(), "");
        auto& ent = values[index];
        if (!ent.isAlias()) {
            return *ent.val;
        }
        index = ent.alias;
    }
    this->dump();
    BUG(Span(), "Loop detected in ivar list when starting at " << slot << ", current is " << index);
}

HMTypeInferrence::IVar& HMTypeInferrence::getPointedIvar(unsigned int slot) const {
    auto index = slot;
    unsigned int count = 0;
    assert(index < ivars.size());
    while (ivars.at(index).isAlias()) {
        index = ivars.at(index).alias;

        if (count >= ivars.size()) {
            this->dump();
            BUG(Span(), "Loop detected in ivar list when starting at " << slot << ", current is " << index);
        }
        count++;
    }
    return const_cast<IVar&>(ivars.at(index));
}

bool HMTypeInferrence::pathparamsContainIvars(const HIRPathParams& pps, bool onlyUnbound) const {
    for (const auto& ty : pps.types) {
        if (this->typeContainsIvars(ty, onlyUnbound)) {
            return true;
        }
    }
    return false;
}

bool HMTypeInferrence::typeContainsIvars(const HIRTypeData* ty, bool onlyUnbound) const {
    if (!ty->hasTypeInfer()) {
        return false;
    }
    TRACE_FUNCTION_F("ty = " << ty);
    auto pathContainsIvars = [this](const HIRPath& path, bool onlyUnbound) {
        TU_MATCH(HIRPath::Data, (path.mData), (pe), (Generic, return this->pathparamsContainIvars(pe.mParams, onlyUnbound);), (UfcsKnown, if (this->typeContainsIvars(pe.type, onlyUnbound)) return true; if (this->pathparamsContainIvars(pe.trait.mParams, onlyUnbound)) return true; return this->pathparamsContainIvars(pe.params, onlyUnbound);), (UfcsInherent, if (this->typeContainsIvars(pe.type, onlyUnbound)) return true; return this->pathparamsContainIvars(pe.params, onlyUnbound);), (UfcsUnknown, BUG(Span(), "UfcsUnknown");))
        throw "";
    };
    //TU_MATCH(::HIR::TypeData, (this->get_type(ty).m_data), (e),
    TU_MATCH(HIRTypeData, (*ty), (e),
    (Infer,
        if( onlyUnbound ) {
        return e.index == ~0u;
        }
        return true;
        ),
    (Primitive, return false; ),
    (Diverge, return false; ),
    (Generic, return false; ),
    (Path,
        return pathContainsIvars(e.path, onlyUnbound);
        ),
    (Borrow,
        return typeContainsIvars(e.inner, onlyUnbound);
        ),
    (Pointer,
        return typeContainsIvars(e.inner, onlyUnbound);
        ),
    (Slice,
        return typeContainsIvars(e.inner, onlyUnbound);
        ),
    (Array,
        return typeContainsIvars(e.inner, onlyUnbound);
        ),
    (NodeType,
        return false;
        ),
    (NamedFunction,
        return pathContainsIvars(e.path, onlyUnbound);
        ),
    (Function,
        for(const auto& arg : e.argTypes)
            if( typeContainsIvars(arg, onlyUnbound) )
                return true;
        return typeContainsIvars(e.mRettype, onlyUnbound);
        ),
    (TraitObject,
        for(const auto& marker : e.markers)
            if( pathparamsContainIvars(marker.mParams, onlyUnbound) )
                return true;
        return pathparamsContainIvars(e.mTrait.mPath.mParams, onlyUnbound);
        ),
    (ErasedType,
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Fcn, ee) {
            return pathContainsIvars(ee.origin, onlyUnbound);
}

TU_ARMA(Known, ee) {
    return typeContainsIvars(ee, onlyUnbound);
}

TU_ARMA(Alias, ee) {
    return false;
}
}
        ),
    (Tuple,
        for(const auto& st : e)
            if( typeContainsIvars(st, onlyUnbound) )
                return true;
        return false;
        )
    )
    throw "";
        }

        namespace {
            bool typeListEqual(const HMTypeInferrence& context, const ::std::vector<HIRTypeRef>& l, const ::std::vector<HIRTypeRef>& r) {
                if (l.size() != r.size()) {
                    return false;
                }

                for (unsigned int i = 0; i < l.size(); i++) {
                    if (!context.typesEqual(l[i], r[i])) {
                        return false;
                    }
                }
                return true;
            }

            bool typeListEqual(const HMTypeInferrence& context, const ThinVector<HIRTypeRef>& l, const ThinVector<HIRTypeRef>& r) {
                if (l.size() != r.size()) {
                    return false;
                }

                for (unsigned int i = 0; i < l.size(); i++) {
                    if (!context.typesEqual(l[i], r[i])) {
                        return false;
                    }
                }
                return true;
            }
        }

        bool HMTypeInferrence::pathparamsEqual(const HIRPathParams& ppsL, const HIRPathParams& ppsR) const {
            return typeListEqual(*this, ppsL.types, ppsR.types);
        }

        bool HMTypeInferrence::typesEqual(const HIRTypeData* rl, const HIRTypeData* rr) const {
            const auto& l = this->getType(rl);
            const auto& r = this->getType(rr);
            if (l->tag() != r->tag()) {
                return false;
            }

            struct H {
                static bool comparePath(const HMTypeInferrence& self, const HIRPath& l, const HIRPath& r) {
                    if (l.mData.tag() != r.mData.tag()) {
                        return false;
                    }
                    TU_MATCH(HIRPath::Data, (l.mData, r.mData), (lpe, rpe), (Generic, if (lpe.mPath != rpe.mPath) return false; return self.pathparamsEqual(lpe.mParams, rpe.mParams);), (UfcsKnown, if (lpe.item != rpe.item) return false; if (!self.typesEqual(lpe.type, rpe.type)) return false; if (!self.pathparamsEqual(lpe.trait.mParams, rpe.trait.mParams)) return false; return self.pathparamsEqual(lpe.params, rpe.params);), (UfcsInherent, if (lpe.item != rpe.item) return false; if (!self.typesEqual(lpe.type, rpe.type)) return false; return self.pathparamsEqual(lpe.params, rpe.params);), (UfcsUnknown, BUG(Span(), "UfcsUnknown");))
                    throw "";
                }
            };

    TU_MATCH(HIRTypeData, (*l, *r), (le, re),
    (Infer, return le.index == re.index; ),
    (Primitive, return le == re; ),
    (Diverge, return true; ),
    (Generic, return le.binding == re.binding; ),
    (Path,
        return H::comparePath(*this, le.path, re.path);
        ),
    (Borrow,
        if( le.type != re.type )
            return false;
        return typesEqual(le.inner, re.inner);
        ),
    (Pointer,
        if( le.type != re.type )
            return false;
        return typesEqual(le.inner, re.inner);
        ),
    (Slice,
        return typesEqual(le.inner, re.inner);
        ),
    (Array,
        if( le.size != re.size )
            return false;
        return typesEqual(le.inner, re.inner);
        ),
    (NodeType,
        return le == re;
        ),
    (NamedFunction,
        return H::comparePath(*this, le.path, re.path);
        ),
    (Function,
        if( le.isUnsafe != re.isUnsafe || le.mAbi != re.mAbi )
            return false;
        if( !typeListEqual(*this, le.argTypes, re.argTypes) )
            return false;
        return typesEqual(le.mRettype, re.mRettype);
        ),
    (TraitObject,
        if( le.markers.size() != re.markers.size() )
            return false;
        for(unsigned int i = 0; i < le.markers.size(); i ++) {
        const auto& lm = le.markers[i];
        const auto& rm = re.markers[i];
        if (lm.mPath != rm.mPath) {
            return false;
        }
        if (!pathparamsEqual(lm.mParams, rm.mParams)) {
            return false;
        }
        }
        if( le.mTrait.mPath.mPath != re.mTrait.mPath.mPath )
            return false;
        return pathparamsEqual(le.mTrait.mPath.mParams, re.mTrait.mPath.mParams);
        ),
    (ErasedType,
        if( le.inner.tag() != re.inner.tag() )
            return false;
        TU_MATCH_HDRA( (le.inner, re.inner), {)
        TU_ARMA(Fcn, l,r) {
            ASSERT_BUG(Span(), l.origin != HIRSimplePath(), "Erased type with unset origin");
            ASSERT_BUG(Span(), r.origin != HIRSimplePath(), "Erased type with unset origin");
            return H::comparePath(*this, l.origin, r.origin);
        }

        TU_ARMA(Known, l, r) {
            return typesEqual(l, r);
        }

        TU_ARMA(Alias, l, r) {
            if (l.inner.get() != r.inner.get()) { // Pointer comparison
                return false;
            }
            return pathparamsEqual(l.params, r.params);
        }
        }
        ),
    (Tuple,
        return typeListEqual(*this, le, re);
        )
    )
    throw "";
        }

        // --------------------------------------------------------------------
        // TraitResolution
        // --------------------------------------------------------------------

        namespace {
            HIRCompare compareValue(const Span& sp, const HIRConstGeneric& leftRaw, const HIRConstGeneric& rightRaw, const HMTypeInferrence& infer) {
                const auto& left = leftRaw.is_Infer() ? infer.getValue(leftRaw.as_Infer().index) : leftRaw;
                const auto& right = rightRaw.is_Infer() ? infer.getValue(rightRaw.as_Infer().index) : rightRaw;
                if (left == right) {
                    return HIRCompare::Equal;
                }
                if (left.is_Infer() || right.is_Infer()) {
                    return HIRCompare::Fuzzy;
                }
                if (left.is_Generic() && left.as_Generic().isPlaceholder()) {
                    return HIRCompare::Fuzzy;
                }
                if (right.is_Generic() && right.as_Generic().isPlaceholder()) {
                    return HIRCompare::Fuzzy;
                }
                //TODO(sp, "compare_value: " << left << " == " << right);
                return HIRCompare::Unequal;
            }
        }

        HIRCompare TraitResolution::comparePp(const Span& sp, const HIRPathParams& left, const HIRPathParams& right) const {
            ASSERT_BUG(sp, left.types.size() == right.types.size(), "Parameter count mismatch - `" << left << "` vs `" << right << "`");
            ASSERT_BUG(sp, left.values.size() == right.values.size(), "Parameter count mismatch - `" << left << "` vs `" << right << "`");
            HIRCompare ord = HIRCompare::Equal;
            for (unsigned int i = 0; i < left.types.size(); i++) {
                // TODO: Should allow fuzzy matches using placeholders (match_test_generics_fuzz works for that)
                // - Better solution is to remove the placeholders in method searching.
                ord &= left.types[i]->compareWithPlaceholders(sp, right.types[i], this->ivars.callbackResolveInfer());
                if (ord == HIRCompare::Unequal) {
                    return ord;
                }
            }
            for (unsigned int i = 0; i < left.values.size(); i++) {
                ord &= compareValue(sp, left.values[i], right.values[i], this->ivars);
                if (ord == HIRCompare::Unequal) {
                    return ord;
                }
            }
            return ord;
        }

        // -------------------------------------------------------------------------------------------------------------------
        // -------------------------------------------------------------------------------------------------------------------
        bool TraitResolution::iterateBoundsTraits(const Span& sp, const HIRTypeData* type, const HIRSimplePath& trait, tCbBound cb) const {
            return iterateBoundsTraits(sp, type, [&](HIRCompare cmp, const HIRTypeData* t, const HIRGenericPath& tr, const CachedBound& b) {
                if (tr.mPath != trait) {
                    return false;
                }
                return cb(cmp, t, tr, b);
            });
        }

        bool TraitResolution::iterateBoundsTraits(const Span& sp, const HIRTypeData* type, tCbBound cb) const {
            for (const auto& b : traitBounds) {
                auto cmp = b.first.first->compareWithPlaceholders(sp, type, this->ivars.callbackResolveInfer());
                if (cmp == HIRCompare::Unequal) {
                    continue;
                }
                if (cb(cmp, b.first.first, b.first.second, b.second)) {
                    return true;
                }
            }
            return false;
        }

        bool TraitResolution::iterateBoundsTraits(const Span& sp, tCbBound cb) const {
            for (const auto& b : traitBounds) {
                if (cb(HIRCompare::Equal, b.first.first, b.first.second, b.second)) {
                    return true;
                }
            }
            return false;
        }

        bool TraitResolution::iterateAtyBounds(const Span& sp, const HIRPath::Data::Data_UfcsKnown& pe, ::std::function<bool(const HIRTraitPath&)> cb) const {
            HIRGenericPath traitPath;
            DEBUG("Checking ATY bounds on " << pe.trait << " :: " << pe.item);
            if (!this->traitContainsType(sp, pe.trait, this->crate.getTraitByPath(sp, pe.trait.mPath), pe.item.c_str(), traitPath)) {
                BUG(sp, "Cannot find associated type " << pe.item << " anywhere in trait " << pe.trait);
            }
            DEBUG("trait_path=" << traitPath);
            const auto& traitRef = crate.getTraitByPath(sp, traitPath.mPath);
            const auto& atyDef = traitRef.types.find(pe.item)->second;

            for (const auto& bound : atyDef.traitBounds) {
                if (cb(bound)) {
                    return true;
                }
            }
            // Search `<Self as Trait>::Name` bounds on the trait itself
            for (const auto& bound : traitRef.mParams.bounds) {
                if (!bound.is_TraitBound()) {
                    continue;
                }
                const auto& be = bound.as_TraitBound();

                if (!be.type->is_Path()) {
                    continue;
                }
                if (!be.type->as_Path().binding.is_Opaque()) {
                    continue;
                }

                const auto& beTypePe = be.type->as_Path().path.mData.as_UfcsKnown();
                if (beTypePe.type != crate.types.self()) {
                    continue;
                }
                if (beTypePe.trait.mPath != pe.trait.mPath) {
                    continue;
                }
                if (beTypePe.item != pe.item) {
                    continue;
                }

                if (cb(be.trait)) {
                    return true;
                }
            }

            return false;
        }

        bool TraitResolution::findTraitImplsMagic(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* ty, tCbTraitImplR callback) const {
            static HIRPathParams nullParams;
            static HIRTraitPath::assocListT nullAssoc;

            const auto langCoerceUnsized = this->crate.getLangItemPathOpt("coerce_unsized");
            const auto langFnPtr = this->crate.getLangItemPathOpt("fn_ptr_trait");
            const auto langTuple = this->crate.getLangItemPathOpt("tuple_trait");

            const auto& type = this->ivars.getType(ty);
            TRACE_FUNCTION_F("trait = " << trait << params << ", type = " << type);

            if (trait == mLangSized) {
                auto cmp = typeIsSized(sp, type);
                if (cmp != HIRCompare::Unequal) {
                    return callback(ImplRef(type, &nullParams, &nullAssoc), cmp);
                } else {
                    return false;
                }
            }

            if (trait == mLangCopy) {
                auto cmp = this->typeIsCopy(sp, type);
                if (cmp != HIRCompare::Unequal) {
                    return callback(ImplRef(type, &nullParams, &nullAssoc), cmp);
                } else {
                    return false;
                }
            }

            if (!langFnPtr.components().empty() && trait == langFnPtr) {
                if (type->is_Function()) {
                    return callback(ImplRef(type, &nullParams, &nullAssoc), HIRCompare::Equal);
                }
            }

            if (trait == mLangClone && (type->is_Tuple() || type->is_Array() || type->is_Function() || type->is_NodeType() || type->is_NamedFunction() || TU_TEST1(*type, Path, .isClosure()))) {
                auto cmp = this->typeIsClone(sp, type);
                if (cmp != HIRCompare::Unequal) {
                    return callback(ImplRef(type, &nullParams, &nullAssoc), cmp);
                } else {
                    return false;
                }
            }

            // - `DiscriminantKind`
            if (!mLangDiscriminantKind.components().empty() && trait == mLangDiscriminantKind) {
                static auto nameDiscriminant = RcString::newInterned("Discriminant");
                // TODO: This logic is near identical to the logic in `static.cpp` - can it be de-duplicated?

                if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
                    // TODO: How to prevent EAT from expanding (or setting opaque) too early?
                    return callback(ImplRef(type, HIRPathParams(), HIRTraitPath::assocListT()), HIRCompare::Fuzzy);
                } else if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
                    HIRTraitPath::assocListT assocList;
                    assocList.insert(std::make_pair(nameDiscriminant, HIRTraitPath::AtyEqual{trait, {}, crate.types.path(HIRPath(type, trait.clone(), nameDiscriminant), HIRTypePathBinding::make_Opaque({}))}));
                    return callback(ImplRef(type, HIRPathParams(), HIRTraitPath::assocListT()), HIRCompare::Equal);
                } else if (type->is_Path() && type->as_Path().binding.is_Enum()) {
                    const auto& enm = *type->as_Path().binding.as_Enum();
                    HIRTypeRef tagTy = crate.types.primitive(enm.getReprType(enm.tagRepr));
                    HIRTraitPath::assocListT assocList;
                    assocList.insert(std::make_pair(nameDiscriminant, HIRTraitPath::AtyEqual{trait, {}, std::move(tagTy)}));
                    return callback(ImplRef(type, {}, std::move(assocList)), HIRCompare::Equal);
                } else {
                    HIRTraitPath::assocListT assocList;
                    assocList.insert(std::make_pair(nameDiscriminant, HIRTraitPath::AtyEqual{trait, {}, crate.types.primitive(HIRCoreType::U8)}));
                    return callback(ImplRef(type, {}, std::move(assocList)), HIRCompare::Equal);
                }
            }
            if (!mLangPointee.components().empty() && trait == mLangPointee) {
                static auto nameMetadata = RcString::newInterned("Metadata");
                // TODO: This logic is near identical to the logic in `static.cpp` - can it be de-duplicated?

                HIRTypeRef metaTy = crate.types.infer();
                bool hasMetaTy = false;
                if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
                    return callback(ImplRef(type, HIRPathParams(), HIRTraitPath::assocListT()), HIRCompare::Fuzzy);
                }
                // Generics (or opaque ATYs)
                else if (type->is_Generic() || (type->is_Path() && type->as_Path().binding.is_Opaque())) {
                    // If the type is `Sized` return `()` as the type
                    if (typeIsSized(sp, type) != HIRCompare::Unequal) {
                        metaTy = crate.types.unit();
                        hasMetaTy = true;
                    } else {
                        // Return unbounded
                        // - leave as `_`
                    }
                }
                // Trait object: `Metadata=DynMetadata<T>`
                else if (type->is_TraitObject()) {
                    metaTy = crate.types.path(HIRPath(HIRGenericPath(this->crate.getLangItemPath(sp, "dyn_metadata"), HIRPathParams(type))), HIRTypePathBinding::make_Struct(&crate.getStructByPath(sp, this->crate.getLangItemPath(sp, "dyn_metadata"))));
                    hasMetaTy = true;
                }
                // Slice and str
                else if (type->is_Slice() || TU_TEST1(*type, Primitive, == HIRCoreType::Str)) {
                    metaTy = crate.types.primitive(HIRCoreType::Usize);
                    hasMetaTy = true;
                }
                // Structs: Can delegate their metadata
                else if (type->is_Path() && type->as_Path().binding.is_Struct()) {
                    const auto& str = *type->as_Path().binding.as_Struct();
                    switch (str.structMarkings.dstType) {
                        case HIRStructMarkings::DstType::None:
                            metaTy = crate.types.unit();
                            hasMetaTy = true;
                            break;
                        case HIRStructMarkings::DstType::Possible:
                        case HIRStructMarkings::DstType::TraitObject: {
                            const HIRTypeData* tailTpl = nullptr;
                            TU_MATCHA((str.mData), (se), (Unit, BUG(sp, "Unsized unit struct in Pointee lookup - " << type);), (Tuple, ASSERT_BUG(sp, !se.empty(), "Unsized tuple struct without fields - " << type); tailTpl = se.back().ent;), (Named, ASSERT_BUG(sp, !se.empty(), "Unsized struct without fields - " << type); tailTpl = se.back().ty;))
                            ASSERT_BUG(sp, tailTpl, "Missing unsized tail field for " << type);

                            const auto& path = type->as_Path().path.mData.as_Generic();
                            auto tailTy = MonomorphStatePtr(crate.types, type, &path.mParams, nullptr).monomorphType(sp, tailTpl);
                            tailTy = this->expandAssociatedTypes(sp, std::move(tailTy));

                            return this->findTraitImpls(sp, trait, params, tailTy, [&](ImplRef impl, HIRCompare cmp) {
                                HIRTraitPath::assocListT assoc;
                                auto metadataTy = impl.getType(crate.types, "Metadata", {});
                                if (metadataTy) {
                                    assoc.insert(std::make_pair(nameMetadata, HIRTraitPath::AtyEqual{trait, {}, std::move(metadataTy)}));
                                }
                                return callback(ImplRef(type, params.clone(), std::move(assoc)), cmp);
                            });
                        }
                        case HIRStructMarkings::DstType::Slice:
                            metaTy = crate.types.primitive(HIRCoreType::Usize);
                            hasMetaTy = true;
                            break;
                    }
                } else {
                    metaTy = crate.types.unit();
                    hasMetaTy = true;
                }
                DEBUG("<" << type << " as Pointee>::Metadata = " << metaTy);
                HIRTraitPath::assocListT assocList;
                if (hasMetaTy) {
                    assocList.insert(std::make_pair(RcString::newInterned("Metadata"), HIRTraitPath::AtyEqual{trait, {}, mv$(metaTy)}));
                }

                return callback(ImplRef(type, {}, std::move(assocList)), HIRCompare::Equal);
            }
            // - `Tuple`
            if (!langTuple.components().empty() && trait == langTuple) {
                // Fuzzy impl for `_` and unbound ATYs
                if (type->is_Infer() || (type->is_Path() && type->as_Path().binding.is_Unbound())) {
                    return callback(ImplRef(type, HIRPathParams(), HIRTraitPath::assocListT()), HIRCompare::Fuzzy);
                }
                // Impl for tuples
                if (type->is_Tuple()) {
                    return callback(ImplRef(type, {}, HIRTraitPath::assocListT()), HIRCompare::Equal);
                }
                // No impls for anything else
                return false;
            }

            // Magic Unsize impls to trait objects
            if (trait == mLangUnsize) {
                ASSERT_BUG(sp, params.types.size() == 1, "Unsize trait requires a single type param");
                const auto& dstTy = this->ivars.getType(params.types[0]);

                if (findTraitImplsBound(sp, trait, params, type, callback)) {
                    return true;
                }

                bool rv = false;
                auto cb = [&](auto newDst) {
                    HIRPathParams realParams{mv$(newDst)};
                    rv = callback(ImplRef(type, mv$(realParams), {}), HIRCompare::Fuzzy);
                };
                //if( dst_ty->is_Infer() || type->is_Infer() )
                //{
                //}
                auto cmp = this->canUnsize(sp, dstTy, type, cb);
                if (cmp == HIRCompare::Equal) {
                    assert(!rv);
                    rv = callback(ImplRef(type, params.clone(), {}), HIRCompare::Equal);
                }
                return rv;
            }

            // Magical CoerceUnsized impls for various types
            if (!langCoerceUnsized.components().empty() && trait == langCoerceUnsized) {
                if (findTraitImplsBound(sp, trait, params, type, callback)) {
                    return true;
                }

                const auto& dstTy = params.types.at(0);
                // - `*mut T => *const T`
                if (const auto* e = type->opt_Pointer()) {
                    if (const auto* de = dstTy->opt_Pointer()) {
                        if (de->type < e->type) {
                            auto cmp = e->inner->compareWithPlaceholders(sp, de->inner, this->ivars.callbackResolveInfer());
                            if (cmp != HIRCompare::Unequal) {
                                HIRPathParams pp;
                                pp.types.push_back(dstTy);
                                if (callback(ImplRef(type, mv$(pp), {}), cmp)) {
                                    return true;
                                }
                            }
                        }
                    }
                }
            } else if (trait == mLangPointeeSized) {
                if (findTraitImplsBound(sp, trait, params, type, callback)) {
                    return true;
                }
                // Lowest level of sizedness: This _might_ be sized (i.e. it's not an extern type?)
                return callback(ImplRef(type, {}, HIRTraitPath::assocListT()), HIRCompare::Equal);
            } else if (trait == mLangMetaSized) {
                TODO(sp, "MetaSized");
                // Next level of sizedness: There's metadata that allows getting the size
                // - No difference to the above?
                //switch( this->metadata_type(sp, type) )
                //{
                //case MetadataType::Unknown:
                //case MetadataType::None:
                //case MetadataType::Slice:
                //case MetadataType::TraitObject:
                //case MetadataType::Zero:    // TODO: Does zero apply here?
                //}
            }

            if (trait == mLangDestruct) {
                // Inidicates that something is droppable
                // - Applies to everything?
                if (findTraitImplsBound(sp, trait, params, type, callback)) {
                    return true;
                }
                // Lowest level of sizedness: This _might_ be sized (i.e. it's not an extern type?)
                return callback(ImplRef(type, {}, HIRTraitPath::assocListT()), HIRCompare::Equal);
            }

            return false;
        }

        bool TraitResolution::findTraitImplsTypes(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, tCbTraitImplR callback) const {
    TU_MATCH_HDRA( (*type), {)
    default:
        break;
        TU_ARMA(NodeType, e) {
        TU_MATCH_HDRA((e), {)
        // Magic impls of the Fn* traits for closure types
        TU_ARMA(Closure, nodeP) {
                    DEBUG("Closure, " << trait << " ?= Fn*");
                    if (trait == mLangFn || trait == mLangFnMut || trait == mLangFnOnce) {
                        if (params.types.size() != 1) {
                            BUG(sp, "Fn* traits require a single tuple argument");
                        }
                        if (!params.types[0]->is_Tuple()) {
                            BUG(sp, "Fn* traits require a single tuple argument");
                        }

                        const auto& argsDes = params.types[0]->as_Tuple();
                        if (argsDes.size() != nodeP->mArgs.size()) {
                            return false;
                        }

                        auto cmp = HIRCompare::Equal;
                        ::std::vector<HIRTypeRef> args;
                        for (unsigned int i = 0; i < nodeP->mArgs.size(); i++) {
                            const auto& at = nodeP->mArgs[i].second;
                            args.push_back(at);
                            DEBUG(at << " ?= " << argsDes[i]);
                            cmp &= at->compareWithPlaceholders(sp, argsDes[i], this->ivars.callbackResolveInfer());
                        }
                        if (cmp != HIRCompare::Unequal) {
                            // NOTE: This is a conditional "true", we know nothing about the move/mut-ness of this closure yet
                            // - Could we?
                            // - Not until after the first stage of typeck

                            DEBUG("Closure Fn* impl - cmp = " << cmp);

                            HIRPathParams pp;
                            pp.types.push_back(crate.types.tuple(mv$(args)));
                            HIRTraitPath::assocListT types;
                            types.insert(::std::make_pair("Output", HIRTraitPath::AtyEqual{HIRGenericPath(mLangFnOnce, pp.clone()), {}, nodeP->returnType}));
                            return callback(ImplRef(type, mv$(pp), mv$(types)), cmp);
                        } else {
                            DEBUG("Closure Fn* impl - cmp = Compare::Unequal");
                            return false;
                        }
                    }
                }
                TU_ARMA(Generator, nodeP) {
                    if (trait == mLangGenerator) {
                        static const RcString rcstringYield = RcString::newInterned("Yield");
                        static const RcString rcstringReturn = RcString::newInterned("Return");
                        HIRTraitPath::assocListT assoc;
                        assoc.insert(::std::make_pair(rcstringYield, HIRTraitPath::AtyEqual{trait.clone(), {}, nodeP->yieldTy}));
                        assoc.insert(::std::make_pair(rcstringReturn, HIRTraitPath::AtyEqual{trait.clone(), {}, nodeP->returnType}));
                        HIRPathParams params;
                        params.types.push_back(nodeP->resumeTy);
                        return callback(ImplRef(type, mv$(params), mv$(assoc)), HIRCompare::Equal);
                    }
                }
                TU_ARMA(Async, nodeP) {
                    if (trait == mLangFuture) {
                        static const RcString rcstringOutput = RcString::newInterned("Output");
                        HIRTraitPath::assocListT assoc;
                        assoc.insert(::std::make_pair(rcstringOutput, HIRTraitPath::AtyEqual{trait.clone(), {}, nodeP->mCode->resType}));
                        return callback(ImplRef(type, {}, mv$(assoc)), HIRCompare::Equal);
                    }
                }
        }
        }
        // Magic Fn* trait impls for function pointers
        TU_ARMA(Function, e) {
            if (trait == mLangFn || trait == mLangFnMut || trait == mLangFnOnce) {
                DEBUG("Fn* trait for fn pointer");
                if (params.types.size() != 1) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }
                if (!params.types[0]->is_Tuple()) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }
                const auto& argsDes = params.types[0]->as_Tuple();
                if (argsDes.size() != e.argTypes.size()) {
                    return false;
                }

                // NOTE: unsafe or non-rust ABI functions aren't valid
                if (e.mAbi != ABI_RUST || e.isUnsafe) {
                    DEBUG("- No magic impl, wrong ABI or unsafe in " << type);
                    return false;
                }
                DEBUG("- Magic impl of Fn* for " << type);

                auto cmp = HIRCompare::Equal;
                ::std::vector<HIRTypeRef> args;
                for (unsigned int i = 0; i < e.argTypes.size(); i++) {
                    const auto& at = e.argTypes[i];
                    args.push_back(at);
                    cmp &= at->compareWithPlaceholders(sp, argsDes[i], this->ivars.callbackResolveInfer());
                }

                HIRPathParams pp;
                pp.types.push_back(crate.types.tuple(mv$(args)));
                HIRTraitPath::assocListT types;
                types.insert(::std::make_pair("Output", HIRTraitPath::AtyEqual{HIRGenericPath(mLangFnOnce, pp.clone()), {}, e.mRettype}));
                        return callback(ImplRef(type, mv$(pp), mv$(types)), cmp);
            }
        }
        // Magic Fn* trait impls for function pointers
        TU_ARMA(NamedFunction, realE) {
            if (trait == mLangFn || trait == mLangFnMut || trait == mLangFnOnce) {
                if (params.types.size() != 1) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }
                if (!params.types[0]->is_Tuple()) {
                    BUG(sp, "Fn* traits require a single tuple argument");
                }

                DEBUG("- Magic impl of Fn* for " << type);
                auto e = realE.decay(crate.types, sp);
                DEBUG("> " << e.mRettype << " - " << e.argTypes);
                const auto& argsDes = params.types[0]->as_Tuple();
                if (argsDes.size() != e.argTypes.size()) {
                    return false;
                }

                // NOTE: unsafe or non-rust ABI functions aren't valid
                if (e.mAbi != ABI_RUST) {
                    DEBUG("- No magic impl, wrong ABI (`" << e.mAbi << "`): " << type);
                    return false;
                }
                if (e.isUnsafe) {
                    DEBUG("- No magic impl, unsafe function: " << type);
                    return false;
                }
                DEBUG("- Magic impl of Fn* for " << type);

                auto cmp = HIRCompare::Equal;
                ::std::vector<HIRTypeRef> args;
                for (unsigned int i = 0; i < e.argTypes.size(); i++) {
                    const auto& at = e.argTypes[i];
                    args.push_back(at);
                    cmp &= at->compareWithPlaceholders(sp, argsDes[i], this->ivars.callbackResolveInfer());
                }

                HIRPathParams pp;
                pp.types.push_back(crate.types.tuple(mv$(args)));
                HIRTraitPath::assocListT types;
                types.insert(::std::make_pair("Output", HIRTraitPath::AtyEqual{HIRGenericPath(mLangFnOnce, pp.clone()), {}, e.mRettype}));
                        return callback(ImplRef(type, mv$(pp), mv$(types)), cmp);
            }
        }
        // Magic index and unsize impls for Arrays
        // NOTE: The index impl for [T] is in libcore.
        TU_ARMA(Array, e) {
        }
    }
    return false;
        }

        bool TraitResolution::findTraitImplsLegacy(
            const Span& sp,
            const HIRSimplePath& trait,
            const HIRPathParams& params,
            const HIRTypeData* ty,
            tCbTraitImplR callback,
            bool magicTraitImpls /*=true*/,
            bool searchCrate /*=true*/,
            bool searchBounds /*=true*/
        ) const {
            static HIRPathParams nullParams;
            static HIRTraitPath::assocListT nullAssoc;

            const auto& type = this->ivars.getType(ty);
            TRACE_FUNCTION_F("trait = " << trait << params << ", type = " << type);

            if (magicTraitImpls) {
                if (findTraitImplsMagic(sp, trait, params, ty, callback)) {
                    return true;
                }
            }

            if (findTraitImplsTypes(sp, trait, params, ty, callback)) {
                return true;
            }

            // Trait impls from complex bounds
    TU_MATCH_HDRA( (*type), {)
    default:
        break;
        // Trait objects automatically implement their own traits
        // - IF object safe (TODO)
        TU_ARMA(TraitObject, e) {
            if (trait == e.mTrait.mPath.mPath) {
                auto cmp = comparePp(sp, e.mTrait.mPath.mParams, params);
                if (cmp != HIRCompare::Unequal) {
                    DEBUG("TraitObject impl params" << e.mTrait.mPath.mParams);
                                return callback(ImplRef(type, &e.mTrait.mPath.mParams, &e.mTrait.typeBounds, e.mTrait.constness), cmp);
                }
            }
            // Markers too
            for (const auto& mt : e.markers) {
                if (trait == mt.mPath) {
                    auto cmp = comparePp(sp, mt.mParams, params);
                    if (cmp != HIRCompare::Unequal) {
                        return callback(ImplRef(type, &mt.mParams, &nullAssoc), cmp);
                    }
                }
            }

            if (e.mTrait.mPath.mPath != HIRSimplePath()) {
                // - Check if the desired trait is a supertrait of this.
                // NOTE: `params` (aka des_params) is not used (TODO)
                bool rv = false;
                bool isSupertrait = false;
                this->findNamedTraitInTrait(sp, trait, params, *e.mTrait.traitPtr, e.mTrait.mPath.mPath, e.mTrait.mPath.mParams, type, [&](const HIRTraitPath& iTp) {
                    // The above is just the monomorphised params and associated set. Comparison is still needed.
                    auto cmp = this->comparePp(sp, iTp.mPath.mParams, params);
                    if (cmp != HIRCompare::Unequal) {
                        // Invoke callback with a proper ImplRef
                        HIRTraitPath::assocListT assocClone;
                        for (const auto& e : iTp.typeBounds) {
                            assocClone.insert(::std::make_pair(e.first, e.second.clone()));
                        }
                        for (const auto& bound : e.mTrait.typeBounds) {
                            if (bound.second.sourceTrait.mPath == trait && comparePp(sp, bound.second.sourceTrait.mParams, iTp.mPath.mParams) != HIRCompare::Unequal) {
                                assocClone.erase(bound.first);
                                assocClone.insert(::std::make_pair(bound.first, bound.second.clone()));
                            }
                        }
                        auto ir = ImplRef(type, iTp.mPath.mParams.clone(), mv$(assocClone));
                        DEBUG("TraitObject: - ir = " << ir);
                        isSupertrait = true;
                        rv = callback(mv$(ir), cmp);
                        return cmp == HIRCompare::Equal; // Shortcut if perfect match
                    }
                    return false;
                });
                if (isSupertrait) {
                    return rv;
                }
            }
        } // TU_ARMA(TraitObject, e)
        TU_ARMA(ErasedType, e) {
            for (const auto& traitPath : e.traits) {
                if (trait == traitPath.mPath.mPath) {
                    auto cmp = comparePp(sp, traitPath.mPath.mParams, params);
                    if (cmp != HIRCompare::Unequal) {
                        DEBUG("TraitObject impl params" << traitPath.mPath.mParams);
                                        return callback(ImplRef(type, &traitPath.mPath.mParams, &traitPath.typeBounds, traitPath.constness), cmp);
                    }
                }

                // - Check if the desired trait is a supertrait of this.
                // NOTE: `params` (aka des_params) is not used (TODO)
                bool rv = false;
                bool isSupertrait = false;
                this->findNamedTraitInTrait(sp, trait, params, *traitPath.traitPtr, traitPath.mPath.mPath, traitPath.mPath.mParams, type, [&](const HIRTraitPath& iTp) {
                    // The above is just the monomorphised params and associated set. Comparison is still needed.
                    auto cmp = this->comparePp(sp, iTp.mPath.mParams, params);
                    if (cmp != HIRCompare::Unequal) {
                        // Invoke callback with a proper ImplRef
                        HIRTraitPath::assocListT assocClone;
                        for (const auto& e : iTp.typeBounds) {
                            assocClone.insert(::std::make_pair(e.first, e.second.clone()));
                        }
                        // Existential equalities are stored on the principal
                        // bound even when the associated item is declared by
                        // a supertrait (e.g. `FnMut` carries `FnOnce::Output`).
                        // Project those equalities together with the
                        // supertrait candidate.
                        for (const auto& e : traitPath.typeBounds) {
                            if (e.second.sourceTrait.mPath == trait && comparePp(sp, e.second.sourceTrait.mParams, iTp.mPath.mParams) != HIRCompare::Unequal) {
                                assocClone.erase(e.first);
                                assocClone.insert(::std::make_pair(e.first, e.second.clone()));
                            }
                        }
                        auto ir = ImplRef(type, iTp.mPath.mParams.clone(), mv$(assocClone));
                        DEBUG("ErasedType: - ir = " << ir);
                        isSupertrait = true;
                        rv = callback(mv$(ir), cmp);
                        return cmp == HIRCompare::Equal;
                    }
                    return false;
                });
                if (isSupertrait) {
                    return rv;
                }
            }
        } // TU_ARMA(ErasedType)
        // If the type in question is a magic placeholder, return a placeholder impl :)
        TU_ARMA(Generic, e) {
            if ((e.binding >> 8) == 2) {
                // TODO: This is probably going to break something in the future.
                DEBUG("- Magic impl for placeholder type");
                return callback(ImplRef(type, &nullParams, &nullAssoc), HIRCompare::Fuzzy);
            }
        } // TU_ARMA(Generic)
        // If this type is an opaque UfcsKnown - check bounds
        TU_ARMA(Path, e) {
            if (e.binding.is_Opaque()) {
                ASSERT_BUG(sp, e.path.mData.is_UfcsKnown(), "Opaque bound type wasn't UfcsKnown - " << type);
                const auto& pe = e.path.mData.as_UfcsKnown();

                // TODO: Should Self here be `type` or `pe.type`
                // - Depends... if implicit it should be `type` (as it relates to the associated type), but if explicit it's referring to the trait
                auto monomorphCb = MonomorphStatePtr(crate.types, pe.type, &pe.trait.mParams, &pe.params);
                auto rv = this->iterateAtyBounds(sp, pe, [&](const HIRTraitPath& bound) {
                    DEBUG("Bound on ATY: " << bound);
                    auto ppHrb = HIRPathParams();
                    monomorphCb.ppHrb = &ppHrb;
                    const auto& bParams = bound.mPath.mParams;
                    HIRPathParams paramsMonoO;
                    const HIRPathParams* bParamsMono = &bParams;
                    if (monomorphisePathparamsNeeded(bParams)) {
                        paramsMonoO = monomorphCb.monomorphPathParams(sp, bParams, false);
                        bParamsMono = &paramsMonoO;
                    }
                    const bool paramsNeedNormalisation = ::std::any_of(bParamsMono->types.begin(), bParamsMono->types.end(), [&](const auto& ty) {
                        return this->hasAssociatedType(ty);
                    });
                    if (paramsNeedNormalisation) {
                        if (bParamsMono != &paramsMonoO) {
                            paramsMonoO = bParams.clone();
                            bParamsMono = &paramsMonoO;
                        }
                        this->expandAssociatedTypesParams(sp, paramsMonoO);
                    }

                    HIRTraitPath::assocListT bAtys;
                    for (const auto& aty : bound.typeBounds) {
                        bAtys.insert(::std::make_pair(aty.first, HIRTraitPath::AtyEqual{monomorphCb.monomorphGenericpath(sp, aty.second.sourceTrait, false), {}, monomorphCb.monomorphType(sp, aty.second.type)}));
                    }

                    if (bound.mPath.mPath == trait) {
                        auto cmp = this->comparePp(sp, *bParamsMono, params);
                        if (cmp != HIRCompare::Unequal) {
                            if (bParamsMono == &paramsMonoO) {
                                // TODO: assoc bounds
                                if (callback(ImplRef(type, mv$(paramsMonoO), mv$(bAtys), bound.constness), cmp)) {
                                    return true;
                                }
                                paramsMonoO = monomorphCb.monomorphPathParams(sp, bParams, false);
                                if (paramsNeedNormalisation) {
                                    this->expandAssociatedTypesParams(sp, paramsMonoO);
                                }
                            } else if (!bAtys.empty()) {
                                if (callback(ImplRef(type, bParamsMono->clone(), mv$(bAtys), bound.constness), cmp)) {
                                    return true;
                                }
                            } else {
                                                        if (callback(ImplRef(type, &bound.mPath.mParams, &nullAssoc, bound.constness), cmp)) {
                                    return true;
                                }
                            }
                        }
                    }
                    monomorphCb.ppHrb = nullptr;

                    bool rv = false;
                    bool ret = false;
                    this->findNamedTraitInTrait(sp, trait, params, *bound.traitPtr, bound.mPath.mPath, *bParamsMono, type, [&](const HIRTraitPath& iTp) {
                        auto cmp = this->comparePp(sp, iTp.mPath.mParams, params);
                        DEBUG("Opaque Path: cmp=" << cmp << ", impl " << iTp.mPath << " for " << type << " -- desired " << trait << params);
                        auto ir = ImplRef(type, iTp.mPath.mParams.clone(), {}, iTp.constness);
                        rv |= (cmp != HIRCompare::Unequal && callback(std::move(ir), cmp));
                        ret = true;
                        return false; // Continue
                    });
                    if (ret) {
                        // NOTE: Callback called in closure's return statement
                        return rv;
                    }
                    return false;
                });
                if (rv) {
                    return true;
                }
            }
        } // TU_ARMA(Path)
    } // TU_MATCH_HDRA

    // 1. Search generic params
    if( searchBounds && findTraitImplsBound(sp, trait, params, type, callback) )
        return true;
    // 2. Search crate-level impls
    if( !searchCrate )
        return false;
    return findTraitImplsCrate(sp, trait, params, type,  callback);
        }

        class NextTraitGoalEvaluator {
            enum class Certainty {
                NoSolution,
                Ambiguous,
                Proven,
            };

            enum class CandidateSource {
                Builtin,
                ParamEnv,
                Other,
                TraitImpl,
            };

            enum class OrphanPerspective {
                Local,
                Remote,
            };

            enum class OrphanVisit {
                NonLocal,
                LocalKey,
                Uncovered,
            };

            struct Candidate {
                ImplRef impl;
                HIRCompare headMatch;
                Certainty certainty;
                const HIRMarkerImpl* markerImpl;
                HIRPathParams markerImplParams;
                bool autoBuiltin;
                CandidateSource source;
                bool ambiguityBeyondHead = false;
                bool discarded = false;

                Candidate(ImplRef impl, HIRCompare headMatch, const HIRMarkerImpl* markerImpl, HIRPathParams markerImplParams, bool autoBuiltin, CandidateSource source)
                    : impl(::std::move(impl))
                    , headMatch(headMatch)
                    , certainty(Certainty::Ambiguous)
                    , markerImpl(markerImpl)
                    , markerImplParams(::std::move(markerImplParams))
                    , autoBuiltin(autoBuiltin)
                    , source(source)
                {
                }

                bool isNegative() const {
                    return markerImpl && !markerImpl->isPositive;
                }

                bool isPositiveMarkerImpl() const {
                    return markerImpl && markerImpl->isPositive;
                }
            };

            struct CandidateFrame {
                ::std::vector<Candidate*> candidates;
                ::std::vector<Candidate*> viable;
                size_t availableDepth = 0;
                bool encounteredOverflow = false;

                CandidateFrame() {
                    candidates.reserve(32);
                    viable.reserve(32);
                }

                void clear(stl::ObjList<Candidate>& nodes) {
                    for (auto* candidate : candidates) {
                        nodes.release(candidate);
                    }
                    candidates.clear();
                    viable.clear();
                    availableDepth = 0;
                    encounteredOverflow = false;
                }
            };

            static constexpr size_t ROOT_DEPTH = 128;
            static constexpr size_t OVERFLOW_DEPTH_DIVISOR = 4;

            struct GoalKey {
                size_t hash;
                HIRSimplePath trait;
                HIRPathParams params;
                HIRTypeRef type;
                HIRTraitPath::assocListT associated;

                GoalKey(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated)
                    : hash(hash)
                    , trait(trait)
                    , params(params.clone())
                    , type(type)
                    , associated(cloneAssociated(associated))
                {
                }
            };

            struct CachedGoal {
                GoalKey goal;
                Certainty certainty;
                ImplRef response;
                HIRCompare responseCertainty = HIRCompare::Fuzzy;
                bool hasResponse = false;

                CachedGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated, Certainty certainty)
                    : goal(hash, trait, params, type, associated)
                    , certainty(certainty)
                {
                }
            };

            const TraitResolution& mResolve;
            const HIRCrate& crate;
            const Span* mSpan = nullptr;
            bool coherenceMode = false;

            // Frames and candidates have stable pool-backed addresses.  Vectors
            // are pointer indexes only, so recursive growth never moves an ImplRef
            // or invalidates a parent candidate.
            stl::ObjList<Candidate> candidateNodes;
            ::std::vector<CandidateFrame*> frames;
            size_t frameDepth = 0;
            stl::ObjList<GoalKey> activeGoalNodes;
            stl::ObjList<CachedGoal> cachedGoalNodes;
            ::std::vector<GoalKey*> goalStack;
            ::std::vector<CachedGoal*> goalCache;
            ::std::unordered_multimap<size_t, GoalKey*> activeGoalIndex;
            ::std::unordered_multimap<size_t, CachedGoal*> goalCacheIndex;
            uint64_t responseInstanceCounter = 0;

            struct CanonicalGoal {
                HIRPathParams params;
                HIRTypeRef type;
                HIRTraitPath::assocListT associated;

                CanonicalGoal(HIRPathParams params, HIRTypeRef type)
                    : params(::std::move(params))
                    , type(type)
                {
                }
            };

            const Span& span() const {
                ASSERT_BUG(Span(), mSpan, "next-solver session used outside an evaluation");
                return *mSpan;
            }

            CanonicalGoal canonicalizeGoal(const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated, CanonicalizeTraitGoal& canonicalizer) const {
                auto canonicalParams = canonicalizer.monomorphPathParams(span(), params, true);
                const auto canonicalType = canonicalizer.monomorphType(span(), type, true);
                CanonicalGoal result(::std::move(canonicalParams), canonicalType);
                if (associated) {
                    for (const auto& entry : *associated) {
                        result.associated.insert({entry.first, HIRTraitPath::AtyEqual{canonicalizer.monomorphGenericpath(span(), entry.second.sourceTrait, true), canonicalizer.monomorphPathParams(span(), entry.second.atyParams, true), canonicalizer.monomorphType(span(), entry.second.type, true)}});
                    }
                }
                return result;
            }

            ::std::optional<size_t> availableDepthForNested() {
                if (frameDepth == 0) {
                    return ROOT_DEPTH;
                }
                auto& parent = *frames[frameDepth - 1];
                if (parent.availableDepth == 0) {
                    parent.encounteredOverflow = true;
                    return {};
                }
                return parent.encounteredOverflow ? parent.availableDepth / OVERFLOW_DEPTH_DIVISOR : parent.availableDepth - 1;
            }

            static bool isEnvironmentOrBuiltin(const ImplRef& impl) {
                return !impl.mData.is_TraitImpl();
            }

            bool paramsHaveUnknownTypes(const HIRPathParams& params) const {
                for (const auto& type : params.types) {
                    if (typeHasUnknown(type)) {
                        return true;
                    }
                }
                return false;
            }

            bool pathHasUnknownTypes(const HIRPath& path) const {
                if (const auto* pe = path.mData.opt_Generic()) {
                    return paramsHaveUnknownTypes(pe->mParams);
                }
                if (const auto* pe = path.mData.opt_UfcsInherent()) {
                    return typeHasUnknown(pe->type) || paramsHaveUnknownTypes(pe->params) || paramsHaveUnknownTypes(pe->implParams);
                }
                if (const auto* pe = path.mData.opt_UfcsKnown()) {
                    return typeHasUnknown(pe->type) || paramsHaveUnknownTypes(pe->trait.mParams) || paramsHaveUnknownTypes(pe->params);
                }
                const auto& pe = path.mData.as_UfcsUnknown();
                return typeHasUnknown(pe.type) || paramsHaveUnknownTypes(pe.params);
            }

            bool traitPathHasUnknownTypes(const HIRTraitPath& trait) const {
                if (paramsHaveUnknownTypes(trait.mPath.mParams)) {
                    return true;
                }
                for (const auto& assoc : trait.typeBounds) {
                    if (paramsHaveUnknownTypes(assoc.second.sourceTrait.mParams) || paramsHaveUnknownTypes(assoc.second.atyParams) || typeHasUnknown(assoc.second.type)) {
                        return true;
                    }
                }
                for (const auto& assoc : trait.traitBounds) {
                    if (paramsHaveUnknownTypes(assoc.second.sourceTrait.mParams) || paramsHaveUnknownTypes(assoc.second.atyParams)) {
                        return true;
                    }
                    for (const auto& bound : assoc.second.traits) {
                        if (traitPathHasUnknownTypes(bound)) {
                            return true;
                        }
                    }
                }
                return false;
            }

            bool valueHasUnassignedInfer(const HIRConstGeneric& value) const {
                if (const auto* infer = value.opt_Infer()) {
                    return infer->index == ~0u;
                }
                if (const auto* unevaluated = value.opt_Unevaluated()) {
                    return paramsHaveUnassignedInfer((*unevaluated)->paramsImpl) || paramsHaveUnassignedInfer((*unevaluated)->paramsItem);
                }
                return false;
            }

            bool paramsHaveUnassignedInfer(const HIRPathParams& params) const {
                for (const auto& type : params.types) {
                    if (typeHasUnassignedInfer(type)) {
                        return true;
                    }
                }
                for (const auto& value : params.values) {
                    if (valueHasUnassignedInfer(value)) {
                        return true;
                    }
                }
                return false;
            }

            bool pathHasUnassignedInfer(const HIRPath& path) const {
                if (const auto* pe = path.mData.opt_Generic()) {
                    return paramsHaveUnassignedInfer(pe->mParams);
                }
                if (const auto* pe = path.mData.opt_UfcsInherent()) {
                    return typeHasUnassignedInfer(pe->type) || paramsHaveUnassignedInfer(pe->params) || paramsHaveUnassignedInfer(pe->implParams);
                }
                if (const auto* pe = path.mData.opt_UfcsKnown()) {
                    return typeHasUnassignedInfer(pe->type) || paramsHaveUnassignedInfer(pe->trait.mParams) || paramsHaveUnassignedInfer(pe->params);
                }
                const auto& pe = path.mData.as_UfcsUnknown();
                return typeHasUnassignedInfer(pe.type) || paramsHaveUnassignedInfer(pe.params);
            }

            bool traitPathHasUnassignedInfer(const HIRTraitPath& trait) const {
                if (paramsHaveUnassignedInfer(trait.mPath.mParams)) {
                    return true;
                }
                for (const auto& assoc : trait.typeBounds) {
                    if (paramsHaveUnassignedInfer(assoc.second.sourceTrait.mParams) || paramsHaveUnassignedInfer(assoc.second.atyParams) || typeHasUnassignedInfer(assoc.second.type)) {
                        return true;
                    }
                }
                for (const auto& assoc : trait.traitBounds) {
                    if (paramsHaveUnassignedInfer(assoc.second.sourceTrait.mParams) || paramsHaveUnassignedInfer(assoc.second.atyParams)) {
                        return true;
                    }
                    for (const auto& bound : assoc.second.traits) {
                        if (traitPathHasUnassignedInfer(bound)) {
                            return true;
                        }
                    }
                }
                return false;
            }

            bool typeHasUnassignedInfer(const HIRTypeData* input) const {
                if (const auto* infer = input->opt_Infer()) {
                    if (infer->index == ~0u) {
                        return true;
                    }
                    const auto* resolved = mResolve.resolveType(input);
                    return resolved != input && typeHasUnassignedInfer(resolved);
                }
                if (const auto* path = input->opt_Path()) {
                    return pathHasUnassignedInfer(path->path);
                }
                if (const auto* object = input->opt_TraitObject()) {
                    if (traitPathHasUnassignedInfer(object->mTrait)) {
                        return true;
                    }
                    for (const auto& marker : object->markers) {
                        if (paramsHaveUnassignedInfer(marker.mParams)) {
                            return true;
                        }
                    }
                    return false;
                }
                if (const auto* erased = input->opt_ErasedType()) {
                    for (const auto& trait : erased->traits) {
                        if (traitPathHasUnassignedInfer(trait)) {
                            return true;
                        }
                    }
                    if (const auto* known = erased->inner.opt_Known()) {
                        return typeHasUnassignedInfer(*known);
                    }
                    if (const auto* alias = erased->inner.opt_Alias()) {
                        return paramsHaveUnassignedInfer(alias->params);
                    }
                    if (const auto* fcn = erased->inner.opt_Fcn()) {
                        return pathHasUnassignedInfer(fcn->origin);
                    }
                    return false;
                }
                if (const auto* array = input->opt_Array()) {
                    const auto* size = array->size.opt_Unevaluated();
                    return typeHasUnassignedInfer(array->inner) || (size && valueHasUnassignedInfer(*size));
                }
                if (const auto* slice = input->opt_Slice()) {
                    return typeHasUnassignedInfer(slice->inner);
                }
                if (const auto* tuple = input->opt_Tuple()) {
                    for (const auto& field : *tuple) {
                        if (typeHasUnassignedInfer(field)) {
                            return true;
                        }
                    }
                    return false;
                }
                if (const auto* borrow = input->opt_Borrow()) {
                    return typeHasUnassignedInfer(borrow->inner);
                }
                if (const auto* pointer = input->opt_Pointer()) {
                    return typeHasUnassignedInfer(pointer->inner);
                }
                if (const auto* named = input->opt_NamedFunction()) {
                    return pathHasUnassignedInfer(named->path);
                }
                if (const auto* fcn = input->opt_Function()) {
                    for (const auto& arg : fcn->argTypes) {
                        if (typeHasUnassignedInfer(arg)) {
                            return true;
                        }
                    }
                    return typeHasUnassignedInfer(fcn->mRettype);
                }
                return false;
            }

            bool goalHasUnassignedInfer(const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) const {
                if (paramsHaveUnassignedInfer(params) || typeHasUnassignedInfer(type)) {
                    return true;
                }
                if (associated) {
                    for (const auto& entry : *associated) {
                        if (paramsHaveUnassignedInfer(entry.second.sourceTrait.mParams) || paramsHaveUnassignedInfer(entry.second.atyParams) || typeHasUnassignedInfer(entry.second.type)) {
                            return true;
                        }
                    }
                }
                return false;
            }

            bool selfIsUnresolvedProjectionOverIvar(const HIRTypeData* type) const {
                const auto* path = type->opt_Path();
                return path && path->binding.is_Unbound() && path->path.mData.is_UfcsKnown() && mResolve.typeContainsIvars(type);
            }

            bool typeHasUnknown(const HIRTypeData* input) const {
                const auto& type = mResolve.resolveType(input);
                if (type->is_Infer() || type->is_Generic()) {
                    return true;
                }
                if (const auto* path = type->opt_Path()) {
                    return pathHasUnknownTypes(path->path);
                }
                if (const auto* object = type->opt_TraitObject()) {
                    if (traitPathHasUnknownTypes(object->mTrait)) {
                        return true;
                    }
                    for (const auto& marker : object->markers) {
                        if (paramsHaveUnknownTypes(marker.mParams)) {
                            return true;
                        }
                    }
                    return false;
                }
                if (const auto* erased = type->opt_ErasedType()) {
                    for (const auto& trait : erased->traits) {
                        if (traitPathHasUnknownTypes(trait)) {
                            return true;
                        }
                    }
                    if (const auto* known = erased->inner.opt_Known()) {
                        return typeHasUnknown(*known);
                    }
                    if (const auto* alias = erased->inner.opt_Alias()) {
                        return paramsHaveUnknownTypes(alias->params);
                    }
                    if (const auto* fcn = erased->inner.opt_Fcn()) {
                        return pathHasUnknownTypes(fcn->origin);
                    }
                    return false;
                }
                if (const auto* array = type->opt_Array()) {
                    return typeHasUnknown(array->inner);
                }
                if (const auto* slice = type->opt_Slice()) {
                    return typeHasUnknown(slice->inner);
                }
                if (const auto* tuple = type->opt_Tuple()) {
                    for (const auto& field : *tuple) {
                        if (typeHasUnknown(field)) {
                            return true;
                        }
                    }
                    return false;
                }
                if (const auto* borrow = type->opt_Borrow()) {
                    return typeHasUnknown(borrow->inner);
                }
                if (const auto* pointer = type->opt_Pointer()) {
                    return typeHasUnknown(pointer->inner);
                }
                if (const auto* named = type->opt_NamedFunction()) {
                    return pathHasUnknownTypes(named->path);
                }
                if (const auto* fcn = type->opt_Function()) {
                    for (const auto& arg : fcn->argTypes) {
                        if (typeHasUnknown(arg)) {
                            return true;
                        }
                    }
                    return typeHasUnknown(fcn->mRettype);
                }
                return false;
            }

            static bool typeHasCandidatePlaceholder(const HIRTypeData* type) {
                bool found = false;
                visitTyWith(type, [&](const HIRTypeData* inner) {
                    if (const auto* generic = inner->opt_Generic()) {
                        found |= generic->group() == GENERICPlaceholder;
                    }
                    return found;
                });
                return found;
            }

            static bool typeHasUfcsUnknown(const HIRTypeData* type) {
                if (!type) {
                    return false;
                }
                return visitTyWith(type, [](const HIRTypeData* inner) {
                    const auto* path = inner->opt_Path();
                    return path && path->path.mData.is_UfcsUnknown();
                });
            }

            static bool paramsHaveCandidatePlaceholders(const HIRPathParams& params) {
                for (const auto& type : params.types) {
                    if (typeHasCandidatePlaceholder(type)) {
                        return true;
                    }
                }
                for (const auto& value : params.values) {
                    if (value.is_Generic() && value.as_Generic().group() == GENERICPlaceholder) {
                        return true;
                    }
                }
                return false;
            }

            bool candidateHasPlaceholders(const Candidate& candidate) const {
                if (typeHasCandidatePlaceholder(candidate.impl.getImplType(crate.types)) || paramsHaveCandidatePlaceholders(candidate.impl.getTraitParams(crate.types))) {
                    return true;
                }
                if (const auto* traitImpl = candidate.impl.mData.opt_TraitImpl()) {
                    if (paramsHaveCandidatePlaceholders(traitImpl->implParams)) {
                        return true;
                    }
                }
                return paramsHaveCandidatePlaceholders(candidate.markerImplParams);
            }

            static bool paramsNeedResponseConstraints(const HIRPathParams& params) {
                for (const auto& type : params.types) {
                    bool found = false;
                    visitTyWith(type, [&](const HIRTypeData* inner) {
                        if (const auto* generic = inner->opt_Generic()) {
                            found |= generic->group() == GENERICPlaceholder;
                        } else if (const auto* infer = inner->opt_Infer()) {
                            found |= !infer->isLit();
                        }
                        return found;
                    });
                    if (found) {
                        return true;
                    }
                }
                for (const auto& value : params.values) {
                    if (value.is_Infer() || (value.is_Generic() && value.as_Generic().group() == GENERICPlaceholder)) {
                        return true;
                    }
                }
                return false;
            }

            bool candidateNeedsResponseConstraints(const Candidate& candidate) const {
                if (const auto* traitImpl = candidate.impl.mData.opt_TraitImpl()) {
                    return paramsNeedResponseConstraints(traitImpl->implParams);
                }
                return candidate.markerImpl && paramsNeedResponseConstraints(candidate.markerImplParams);
            }

            OrphanVisit orphanVisitResolvedType(const HIRTypeData* type, OrphanPerspective perspective) const {
                if (type->is_Infer() || type->is_Generic()) {
                    return perspective == OrphanPerspective::Remote ? OrphanVisit::LocalKey : OrphanVisit::Uncovered;
                }

                if (const auto* path = type->opt_Path()) {
                    const auto* generic = path->path.mData.opt_Generic();
                    const bool concreteAdt = generic && (path->binding.is_Struct() || path->binding.is_Enum() || path->binding.is_Union() || path->binding.is_ExternType());
                    if (!concreteAdt) {
                        if (typeHasUnknown(type)) {
                            return perspective == OrphanPerspective::Remote ? OrphanVisit::LocalKey : OrphanVisit::Uncovered;
                        }
                        return OrphanVisit::NonLocal;
                    }

                    const bool local = perspective == OrphanPerspective::Local && generic->mPath.crateName() == crate.crateName;
                    if (local) {
                        return OrphanVisit::LocalKey;
                    }

                    const auto* strPtr = path->binding.opt_Struct();
                    if (strPtr && (*strPtr)->structMarkings.isFundamental) {
                        for (const auto& param : generic->mParams.types) {
                            const auto result = orphanVisitType(param, perspective);
                            if (result != OrphanVisit::NonLocal) {
                                return result;
                            }
                        }
                    }
                    return OrphanVisit::NonLocal;
                }

                if (const auto* borrow = type->opt_Borrow()) {
                    // References are fundamental even though raw pointers are not.
                    return orphanVisitType(borrow->inner, perspective);
                }

                if (const auto* object = type->opt_TraitObject()) {
                    const auto& principal = object->mTrait.mPath.mPath;
                    if (perspective == OrphanPerspective::Local && principal != HIRSimplePath() && principal.crateName() == crate.crateName) {
                        return OrphanVisit::LocalKey;
                    }
                    return OrphanVisit::NonLocal;
                }

                if (type->is_NodeType()) {
                    return perspective == OrphanPerspective::Local ? OrphanVisit::LocalKey : OrphanVisit::NonLocal;
                }

                if (type->is_ErasedType() && typeHasUnknown(type)) {
                    return perspective == OrphanPerspective::Remote ? OrphanVisit::LocalKey : OrphanVisit::Uncovered;
                }

                // Primitive, tuple, array, slice, raw-pointer, function, opaque,
                // and foreign rigid types are non-local and cover their contents.
                return OrphanVisit::NonLocal;
            }

            OrphanVisit orphanVisitType(const HIRTypeData* input, OrphanPerspective perspective) const {
                const auto& resolved = mResolve.resolveType(input);
                const auto* path = resolved->opt_Path();
                const bool isAlias = path && (!path->path.mData.is_Generic() || path->binding.is_Unbound() || path->binding.is_Opaque());
                if (isAlias) {
                    // rustc's orphan checker normalizes aliases lazily.  Keep a
                    // rigid alias if normalization only produces a fresh type
                    // variable; such an alias still carries coverage information.
                    auto normalized = mResolve.expandAssociatedTypes(span(), resolved);
                    if (!(normalized->is_Infer() && !resolved->is_Infer())) {
                        return orphanVisitResolvedType(normalized, perspective);
                    }
                }
                return orphanVisitResolvedType(resolved, perspective);
            }

            bool orphanCheckTraitRef(const HIRPathParams& params, const HIRTypeData* type, OrphanPerspective perspective) const {
                const auto selfResult = orphanVisitType(type, perspective);
                if (selfResult != OrphanVisit::NonLocal) {
                    return selfResult == OrphanVisit::LocalKey;
                }
                for (const auto& param : params.types) {
                    const auto result = orphanVisitType(param, perspective);
                    if (result != OrphanVisit::NonLocal) {
                        return result == OrphanVisit::LocalKey;
                    }
                }
                return false;
            }

            bool traitRefIsKnowable(const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type) const {
                if (orphanCheckTraitRef(params, type, OrphanPerspective::Remote)) {
                    return false;
                }

                const auto& traitDef = crate.getTraitByPath(span(), trait);
                if (trait.crateName() == crate.crateName || traitDef.isFundamental) {
                    return true;
                }

                return orphanCheckTraitRef(params, type, OrphanPerspective::Local);
            }

            static size_t hashMix(size_t state, size_t value) {
                // boost::hash_combine's avalanche step.  Equality never relies on
                // this fingerprint: hash collisions are resolved by goal_matches.
                return state ^ (value + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2));
            }

            static size_t hashSimplePath(const HIRSimplePath& path) {
                size_t result = ::std::hash<RcString>()(path.crateName());
                for (const auto& component : path.components()) {
                    result = hashMix(result, ::std::hash<RcString>()(component));
                }
                return result;
            }

            static size_t hashType(const HIRTypeData* type) {
                if (const auto* path = type->getSortPath()) {
                    return hashMix(0x10, hashSimplePath(*path));
                }
                if (const auto* primitive = type->opt_Primitive()) {
                    return hashMix(0x20, static_cast<size_t>(*primitive));
                }
                if (const auto* generic = type->opt_Generic()) {
                    return hashMix(0x30, generic->binding);
                }
                if (const auto* infer = type->opt_Infer()) {
                    return hashMix(0x40, infer->index);
                }
                if (const auto* tuple = type->opt_Tuple()) {
                    size_t result = hashMix(0x50, tuple->size());
                    for (const auto& field : *tuple) {
                        result = hashMix(result, hashType(field));
                    }
                    return result;
                }
                if (const auto* array = type->opt_Array()) {
                    return hashMix(0x60, hashType(array->inner));
                }
                if (const auto* slice = type->opt_Slice()) {
                    return hashMix(0x70, hashType(slice->inner));
                }
                if (const auto* borrow = type->opt_Borrow()) {
                    return hashMix(hashMix(0x80, static_cast<size_t>(borrow->type)), hashType(borrow->inner));
                }
                if (const auto* pointer = type->opt_Pointer()) {
                    return hashMix(hashMix(0x90, static_cast<size_t>(pointer->type)), hashType(pointer->inner));
                }
                if (const auto* traitObject = type->opt_TraitObject()) {
                    return hashMix(0xa0, hashSimplePath(traitObject->mTrait.mPath.mPath));
                }
                if (type->is_Diverge()) {
                    return 0xb0;
                }
                // Function, erased, and compiler-generated node types are rare in
                // recursive solver tables.  A stable tag is sufficient; full
                // equality below still resolves every collision correctly.
                return 0xc0;
            }

            static size_t goalHash(const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) {
                size_t result = hashSimplePath(trait);
                result = hashMix(result, params.types.size());
                for (const auto& param : params.types) {
                    result = hashMix(result, hashType(param));
                }
                result = hashMix(result, params.values.size());
                result = hashMix(result, hashType(type));
                if (associated && !associated->empty()) {
                    result = hashMix(result, associated->size());
                    for (const auto& entry : *associated) {
                        result = hashMix(result, ::std::hash<RcString>()(entry.first));
                        result = hashMix(result, hashSimplePath(entry.second.sourceTrait.mPath));
                        result = hashMix(result, hashType(entry.second.type));
                    }
                }
                return result;
            }

            static HIRTraitPath::assocListT cloneAssociated(const HIRTraitPath::assocListT* associated) {
                HIRTraitPath::assocListT result;
                if (associated) {
                    for (const auto& entry : *associated) {
                        result.insert({entry.first, entry.second.clone()});
                    }
                }
                return result;
            }

            ImplRef monomorphImplRef(const ImplRef& source, const Monomorphiser& monomorph) const {
                auto monomorphAssociated = [&](const HIRTraitPath::assocListT* associated) {
                    HIRTraitPath::assocListT result;
                    if (associated) {
                        for (const auto& entry : *associated) {
                            result.insert({entry.first, monomorph.monomorphTpAtyEqual(span(), entry.second, true)});
                        }
                    }
                    return result;
                };

                ImplRef result;
                if (const auto* impl = source.mData.opt_TraitImpl()) {
                    ASSERT_BUG(span(), impl->traitPtr && impl->traitPath && impl->impl, "Cannot monomorphise an invalid trait impl response");
                    result = ImplRef(monomorph.monomorphPathParams(span(), impl->implParams, true), *impl->traitPtr, *impl->traitPath, *impl->impl);
                } else if (const auto* bounded = source.mData.opt_BoundedPtr()) {
                    result = ImplRef(monomorph.monomorphType(span(), bounded->type, true), monomorph.monomorphPathParams(span(), *bounded->traitArgs, true), monomorphAssociated(bounded->assoc));
                } else {
                    const auto& owned = source.mData.as_Bounded();
                    result = ImplRef(monomorph.monomorphType(span(), owned.type, true), monomorph.monomorphPathParams(span(), owned.traitArgs, true), monomorphAssociated(&owned.assoc));
                }
                if (source.isAmbiguousIdentity()) {
                    result.markAmbiguousIdentity();
                }
                return result;
            }

            static bool goalMatches(const GoalKey& goal, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) {
                if (goal.trait != trait || goal.params != params || goal.type != type) {
                    return false;
                }
                if (!associated || associated->empty()) {
                    return goal.associated.empty();
                }
                if (goal.associated.size() != associated->size()) {
                    return false;
                }
                auto left = goal.associated.begin();
                auto right = associated->begin();
                for (; left != goal.associated.end(); ++left, ++right) {
                    if (left->first != right->first || left->second.sourceTrait != right->second.sourceTrait || left->second.atyParams != right->second.atyParams || left->second.type != right->second.type) {
                        return false;
                    }
                }
                return true;
            }

            CachedGoal* findCachedGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) const {
                const auto range = goalCacheIndex.equal_range(hash);
                for (auto it = range.first; it != range.second; ++it) {
                    if (goalMatches(it->second->goal, trait, params, type, associated)) {
                        return it->second;
                    }
                }
                return nullptr;
            }

            GoalKey* findActiveGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) const {
                const auto range = activeGoalIndex.equal_range(hash);
                for (auto it = range.first; it != range.second; ++it) {
                    if (goalMatches(*it->second, trait, params, type, associated)) {
                        return it->second;
                    }
                }
                return nullptr;
            }

            GoalKey* pushActiveGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) {
                auto* goal = activeGoalNodes.make(hash, trait, params, type, associated);
                goalStack.push_back(goal);
                activeGoalIndex.emplace(hash, goal);
                return goal;
            }

            void popActiveGoal(GoalKey* goal) {
                assert(!goalStack.empty() && goalStack.back() == goal);
                const auto range = activeGoalIndex.equal_range(goal->hash);
                for (auto it = range.first; it != range.second; ++it) {
                    if (it->second == goal) {
                        activeGoalIndex.erase(it);
                        goalStack.pop_back();
                        activeGoalNodes.release(goal);
                        return;
                    }
                }
                assert(!"next-solver active goal missing from hash index");
                ::std::abort();
            }

            Certainty cacheGoal(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated, Certainty certainty) {
                auto* goal = cachedGoalNodes.make(hash, trait, params, type, associated, certainty);
                goalCache.push_back(goal);
                goalCacheIndex.emplace(hash, goal);
                return certainty;
            }

            CachedGoal* cacheResponse(size_t hash, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated, ImplRef response, HIRCompare responseCertainty) {
                auto* cached = findCachedGoal(hash, trait, params, type, associated);
                const auto certainty = responseCertainty == HIRCompare::Equal ? Certainty::Proven : Certainty::Ambiguous;
                if (!cached) {
                    cached = cachedGoalNodes.make(hash, trait, params, type, associated, certainty);
                    goalCache.push_back(cached);
                    goalCacheIndex.emplace(hash, cached);
                }
                cached->certainty = certainty;
                cached->response = ::std::move(response);
                cached->responseCertainty = responseCertainty;
                cached->hasResponse = true;
                return cached;
            }

            void clearGoalCache() {
                goalCacheIndex.clear();
                for (auto* goal : goalCache) {
                    cachedGoalNodes.release(goal);
                }
                goalCache.clear();
            }

            static const HIRTraitPath::assocListT& boundedAssociated(const ImplRef& impl) {
                if (const auto* bounded = impl.mData.opt_BoundedPtr()) {
                    return *bounded->assoc;
                }
                return impl.mData.as_Bounded().assoc;
            }

            static bool associatedResponsesEqual(const HIRTraitPath::assocListT& left, const HIRTraitPath::assocListT& right) {
                if (left.size() != right.size()) {
                    return false;
                }
                auto li = left.begin();
                auto ri = right.begin();
                for (; li != left.end(); ++li, ++ri) {
                    if (li->first != ri->first || li->second.ord(ri->second) != OrdEqual) {
                        return false;
                    }
                }
                return true;
            }

            bool isSameImpl(const ImplRef& left, const ImplRef& right) const {
                const auto* li = left.mData.opt_TraitImpl();
                const auto* ri = right.mData.opt_TraitImpl();
                if (li || ri) {
                    return li && ri && li->impl == ri->impl && li->implParams == ri->implParams;
                }
                return left.getImplType(crate.types) == right.getImplType(crate.types) && left.getTraitParams(crate.types) == right.getTraitParams(crate.types) && associatedResponsesEqual(boundedAssociated(left), boundedAssociated(right));
            }

            bool paramEnvCandidateIsNonGlobal(const Candidate& candidate) const {
                if (candidate.source != CandidateSource::ParamEnv) {
                    return false;
                }
                if (typeHasUnknown(candidate.impl.getImplType(crate.types)) || paramsHaveUnknownTypes(candidate.impl.getTraitParams(crate.types))) {
                    return true;
                }
                for (const auto& associated : boundedAssociated(candidate.impl)) {
                    if (paramsHaveUnknownTypes(associated.second.sourceTrait.mParams) || paramsHaveUnknownTypes(associated.second.atyParams) || typeHasUnknown(associated.second.type)) {
                        return true;
                    }
                }
                return false;
            }

            void pushCandidate(size_t frameIndex, ImplRef impl, HIRCompare match, const HIRMarkerImpl* markerImpl = nullptr, HIRPathParams markerImplParams = {}, bool autoBuiltin = false, CandidateSource source = CandidateSource::Other) {
                if (match == HIRCompare::Unequal) {
                    return;
                }
                auto& candidates = frames[frameIndex]->candidates;
                for (size_t i = 0; i < candidates.size(); i++) {
                    const bool sameSource = candidates[i]->markerImpl == markerImpl && candidates[i]->autoBuiltin == autoBuiltin && candidates[i]->source == source;
                    const bool same = markerImpl ? sameSource && candidates[i]->markerImplParams == markerImplParams : sameSource && isSameImpl(candidates[i]->impl, impl);
                    if (same) {
                        candidates[i]->headMatch &= match;
                        return;
                    }
                }
                candidates.push_back(candidateNodes.make(::std::move(impl), match, markerImpl, ::std::move(markerImplParams), autoBuiltin, source));
            }

            void assembleCandidates(size_t frameIndex, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type) {
                auto collect = [&](CandidateSource source) {
                    return [&, source](ImplRef impl, HIRCompare match) {
                        pushCandidate(frameIndex, ::std::move(impl), match, nullptr, {}, false, source);
                        return false;
                    };
                };

                // Candidate source is semantically significant: a non-global
                // ParamEnv predicate shadows builtin and impl candidates in the
                // next solver.  The legacy lookup flattened these sources into
                // the same bounded ImplRef, so collect each source independently.
                mResolve.findTraitImplsMagic(span(), trait, params, type, collect(CandidateSource::Builtin));
                mResolve.findTraitImplsLegacy(span(), trait, params, type, collect(CandidateSource::Other), false, false, false);
                mResolve.findTraitImplsBound(span(), trait, params, type, collect(CandidateSource::ParamEnv));

                const auto& resolvedType = mResolve.resolveType(type);
                const auto& traitDef = crate.getTraitByPath(span(), trait);
                if (!traitDef.mIsMarker) {
                    // Assemble impl heads without evaluating their where-clauses.
                    // Those nested goals belong exclusively to evaluate_candidate.
                    crate.findTraitImpls(trait, resolvedType, mResolve.ivars.callbackResolveInfer(), [&](const HIRTraitImpl& impl) {
                        HIRPathParams implParams;
                        const auto match = mResolve.fticCheckParams(span(), trait, &params, resolvedType, impl.mParams, impl.traitArgs, impl.mType, implParams, false);
                        if (match != HIRCompare::Unequal) {
                            pushCandidate(frameIndex, ImplRef(::std::move(implParams), traitDef, trait, impl), match, nullptr, {}, false, CandidateSource::TraitImpl);
                        }
                        return false;
                    });
                } else {
                    // Explicit positive and negative auto-trait impls are
                    // candidates with polarity.  Only their heads are matched
                    // here; their bounds are nested goals evaluated below.
                    crate.findAutoTraitImpls(trait, resolvedType, mResolve.ivars.callbackResolveInfer(), [&](const HIRMarkerImpl& impl) {
                        HIRPathParams implParams;
                        const auto match = mResolve.fticCheckParams(span(), trait, &params, resolvedType, impl.mParams, impl.traitArgs, impl.mType, implParams, false);
                        if (match != HIRCompare::Unequal) {
                            auto monomorph = MonomorphStatePtr(crate.types, nullptr, &implParams, nullptr);
                            auto responseType = monomorph.monomorphType(span(), impl.mType, false);
                            auto responseParams = monomorph.monomorphPathParams(span(), impl.traitArgs, false);
                            pushCandidate(frameIndex, ImplRef(::std::move(responseType), ::std::move(responseParams), HIRTraitPath::assocListT()), match, &impl, ::std::move(implParams), false, CandidateSource::TraitImpl);
                        }
                        return false;
                    });

                    // The structural auto candidate is evaluated recursively in
                    // evaluate_candidate, after explicit polarity is known.
                    pushCandidate(frameIndex, ImplRef(resolvedType, params.clone(), HIRTraitPath::assocListT()), mResolve.typeContainsIvars(resolvedType) || mResolve.paramsContainIvars(params) ? HIRCompare::Fuzzy : HIRCompare::Equal, nullptr, {}, true, CandidateSource::Builtin);
                }
            }

            HIRTypeRef makeAssociatedProjection(const HIRTypeData* type, const HIRGenericPath& sourceTrait, const RcString& name, const HIRPathParams& associatedParams) const {
                return crate.types.path(HIRPath(type, sourceTrait.clone(), name, associatedParams.clone()), HIRTypePathBinding::make_Opaque({}));
            }

            HIRTypeRef makeAssociatedProjection(const ImplRef& impl, const HIRGenericPath& sourceTrait, const RcString& name, const HIRPathParams& associatedParams) const {
                return makeAssociatedProjection(impl.getImplType(crate.types), sourceTrait, name, associatedParams);
            }

            bool bindCandidatePlaceholders(Candidate& candidate, const HIRTypeData* nestedType, const HIRTraitPath::assocListT& associated, bool useCandidateResponse = false) {
                HIRPathParams* candidateParams = nullptr;
                if (auto* traitImpl = candidate.impl.mData.opt_TraitImpl()) {
                    candidateParams = &traitImpl->implParams;
                } else if (candidate.markerImpl) {
                    candidateParams = &candidate.markerImplParams;
                }
                if (!candidateParams || associated.empty()) {
                    return false;
                }

                class BindPlaceholders final: public HIRMatchGenerics {
                    const Span& mSpan;
                    HIRTypeInterner& types;
                    HIRPathParams& mParams;
                    ::std::vector<::std::pair<HIRTypeRef, HIRTypeRef>> mBindings;

                    bool isBindable(const HIRTypeData* type) const {
                        if (const auto* generic = type->opt_Generic()) {
                            return generic->group() == GENERICPlaceholder;
                        }
                        if (const auto* infer = type->opt_Infer()) {
                            return !infer->isLit();
                        }
                        return false;
                    }

                    ::std::optional<HIRCompare> bindType(const HIRTypeData* pattern, const HIRTypeData* value, tCbResolveType resolve) {
                        for (const auto& binding : mBindings) {
                            if (binding.first == pattern) {
                                return binding.second->compareWithPlaceholders(mSpan, value, resolve);
                            }
                        }
                        if (!isBindable(pattern)) {
                            return {};
                        }
                        bool isParameter = false;
                        for (const auto& parameter : mParams.types) {
                            isParameter |= visitTyWith(parameter, [&](const HIRTypeData* inner) {
                                return inner == pattern;
                            });
                        }
                        if (!isParameter) {
                            return {};
                        }
                        if (pattern == value) {
                            return HIRCompare::Equal;
                        }
                        for (auto& parameter : mParams.types) {
                            parameter = cloneTyWith(types, mSpan, parameter, [&](const HIRTypeData* input, HIRTypeRef& output) {
                                if (input != pattern) {
                                    return false;
                                }
                                output = value;
                                return true;
                            });
                        }
                        mBindings.push_back({pattern, value});
                        changed = true;
                        return HIRCompare::Equal;
                    }

                public:
                    bool changed = false;

                    BindPlaceholders(const Span& span, HIRTypeInterner& types, HIRPathParams& params)
                        : mSpan(span)
                        , types(types)
                        , mParams(params)
                    {
                    }

                    HIRCompare cmpType(const Span& span, const HIRTypeData* pattern, const HIRTypeData* value, tCbResolveType resolve) override {
                        if (auto result = bindType(pattern, value, resolve)) {
                            return *result;
                        }
                        return HIRMatchGenerics::cmpType(span, pattern, value, resolve);
                    }

                    HIRCompare matchTy(const HIRGenericRef& generic, const HIRTypeData* type, tCbResolveType resolve) override {
                        const auto pattern = types.generic(generic.name, generic.binding);
                        if (auto result = bindType(pattern, type, resolve)) {
                            return *result;
                        }
                        return pattern->compareWithPlaceholders(mSpan, type, resolve);
                    }

                    HIRCompare matchVal(const HIRGenericRef& generic, const HIRConstGeneric& value) override {
                        if (value.is_Generic() && value.as_Generic() == generic) {
                            return HIRCompare::Equal;
                        }
                        if (generic.group() == GENERICPlaceholder) {
                            for (auto& parameter : mParams.values) {
                                if (parameter.is_Generic() && parameter.as_Generic() == generic) {
                                    parameter = value.clone();
                                    changed = true;
                                    return HIRCompare::Equal;
                                }
                            }
                        }
                        return HIRCompare::Fuzzy;
                    }
                } binder{span(), crate.types, *candidateParams};

                for (const auto& requirement : associated) {
                    const auto saved = candidateParams->clone();
                    auto candidateOutput = useCandidateResponse ? candidate.impl.getType(crate.types, requirement.first.c_str(), requirement.second.atyParams) : HIRTypeRef();
                    if (!useCandidateResponse) {
                        // An impl parameter can occur only in a nested projection
                        // equality (for example `I: Iterator<Item = &'a T>`).
                        // Ask the solver for that projection's actual response so
                        // `T` is bound to the response, not to the alias syntax.
                        evaluate(span(), requirement.second.sourceTrait.mPath, requirement.second.sourceTrait.mParams, nestedType, [&](ImplRef impl, HIRCompare certainty) {
                            if (certainty != HIRCompare::Equal || impl.isAmbiguousIdentity()) {
                                return false;
                            }
                            auto output = impl.getType(crate.types, requirement.first.c_str(), requirement.second.atyParams);
                            if (output == HIRTypeRef()) {
                                return false;
                            }
                            candidateOutput = ::std::move(output);
                            return true;
                        }, requirement.first.c_str(), nullptr, &requirement.second.atyParams);
                    }
                    if (candidateOutput == HIRTypeRef()) {
                        candidateOutput = makeAssociatedProjection(nestedType, requirement.second.sourceTrait, requirement.first, requirement.second.atyParams);
                    }
                    if (!useCandidateResponse && !typeHasUfcsUnknown(candidateOutput)) {
                        // rustc normalises a nested projection response before it
                        // is unified with the outer candidate.  In particular,
                        // `<&mut I as Iterator>::Item` first becomes
                        // `<I as Iterator>::Item` and then the ParamEnv equality
                        // `&T`; matching the unnormalised alias against
                        // `&placeholder` only reports a fuzzy relation and loses
                        // the constraint.  A candidate's own response is not a
                        // nested solver response: during Resolve UFCS Outer either
                        // form can still legally contain a local UfcsUnknown, and
                        // such a response must remain deferred until that pass
                        // resolves its trait path.
                        candidateOutput = mResolve.expandAssociatedTypes(span(), ::std::move(candidateOutput));
                    }
                    const auto match = (useCandidateResponse ? candidateOutput : requirement.second.type)->matchTestGenericsFuzz(span(), useCandidateResponse ? requirement.second.type : candidateOutput, mResolve.ivars.callbackResolveInfer(), binder);
                    if (match == HIRCompare::Unequal) {
                        *candidateParams = saved.clone();
                    }
                }

                if (binder.changed && candidate.markerImpl) {
                    auto monomorph = MonomorphStatePtr(crate.types, nullptr, &candidate.markerImplParams, nullptr);
                    auto& response = candidate.impl.mData.as_Bounded();
                    response.type = monomorph.monomorphType(span(), candidate.markerImpl->mType, false);
                    response.traitArgs = monomorph.monomorphPathParams(span(), candidate.markerImpl->traitArgs, false);
                }
                return binder.changed;
            }

            bool bindCandidateResponse(Candidate& candidate, const HIRTypeData* nestedType, const HIRPathParams& nestedParams, const ImplRef& response) {
                HIRPathParams* candidateParams = nullptr;
                if (auto* traitImpl = candidate.impl.mData.opt_TraitImpl()) {
                    candidateParams = &traitImpl->implParams;
                } else if (candidate.markerImpl) {
                    candidateParams = &candidate.markerImplParams;
                }
                if (!candidateParams || response.isAmbiguousIdentity()) {
                    return false;
                }

                class BindResponse final: public HIRMatchGenerics {
                    const Span& mSpan;
                    HIRTypeInterner& types;
                    HIRPathParams& mParams;
                    ::std::vector<::std::pair<HIRTypeRef, HIRTypeRef>> mBindings;

                    bool isBindable(const HIRTypeData* type) const {
                        if (const auto* generic = type->opt_Generic()) {
                            return generic->group() == GENERICPlaceholder;
                        }
                        if (const auto* infer = type->opt_Infer()) {
                            return !infer->isLit();
                        }
                        return false;
                    }

                    ::std::optional<HIRCompare> bindType(const HIRTypeData* pattern, const HIRTypeData* value, tCbResolveType resolve) {
                        for (const auto& binding : mBindings) {
                            if (binding.first == pattern) {
                                return binding.second->compareWithPlaceholders(mSpan, value, resolve);
                            }
                        }
                        if (!isBindable(pattern)) {
                            return {};
                        }
                        bool isParameter = false;
                        for (const auto& parameter : mParams.types) {
                            isParameter |= visitTyWith(parameter, [&](const HIRTypeData* inner) {
                                return inner == pattern;
                            });
                        }
                        if (!isParameter) {
                            return {};
                        }
                        if (pattern == value) {
                            return HIRCompare::Equal;
                        }
                        for (auto& parameter : mParams.types) {
                            parameter = cloneTyWith(types, mSpan, parameter, [&](const HIRTypeData* input, HIRTypeRef& output) {
                                if (input != pattern) {
                                    return false;
                                }
                                output = value;
                                return true;
                            });
                        }
                        mBindings.push_back({pattern, value});
                        changed = true;
                        return HIRCompare::Equal;
                    }

                public:
                    bool changed = false;

                    BindResponse(const Span& span, HIRTypeInterner& types, HIRPathParams& params)
                        : mSpan(span)
                        , types(types)
                        , mParams(params)
                    {
                    }

                    HIRCompare cmpType(const Span& span, const HIRTypeData* pattern, const HIRTypeData* value, tCbResolveType resolve) override {
                        if (auto result = bindType(pattern, value, resolve)) {
                            return *result;
                        }
                        return HIRMatchGenerics::cmpType(span, pattern, value, resolve);
                    }

                    HIRCompare matchTy(const HIRGenericRef& generic, const HIRTypeData* value, tCbResolveType resolve) override {
                        const auto pattern = types.generic(generic.name, generic.binding);
                        if (auto result = bindType(pattern, value, resolve)) {
                            return *result;
                        }
                        return pattern->compareWithPlaceholders(mSpan, value, resolve);
                    }

                    HIRCompare matchVal(const HIRGenericRef& generic, const HIRConstGeneric& value) override {
                        if (value.is_Generic() && value.as_Generic() == generic) {
                            return HIRCompare::Equal;
                        }
                        if (generic.group() != GENERICPlaceholder) {
                            return HIRCompare::Fuzzy;
                        }
                        for (auto& parameter : mParams.values) {
                            if (parameter.is_Generic() && parameter.as_Generic() == generic) {
                                parameter = value.clone();
                                changed = true;
                                return HIRCompare::Equal;
                            }
                        }
                        return HIRCompare::Fuzzy;
                    }
                } binder{span(), crate.types, *candidateParams};

                const auto saved = candidateParams->clone();
                auto match = nestedType->matchTestGenericsFuzz(span(), response.getImplType(crate.types), mResolve.ivars.callbackResolveInfer(), binder);
                match &= nestedParams.matchTestGenericsFuzz(span(), response.getTraitParams(crate.types), mResolve.ivars.callbackResolveInfer(), binder);
                if (match == HIRCompare::Unequal) {
                    *candidateParams = saved.clone();
                    return false;
                }

                if (binder.changed && candidate.markerImpl) {
                    auto monomorph = MonomorphStatePtr(crate.types, nullptr, &candidate.markerImplParams, nullptr);
                    auto& bounded = candidate.impl.mData.as_Bounded();
                    bounded.type = monomorph.monomorphType(span(), candidate.markerImpl->mType, false);
                    bounded.traitArgs = monomorph.monomorphPathParams(span(), candidate.markerImpl->traitArgs, false);
                }
                return binder.changed;
            }

            Certainty matchAssociatedTypes(const HIRSimplePath& trait, const ImplRef& impl, const HIRTraitPath::assocListT* associated) {
                if (!associated || associated->empty()) {
                    return Certainty::Proven;
                }

                Certainty result = Certainty::Proven;
                for (const auto& requirement : *associated) {
                    const auto& aty = requirement.second;
                    if (!impl.mData.is_TraitImpl() && aty.atyParams.hasParams()) {
                        // Bounded candidates currently store non-GAT projections.
                        // They remain a valid but non-guiding response instead of
                        // being rejected or calling ImplRef's non-GAT assertion.
                        result = Certainty::Ambiguous;
                        continue;
                    }
                    auto output = impl.getType(crate.types, requirement.first.c_str(), aty.atyParams);
                    if (output == HIRTypeRef()) {
                        if (aty.sourceTrait.mPath != trait) {
                            HIRTraitPath::assocListT sourceAssociated;
                            sourceAssociated.insert({requirement.first, requirement.second.clone()});
                            const auto sourceResult = solveGoal(aty.sourceTrait.mPath, aty.sourceTrait.mParams, impl.getImplType(crate.types), &sourceAssociated);
                            if (sourceResult == Certainty::NoSolution) {
                                return Certainty::NoSolution;
                            }
                            if (sourceResult == Certainty::Ambiguous) {
                                result = Certainty::Ambiguous;
                            }
                            continue;
                        }
                        if (impl.mData.is_TraitImpl()) {
                            result = Certainty::Ambiguous;
                            continue;
                        }
                        // A ParamEnv predicate without an explicit equality still
                        // has a canonical projection response.  This is what lets
                        // `T: Trait` prove a nested `T: Trait<Assoc = U>` while
                        // constraining U to `<T as Trait>::Assoc`.
                        output = makeAssociatedProjection(impl, aty.sourceTrait, requirement.first, aty.atyParams);
                    }
                    // The projection response may contain the very caller-owned
                    // inference variable from the requested equality. That is an
                    // exact response, not an ambiguous comparison of two ivars.
                    const auto cmp = output == aty.type ? HIRCompare::Equal : mResolve.compareTy(span(), output, aty.type);
                    if (cmp == HIRCompare::Unequal) {
                        return Certainty::NoSolution;
                    }
                    if (cmp == HIRCompare::Fuzzy) {
                        result = Certainty::Ambiguous;
                    }
                }
                return result;
            }

            Certainty evaluateAutoBuiltin(const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type) {
                auto combine = [](Certainty& result, Certainty nested) {
                    if (nested == Certainty::NoSolution) {
                        result = Certainty::NoSolution;
                    } else if (nested == Certainty::Ambiguous && result == Certainty::Proven) {
                        result = Certainty::Ambiguous;
                    }
                };
                auto evaluateInner = [&](const HIRTypeData* inner) {
                    return solveGoal(trait, params, inner, nullptr);
                };

            TU_MATCH_HDRA((*type), {)
            default:
                return Certainty::Proven;
            TU_ARMA(Path, e) {
                if (const auto* pe = e.path.mData.opt_Generic()) {
                    HIRTypeRef tmp;
                    auto monomorph = MonomorphStatePtr(crate.types, nullptr, &pe->mParams, nullptr);
                    auto evaluateField = [&](const HIRTypeData* field) {
                        const auto& fieldType = monomorphiseTypeNeeded(field) ? (tmp = mResolve.expandAssociatedTypes(span(), monomorph.monomorphType(span(), field))) : field;
                        return evaluateInner(fieldType);
                    };

                    if (e.binding.is_Unbound() || e.binding.is_Opaque()) {
                        return Certainty::Ambiguous;
                    }
                    Certainty result = Certainty::Proven;
                    if (const auto* strPtr = e.binding.opt_Struct()) {
                        const auto& str = **strPtr;
                        TU_MATCH(
                            HIRStruct::Data,
                            (str.mData),
                            (se),
                            (Unit, ),
                            (Tuple,
                             for (const auto& field : se) {
                                 combine(result, evaluateField(field.ent));
                                 if (result == Certainty::NoSolution) {
                                     return result;
                                 }
                             }),
                            (Named, for (const auto& field : se) {
                                combine(result, evaluateField(field.ty));
                                if (result == Certainty::NoSolution) {
                                    return result;
                                }
                            })
                        )
                    } else if (const auto* enmPtr = e.binding.opt_Enum()) {
                        const auto& enm = **enmPtr;
                        if (const auto* variants = enm.mData.opt_Data()) {
                            for (const auto& variant : *variants) {
                                combine(result, evaluateField(variant.type));
                                if (result == Certainty::NoSolution) {
                                    return result;
                                }
                            }
                        }
                    } else if (const auto* unnPtr = e.binding.opt_Union()) {
                        const auto& unn = **unnPtr;
                        for (const auto& field : unn.mVariants) {
                            combine(result, evaluateField(field.ty));
                            if (result == Certainty::NoSolution) {
                                return result;
                            }
                        }
                    } else if (e.binding.is_ExternType()) {
                        return Certainty::NoSolution;
                    }
                    return result;
                }
                if (e.path.mData.is_UfcsKnown() && (e.binding.is_Unbound() || e.binding.is_Opaque())) {
                    return Certainty::Ambiguous;
                }
                return Certainty::Ambiguous;
            }
            TU_ARMA(Generic, e) {
                return evaluateInner(type);
            }
            TU_ARMA(Tuple, e) {
                Certainty result = Certainty::Proven;
                for (const auto& field : e) {
                    combine(result, evaluateInner(field));
                    if (result == Certainty::NoSolution) {
                        return result;
                    }
                }
                return result;
            }
            TU_ARMA(Array, e) {
                return evaluateInner(e.inner);
            }
            }
            throw "";
            }

            Certainty evaluateCandidate(size_t frameIndex, size_t candidateIndex, const HIRSimplePath& trait, const HIRTraitPath::assocListT* associated) {
                auto* candidate = frames[frameIndex]->candidates[candidateIndex];
                candidate->ambiguityBeyondHead = false;
                if (associated) {
                    bindCandidatePlaceholders(*candidate, candidate->impl.getImplType(crate.types), *associated, true);
                }
                const bool environmentResponseConstraint = candidate->headMatch == HIRCompare::Fuzzy && isEnvironmentOrBuiltin(candidate->impl) && !candidateHasPlaceholders(*candidate);
                auto result = candidate->headMatch == HIRCompare::Equal || environmentResponseConstraint ? Certainty::Proven : Certainty::Ambiguous;

                const bool autoBuiltin = candidate->autoBuiltin;
                const auto* markerImpl = candidate->markerImpl;
                if (autoBuiltin) {
                    const auto& response = candidate->impl.mData.as_Bounded();
                    const auto structural = evaluateAutoBuiltin(trait, response.traitArgs, response.type);
                    if (structural == Certainty::NoSolution) {
                        return Certainty::NoSolution;
                    }
                    if (structural == Certainty::Ambiguous) {
                        candidate->ambiguityBeyondHead = true;
                        result = Certainty::Ambiguous;
                    }
                }

                const auto assocResult = matchAssociatedTypes(trait, candidate->impl, associated);
                if (assocResult == Certainty::NoSolution) {
                    return Certainty::NoSolution;
                }
                if (assocResult == Certainty::Ambiguous) {
                    candidate->ambiguityBeyondHead = true;
                    result = Certainty::Ambiguous;
                }

                const auto* traitImpl = candidate->impl.mData.opt_TraitImpl();
                const HIRGenericParams* implParamsDef = markerImpl ? &markerImpl->mParams : (traitImpl && traitImpl->impl ? &traitImpl->impl->mParams : nullptr);
                if (!implParamsDef) {
                    return result;
                }

                for (const auto& bound : implParamsDef->bounds) {
                    if (const auto* be = bound.opt_TraitBound()) {
                        HIRTypeRef nestedType;
                        HIRSimplePath nestedTrait;
                        HIRPathParams nestedParams;
                        HIRTraitPath::assocListT nestedAssociated;

                        // Candidate and response storage is pool-backed, so nested
                        // goals cannot relocate this parent slot.
                        auto monomorphBound = [&](auto& ms) {
                            auto boundType = ms.monomorphType(span(), be->type);
                            auto boundTrait = ms.monomorphTraitpath(span(), be->trait, true);

                            nestedType = mv$(boundType);
                            nestedTrait = boundTrait.mPath.mPath;
                            nestedParams = boundTrait.mPath.mParams.clone();
                            for (const auto& aty : boundTrait.typeBounds) {
                                nestedAssociated.insert({aty.first, aty.second.clone()});
                            }
                        };
                        if (markerImpl) {
                            auto ms = MonomorphStatePtr(crate.types, nullptr, &candidate->markerImplParams, nullptr);
                            monomorphBound(ms);
                        } else {
                            auto ms = candidate->impl.getCbMonomorphTraitimpl(crate.types, span(), {});
                            monomorphBound(ms);
                        }

                        // An impl parameter may occur only in an associated-type
                        // equality of a nested goal.  Canonical solvers infer that
                        // parameter from the projection response of the nested
                        // goal; preserve the same response in our impl parameters
                        // before evaluating the goal itself.
                        if (bindCandidatePlaceholders(*candidate, nestedType, nestedAssociated)) {
                            nestedAssociated.clear();
                            if (markerImpl) {
                                auto ms = MonomorphStatePtr(crate.types, nullptr, &candidate->markerImplParams, nullptr);
                                monomorphBound(ms);
                            } else {
                                auto ms = candidate->impl.getCbMonomorphTraitimpl(crate.types, span(), {});
                                monomorphBound(ms);
                            }
                        }

                        // The certainty-only table is the fast path for the vast
                        // majority of nested obligations and also validates all
                        // associated-type constraints. Only an ambiguous goal
                        // whose response can still bind this candidate needs the
                        // more expensive canonical response assembly.
                        auto nested = solveGoal(nestedTrait, nestedParams, nestedType, &nestedAssociated);
                        if (nested == Certainty::NoSolution) {
                            return Certainty::NoSolution;
                        }
                        if (nested == Certainty::Ambiguous && candidateNeedsResponseConstraints(*candidate)) {
                            Certainty responseCertainty = Certainty::NoSolution;
                            const bool hasResponse = evaluate(span(), nestedTrait, nestedParams, nestedType, [&](ImplRef response, HIRCompare certainty) {
                                bindCandidateResponse(*candidate, nestedType, nestedParams, response);
                                responseCertainty = certainty == HIRCompare::Equal ? Certainty::Proven : Certainty::Ambiguous;
                                return true;
                            }, "", nullptr, nullptr);
                            if (!hasResponse) {
                                return Certainty::NoSolution;
                            }
                            nested = responseCertainty;
                        }
                        if (nested == Certainty::Ambiguous) {
                            candidate->ambiguityBeyondHead = true;
                            result = Certainty::Ambiguous;
                        }
                    } else if (const auto* equality = bound.opt_TypeEquality()) {
                        HIRTypeRef left;
                        HIRTypeRef right;
                        if (markerImpl) {
                            auto ms = MonomorphStatePtr(crate.types, nullptr, &candidate->markerImplParams, nullptr);
                            left = ms.monomorphType(span(), equality->type);
                            right = ms.monomorphType(span(), equality->otherType);
                        } else {
                            auto ms = candidate->impl.getCbMonomorphTraitimpl(crate.types, span(), {});
                            left = ms.monomorphType(span(), equality->type);
                            right = ms.monomorphType(span(), equality->otherType);
                        }
                        const auto cmp = mResolve.compareTy(span(), left, right);
                        if (cmp == HIRCompare::Unequal) {
                            return Certainty::NoSolution;
                        }
                        if (cmp == HIRCompare::Fuzzy) {
                            candidate->ambiguityBeyondHead = true;
                            result = Certainty::Ambiguous;
                        }
                    }
                }
                return result;
            }

            Certainty solveGoal(const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, const HIRTraitPath::assocListT* associated) {
                const auto availableDepth = availableDepthForNested();
                if (!availableDepth) {
                    return Certainty::Ambiguous;
                }
                auto goalType = type;
                auto goalParams = params.clone();
                if (goalHasUnassignedInfer(goalParams, goalType, associated)) {
                    return Certainty::Ambiguous;
                }
                // Nested obligations are formed directly from monomorphised impl
                // bounds.  Their Self type can therefore still be a projection,
                // e.g. `<Option::IntoIter<T> as Iterator>::Item: IntoIterator`.
                // Candidate assembly operates on the normalized goal input, just
                // as it already does for trait arguments.
                goalType = mResolve.expandAssociatedTypes(span(), goalType);
                for (auto& param : goalParams.types) {
                    param = mResolve.expandAssociatedTypes(span(), ::std::move(param));
                }
                if (goalHasUnassignedInfer(goalParams, goalType, associated)) {
                    return Certainty::Ambiguous;
                }
                // rustc structurally normalises Self before candidate assembly.
                // An unresolved projection over a type variable normalises to an
                // inference variable and therefore forces ambiguity; treating the
                // projection syntax as rigid lets an unrelated fuzzy ParamEnv
                // predicate constrain its output.
                if (selfIsUnresolvedProjectionOverIvar(goalType)) {
                    return Certainty::Ambiguous;
                }
                const auto& resolvedType = mResolve.resolveType(goalType);
                // Candidate assembly must not use an unconstrained `Self` type to
                // guide inference.  A concrete associated-type equality does
                // constrain the goal, however, and may uniquely determine Self.
                bool associatedConstrainsSelf = false;
                if (associated) {
                    for (const auto& entry : *associated) {
                        associatedConstrainsSelf |= !typeHasUnknown(entry.second.type);
                    }
                }
                if (const auto* infer = resolvedType->opt_Infer()) {
                    if (!infer->isLit() && !associatedConstrainsSelf) {
                        return Certainty::Ambiguous;
                    }
                }
                CanonicalizeTraitGoal canonicalizer(crate.types);
                const auto canonical = canonicalizeGoal(goalParams, resolvedType, associated, canonicalizer);
                const auto* canonicalAssociated = canonical.associated.empty() ? nullptr : &canonical.associated;
                const auto hash = goalHash(trait, canonical.params, canonical.type, canonicalAssociated);
                if (const auto* cached = findCachedGoal(hash, trait, canonical.params, canonical.type, canonicalAssociated)) {
                    return cached->certainty;
                }
                if (findActiveGoal(hash, trait, canonical.params, canonical.type, canonicalAssociated)) {
                    // Productive recursive traits prove their provisional goal;
                    // ordinary trait cycles remain ambiguous.
                    return crate.getTraitByPath(span(), trait).isCoinductive ? Certainty::Proven : Certainty::Ambiguous;
                }

                auto* activeGoal = pushActiveGoal(hash, trait, canonical.params, canonical.type, canonicalAssociated);

                struct StackGuard {
                    NextTraitGoalEvaluator& self;
                    GoalKey* goal;

                    ~StackGuard() {
                        self.popActiveGoal(goal);
                    }
                } guard{*this, activeGoal};

                auto cacheResult = [&](Certainty certainty) {
                    return cacheGoal(hash, trait, canonical.params, canonical.type, canonicalAssociated, certainty);
                };

                const size_t frameIndex = frameDepth++;
                if (frameIndex == frames.size()) {
                    frames.push_back(crate.pool->make<CandidateFrame>());
                }
                frames[frameIndex]->clear(candidateNodes);
                frames[frameIndex]->availableDepth = *availableDepth;

                struct FrameGuard {
                    NextTraitGoalEvaluator& self;
                    size_t index;

                    ~FrameGuard() {
                        const bool encounteredOverflow = self.frames[index]->encounteredOverflow;
                        self.frames[index]->clear(self.candidateNodes);
                        assert(self.frameDepth == index + 1);
                        self.frameDepth--;
                        if (encounteredOverflow && index > 0) {
                            self.frames[index - 1]->encounteredOverflow = true;
                        }
                    }
                } frameGuard{*this, frameIndex};

                try {
                    assembleCandidates(frameIndex, trait, goalParams, resolvedType);
                } catch (const TraitResolution::RecursionDetected&) {
                    return cacheResult(Certainty::Ambiguous);
                }

                bool sawAmbiguous = false;
                bool suppressAutoBuiltin = false;
                bool negativeProven = false;
                bool negativeAmbiguous = false;
                Certainty autoBuiltinResult = Certainty::NoSolution;
                const size_t candidateCount = frames[frameIndex]->candidates.size();
                for (size_t i = 0; i < candidateCount; i++) {
                    const auto result = evaluateCandidate(frameIndex, i, trait, associated);
                    auto* candidate = frames[frameIndex]->candidates[i];
                    candidate->certainty = result;
                    if (candidate->isNegative()) {
                        negativeProven |= result == Certainty::Proven;
                        negativeAmbiguous |= result == Certainty::Ambiguous;
                        continue;
                    }
                    if (candidate->autoBuiltin) {
                        autoBuiltinResult = result;
                        continue;
                    }
                    suppressAutoBuiltin |= candidate->isPositiveMarkerImpl() && result != Certainty::NoSolution;
                    if (result == Certainty::Proven) {
                        return cacheResult(Certainty::Proven);
                    }
                    sawAmbiguous |= result == Certainty::Ambiguous;
                }
                if (!suppressAutoBuiltin && !negativeProven) {
                    if (negativeAmbiguous && autoBuiltinResult == Certainty::Proven) {
                        autoBuiltinResult = Certainty::Ambiguous;
                    }
                    if (autoBuiltinResult == Certainty::Proven) {
                        return cacheResult(Certainty::Proven);
                    }
                    sawAmbiguous |= autoBuiltinResult == Certainty::Ambiguous;
                }
                if (sawAmbiguous || mResolve.typeContainsIvars(resolvedType) || mResolve.paramsContainIvars(goalParams) || (coherenceMode && !traitRefIsKnowable(trait, goalParams, resolvedType))) {
                    return cacheResult(Certainty::Ambiguous);
                }
                return cacheResult(Certainty::NoSolution);
            }

            Certainty matchRootAssociated(const HIRSimplePath& trait, const ImplRef& impl, const char* assocName, const HIRTypeData* assocType, const HIRPathParams* assocParams) const {
                if (!assocName || !assocName[0]) {
                    return Certainty::Proven;
                }
                const static HIRPathParams noParams;
                const auto& params = assocParams ? *assocParams : noParams;
                if (!impl.mData.is_TraitImpl() && params.hasParams()) {
                    return Certainty::Ambiguous;
                }
                auto output = impl.getType(crate.types, assocName, params);
                if (output == HIRTypeRef()) {
                    if (impl.mData.is_TraitImpl()) {
                        return Certainty::Ambiguous;
                    }
                    if (!assocType) {
                        // A bare ParamEnv trait predicate does not normalize its
                        // associated type.  It only proves that the projection is
                        // well-formed, so the normalizes-to response is ambiguous.
                        return Certainty::Ambiguous;
                    }
                    output = makeAssociatedProjection(impl, HIRGenericPath(trait, impl.getTraitParams(crate.types)), RcString::newInterned(assocName), params);
                }
                if (!assocType) {
                    return Certainty::Proven;
                }
                const auto cmp = mResolve.compareTy(span(), assocType, output);
                if (cmp == HIRCompare::Unequal) {
                    return Certainty::NoSolution;
                }
                // A normalizes-to goal with a caller inference variable has a
                // proven response plus an equality constraint. The caller applies
                // that constraint from the returned ImplRef; the unassigned
                // destination alone must not turn a unique response into `Maybe`.
                if (cmp == HIRCompare::Fuzzy && mResolve.typeContainsIvars(assocType) && !mResolve.typeContainsIvars(output) && !typeHasCandidatePlaceholder(output)) {
                    return Certainty::Proven;
                }
                return cmp == HIRCompare::Equal ? Certainty::Proven : Certainty::Ambiguous;
            }

            ImplRef materializeRootAssociated(ImplRef impl, const HIRSimplePath& trait, const char* assocName, const HIRPathParams* assocParams) const {
                if (!assocName || !assocName[0] || impl.mData.is_TraitImpl()) {
                    return impl;
                }
                const static HIRPathParams noParams;
                const auto& itemParams = assocParams ? *assocParams : noParams;
                if (impl.getType(crate.types, assocName, itemParams) != HIRTypeRef()) {
                    return impl;
                }

                auto type = impl.getImplType(crate.types);
                auto params = impl.getTraitParams(crate.types);
                HIRTraitPath::assocListT associated;
                if (const auto* bounded = impl.mData.opt_BoundedPtr()) {
                    for (const auto& entry : *bounded->assoc) {
                        associated.insert({entry.first, entry.second.clone()});
                    }
                } else if (const auto* bounded = impl.mData.opt_Bounded()) {
                    for (const auto& entry : bounded->assoc) {
                        associated.insert({entry.first, entry.second.clone()});
                    }
                }

                const auto name = RcString::newInterned(assocName);
                auto sourceTrait = HIRGenericPath(trait, params.clone());
                auto projection = makeAssociatedProjection(type, sourceTrait, name, itemParams);
                associated.erase(name);
                associated.insert({name, HIRTraitPath::AtyEqual{::std::move(sourceTrait), itemParams.clone(), ::std::move(projection)}});
                const bool ambiguousIdentity = impl.isAmbiguousIdentity();
                auto result = ImplRef(::std::move(type), ::std::move(params), ::std::move(associated));
                if (ambiguousIdentity) {
                    result.markAmbiguousIdentity();
                }
                return result;
            }

            bool responsesEqual(const ImplRef& left, const ImplRef& right, const char* assocName, const HIRPathParams* assocParams) const {
                auto typesEqualAfterNormalization = [&](const HIRTypeData* lhs, const HIRTypeData* rhs) {
                    if (lhs == HIRTypeRef() || rhs == HIRTypeRef()) {
                        return lhs == rhs;
                    }
                    // TypeRef identity is structural equality after interning.
                    // Avoid recursively normalising and re-interning the common
                    // case where both canonical responses already share a type.
                    if (lhs == rhs) {
                        return true;
                    }
                    auto normalizedLhs = mResolve.expandAssociatedTypes(span(), lhs);
                    auto normalizedRhs = mResolve.expandAssociatedTypes(span(), rhs);
                    if (normalizedLhs == HIRTypeRef() || normalizedRhs == HIRTypeRef()) {
                        return normalizedLhs == normalizedRhs;
                    }
                    const auto* resolvedLhs = mResolve.resolveType(normalizedLhs);
                    const auto* resolvedRhs = mResolve.resolveType(normalizedRhs);
                    return resolvedLhs == resolvedRhs || resolvedLhs->equalsIgnoringRegions(resolvedRhs);
                };
                auto paramsEqualAfterNormalization = [&](const HIRPathParams& lhs, const HIRPathParams& rhs) {
                    if (lhs.types.size() != rhs.types.size() || lhs.values.size() != rhs.values.size()) {
                        return false;
                    }
                    // Ordinary regions are deliberately erased when a type is
                    // stored in an HM inference variable, and trait selection
                    // defers their constraints to lifetime inference.  Thus a
                    // ParamEnv proof for `Projection<'a>` and the same declared
                    // GAT bound seen through `Projection<'#omitted>` are one
                    // canonical solver response.  Higher-ranked leak checking is
                    // performed while evaluating the candidate bounds above; it
                    // must not be reintroduced here as response identity.
                    for (size_t i = 0; i < lhs.types.size(); i++) {
                        if (!typesEqualAfterNormalization(lhs.types[i], rhs.types[i])) {
                            return false;
                        }
                    }
                    for (size_t i = 0; i < lhs.values.size(); i++) {
                        if (lhs.values[i] != rhs.values[i]) {
                            return false;
                        }
                    }
                    return true;
                };

                if (!typesEqualAfterNormalization(left.getImplType(crate.types), right.getImplType(crate.types)) || !paramsEqualAfterNormalization(left.getTraitParams(crate.types), right.getTraitParams(crate.types))) {
                    return false;
                }
                if (!assocName || !assocName[0]) {
                    return true;
                }
                const static HIRPathParams noParams;
                const auto& params = assocParams ? *assocParams : noParams;
                if ((!left.mData.is_TraitImpl() || !right.mData.is_TraitImpl()) && params.hasParams()) {
                    return false;
                }
                return typesEqualAfterNormalization(left.getType(crate.types, assocName, params), right.getType(crate.types, assocName, params));
            }

        public:
            NextTraitGoalEvaluator(const TraitResolution& resolve, const HIRCrate& crate)
                : mResolve(resolve)
                , crate(crate)
                , candidateNodes(crate.pool)
                , activeGoalNodes(crate.pool)
                , cachedGoalNodes(crate.pool)
            {
                frames.reserve(16);
                goalStack.reserve(16);
                goalCache.reserve(64);
                activeGoalIndex.reserve(32);
                goalCacheIndex.reserve(128);
            }

            bool evaluateOverlap(const Span& callSpan, const HIRSimplePath& trait, const HIRTraitImpl& left, const HIRTraitImpl& right) {
                ASSERT_BUG(callSpan, !mSpan, "nested coherence overlap session");
                ASSERT_BUG(callSpan, !coherenceMode, "coherence mode leaked before overlap probe");
                ASSERT_BUG(callSpan, goalStack.empty(), "next-solver goal stack leaked before coherence probe");
                ASSERT_BUG(callSpan, activeGoalIndex.empty(), "next-solver active goal index leaked before coherence probe");
                ASSERT_BUG(callSpan, frameDepth == 0, "next-solver candidate frames leaked before coherence probe");
                clearGoalCache();
                mSpan = &callSpan;
                coherenceMode = true;

                struct SessionGuard {
                    NextTraitGoalEvaluator& self;

                    ~SessionGuard() {
                        assert(self.goalStack.empty());
                        assert(self.activeGoalIndex.empty());
                        self.clearGoalCache();
                        self.frameDepth = 0;
                        self.coherenceMode = false;
                        self.mSpan = nullptr;
                    }
                } sessionGuard{*this};

                // Instantiate the first header with fresh inference variables, then
                // match the second header against it.  This is a unification of two
                // independently generic impls, not a one-way syntactic ordering.
                auto leftParams = mResolve.makeFreshImplParams(left.mParams);
                auto leftMonomorph = MonomorphStatePtr(crate.types, nullptr, &leftParams, nullptr);
                auto goalType = leftMonomorph.monomorphType(callSpan, left.mType, true);
                auto goalParams = leftMonomorph.monomorphPathParams(callSpan, left.traitArgs, true);

                HIRPathParams rightParams;
                const auto rightMatch = mResolve.fticCheckParams(callSpan, trait, &goalParams, goalType, right.mParams, right.traitArgs, right.mType, rightParams, false);
                if (rightMatch == HIRCompare::Unequal) {
                    return false;
                }

                const size_t frameIndex = frameDepth++;
                if (frameIndex == frames.size()) {
                    frames.push_back(crate.pool->make<CandidateFrame>());
                }
                frames[frameIndex]->clear(candidateNodes);
                frames[frameIndex]->availableDepth = ROOT_DEPTH;

                struct FrameGuard {
                    NextTraitGoalEvaluator& self;
                    size_t index;

                    ~FrameGuard() {
                        const bool encounteredOverflow = self.frames[index]->encounteredOverflow;
                        self.frames[index]->clear(self.candidateNodes);
                        assert(self.frameDepth == index + 1);
                        self.frameDepth--;
                        if (encounteredOverflow && index > 0) {
                            self.frames[index - 1]->encounteredOverflow = true;
                        }
                    }
                } frameGuard{*this, frameIndex};

                const auto& traitDef = crate.getTraitByPath(callSpan, trait);
                pushCandidate(frameIndex, ImplRef(::std::move(leftParams), traitDef, trait, left), HIRCompare::Equal, nullptr, {}, false, CandidateSource::TraitImpl);
                pushCandidate(frameIndex, ImplRef(::std::move(rightParams), traitDef, trait, right), rightMatch, nullptr, {}, false, CandidateSource::TraitImpl);

                const auto& candidates = frames[frameIndex]->candidates;
                ASSERT_BUG(callSpan, candidates.size() == 2, "coherence probe lost an impl candidate");
                const auto leftResult = evaluateCandidate(frameIndex, 0, trait, nullptr);
                if (leftResult == Certainty::NoSolution) {
                    return false;
                }
                const auto rightResult = evaluateCandidate(frameIndex, 1, trait, nullptr);
                return rightResult != Certainty::NoSolution;
            }

            bool evaluate(const Span& callSpan, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, TraitResolution::tCbTraitImplR callback, const char* assocName, const HIRTypeData* assocType, const HIRPathParams* assocParams) {
                const bool outermost = mSpan == nullptr;
                if (outermost) {
                    ASSERT_BUG(callSpan, goalStack.empty(), "next-solver goal stack leaked between evaluations");
                    ASSERT_BUG(callSpan, activeGoalIndex.empty(), "next-solver active goal index leaked between evaluations");
                    ASSERT_BUG(callSpan, frameDepth == 0, "next-solver candidate frames leaked between evaluations");
                    clearGoalCache();
                    mSpan = &callSpan;
                }

                struct SessionGuard {
                    NextTraitGoalEvaluator& self;
                    bool outermost;

                    ~SessionGuard() {
                        if (outermost) {
                            assert(self.goalStack.empty());
                            assert(self.activeGoalIndex.empty());
                            self.clearGoalCache();
                            self.frameDepth = 0;
                            self.mSpan = nullptr;
                        }
                    }
                } sessionGuard{*this, outermost};

                auto goalType = type;
                auto goalParams = params.clone();
                auto emitForcedAmbiguity = [&]() {
                    // Ordinary lookup cannot consume an identity response, while
                    // extended solver callers use it to retain the original goal
                    // without committing any candidate substitutions.
                    if (!assocName) {
                        return false;
                    }
                    auto ambiguous = ImplRef(goalType, goalParams.clone(), HIRTraitPath::assocListT());
                    ambiguous.markAmbiguousIdentity();
                    return callback(materializeRootAssociated(::std::move(ambiguous), trait, assocName, assocParams), HIRCompare::Fuzzy);
                };
                if (goalHasUnassignedInfer(goalParams, goalType, nullptr)) {
                    return emitForcedAmbiguity();
                }
                goalType = mResolve.expandAssociatedTypes(span(), goalType);
                for (auto& param : goalParams.types) {
                    param = mResolve.expandAssociatedTypes(span(), ::std::move(param));
                }
                if (selfIsUnresolvedProjectionOverIvar(goalType)) {
                    return emitForcedAmbiguity();
                }
                const auto& resolvedType = mResolve.resolveType(goalType);
                // Match rustc's forced-ambiguity response for a genuinely
                // unconstrained `Self` type.  A known associated output is an
                // input constraint and can legitimately select a unique response.
                const bool associatedConstrainsSelf = assocName && assocName[0] && assocType && !typeHasUnknown(assocType);
                if (const auto* infer = resolvedType->opt_Infer()) {
                    if (!infer->isLit() && !associatedConstrainsSelf) {
                        return emitForcedAmbiguity();
                    }
                }
                CanonicalizeTraitGoal canonicalizer(crate.types);
                const auto canonical = canonicalizeGoal(goalParams, resolvedType, nullptr, canonicalizer);
                // The associated output is not part of the response cache key,
                // but its placeholders are still inputs of this query.  Record
                // them so root response instantiation does not mistake them for
                // existential variables created by candidate evaluation.
                if (assocType) {
                    canonicalizer.monomorphType(span(), assocType, true);
                }
                if (assocParams) {
                    canonicalizer.monomorphPathParams(span(), *assocParams, true);
                }
                const auto rootHash = goalHash(trait, canonical.params, canonical.type, nullptr);
                auto instantiateForCaller = [&](ImplRef response) {
                    if (!outermost) {
                        return response;
                    }
                    InstantiateTraitResponseForCaller instantiator(crate.types, const_cast<HMTypeInferrence&>(mResolve.ivars), canonicalizer.placeholderNames());
                    return monomorphImplRef(response, instantiator);
                };
                // Extended callers use an explicit empty associated-item name
                // when they need the canonical trait response itself. Cache that
                // completed response, not just its certainty: otherwise every
                // repeated nested obligation rebuilds the entire candidate graph.
                const bool cacheableResponse = assocName && !assocName[0];
                if (cacheableResponse) {
                    if (const auto* cached = findCachedGoal(rootHash, trait, canonical.params, canonical.type, nullptr); cached && cached->hasResponse) {
                        InstantiateCanonicalTraitResponse instantiator(crate.types, canonicalizer.placeholderNames(), responseInstanceCounter++);
                        auto response = monomorphImplRef(cached->response, instantiator);
                        return callback(instantiateForCaller(::std::move(response)), cached->responseCertainty);
                    }
                }
                auto emitResponse = [&](ImplRef response, HIRCompare certainty) {
                    if (!cacheableResponse) {
                        return callback(instantiateForCaller(::std::move(response)), certainty);
                    }
                    auto canonicalResponse = monomorphImplRef(response, canonicalizer);
                    auto* cached = cacheResponse(rootHash, trait, canonical.params, canonical.type, nullptr, ::std::move(canonicalResponse), certainty);
                    return callback(instantiateForCaller(::std::move(response)), cached->responseCertainty);
                };
                if (findActiveGoal(rootHash, trait, canonical.params, canonical.type, nullptr)) {
                    static const HIRTraitPath::assocListT noAssociated;
                    const bool coinductive = crate.getTraitByPath(span(), trait).isCoinductive;
                    return callback(ImplRef(resolvedType, &goalParams, &noAssociated), coinductive ? HIRCompare::Equal : HIRCompare::Fuzzy);
                }
                auto* rootGoal = pushActiveGoal(rootHash, trait, canonical.params, canonical.type, nullptr);

                struct RootStackGuard {
                    NextTraitGoalEvaluator& self;
                    GoalKey* goal;

                    ~RootStackGuard() {
                        self.popActiveGoal(goal);
                    }
                } rootGuard{*this, rootGoal};

                const size_t frameIndex = frameDepth++;
                if (frameIndex == frames.size()) {
                    frames.push_back(crate.pool->make<CandidateFrame>());
                }
                frames[frameIndex]->clear(candidateNodes);
                frames[frameIndex]->availableDepth = ROOT_DEPTH;

                struct FrameGuard {
                    NextTraitGoalEvaluator& self;
                    size_t index;

                    ~FrameGuard() {
                        const bool encounteredOverflow = self.frames[index]->encounteredOverflow;
                        self.frames[index]->clear(self.candidateNodes);
                        assert(self.frameDepth == index + 1);
                        self.frameDepth--;
                        if (encounteredOverflow && index > 0) {
                            self.frames[index - 1]->encounteredOverflow = true;
                        }
                    }
                } frameGuard{*this, frameIndex};

                try {
                    assembleCandidates(frameIndex, trait, goalParams, resolvedType);
                } catch (const TraitResolution::RecursionDetected&) {
                    return false;
                }
                auto& frame = *frames[frameIndex];
                const size_t candidateCount = frame.candidates.size();
                DEBUG("next-solver assembled " << candidateCount << " candidate(s) for " << type << ": " << trait << params);

                bool suppressAutoBuiltin = false;
                bool negativeProven = false;
                bool negativeAmbiguous = false;
                const HIRTypeData* candidateAssocType = assocType;
                if (candidateAssocType) {
                    if (const auto* erased = candidateAssocType->opt_ErasedType()) {
                        if (const auto* alias = erased->inner.opt_Alias(); alias && alias->inner->isPublicTo(mResolve.mVisPath)) {
                            // A defining opaque is an output of alias-relate, not
                            // an input that can reject an otherwise valid impl.
                            // Return the projection response to the caller, which
                            // then equates it with this opaque and records its
                            // hidden type.
                            candidateAssocType = nullptr;
                        }
                    }
                }
                HIRTraitPath::assocListT rootAssociated;
                if (assocName && assocName[0] && candidateAssocType) {
                    const static HIRPathParams noAssocParams;
                    rootAssociated.insert({RcString::newInterned(assocName), HIRTraitPath::AtyEqual{HIRGenericPath(trait, goalParams.clone()), assocParams ? assocParams->clone() : noAssocParams.clone(), candidateAssocType}});
                }
                for (size_t i = 0; i < candidateCount; i++) {
                    auto certainty = evaluateCandidate(frameIndex, i, trait, rootAssociated.empty() ? nullptr : &rootAssociated);
                    auto* candidate = frame.candidates[i];
                    if (!candidate->isNegative()) {
                        const auto assocCertainty = matchRootAssociated(trait, candidate->impl, assocName, candidateAssocType, assocParams);
                        if (assocCertainty == Certainty::NoSolution) {
                            certainty = Certainty::NoSolution;
                        } else if (assocCertainty == Certainty::Ambiguous && certainty == Certainty::Proven) {
                            certainty = Certainty::Ambiguous;
                        }
                    }
                    candidate->certainty = certainty;
                    DEBUG("next-solver candidate " << candidate->impl << " => " << static_cast<unsigned>(certainty));
                    if (candidate->isNegative()) {
                        negativeProven |= certainty == Certainty::Proven;
                        negativeAmbiguous |= certainty == Certainty::Ambiguous;
                        continue;
                    }
                    suppressAutoBuiltin |= candidate->isPositiveMarkerImpl() && certainty != Certainty::NoSolution;
                    if (certainty != Certainty::NoSolution) {
                        frame.viable.push_back(candidate);
                    }
                }

                if (suppressAutoBuiltin || negativeProven) {
                    auto& viable = frame.viable;
                    viable.erase(
                        ::std::remove_if(
                            viable.begin(),
                            viable.end(),
                            [&](Candidate* candidate) {
                        return candidate->autoBuiltin;
                    }
                        ),
                        viable.end()
                    );
                } else if (negativeAmbiguous) {
                    for (auto* candidate : frame.viable) {
                        if (candidate->autoBuiltin && candidate->certainty == Certainty::Proven) {
                            candidate->certainty = Certainty::Ambiguous;
                        }
                    }
                }

                if (frame.viable.empty()) {
                    DEBUG("next-solver: no viable response");
                    // solve_goal keeps an obligation ambiguous while inference
                    // still occurs in its inputs.  The response-producing path
                    // must preserve the same result: nested candidate evaluation
                    // calls it specifically to recover constraints from an
                    // ambiguous goal.  Returning false here would turn e.g.
                    // `<_ as IntoIterator>::IntoIter: Iterator` into NoSolution
                    // and incorrectly discard an enclosing `Zip` candidate.
                    if (mResolve.typeContainsIvars(resolvedType) || mResolve.paramsContainIvars(goalParams)) {
                        return emitForcedAmbiguity();
                    }
                    return false;
                }

                // rustc prefers all ParamEnv responses when any applicable
                // non-global where-bound exists. In particular, `T:
                // Pointee<Metadata = ()>` must retain the environment response
                // instead of normalising through the generic builtin fallback.
                const bool hasNonGlobalParamEnv = ::std::any_of(frame.viable.begin(), frame.viable.end(), [&](const Candidate* candidate) {
                    return paramEnvCandidateIsNonGlobal(*candidate);
                });
                if (hasNonGlobalParamEnv) {
                    auto& viable = frame.viable;
                    viable.erase(
                        ::std::remove_if(
                            viable.begin(),
                            viable.end(),
                            [](const Candidate* candidate) {
                        return candidate->source != CandidateSource::ParamEnv;
                    }
                        ),
                        viable.end()
                    );
                }

                // A proven ParamEnv or builtin candidate shadows impl candidates.
                // This is the central next-solver candidate preference used for
                // projection normalization and dyn-object builtins.
                bool hasPreferredNonImpl = false;
                for (const auto* candidate : frame.viable) {
                    hasPreferredNonImpl |= isEnvironmentOrBuiltin(candidate->impl) && candidate->certainty == Certainty::Proven;
                }
                if (hasPreferredNonImpl) {
                    auto& viable = frame.viable;
                    viable.erase(
                        ::std::remove_if(
                            viable.begin(),
                            viable.end(),
                            [&](Candidate* candidate) {
                        return !isEnvironmentOrBuiltin(candidate->impl);
                    }
                        ),
                        viable.end()
                    );
                }

                // Apply specialization only after nested goals have been probed.
                for (auto* candidate : frame.viable) {
                    candidate->discarded = false;
                }
                for (size_t i = 0; i < frame.viable.size(); i++) {
                    if (frame.viable[i]->discarded) {
                        continue;
                    }
                    for (size_t j = i + 1; j < frame.viable.size(); j++) {
                        if (frame.viable[j]->discarded) {
                            continue;
                        }
                        auto& left = frame.viable[i]->impl;
                        auto& right = frame.viable[j]->impl;
                        // Specialization only distinguishes different canonical
                        // responses.  If both candidates constrain the caller in
                        // exactly the same way, merge their certainties instead;
                        // a proven route must not be discarded behind an
                        // ambiguous, cyclic route to the same response.
                        if (responsesEqual(left, right, assocName, assocParams)) {
                            continue;
                        }
                        if (!left.mData.is_TraitImpl() || !right.mData.is_TraitImpl()) {
                            continue;
                        }
                        // evaluate_overlap is itself the recursive overlap query.
                        // Re-entering either overlap implementation here makes a
                        // coinductive pair recurse without a solver cycle head.
                        // Keeping both responses is conservative: ambiguity is
                        // already sufficient to report that the impls may overlap.
                        if (coherenceMode || !mResolve.implsOverlap(span(), left, right)) {
                            continue;
                        }
                        // A more-specific impl with an ambiguous where-clause
                        // cannot shadow the fallback: that nested goal may still
                        // fail.  Head ambiguity alone is inference guidance and
                        // remains eligible for specialization.
                        if (left.moreSpecificThan(crate.types, right) && !frame.viable[i]->ambiguityBeyondHead) {
                            frame.viable[j]->discarded = true;
                        } else if (right.moreSpecificThan(crate.types, left) && !frame.viable[j]->ambiguityBeyondHead) {
                            frame.viable[i]->discarded = true;
                            break;
                        }
                    }
                }
                frame.viable.erase(
                    ::std::remove_if(
                        frame.viable.begin(),
                        frame.viable.end(),
                        [](const Candidate* candidate) {
                    return candidate->discarded;
                }
                    ),
                    frame.viable.end()
                );

                bool oneResponse = true;
                for (size_t i = 1; i < frame.viable.size(); i++) {
                    if (!responsesEqual(frame.viable.front()->impl, frame.viable[i]->impl, assocName, assocParams)) {
                        oneResponse = false;
                        break;
                    }
                }

                if (oneResponse) {
                    Candidate* selected = frame.viable.front();
                    for (auto* candidate : frame.viable) {
                        if (candidate->certainty == Certainty::Proven) {
                            selected = candidate;
                            break;
                        }
                    }
                    const auto certainty = selected->certainty;
                    DEBUG("next-solver: applying merged response " << selected->impl << " certainty=" << static_cast<unsigned>(certainty));
                    if (certainty != Certainty::Proven) {
                        return emitResponse(::std::move(selected->impl), HIRCompare::Fuzzy);
                    }
                    return emitResponse(materializeRootAssociated(::std::move(selected->impl), trait, assocName, assocParams), HIRCompare::Equal);
                }

                // Distinct canonical responses cannot guide inference.  Return a
                // single identity response for the original goal: exposing any
                // concrete candidate here lets a callback accidentally commit the
                // first candidate's substitutions despite the ambiguity.
                auto ambiguous = ImplRef(resolvedType, goalParams.clone(), HIRTraitPath::assocListT());
                ambiguous.markAmbiguousIdentity();
                return emitResponse(materializeRootAssociated(::std::move(ambiguous), trait, assocName, assocParams), HIRCompare::Fuzzy);
            }
        };

        TraitResolution::TraitResolution(const HMTypeInferrence& ivars, const WireBoard& wb, const HIRGenericParams* implParams, const HIRGenericParams* itemParams, const HIRSimplePath& visPath, const HIRGenericPath* currentTrait)
            : TraitResolveCommon(wb)
            , mLangDeref(crate.getLangItemPathOpt("deref"))
            , ivars(ivars)
            , coherenceIvars(crate.types)
            , mVisPath(visPath)
            , mCurrentTraitPath(currentTrait)
            , currentTraitPtr(currentTrait ? &crate.getTraitByPath(Span(), currentTrait->mPath) : nullptr)
        {
            mImplGenerics = implParams;
            mItemGenerics = itemParams;
            prepIndexes(Span());
        }

        TraitResolution::~TraitResolution() = default;

        void TraitResolution::setGenericContext(const HIRGenericParams* implParams, const HIRGenericParams* itemParams) {
            if (mImplGenerics == implParams && mItemGenerics == itemParams) {
                return;
            }
            ASSERT_BUG(Span(), eatActiveStack.empty(), "changing trait environment during associated-type expansion");
            mImplGenerics = implParams;
            mItemGenerics = itemParams;
            eatCache.clear();
            prepIndexes(Span());
        }

        HIRPathParams TraitResolution::makeFreshImplParams(const HIRGenericParams& params) const {
            auto& mutIvars = const_cast<HMTypeInferrence&>(this->ivars);
            HIRPathParams result;
            result.types.reserve(params.types.size());
            for (size_t i = 0; i < params.types.size(); i++) {
                result.types.push_back(mutIvars.newIvarTr());
            }
            result.values.reserve(params.values.size());
            for (size_t i = 0; i < params.values.size(); i++) {
                result.values.push_back(HIRConstGeneric::make_Infer({mutIvars.newIvarVal()}));
            }
            return result;
        }

        bool TraitResolution::implsOverlap(const Span& sp, const ImplRef& left, const ImplRef& right) const {
            const auto* leftImpl = left.mData.opt_TraitImpl();
            const auto* rightImpl = right.mData.opt_TraitImpl();
            if (!this->wb.settings->solver.coherence || !leftImpl || !rightImpl || !leftImpl->impl || !rightImpl->impl) {
                return left.overlapsWith(crate, right);
            }
            if (!leftImpl->traitPath || !rightImpl->traitPath || *leftImpl->traitPath != *rightImpl->traitPath) {
                return false;
            }
            if (leftImpl->impl == rightImpl->impl) {
                return true;
            }

            // The probe resolver is pool-owned and reused, while its inference table
            // is reset per overlap query.  No probe variable can escape into m_ivars.
            coherenceIvars.ivars.clear();
            coherenceIvars.values.clear();
            coherenceIvars.hasChanged = false;
            if (!coherenceResolve) {
                ASSERT_BUG(sp, crate.pool, "next-solver coherence requires the crate object pool");
                coherenceResolve = crate.pool->make<TraitResolution>(coherenceIvars, this->wb, mImplGenerics, mItemGenerics, mVisPath, mCurrentTraitPath);
            } else {
                coherenceResolve->setGenericContext(mImplGenerics, mItemGenerics);
            }
            if (!coherenceResolve->nextSolver) {
                coherenceResolve->nextSolver = crate.pool->make<NextTraitGoalEvaluator>(*coherenceResolve, crate);
            }
            return coherenceResolve->nextSolver->evaluateOverlap(sp, *leftImpl->traitPath, *leftImpl->impl, *rightImpl->impl);
        }

        bool TraitResolution::findTraitImplsNext(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, tCbTraitImplR callback, const char* assocName, const HIRTypeData* assocType, const HIRPathParams* assocParams) const {
            TRACE_FUNCTION_F("trait = " << trait << params << ", type = " << type);
            if (!nextSolver) {
                ASSERT_BUG(sp, crate.pool, "next-solver requires the crate object pool");
                nextSolver = crate.pool->make<NextTraitGoalEvaluator>(*this, crate);
            }
            return nextSolver->evaluate(sp, trait, params, type, ::std::move(callback), assocName, assocType, assocParams);
        }

        bool TraitResolution::findTraitImpls(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, tCbTraitImplR callback, bool magicTraitImpls) const {
            if (this->wb.settings->solver.globally && magicTraitImpls) {
                return findTraitImplsNext(sp, trait, params, type, ::std::move(callback));
            }
            return findTraitImplsLegacy(sp, trait, params, type, ::std::move(callback), magicTraitImpls);
        }

        // -------------------------------------------------------------------------------------------------------------------
        // -------------------------------------------------------------------------------------------------------------------

        void TraitResolution::compactIvars(HMTypeInferrence& ivars) {
            ivars.checkForLoops();

            unsigned int i = 0;
            for (auto& v : ivars.ivars) {
                if (!v.isAlias()) {
                    ivars.expandIvars(v.type);
                    // Don't expand unless it is needed
                    if (this->hasAssociatedType(v.type)) {
                        auto nt = this->expandAssociatedTypes(Span(), v.type);
                        DEBUG("- " << i << " " << v.type << " -> " << nt);
                        v.type = nt;
                    }
                } else {
                    auto index = v.alias;
                    unsigned int count = 0;
                    assert(index < ivars.ivars.size());
                    while (ivars.ivars.at(index).isAlias()) {
                        index = ivars.ivars.at(index).alias;

                        if (count >= ivars.ivars.size()) {
                            this->ivars.dump();
                            BUG(Span(), "Loop detected in ivar list when starting at " << v.alias << ", current is " << index);
                        }
                        count++;
                    }
                    v.alias = index;
                }
                i++;
            }
        }

        bool TraitResolution::hasAssociatedType(const HIRTypeData* input) const {
            if (!input->mayHaveAssociatedType()) {
                return false;
            }

            struct H {
                static bool checkPathparams(const TraitResolution& r, const HIRPathParams& pp) {
                    for (const auto& arg : pp.types) {
                        if (r.hasAssociatedType(arg)) {
                            return true;
                        }
                    }
                    return false;
                }

                static bool checkPath(const TraitResolution& r, const HIRPath& p) {
                    TU_MATCH(HIRPath::Data, (p.mData), (e2), (Generic, return H::checkPathparams(r, e2.mParams);), (UfcsInherent, if (r.hasAssociatedType(e2.type)) return true; if (H::checkPathparams(r, e2.params)) return true; return false;), (UfcsKnown, if (r.hasAssociatedType(e2.type)) return true; if (H::checkPathparams(r, e2.trait.mParams)) return true; if (H::checkPathparams(r, e2.params)) return true; return false;), (UfcsUnknown, BUG(Span(), "Encountered UfcsUnknown - " << p);))
                    throw "";
                }
            };

    TU_MATCH_HDRA( (*input), {)
    TU_ARMA(Infer, e) {
            const auto& ty = this->ivars.getType(input);
            if (ty != input) {
                return this->hasAssociatedType(ty);
            }
            return false;
        }
        TU_ARMA(Diverge, e) {
            return false;
        }
        TU_ARMA(Primitive, e) {
            return false;
        }
        TU_ARMA(Path, e) {
            // Both states still need projection normalisation. `Opaque` means
            // that no rule was available at the previous attempt, not that
            // the projection can never become known.
            if (e.path.mData.is_UfcsKnown() && (e.binding.is_Unbound() || e.binding.is_Opaque())) {
                return true;
            }
            return H::checkPath(*this, e.path);
        }
        TU_ARMA(Generic, e) {
            return false;
        }
        TU_ARMA(TraitObject, e) {
            // Recurse?
            if (H::checkPathparams(*this, e.mTrait.mPath.mParams)) {
                return true;
            }
            for (const auto& m : e.markers) {
                if (H::checkPathparams(*this, m.mParams)) {
                    return true;
                }
            }
            return false;
        }
        TU_ARMA(ErasedType, e) {
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Fcn, ee) {
                    if (H::checkPath(*this, ee.origin)) {
                        return true;
                    }
                }
                TU_ARMA(Known, ee) {
                    if (hasAssociatedType(ee)) {
                        return true;
                    }
                }
                TU_ARMA(Alias, ee) {
                }
        }
        for(const auto& m : e.traits) {
                if (H::checkPathparams(*this, m.mPath.mParams)) {
                    return true;
                }
        }
        return false;
        }
        TU_ARMA(Array, e) {
            return hasAssociatedType(e.inner);
        }
        TU_ARMA(Slice, e) {
            return hasAssociatedType(e.inner);
        }
        TU_ARMA(Tuple, e) {
            bool rv = false;
            for (const auto& sub : e) {
                rv |= hasAssociatedType(sub);
            }
            return rv;
        }
        TU_ARMA(Borrow, e) {
            return hasAssociatedType(e.inner);
        }
        TU_ARMA(Pointer, e) {
            return hasAssociatedType(e.inner);
        }
        TU_ARMA(NamedFunction, e) {
            return H::checkPath(*this, e.path);
        }
        TU_ARMA(Function, e) {
            // Recurse?
            return false;
        }
        TU_ARMA(NodeType, e) {
            // Recurse?
            return false;
        }
    }
    BUG(Span(), "Fell off the end of has_associated_type - input=" << input);
        }

        void TraitResolution::expandAssociatedTypesInplace(const Span& sp, HIRTypeRef& input, LList<const HIRTypeData*> stack) const {
            struct H {
                static void expandAssociatedTypesParams(const Span& sp, const TraitResolution& res, HIRPathParams& params, LList<const HIRTypeData*> stack) {
                    for (auto& arg : params.types) {
                        res.expandAssociatedTypesInplace(sp, arg, stack);
                    }
                }

                static void expandAssociatedTypesTp(const Span& sp, const TraitResolution& res, HIRTraitPath& input, LList<const HIRTypeData*> stack) {
                    expandAssociatedTypesParams(sp, res, input.mPath.mParams, stack);
                    for (auto& arg : input.typeBounds) {
                        expandAssociatedTypesParams(sp, res, arg.second.sourceTrait.mParams, stack);
                        res.expandAssociatedTypesInplace(sp, arg.second.type, stack);
                    }
                    for (auto& arg : input.traitBounds) {
                        expandAssociatedTypesParams(sp, res, arg.second.sourceTrait.mParams, stack);
                        for (auto& t : arg.second.traits) {
                            expandAssociatedTypesTp(sp, res, t, stack);
                        }
                    }
                }
            };

            for (const auto& ty : eatActiveStack) {
                if (input == ty) {
                    DEBUG("Recursive lookup, skipping - &input = " << &input);
                    return;
                }
            }
            auto data = input->cloneData();
    TU_MATCH_HDRA( (data), {)
    TU_ARMA(Infer, e) {
            const auto* ty = this->ivars.getType(input);
            if (ty != input) {
                input = ty;
                expandAssociatedTypesInplace(sp, input, stack);
                return;
            }
        }
        TU_ARMA(Diverge, e) {
        }
        TU_ARMA(Primitive, e) {
        }
        TU_ARMA(Path, e) {
        TU_MATCH_HDRA( (e.path.mData), {)
        TU_ARMA(Generic, pe) {
                    ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, mVisPath, mImplGenerics, mItemGenerics, e.binding.getGenerics(), pe.mParams);
                    H::expandAssociatedTypesParams(sp, *this, pe.mParams, stack);
                }
                TU_ARMA(UfcsInherent, pe) {
                    expandAssociatedTypesInplace(sp, pe.type, stack);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, stack);
                    H::expandAssociatedTypesParams(sp, *this, pe.implParams, stack);
                    input = crate.types.intern(mv$(data));
                    if (this->expandAssociatedTypesInplaceUfcsInherent(sp, input, stack)) {
                        this->expandAssociatedTypesInplace(sp, input, stack);
                    }
                    return;
                }
                TU_ARMA(UfcsKnown, pe) {
                    struct D {
                        const TraitResolution& tr;
                        D(const TraitResolution& tr, HIRTypeRef v)
                            : tr(tr)
                        {
                            tr.eatActiveStack.push_back(v);
                        }
                        ~D() {
                            tr.eatActiveStack.pop_back();
                        }
                        D(D&&) = delete;
                        D(const D&) = delete;
                    };
                    D _(*this, input);
                    // State stack to avoid infinite recursion
                    assert(eatActiveStack.size() > 0);
                    auto& prevStack = stack;
                    LList<const HIRTypeData*> stack(&prevStack, eatActiveStack.back());

                    expandAssociatedTypesInplace(sp, pe.type, stack);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, stack);
                    H::expandAssociatedTypesParams(sp, *this, pe.trait.mParams, stack);
                    input = crate.types.intern(mv$(data));
                    // Retry opaque projections too: equality bounds can be
                    // learned after an earlier normalisation attempt.
                    const bool wasUnbound = input->as_Path().binding.is_Unbound();
                    const bool wasOpaque = input->as_Path().binding.is_Opaque();
                    if (wasUnbound || wasOpaque) {
                        if (wasOpaque) {
                            this->expandAssociatedTypesInplaceUfcsKnown(sp, input, stack);
                            return;
                        }

                        // Cache the result of this to avoid needing to do the full resolution too often.
                        // - This avoids VERY slow typechecking in 1.90's librustc_target
                        auto k = FMT(input);
                        auto it = eatCache.find(k);
                        if (it != eatCache.end()) {
                            if (input != it->second) {
                                this->expandAssociatedTypesInplace(sp, it->second, stack);
                            }
                            DEBUG("CACHED: " << input << " -> " << it->second);
                            input = it->second;
                        } else {
                            this->expandAssociatedTypesInplaceUfcsKnown(sp, input, stack);
                            if (input->is_Path() && (input->as_Path().binding.is_Unbound() || input->as_Path().binding.is_Opaque())) {
                            } else {
                                DEBUG("CACHE+: " << k << " = " << input);
                                eatCache.insert(::std::make_pair(k, input));
                            }
                        }
                    }
                    return;
                }
                TU_ARMA(UfcsUnknown, pe) {
                    BUG(sp, "Encountered UfcsUnknown");
                }
        }
        }
        TU_ARMA(Generic, e) {
        }
        TU_ARMA(TraitObject, e) {
            // Recurse?
            H::expandAssociatedTypesTp(sp, *this, e.mTrait, stack);
            for (auto& m : e.markers) {
                H::expandAssociatedTypesParams(sp, *this, m.mParams, stack);
            }
        }
        TU_ARMA(ErasedType, e) {
            // Recurse?
        }
        TU_ARMA(Array, e) {
            ConvertHIRConstantEvaluateArraySize(sp, this->wb, crate, mVisPath, e.size);
            expandAssociatedTypesInplace(sp, e.inner, stack);
        }
        TU_ARMA(Slice, e) {
            expandAssociatedTypesInplace(sp, e.inner, stack);
        }
        TU_ARMA(Tuple, e) {
            for (auto& sub : e) {
                expandAssociatedTypesInplace(sp, sub, stack);
            }
        }
        TU_ARMA(Borrow, e) {
            expandAssociatedTypesInplace(sp, e.inner, stack);
        }
        TU_ARMA(Pointer, e) {
            expandAssociatedTypesInplace(sp, e.inner, stack);
        }
        TU_ARMA(NamedFunction, e) {
        TU_MATCH_HDRA( (e.path.mData), {)
        TU_ARMA(Generic, pe) {
                    H::expandAssociatedTypesParams(sp, *this, pe.mParams, stack);
                }
                TU_ARMA(UfcsInherent, pe) {
                    expandAssociatedTypesInplace(sp, pe.type, stack);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, stack);
                }
                TU_ARMA(UfcsKnown, pe) {
                    expandAssociatedTypesInplace(sp, pe.type, stack);
                    H::expandAssociatedTypesParams(sp, *this, pe.params, stack);
                    H::expandAssociatedTypesParams(sp, *this, pe.trait.mParams, stack);
                }
                TU_ARMA(UfcsUnknown, pe) {
                    BUG(sp, "Encountered UfcsUnknown");
                }
        }
        // TODO: Should this re-populate `def`? Not right now, assuming it's set once only
        }
        TU_ARMA(Function, e) {
            for (auto& ty : e.argTypes) {
                expandAssociatedTypesInplace(sp, ty, stack);
            }
            expandAssociatedTypesInplace(sp, e.mRettype, stack);
        }
        TU_ARMA(NodeType, e) {
            // Recurse? Nah.
        }
    }
            input = crate.types.intern(mv$(data));
        }

        bool TraitResolution::expandAssociatedTypesInplaceUfcsInherent(const Span& sp, HIRTypeRef& input, LList<const HIRTypeData*> stack) const {
            TRACE_FUNCTION_FR(input, input);
            ASSERT_BUG(sp, input->is_Path() && input->as_Path().path.mData.is_UfcsInherent(), input);

            const auto& pe = input->as_Path().path.mData.as_UfcsInherent();
            const HIRTypeAlias* alias = nullptr;
            const HIRGenericParams* implParamsDef = nullptr;
            const HIRTypeImpl* selectedImpl = nullptr;
            HIRPathParams implParams;
            HIRCompare bestMatch = HIRCompare::Unequal;
            static const HIRPathParams noTraitParams;

            crate.findTypeImpls(pe.type, ivars.callbackResolveInfer(), [&](const auto& impl) {
                const auto itemIt = impl.types.find(pe.item);
                if (itemIt == impl.types.end()) {
                    return false;
                }

                HIRPathParams candidateParams;
                const auto match = this->fticCheckParams(sp, HIRSimplePath(), nullptr, pe.type, impl.mParams, noTraitParams, impl.mType, candidateParams);
                if (match != HIRCompare::Unequal && (bestMatch == HIRCompare::Unequal || match == HIRCompare::Equal)) {
                    alias = &itemIt->second.data;
                    implParamsDef = &impl.mParams;
                    selectedImpl = &impl;
                    implParams = mv$(candidateParams);
                    bestMatch = match;
                }
                return bestMatch == HIRCompare::Equal;
            });

            if (!alias) {
                DEBUG("No inherent associated type candidate for " << input);
                return false;
            }

            ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, mVisPath, mImplGenerics, mItemGenerics, implParamsDef, implParams);
            if (inherentTypeConstraint) {
                auto selectedType = MonomorphStatePtr(crate.types, nullptr, &implParams, nullptr).monomorphType(sp, selectedImpl->mType);
                inherentTypeConstraint(sp, pe.type, selectedType);
            }

            auto itemParams = pe.params.clone();
            if (itemParams.types.size() != alias->mParams.types.size() || itemParams.values.size() != alias->mParams.values.size()) {
                ERROR(sp, E0000, "Incorrect generic arguments for inherent associated type " << input);
            }
            ConvertHIRConstantEvaluateMethodParams(sp, this->wb, crate, mVisPath, mImplGenerics, mItemGenerics, &alias->mParams, itemParams);

            input = MonomorphStatePtr(crate.types, pe.type, &implParams, &itemParams).monomorphType(sp, alias->mType);
            return true;
        }

        void TraitResolution::expandAssociatedTypesInplaceUfcsKnown(const Span& sp, HIRTypeRef& input, LList<const HIRTypeData*> stack) const {
            TRACE_FUNCTION_FR("input=" << input, input);
            auto data = input->cloneData();
            auto& builderE = data.as_Path();
            auto& builderPe = builderE.path.mData.as_UfcsKnown();

            expandAssociatedTypesInplace(sp, builderPe.type, stack);
            for (auto& ty : builderPe.trait.mParams.types) {
                expandAssociatedTypesInplace(sp, ty, stack);
            }
            input = crate.types.intern(mv$(data));
            const auto& e = input->as_Path();
            const auto& pe = e.path.mData.as_UfcsKnown();
            auto markOpaque = [&]() {
                auto opaqueData = input->cloneData();
                opaqueData.as_Path().binding = HIRTypePathBinding::make_Opaque({});
                input = crate.types.intern(mv$(opaqueData));
            };

            // Ignore unbounder infer literals
            if (pe.type->is_Infer() && !pe.type->as_Infer().isLit()) {
                return;
            }
            // ATYs of placeholders are kept as unknown
            if (pe.type->is_Generic() && pe.type->as_Generic().isPlaceholder()) {
                return;
            }

            // If there are impl params present, return early
            // TODO: There is still information available for placeholders (if the impl block is available)
            {
                auto cb = [](const HIRTypeData* ty) {
                    return !(ty->is_Generic() && ty->as_Generic().isPlaceholder());
                };
                bool hasImplPlaceholders = false;
                if (!visitTyWith(pe.type, cb)) {
                    hasImplPlaceholders = true;
                }
                for (const auto& ty : pe.trait.mParams.types) {
                    if (!visitTyWith(ty, cb)) {
                        hasImplPlaceholders = true;
                    }
                }
                if (hasImplPlaceholders) {
                    DEBUG("Has placeholder, skip");
                    // TODO: Why opaque? Like ivars, these could resolve in the future.
                    return;
                }
            }

            // Search for the actual trait containing this associated type
            HIRGenericPath traitPath;
            if (!this->traitContainsType(sp, pe.trait, this->crate.getTraitByPath(sp, pe.trait.mPath), pe.item.c_str(), traitPath)) {
                BUG(sp, "Cannot find associated type " << pe.item << " anywhere in trait " << pe.trait);
            }

            // Special type-specific rules
    TU_MATCH_HDRA( (*pe.type), {)
    default:
        // No special handling
    TU_ARMA(NodeType, te) {
        TU_MATCH_HDRA((te), {)
        // - If it's a closure, then the only trait impls are those generated by typeck
        TU_ARMA(Closure, nodeP) {
                    if (pe.trait.mPath == mLangFn || pe.trait.mPath == mLangFnMut || pe.trait.mPath == mLangFnOnce) {
                        if (pe.item == "Output") {
                            input = nodeP->returnType;
                            return;
                        } else {
                            ERROR(sp, E0000, "No associated type " << pe.item << " for trait " << pe.trait);
                        }
                    }
                    // TODO: Fall through? Maybe there's a generic impl that could match.
                }
                TU_ARMA(Generator, nodeP) {
                    if (pe.trait.mPath == this->mLangGenerator) {
                        if (pe.item == "Return") {
                            input = nodeP->returnType;
                            return;
                        } else if (pe.item == "Yield") {
                            input = nodeP->yieldTy;
                            return;
                        } else {
                            ERROR(sp, E0000, "No associated type " << pe.item << " for trait " << pe.trait);
                        }
                    }
                    // Fall through for generic impls
                }
                TU_ARMA(Async, nodeP) {
                    // TODO: `Future` impl
                }
        }
        }
        TU_ARMA(Function, te) {
            if (te.mAbi == ABI_RUST && !te.isUnsafe) {
                if (pe.trait.mPath == mLangFn || pe.trait.mPath == mLangFnMut || pe.trait.mPath == mLangFnOnce) {
                    if (pe.item == "Output") {
                        input = te.mRettype;
                        return;
                    } else {
                        ERROR(sp, E0000, "No associated type " << pe.item << " for trait " << pe.trait);
                    }
                }
            }
        }
        // If it's a TraitObject, then maybe we're asking for a bound
        TU_ARMA(TraitObject, te) {
            const auto& dataTrait = te.mTrait.mPath;
            if (pe.trait.mPath == dataTrait.mPath) {
                auto cmp = HIRCompare::Equal;
                if (pe.trait.mParams.types.size() != dataTrait.mParams.types.size()) {
                    cmp = HIRCompare::Unequal;
                } else {
                    for (unsigned int i = 0; i < pe.trait.mParams.types.size(); i++) {
                        const auto& l = pe.trait.mParams.types[i];
                        const auto& r = dataTrait.mParams.types[i];
                        cmp &= l->compareWithPlaceholders(sp, r, ivars.callbackResolveInfer());
                    }
                }
                if (cmp != HIRCompare::Unequal) {
                    auto it = te.mTrait.typeBounds.find(pe.item);
                    if (it == te.mTrait.typeBounds.end()) {
                        // TODO: Mark as opaque and return.
                        // - Why opaque? It's not bounded, don't even bother
                        TODO(sp, "Handle unconstrained associate type " << pe.item << " from " << pe.type);
                    }

                    auto hrlPps = HIRPathParams();
                    input = it->second.type;
                    return;
                }
            }

            // - Check if the desired trait is a supertrait of this.
            // NOTE: `params` (aka des_params) is not used (TODO)
            bool isSupertrait = this->findNamedTraitInTrait(sp, pe.trait.mPath, pe.trait.mParams, *te.mTrait.traitPtr, dataTrait.mPath, dataTrait.mParams, pe.type, [&](const HIRTraitPath& iTp) {
                // The above is just the monomorphised params and associated set. Comparison is still needed.
                auto cmp = this->comparePp(sp, iTp.mPath.mParams, pe.trait.mParams);
                if (cmp != HIRCompare::Unequal) {
                    // Search for bounded types in this TraitPath (from `find_named_trait_in_trait` and in the original input TraitPath `te.m_trait`)
                    auto it = iTp.typeBounds.find(pe.item);
                    if (it == iTp.typeBounds.end()) {
                        // NOTE: (currently) there can only be one trait with this name, so if we found this trait and the item is present - good.
                        it = te.mTrait.typeBounds.find(pe.item);
                    }
                    if (it != te.mTrait.typeBounds.end()) {
                        // Remove HRLs (TODO: Match them? not really needed in this stage I think)
                        auto hrlPps = HIRPathParams();
                        input = it->second.type;
                        return true;
                    }
                    return false;
                }
                return false;
            });
            if (isSupertrait) {
                return;
            }
        }
        // If it's a ErasedType, then maybe we're asking for a bound
        TU_ARMA(ErasedType, te) {
            DEBUG("- ErasedType");
            for (const auto& trait : te.traits) {
                const auto& traitGp = trait.mPath;
                if (traitPath.mPath == traitGp.mPath) {
                    auto cmp = HIRCompare::Equal;
                    if (traitPath.mParams.types.size() != traitGp.mParams.types.size()) {
                        cmp = HIRCompare::Unequal;
                    } else {
                        for (unsigned int i = 0; i < traitPath.mParams.types.size(); i++) {
                            const auto& l = traitPath.mParams.types[i];
                            const auto& r = traitGp.mParams.types[i];
                            cmp &= l->compareWithPlaceholders(sp, r, ivars.callbackResolveInfer());
                        }
                    }
                    if (cmp != HIRCompare::Unequal) {
                        auto hrls = HIRPathParams();
                        {
                            auto it = trait.typeBounds.find(pe.item);
                            if (it != trait.typeBounds.end()) {
                                input = it->second.type;
                                return;
                            }
                        }
                        // Mark as opaque and return, and ensure that the bounds are added to the bounds cache
                        markOpaque();
                        {
                            auto it = trait.traitBounds.find(pe.item);
                            if (it != trait.traitBounds.end()) {
                                for (const auto& bound : it->second.traits) {
                                    const_cast<TraitResolution&>(*this).prepIndexesAddTraitBound(sp, input, bound.clone());
                                }
                            }
                        }
                        return;
                    }
                }

                // - Check if the desired trait is a supertrait of this.
                // NOTE: `params` (aka des_params) is not used (TODO)
                bool isSupertrait = this->findNamedTraitInTrait(sp, traitPath.mPath, traitPath.mParams, *trait.traitPtr, traitGp.mPath, traitGp.mParams, pe.type, [&](const HIRTraitPath& iTp) {
                    // The above is just the monomorphised params and associated set. Comparison is still needed.
                    auto cmp = this->comparePp(sp, iTp.mPath.mParams, pe.trait.mParams);
                    if (cmp != HIRCompare::Unequal) {
                        auto it = iTp.typeBounds.find(pe.item);
                        if (it == iTp.typeBounds.end()) {
                            // NOTE: (currently) there can only be one trait with this name, so if we found this trait and the item is present - good.
                            it = trait.typeBounds.find(pe.item);
                        }
                        if (it != trait.typeBounds.end()) {
                            auto hrls = HIRPathParams();
                            DEBUG("hrls = " << hrls);
                            input = it->second.type;
                            return true;
                        }
                        return false;
                    }
                    return false;
                });
                if (isSupertrait) {
                    return;
                }
            }
        }
    }

    // 1. Bounds
    bool rv = false;
    bool foundBoundWithNoType = false;
    enum class ResultType {
        Opaque,
        LeaveUnbound,
        Recurse,
    } resultType = ResultType::Opaque;

    if(!rv)
    {
        auto it = typeEqualities.find(input);
        if (it == typeEqualities.end()) {
            it = ::std::find_if(typeEqualities.begin(), typeEqualities.end(), [&](const auto& entry) {
                return entry.first->equalsIgnoringRegions(input);
            });
        }
        if (it != typeEqualities.end()) {
            resultType = ResultType::Recurse;
            input = it->second.ty;
            rv = true;
        }
    }
    if(!rv)
    {
        rv = this->iterateBoundsTraits(sp, pe.type, traitPath.mPath, [&](HIRCompare cmp, const HIRTypeData* boundType, const HIRGenericPath& boundTrait, const CachedBound& boundInfo) -> bool {
            DEBUG("[expand_associated_types_inplace__UfcsKnown] Trait bound - " << boundType << " : " << boundTrait);
            // 2. Check if the trait (or any supertrait) includes pe.trait
            // TODO: If fuzzy, bail and leave unresolved?
            cmp &= boundTrait.compareWithPlaceholders(sp, traitPath, this->ivars.callbackResolveInfer());
            if (cmp == HIRCompare::Equal) {
                auto it = boundInfo.assoc.find(pe.item);
                // 1. Check if the bounds include the desired item
                if (it == boundInfo.assoc.end()) {
                    // If not, assume it's opaque and return as such
                    // TODO: What happens if there's two bounds that overlap? 'F: FnMut<()>, F: FnOnce<(), Output=Bar>'
                    DEBUG("[expand_associated_types_inplace__UfcsKnown] Found impl for " << input << " but no bound on item");

                    // Flag so if no impl was found by the lower checks, it gets correctly set to Opaque (or left unbound)
                    foundBoundWithNoType = true;
                    if (cmp == HIRCompare::Fuzzy) {
                        resultType = ResultType::LeaveUnbound;
                    } else {
                        resultType = ResultType::Opaque;
                    }
                    return false;
                } else {
                    resultType = ResultType::Recurse;
                    DEBUG("TraitBound");
                    input = it->second.type;
                }
                return true;
            }

            // - Didn't match
            return false;
        });
    }

    if( rv ) {
        assert(resultType == ResultType::Recurse); // Nothing else can happen without `rv` being false
        DEBUG("- Found replacement: " << input);
        this->expandAssociatedTypesInplace(sp, input, stack);
        return;
    }

    // If the type of this UfcsKnown is ALSO a UfcsKnown - Check if it's bounded by this trait with equality
    //  e.g. `<<Foo as Bar>::Baz as Trait2>::Type` may have an ATY bound `trait Bar { type Baz: Trait2<Type=...> }`
    // Use bounds on other associated types too (if `pe.type` was resolved to a fixed associated type)
    if(const auto* teInner = pe.type->opt_Path())
    {
        if (const auto* peInnerP = teInner->path.mData.opt_UfcsKnown()) {
            DEBUG("Checking inner bounds");
            const auto& peInner = *peInnerP;
            // TODO: Search for equality bounds on this associated type (pe_inner) that match the entire type (pe)
            // - Does simplification of complex associated types
            HIRGenericPath traitPath;
            if (!this->traitContainsType(sp, peInner.trait, this->crate.getTraitByPath(sp, peInner.trait.mPath), peInner.item.c_str(), traitPath)) {
                BUG(sp, "Cannot find associated type " << peInner.item << " anywhere in trait " << peInner.trait);
            }
            const auto& traitPtr = this->crate.getTraitByPath(sp, traitPath.mPath);
            const auto& assocTy = traitPtr.types.at(peInner.item);

            // Resolve where Self=pe_inner.type (i.e. for the trait this inner UFCS is on)
            auto cbPlaceholdersTrait = MonomorphStatePtr(crate.types, peInner.type, &peInner.trait.mParams, &peInner.params);
            for (const auto& bound : assocTy.traitBounds) {
                auto it = bound.typeBounds.find(pe.item);
                if (it != bound.typeBounds.end()) {
                    auto sourceTrait = cbPlaceholdersTrait.monomorphGenericpath(sp, it->second.sourceTrait, false);
                    auto atyParams = cbPlaceholdersTrait.monomorphPathParams(sp, it->second.atyParams, false);
                    for (auto& t : sourceTrait.mParams.types) {
                        expandAssociatedTypesInplace(sp, t, stack);
                    }
                    for (auto& t : atyParams.types) {
                        expandAssociatedTypesInplace(sp, t, stack);
                    }
                    auto cmp = sourceTrait.compareWithPlaceholders(sp, pe.trait, ivars.callbackResolveInfer());
                    cmp &= atyParams.compareWithPlaceholders(sp, pe.params, ivars.callbackResolveInfer());
                    if (cmp == HIRCompare::Equal) {
                        input = monomorphiseTypeNeeded(it->second.type) ? cbPlaceholdersTrait.monomorphType(sp, it->second.type) : it->second.type;
                        DEBUG("- Found replacement from " << sourceTrait << ": " << input);
                        this->expandAssociatedTypesInplace(sp, input, stack);
                        return;
                    }
                }

                auto boundTp = cbPlaceholdersTrait.monomorphGenericpath(sp, bound.mPath, false);
                for (auto& t : boundTp.mParams.types) {
                    expandAssociatedTypesInplace(sp, t, stack);
                }
                DEBUG("B " << bound.mPath);
                DEBUG("-> " << boundTp);

                // TODO: Find trait in this trait.
                const auto& boundTrait = crate.getTraitByPath(sp, boundTp.mPath);
                bool replaced = this->findNamedTraitInTrait(sp, pe.trait.mPath, pe.trait.mParams, boundTrait, boundTp.mPath, boundTp.mParams, pe.type, [&](const HIRTraitPath& tp) {
                    auto it = tp.typeBounds.find(pe.item);
                    if (it != tp.typeBounds.end()) {
                        input = it->second.type;
                        return true;
                    }
                    return false;
                });
                if (replaced) {
                    return;
                }
            }
            DEBUG("pe = " << pe.type << ", input = " << input);
        }
    }

    if (this->wb.settings->solver.globally) {
        bool normalized = false;
        bool ambiguous = false;
        this->findTraitImplsNext(sp, traitPath.mPath, traitPath.mParams, pe.type, [&](ImplRef impl, HIRCompare certainty) {
            if (impl.isAmbiguousIdentity()) {
                ambiguous = true;
                return true;
            }

            auto output = impl.getType(crate.types, pe.item.c_str(), pe.params);
            if (output == HIRTypeRef() || output == input) {
                ambiguous = true;
                return true;
            }
            input = ::std::move(output);
            normalized = true;
            ambiguous = certainty == HIRCompare::Fuzzy;
            return true;
        }, pe.item.c_str(), nullptr, &pe.params);
        if (normalized) {
            this->expandAssociatedTypesInplace(sp, input, stack);
            return;
        }
        if (ambiguous) {
            // A rigid unresolved projection is still a usable alias: method
            // lookup and associated-type bounds must be allowed to inspect it.
            // Only projections containing inference variables stay unbound,
            // because those are obligations the constraint loop must retry.
            if (!this->ivars.typeContainsIvars(input, false)) {
                markOpaque();
            }
            return;
        }
    }

    if( this->findTraitImplsMagic(sp, traitPath.mPath, traitPath.mParams, pe.type, [&](auto impl, auto qual)->bool {
        DEBUG("[expand_associated_types__UfcsKnown] Found " << impl << " qual=" << qual);
        // If it's a fuzzy match, keep going (but count if a concrete hasn't been found)
        if (qual == HIRCompare::Fuzzy) {
        } else {
            auto ty = impl.getType(crate.types, pe.item.c_str(), pe.params);
            if (ty == HIRTypeRef()) {
                DEBUG("Assuming that " << input << " is an opaque name");
                markOpaque();
            } else {
                input = mv$(ty);
            }
        }
        return true;
        }) )
    {
        return;
    }

    if( this->findTraitImplsTypes(sp, traitPath.mPath, traitPath.mParams, pe.type, [&](auto impl, auto qual)->bool {
        DEBUG("[expand_associated_types__UfcsKnown] Found " << impl << " qual=" << qual);
        // If it's a fuzzy match, keep going (but count if a concrete hasn't been found)
        if (qual == HIRCompare::Fuzzy) {
        } else {
            auto ty = impl.getType(crate.types, pe.item.c_str(), pe.params);
            if (ty == HIRTypeRef()) {
                DEBUG("Assuming that " << input << " is an opaque name");
                markOpaque();
            } else {
                input = mv$(ty);
            }
        }
        return true;
        }) )
    {
        return;
    }

    // 2. Crate-level impls
    DEBUG("Searching for impl");
    bool    canFuzz = true;
    unsigned int    count = 0;
    bool isSpecialisable = false;
    bool isBound = false;
    ImplRef bestImpl;
    auto cbFindImpl = [&](ImplRef impl, HIRCompare qual)->bool {
        DEBUG("[expand_associated_types__UfcsKnown] Found " << impl << " qual=" << qual);
        // If it's a fuzzy match, keep going (but count if a concrete hasn't been found)
        if (qual == HIRCompare::Fuzzy) {
            if (canFuzz) {
                count += 1;
                if (count == 1 && impl.getImplType(crate.types)->tag() == pe.type->tag()) {
                    bestImpl = mv$(impl);
                }
            }
            return false;
        } else {
            // If a fuzzy match could have been seen, ensure that best_impl is unsed
            if (canFuzz) {
                bestImpl = ImplRef();
                canFuzz = false;
            }

            // If the type is specialisable
            if (impl.typeIsSpecialisable(pe.item.c_str())) {
                // Check if this is more specific
                if (impl.moreSpecificThan(crate.types, bestImpl)) {
                    isSpecialisable = true;
                    bestImpl = mv$(impl);
                }
                return false;
            } else {
                auto ty = impl.getType(crate.types, pe.item.c_str(), pe.params);
                if (ty == HIRTypeRef()) {
                    if (isBound) {
                        return false;
                    } else {
                        if (pe.item.compare(0, strlen(ATY_PREFIX_ERASED), ATY_PREFIX_ERASED) == 0) {
                            DEBUG("Erased (ITIT), setting opaque");
                            markOpaque();
                            return true;
                        } else {
                            ERROR(sp, E0000, "Couldn't find assocated type " << pe.item << " in impl of " << pe.trait << " for " << pe.type);
                        }
                    }
                }

                if (impl.hasMagicParams()) {
                }

                // TODO: What if there's multiple impls?
                DEBUG("Converted UfcsKnown - " << e.path << " = " << ty);
                input = mv$(ty);
                return true;
            }
        }
        };

    rv = this->findTraitImplsCrate(sp, traitPath.mPath, traitPath.mParams, pe.type, cbFindImpl);
    if( !rv ) {
        isBound = true;
        rv = findTraitImplsBound(sp, traitPath.mPath, traitPath.mParams, pe.type, cbFindImpl);
    }
    if( !rv && bestImpl.isValid() ) {
        if (canFuzz && count > 1) {
            // Fuzzy match with multiple choices - can't know yet
        } else if (isSpecialisable) {
            if (!this->ivars.typeContainsIvars(input, false)) {
                DEBUG("Assuming opaque - specialisable impl");
                markOpaque();
            } else {
                DEBUG("Derferring - specialisable impl (ivars present)");
            }
            return;
        } else {
            auto ty = bestImpl.getType(crate.types, pe.item.c_str(), pe.params);
            if (ty == HIRTypeRef()) {
                if (!this->ivars.typeContainsIvars(input, false)) {
                    DEBUG("Assuming opaque - best impl didn't have ATY");
                    markOpaque();
                } else {
                    DEBUG("Derferring - best impl didn't have ATY (ivars present)");
                }
                return;
            }

            // Try again later?
            if (bestImpl.hasMagicParams()) {
                DEBUG("- Placeholder parameters present in impl, can't expand");
                return;
            }

            DEBUG("Converted UfcsKnown - " << e.path << " = " << ty);
            input = mv$(ty);
            rv = true;
        }
    }
    if( rv ) {
        expandAssociatedTypesInplace(sp, input, stack);
        return;
    }

    if( foundBoundWithNoType )
    {
        switch (resultType) {
            case ResultType::Opaque: {
                DEBUG("Assuming that " << input << " is an opaque name");
                markOpaque();
                ASSERT_BUG(
                    sp,
                    visitTyWith(
                        input,
                        [](const HIRTypeData* ty) {
                    return ty->is_ErasedType() || ty->is_Infer();
                }
                    ) || monomorphiseTypeNeeded(input),
                    "Set opaque on a non-generic type: " << input
                );

                DEBUG("- " << typeEqualities.size() << " replacements");
                for (const auto& v : typeEqualities) {
                    DEBUG(" > " << v.first << " = " << v.second);
                }

                auto a = typeEqualities.find(input);
                if (a == typeEqualities.end()) {
                    a = ::std::find_if(typeEqualities.begin(), typeEqualities.end(), [&](const auto& entry) {
                        return entry.first->equalsIgnoringRegions(input);
                    });
                }
                if (a != typeEqualities.end()) {
                    DEBUG("- Replace to " << a->second << " from " << input);
                    input = a->second.ty;
                }
                this->expandAssociatedTypesInplace(sp, input, stack);
            } break;
            case ResultType::Recurse:
                assert(false);
                break;
            case ResultType::LeaveUnbound:
                DEBUG("- Keep as unbound: " << input);
                break;
        }
        return;
    }

    // If there are no ivars in this path, set its binding to Opaque
    if( !this->ivars.typeContainsIvars(input, false) ) {
        // TODO: If the type is a generic or an opaque associated, we can't know.
        // - If the trait contains any of the above, it's unknowable
        // - Otherwise, it's an error
        DEBUG("Assuming that " << input << " is an opaque name");
        markOpaque();
        DEBUG("Couldn't resolve associated type for " << input << " (and won't ever be able to)");
    }
    else {
        DEBUG("Couldn't resolve associated type for " << input << " (will try again later)");
    }
        }

        // -------------------------------------------------------------------------------------------------------------------
        // -------------------------------------------------------------------------------------------------------------------
        bool TraitResolution::findNamedTraitInTrait(const Span& sp, const HIRSimplePath& des, const HIRPathParams& desParams, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* targetType, tCbFindTrait callback) const {
            TRACE_FUNCTION_F(des << desParams << " in " << traitPath << pp);
            if (pp.types.size() != traitPtr.mParams.types.size()) {
                BUG(sp, "Incorrect number of parameters for trait " << traitPath);
            }

            DEBUG(traitPtr.allParentTraits);
            auto monomorphCb = MonomorphStatePtr(crate.types, targetType, &pp, nullptr);
            for (const auto& pt : traitPtr.allParentTraits) {
                auto ptMono = monomorphCb.monomorphTraitpath(sp, pt, false);
                for (auto& ty : ptMono.mPath.mParams.types) {
                    ty = this->expandAssociatedTypes(sp, mv$(ty));
                }
                for (auto& ty : ptMono.typeBounds) {
                    ty.second.type = this->expandAssociatedTypes(sp, mv$(ty.second.type));
                }

                if (pt.mPath.mPath == des) {
                    DEBUG("Found potential " << ptMono);
                    // NOTE: Doesn't quite work...
                    //if( cmp != ::HIR::Compare::Unequal )
                    //{
                    if (callback(ptMono)) {
                        return true;
                    }
                    //}
                }
            }

            return false;
        }

        bool TraitResolution::findTraitImplsBound(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* type, tCbTraitImplR callback) const {
            TRACE_FUNCTION_F("trait = " << trait << params << ", type = " << type);
            const HIRPath::Data::Data_UfcsKnown* assocInfo = nullptr;
            if (const auto* e = type->opt_Path()) {
                assocInfo = e->path.mData.opt_UfcsKnown();
            }

            // If the type is a fully unknown type, then don't bother looking?
            // - Ah, but what if the prams provide sufficient information?
            // - TODO: Determine if the params could provide enough info to be worth checking for bounds.
            if (type->is_Infer() && !type->as_Infer().isLit()) {
                return false;
            }

            // NOTE: Even if the type is completely unknown (unbound UFCS), search the bound list.

            // TODO: A bound can imply something via its associated types. How deep can this go?
            // E.g. `T: IntoIterator<Item=&u8>` implies `<T as IntoIterator>::IntoIter : Iterator<Item=&u8>`
            // > Would maybe want a list of all explicit and implied bounds instead.
            {
                bool rv = this->iterateBoundsTraits(sp, type, trait, [&](HIRCompare cmp, const HIRTypeData* boundTy, const HIRGenericPath& boundTrait, const CachedBound& boundInfo) -> bool {
                    const auto& storedParams = boundTrait.mParams;
                    HIRPathParams normalisedParams;
                    const HIRPathParams* bParams = &storedParams;
                    if (::std::any_of(storedParams.types.begin(), storedParams.types.end(), [&](const auto& ty) {
                        return this->hasAssociatedType(ty);
                    })) {
                        normalisedParams = storedParams.clone();
                        this->expandAssociatedTypesParams(sp, normalisedParams);
                        bParams = &normalisedParams;
                    }

                    DEBUG("[find_trait_impls_bound] " << boundTrait << " for " << boundTy << " cmp = " << cmp);

                    // Check against `params`
                    DEBUG("[find_trait_impls_bound] Checking params " << params << " vs " << *bParams);
                    auto ord = cmp;
                    ord &= this->comparePp(sp, *bParams, params);
                    if (ord == HIRCompare::Unequal) {
                        DEBUG("[find_trait_impls_bound] - Mismatch");
                        return false;
                    }
                    if (ord == HIRCompare::Fuzzy) {
                        DEBUG("[find_trait_impls_bound] - Fuzzy match");
                    }
                    DEBUG("[find_trait_impls_bound] Match " << boundTy << " : " << boundTrait);
                    // Hand off to the closure, and return true if it does
                    // TODO: The type bounds are only the types that are specified.
                                if (callback(ImplRef(boundTy, &boundTrait.mParams, &boundInfo.assoc, boundInfo.constness), ord)) {
                        return true;
                    }

                    return false;
                });
                if (rv) {
                    return rv;
                }
            }

            if (assocInfo) {
                bool rv = this->iterateBoundsTraits(sp, assocInfo->type, assocInfo->trait.mPath, [&](HIRCompare cmp, const HIRTypeData* boundTy, const HIRGenericPath& boundTrait, const CachedBound& boundInfo) -> bool {
                    // Check the trait params
                    cmp &= this->comparePp(sp, boundTrait.mParams, assocInfo->trait.mParams);
                    if (cmp == HIRCompare::Fuzzy) {
                        //TODO(sp, "Handle fuzzy matches searching for associated type bounds");
                    } else if (cmp == HIRCompare::Unequal) {
                        return false;
                    }
                    auto outerOrd = cmp;

                    const auto& traitRef = *boundInfo.traitPtr;
                    const auto& at = traitRef.types.at(assocInfo->item);
                    for (const auto& bound : at.traitBounds) {
                        if (bound.mPath.mPath == trait) {
                            auto monomorphCb = MonomorphStatePtr(crate.types, assocInfo->type, &assocInfo->trait.mParams, nullptr);

                            DEBUG("- Found an associated type bound for this trait via another bound");
                            HIRCompare ord = outerOrd;
                            if (monomorphisePathparamsNeeded(bound.mPath.mParams)) {
                                // TODO: Use a compare+callback method instead
                                auto bParamsMono = monomorphCb.monomorphPathParams(sp, bound.mPath.mParams, false);
                                this->expandAssociatedTypesParams(sp, bParamsMono);
                                ord &= this->comparePp(sp, bParamsMono, params);
                            } else {
                                ord &= this->comparePp(sp, bound.mPath.mParams, params);
                            }
                            if (ord == HIRCompare::Unequal) {
                                return false;
                            }
                            if (ord == HIRCompare::Fuzzy) {
                                DEBUG("Fuzzy match");
                            }

                            auto tpMono = monomorphCb.monomorphTraitpath(sp, bound, false);
                            // - Expand associated types
                            this->expandAssociatedTypesParams(sp, tpMono.mPath.mParams);
                            for (auto& ty : tpMono.typeBounds) {
                                ty.second.type = this->expandAssociatedTypes(sp, mv$(ty.second.type));
                            }
                            DEBUG("- tp_mono = " << tpMono);
                            // TODO: Instead of using `type` here, build the real type
                            if (callback(ImplRef(type, mv$(tpMono.mPath.mParams), mv$(tpMono.typeBounds), tpMono.constness), ord)) {
                                return true;
                            }
                        }
                    }
                    return false;
                });
                if (rv) {
                    return true;
                }
            }
            return false;
        }

        bool TraitResolution::findTraitImplsCrate(const Span& sp, const HIRSimplePath& trait, const HIRPathParams* paramsPtr, const HIRTypeData* type, tCbTraitImplR callback) const {
            // TODO: Have a global cache of impls that don't reference either generics or ivars

            static HIRTraitPath::assocListT nullAssoc;
            TRACE_FUNCTION_F(trait << FMT_CB(ss, if (paramsPtr) { ss << *paramsPtr; } else { ss << "<?>"; }) << " for " << type);

            CanonicalizeTraitGoal canonicalizer(crate.types);
            const auto canonicalType = canonicalizer.monomorphType(sp, type, true);
            HIRPathParams canonicalParams;
            const bool hasParams = paramsPtr != nullptr;
            if (hasParams) {
                canonicalParams = canonicalizer.monomorphPathParams(sp, *paramsPtr, true);
            }

            for (const auto& activeGoal : legacyTraitGoalStack) {
                if (!activeGoal.matches(trait, canonicalParams, hasParams, canonicalType)) {
                    continue;
                }

                // rustc treats an inductive recursive trait predicate as
                // ambiguous, while productive recursive traits are proven.
                const auto cmp = crate.getTraitByPath(sp, trait).isCoinductive ? HIRCompare::Equal : HIRCompare::Fuzzy;
                DEBUG("Legacy trait goal recurred: " << trait << FMT_CB(ss, if (paramsPtr) { ss << *paramsPtr; } else { ss << "<?>"; }) << " for " << type << ", result=" << cmp);
                return callback(ImplRef(type, paramsPtr, &nullAssoc), cmp);
            }

            // rustc's legacy solver has a second cycle check for fresh input
            // types.  Exact goal equality is not sufficient here: a blanket
            // candidate can replace one unknown with a newly-created unknown
            // on every step (for example, tuple Distribution impls).  If the
            // current fresh goal is compatible with an older goal for the
            // same trait, further candidate search is ambiguous.
            const auto typeIsFresh = [&](const HIRTypeData* ty) {
                if (ivars.typeContainsIvars(ty, false)) {
                    return true;
                }
                return visitTyWith(ty, [](const HIRTypeData* inner) {
                    return inner->is_Generic() && inner->as_Generic().isPlaceholder();
                });
            };
            bool hasFreshInputs = !hasParams || typeIsFresh(type);
            if (hasParams && !hasFreshInputs) {
                hasFreshInputs = ivars.pathparamsContainIvars(*paramsPtr, false);
                for (const auto& param : paramsPtr->types) {
                    hasFreshInputs = hasFreshInputs || typeIsFresh(param);
                }
                for (const auto& param : paramsPtr->values) {
                    hasFreshInputs = hasFreshInputs || param.is_Infer() || (param.is_Generic() && param.as_Generic().isPlaceholder());
                }
            }

            if (hasFreshInputs) {
                const auto resolve = ivars.callbackResolveInfer();
                for (const auto& activeGoal : legacyTraitGoalStack) {
                    if (activeGoal.trait != trait) {
                        continue;
                    }
                    if (canonicalType->compareWithPlaceholders(sp, activeGoal.type, resolve) == HIRCompare::Unequal) {
                        continue;
                    }
                    if (hasParams && activeGoal.hasParams && canonicalParams.compareWithPlaceholders(sp, activeGoal.params, resolve) == HIRCompare::Unequal) {
                        continue;
                    }

                    DEBUG("Fresh legacy trait goal matched an active goal: " << trait << FMT_CB(ss, if (paramsPtr) { ss << *paramsPtr; } else { ss << "<?>"; }) << " for " << type << ", result=Fuzzy");
                    return callback(ImplRef(type, paramsPtr, &nullAssoc), HIRCompare::Fuzzy);
                }
            }

            legacyTraitGoalStack.emplace_back(trait, canonicalParams, hasParams, canonicalType);

            struct StackGuard {
                ::std::vector<LegacyTraitGoal>& stack;

                ~StackGuard() {
                    stack.pop_back();
                }
            } guard{legacyTraitGoalStack};

            // Handle auto traits (aka OIBITs)
            if (crate.getTraitByPath(sp, trait).mIsMarker) {
                // NOTE: Expected behavior is for Ivars to return false
                // TODO: Should they return Compare::Fuzzy instead?
                if (type->is_Infer()) {
                    return callback(ImplRef(type, paramsPtr, &nullAssoc), HIRCompare::Fuzzy);
                }

                const HIRTraitMarkings* markings = nullptr;
                if (const auto* e = type->opt_Path()) {
                    if (TU_TEST1(e->path.mData, Generic, .mParams.types.size() == 0)) {
                        markings = e->binding.getTraitMarkings();
                    }
                }

                // NOTE: `markings` is only set if there's no type params to a path type
                // - Cache populated after destructure
                if (markings) {
                    auto it = markings->autoImpls.find(trait);
                    if (it != markings->autoImpls.end()) {
                        if (!it->second.conditions.empty()) {
                            TODO(sp, "Conditional auto trait impl");
                        } else if (it->second.isImpled) {
                            return callback(ImplRef(type, paramsPtr, &nullAssoc), HIRCompare::Equal);
                        } else {
                            return false;
                        }
                    }
                }

                // - Search for positive impls for this type
                DEBUG("- Search positive impls");
                bool positiveFound = false;
                this->crate.findAutoTraitImpls(trait, type, this->ivars.callbackResolveInfer(), [&](const auto& impl) -> bool {
                    // Skip any negative impls on this pass
                    if (impl.isPositive != true) {
                        return false;
                    }

                    DEBUG("[find_trait_impls_crate] - Auto Pos Found impl" << impl.mParams.fmtArgs() << " " << trait << impl.traitArgs << " for " << impl.mType << " " << impl.mParams.fmtBounds());

                    // Compare with `params`
                    HIRPathParams implParams;
                    auto match = this->fticCheckParams(sp, trait, paramsPtr, type, impl.mParams, impl.traitArgs, impl.mType, implParams);
                    if (match == HIRCompare::Unequal) {
                        // If any bound failed, return false (continue searching)
                        return false;
                    }

                    auto monomorph = MonomorphStatePtr(crate.types, nullptr, &implParams, nullptr);
                    // TODO: Ensure that there are no-longer any magic params?

                    auto tyMono = monomorph.monomorphType(sp, impl.mType, false);
                    auto argsMono = monomorph.monomorphPathParams(sp, impl.traitArgs, false);
                    // NOTE: Auto traits can't have items, so no associated types

                    positiveFound = true;
                    DEBUG("[find_trait_impls_crate] Auto Positive callback(args=" << argsMono << ")");
                    return callback(ImplRef(mv$(tyMono), mv$(argsMono), {}), match);
                });
                if (positiveFound) {
                    // A positive impl was found, so return true (callback should have been called)
                    return true;
                }

                // - Search for negative impls for this type
                DEBUG("- Search negative impls");
                bool negativeFound = this->crate.findAutoTraitImpls(trait, type, this->ivars.callbackResolveInfer(), [&](const auto& impl) {
                    // Skip any positive impls
                    if (impl.isPositive != false) {
                        return false;
                    }
                    DEBUG("[find_trait_impls_crate] - Found auto neg impl" << impl.mParams.fmtArgs() << " " << trait << impl.traitArgs << " for " << impl.mType << " " << impl.mParams.fmtBounds());

                    // Compare with `params`
                    HIRPathParams implParams;
                    auto match = this->fticCheckParams(sp, trait, paramsPtr, type, impl.mParams, impl.traitArgs, impl.mType, implParams);
                    if (match == HIRCompare::Unequal) {
                        // If any bound failed, return false (continue searching)
                        return false;
                    }

                    DEBUG("[find_trait_impls_crate] - Found neg impl");
                    return true;
                });
                if (negativeFound) {
                    // A negative impl _was_ found, so return false
                    return false;
                }

                auto cmp = this->checkAutoTraitImplDestructure(sp, trait, paramsPtr, type);
                if (cmp != HIRCompare::Unequal) {
                    if (markings) {
                        ASSERT_BUG(sp, cmp == HIRCompare::Equal, "Auto trait with no params returned a fuzzy match from destructure - " << trait << " for " << type);
                        markings->autoImpls.insert(::std::make_pair(trait, HIRTraitMarkings::AutoMarking{{}, true}));
                    }
                    return callback(ImplRef(type, paramsPtr, &nullAssoc), cmp);
                } else {
                    if (markings) {
                        markings->autoImpls.insert(::std::make_pair(trait, HIRTraitMarkings::AutoMarking{{}, false}));
                    }
                    return false;
                }
            }

            // TODO: Don't search if ALL types are unbounded ivar (what about a tuple of unbounded?)
            // If the type is an unbounded ivar, don't search.
            if (type->is_Infer() && !type->as_Infer().isLit()) {
                return false;
            }

            return this->crate.findTraitImpls(trait, type, this->ivars.callbackResolveInfer(), [&](const HIRTraitImpl& impl) {
                DEBUG("[find_trait_impls_crate] Found impl" << impl.mParams.fmtArgs() << " " << trait << impl.traitArgs << " for " << impl.mType << " " << impl.mParams.fmtBounds());
                // Compare with `params`
                HIRPathParams implParams;
                auto match = this->fticCheckParams(sp, trait, paramsPtr, type, impl.mParams, impl.traitArgs, impl.mType, implParams);
                if (match == HIRCompare::Unequal) {
                    // If any bound failed, return false (continue searching)
                    DEBUG("[find_trait_impls_crate] - Params mismatch");
                    return false;
                }
                DEBUG("[find_trait_impls_crate] - Found with impl_params=" << implParams);

                return callback(ImplRef(mv$(implParams), crate.getTraitByPath(sp, trait), trait, impl), match);
            });
        }

        HIRCompare TraitResolution::checkAutoTraitImplDestructure(const Span& sp, const HIRSimplePath& trait, const HIRPathParams* paramsPtr, const HIRTypeData* type) const {
            TRACE_FUNCTION_F("trait = " << trait << ", type = " << type);
            // HELPER: Search for an impl of this trait for an inner type, and return the match type
            auto typeImplsTrait = [&](const auto& innerTy) -> HIRCompare {
                auto lRes = HIRCompare::Unequal;
                this->findTraitImpls(sp, trait, *paramsPtr, innerTy, [&](auto, auto cmp) {
                    lRes = cmp;
                    return (cmp == HIRCompare::Equal);
                });
                DEBUG("[check_auto_trait_impl_destructure] " << innerTy << " - " << lRes);
                return lRes;
            };

            // - If the type is a path (struct/enum/...), search for impls for all contained types.
    TU_MATCH_HDRA( (*type), { )
    default:
        // Otherwise, there's no negative so it must be positive
        return HIRCompare::Equal;
        TU_ARMA(Path, e) {
            HIRCompare res = HIRCompare::Equal;
        TU_MATCH_HDRA( (e.path.mData), {)
        TU_ARMA(Generic, pe) { //(
                    HIRTypeRef tmp;
                    auto monomorph = MonomorphStatePtr(crate.types, nullptr, &pe.mParams, nullptr);
                    // HELPER: Get a possibily monomorphised version of the input type (stored in `tmp` if needed)
                    auto monomorphGet = [&](const auto& ty) -> const HIRTypeData* {
                        if (monomorphiseTypeNeeded(ty)) {
                            return (tmp = this->expandAssociatedTypes(sp, monomorph.monomorphType(sp, ty)));
                        } else {
                            return ty;
                        }
                    };

            TU_MATCH_HDRA( (e.binding), {)
            TU_ARMA(Opaque, tpb) {
                            BUG(sp, "Opaque binding on generic path - " << type);
                        }
                        TU_ARMA(Unbound, tpb) {
                            BUG(sp, "Unbound binding on generic path - " << type);
                        }
                        TU_ARMA(Struct, tpb) {
                            const auto& str = *tpb;

                            // TODO: Somehow store a ruleset for auto traits on the type
                            // - Map of trait->does_impl for local fields?
                            // - Problems occur with type parameters
                            TU_MATCH(
                                HIRStruct::Data,
                                (str.mData),
                                (se),
                                (Unit, ),
                                (Tuple,
                                 for (const auto& fld : se) {
                                     const auto& fldTyMono = monomorphGet(fld.ent);
                                     DEBUG("Struct::Tuple " << fldTyMono);
                                     res &= typeImplsTrait(fldTyMono);
                                     if (res == HIRCompare::Unequal) {
                                         return HIRCompare::Unequal;
                                     }
                                 }),
                                (Named, for (const auto& fld : se) {
                                    DEBUG(type << " FIELD '" << fld.name << "' " << fld.ty);
                                    const auto& fldTyMono = monomorphGet(fld.ty);
                                    DEBUG("Struct::Named '" << fld.name << "' " << fldTyMono);

                                    res &= typeImplsTrait(fldTyMono);
                                    if (res == HIRCompare::Unequal) {
                                        return HIRCompare::Unequal;
                                    }
                                })
                            )
                        }
                        TU_ARMA(Enum, tpb) {
                            if (const auto* e = tpb->mData.opt_Data()) {
                                for (const auto& var : *e) {
                                    const auto& fldTyMono = monomorphGet(var.type);
                                    DEBUG("Enum '" << var.name << "'" << fldTyMono);
                                    res &= typeImplsTrait(fldTyMono);
                                    if (res == HIRCompare::Unequal) {
                                        return HIRCompare::Unequal;
                                    }
                                }
                            }
                        }
                        TU_ARMA(Union, tpb) {
                            for (const auto& fld : tpb->mVariants) {
                                const auto& fldTyMono = monomorphGet(fld.ty);
                                DEBUG("Union '" << fld.name << "' " << fldTyMono);
                                res &= typeImplsTrait(fldTyMono);
                                if (res == HIRCompare::Unequal) {
                                    return HIRCompare::Unequal;
                                }
                            }
                        }
                        TU_ARMA(ExternType, tpb) {
                            TODO(sp, "Check auto trait destructure on extern type " << type);
                        }
            }
            DEBUG("- Nothing failed, calling callback");
                }
                TU_ARMA(UfcsUnknown, pe) {
                    BUG(sp, "UfcsUnknown in typeck - " << type);
                }
                TU_ARMA(UfcsKnown, pe) {
                    // If unbound, use Fuzzy {
                    if (e.binding.is_Unbound()) {
                        DEBUG("- Unbound UfcsKnown, returning Fuzzy");
                        return HIRCompare::Fuzzy;
                    }
                    // Otherwise, it's opaque. Check the bounds on the trait.
                    if (TU_TEST1(*pe.type, Generic, .binding >> 8 == 2)) {
                        DEBUG("- UfcsKnown of placeholder, returning Fuzzy");
                        return HIRCompare::Fuzzy;
                    }
                    TODO(sp, "Check trait bounds for bound on " << type);
                }
                TU_ARMA(UfcsInherent, pe) {
                    TODO(sp, "Auto trait lookup on UFCS Inherent type");
                }
        }
        return res;
        }
        TU_ARMA(Generic, e) {
            auto lRes = HIRCompare::Unequal;
            this->findTraitImpls(sp, trait, *paramsPtr, type, [&](auto, auto cmp) {
                lRes = cmp;
                return (cmp == HIRCompare::Equal);
            });
            return lRes;
        }
        TU_ARMA(Tuple, e) {
            HIRCompare res = HIRCompare::Equal;
            for (const auto& sty : e) {
                res &= typeImplsTrait(sty);
                if (res == HIRCompare::Unequal) {
                    return HIRCompare::Unequal;
                }
            }
            return res;
        }
        TU_ARMA(Array, e) {
            return typeImplsTrait(e.inner);
        }
    }
    throw "";
        }

        HIRCompare TraitResolution::fticCheckParams(
            const Span& sp,
            const HIRSimplePath& trait,
            const HIRPathParams* paramsPtr,
            const HIRTypeData* type,
            const HIRGenericParams& implParamsDef,
            const HIRPathParams& implTraitArgs,
            const HIRTypeData* implTy,
            /*Out->*/ HIRPathParams& outImplParams,
            bool evaluateBounds /*=true*/
        ) const {
            TRACE_FUNCTION_FR("impl" << implParamsDef.fmtArgs() << " " << trait << implTraitArgs << " for " << implTy, outImplParams);

            class GetParams: public HIRMatchGenerics {
                Span sp;
                HIRPathParams& outImplParams;

            public:
                GetParams(Span sp, HIRPathParams& outImplParams)
                    : sp(sp)
                    , outImplParams(outImplParams)
                {
                }

                HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) override {
                    assert(g.binding < outImplParams.types.size());
                    if (outImplParams.types[g.binding] == HIRTypeRef()) {
                        DEBUG("[ftic_check_params] Param " << g.binding << " = " << ty);
                        outImplParams.types[g.binding] = ty;
                        return HIRCompare::Equal;
                    } else {
                        DEBUG("[ftic_check_params] Param " << g.binding << " " << outImplParams.types[g.binding] << " == " << ty);
                        auto rv = outImplParams.types[g.binding]->compareWithPlaceholders(sp, ty, resolveCb);
                        // If the existing is an ivar, replace with this.
                        // - TODO: Store the least fuzzy option, or store all fuzzy options?
                        if (rv == HIRCompare::Fuzzy && outImplParams.types[g.binding]->is_Infer()) {
                            // The same impl parameter can be learned through more than one
                            // component of an impl header.  `Y = X` followed by `Y = &X`
                            // is not a fuzzy refinement: it would require the infinite type
                            // `X = &X`.  Treat that header as disjoint instead of replacing
                            // the first constraint and letting specialization pick it.
                            const auto& existingResolved = resolveCb.getType(sp, outImplParams.types[g.binding]);
                            const auto* existingInfer = existingResolved->opt_Infer();
                            if (existingInfer && existingInfer->index != ~0u) {
                                const bool recursive = visitTyWith(ty, [&](const HIRTypeData* inner) {
                                    if (const auto* infer = inner->opt_Infer()) {
                                        if (infer->index == ~0u) {
                                            return false;
                                        }
                                        const auto& resolved = resolveCb.getType(sp, inner);
                                        const auto* resolvedInfer = resolved->opt_Infer();
                                        return resolvedInfer && resolvedInfer->index == existingInfer->index;
                                    }
                                    return false;
                                });
                                if (recursive) {
                                    DEBUG("[ftic_check_params] Param " << g.binding << " would form an infinite type " << existingResolved << " = " << ty);
                                    return HIRCompare::Unequal;
                                }
                            }
                            DEBUG("[ftic_check_params] Param " << g.binding << " fuzzy, use " << ty);
                            outImplParams.types[g.binding] = ty;
                        }
                        return rv;
                    }
                }

                HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
                    ASSERT_BUG(sp, g.binding < outImplParams.values.size(), "Value generic " << g << " out of range (" << outImplParams.values.size() << ")");
                    if (sz.is_Infer()) {
                        ASSERT_BUG(sp, sz.as_Infer().index != ~0u, "");
                    }
                    if (outImplParams.values[g.binding] == HIRConstGeneric()) {
                        DEBUG("[ftic_check_params] Value param " << g.binding << " = " << sz);
                        outImplParams.values[g.binding] = sz.clone();
                        return HIRCompare::Equal;
                    } else {
                        if (outImplParams.values[g.binding] == sz) {
                            return HIRCompare::Equal;
                        }
                        if (outImplParams.values[g.binding].is_Infer()) {
                            if (!sz.is_Infer()) {
                                DEBUG("[ftic_check_params] Value param " << g.binding << " fuzzy, use " << sz);
                                outImplParams.values[g.binding] = sz.clone();
                            }
                            return HIRCompare::Fuzzy;
                        }
                        if (sz.is_Infer()) {
                            return HIRCompare::Fuzzy;
                        }
                        TODO(Span(), "PtrImplMatcher::match_val " << g << "(" << outImplParams.values[g.binding] << ") with " << sz);
                    }
                }

            };

            GetParams getParams{sp, outImplParams};

            outImplParams.types.resize(implParamsDef.types.size());
            outImplParams.values.resize(implParamsDef.values.size());

            // NOTE: If this type references an associated type, the match will incorrectly fail.
            // - HACK: match_test_generics_fuzz has been changed to return Fuzzy if there's a tag mismatch and the LHS is an Opaque path
            auto match = HIRCompare::Equal;
            match &= implTy->matchTestGenericsFuzz(sp, type, this->ivars.callbackResolveInfer(), getParams);
            if (paramsPtr) {
                const auto& params = *paramsPtr;
                match &= implTraitArgs.matchTestGenericsFuzz(sp, params, this->ivars.callbackResolveInfer(), getParams);
                if (match == HIRCompare::Unequal) {
                    DEBUG("- Failed to match parameters - " << implTraitArgs << "+" << implTy << " != " << params << "+" << type);
                    return HIRCompare::Unequal;
                }
            } else {
                if (match == HIRCompare::Unequal) {
                    DEBUG("- Failed to match type - " << implTy << " != " << type);
                    return HIRCompare::Unequal;
                }
            }

            DEBUG("Matched params: " << outImplParams);

            // Some impl blocks have type params used as part of type bounds.
            // - A rough idea is to have monomorph return a third class of generic for params that are not yet bound.
            //  - compare_with_placeholders gets called on both ivars and generics, so that can be used to replace it once known.
            HIRPathParams placeholders;
            RcString placeholderName;
            bool placeholdersNeeded = false;
            {
                for (const auto& ty : outImplParams.types) {
                    if (ty == HIRTypeRef()) {
                        placeholdersNeeded = true;
                    }
                }
                for (const auto& val : outImplParams.values) {
                    if (val == HIRConstGeneric()) {
                        placeholdersNeeded = true;
                    }
                }
            }
            if (placeholdersNeeded) {
                // NOTE: Not using interning, because these are short-lived
                // - Also, adding an interned string is quite expensive
                placeholderName = RcString(FMT("ph_" << &implParamsDef << "_" << freshImplPlaceholderCounter++));
                for (unsigned int i = 0; i < outImplParams.types.size(); i++) {
                    if (outImplParams.types[i] == HIRTypeRef()) {
                        if (placeholders.types.size() == 0) {
                            placeholders.types.resize(outImplParams.types.size());
                        }
                        placeholders.types[i] = crate.types.generic(placeholderName, 2 * 256 + i);
                        DEBUG("Create placeholder type for " << i << " = " << placeholders.types[i]);
                    }
                }
                for (unsigned int i = 0; i < outImplParams.values.size(); i++) {
                    if (outImplParams.values[i] == HIRConstGeneric()) {
                        if (placeholders.values.size() == 0) {
                            placeholders.values.resize(outImplParams.values.size());
                        }
                        placeholders.values[i] = HIRGenericRef(placeholderName, 2 * 256 + i);
                        DEBUG("Create placeholder value for " << i << " = " << placeholders.values[i]);
                    }
                }
                DEBUG("Placeholders (" << placeholderName << "): " << placeholders);
            } else {
                DEBUG("Placeholders not needed");
            }

            if (!evaluateBounds) {
                for (size_t i = 0; i < outImplParams.types.size(); i++) {
                    if (outImplParams.types[i] == HIRTypeRef()) {
                        outImplParams.types[i] = ::std::move(placeholders.types[i]);
                    }
                }
                for (size_t i = 0; i < outImplParams.values.size(); i++) {
                    if (outImplParams.values[i] == HIRConstGeneric()) {
                        outImplParams.values[i] = ::std::move(placeholders.values[i]);
                    }
                }
                return match;
            }
            auto cbInfer = this->ivars.callbackResolveInfer();

            struct Matcher: public HIRMatchGenerics, public Monomorphiser {
                Span sp;
                const HIRPathParams& implParams;
                RcString placeholderName;
                HIRPathParams& placeholders;

                Matcher(HIRTypeInterner& types, Span sp, const HIRPathParams& implParams, RcString placeholderName, HIRPathParams& placeholders)
                    : Monomorphiser(types)
                    , sp(sp)
                    , implParams(implParams)
                    , placeholderName(placeholderName)
                    , placeholders(placeholders)
                {
                }

                HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) override {
                    if (const auto* e = ty->opt_Generic()) {
                        if (e->binding == g.binding && e->name == g.name) {
                            return HIRCompare::Equal;
                        }
                    }
                    if (g.isPlaceholder() && g.name == placeholderName) {
                        auto i = g.idx();
                        ASSERT_BUG(sp, implParams.types[i] == HIRTypeRef(), "Placeholder to populated type returned - " << implParams.types[i] << " vs " << ty);
                        auto& ph = placeholders.types[i];
                        // TODO: Only want to do this if ... what?
                        // - Problem: This can poison the output if the result was fuzzy
                        // - E.g. `Q: Borrow<V>` can equate Q and V
                        if (ph->is_Generic() && ph->as_Generic().binding == g.binding) {
                            DEBUG("[ftic_check_params:cb_match] Bind placeholder " << i << " to " << ty);
                            ph = ty;
                            return HIRCompare::Equal;
                        } else {
                            DEBUG("[ftic_check_params:cb_match] Compare placeholder " << i << " " << ph << " == " << ty);
                            return ph->compareWithPlaceholders(sp, ty, resolveCb);
                        }
                    } else {
                        if (g.isPlaceholder()) {
                            DEBUG("[ftic_check_params:cb_match] External impl param " << g);
                            return HIRCompare::Fuzzy;
                        }
                        // If the RHS is a non-literal ivar, return fuzzy
                        if (ty->is_Infer() && !ty->as_Infer().isLit()) {
                            return HIRCompare::Fuzzy;
                        }
                        // If the RHS is an unbound UfcsKnown, also fuzzy
                        if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
                            return HIRCompare::Fuzzy;
                        }
                        if (ty->is_Generic() && ty->as_Generic().isPlaceholder()) {
                            return HIRCompare::Fuzzy;
                        }
                        DEBUG("Unequal generic type - " << g << " != " << ty);
                        return HIRCompare::Unequal;
                    }
                }

                HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& v) override {
                    if (const auto* e = v.opt_Generic()) {
                        if (e->binding == g.binding && e->name == g.name) {
                            return HIRCompare::Equal;
                        }
                    }
                    if (g.isPlaceholder() && g.name == placeholderName) {
                        auto i = g.idx();
                        ASSERT_BUG(sp, implParams.values[i] == HIRConstGeneric(), "Placeholder to populated value returned - " << implParams.values[i] << " vs " << v);
                        auto& ph = placeholders.values[i];
                        if (ph.is_Generic() && ph.as_Generic().binding == g.binding) {
                            DEBUG("[ftic_check_params:cb_match] Bind placeholder " << i << " to " << v);
                            ph = v.clone();
                            return HIRCompare::Equal;
                        } else {
                            DEBUG("[ftic_check_params:cb_match] Compare placeholder " << i << " " << ph << " == " << v);
                            TODO(Span(), "[ftic_check_params:cb_match] Compare placeholder " << i << " " << ph << " == " << v);
                        }
                    } else {
                        if (g.isPlaceholder()) {
                            DEBUG("[ftic_check_params:cb_match] External impl param " << g);
                            return HIRCompare::Fuzzy;
                        }
                        // If the RHS is a non-literal ivar, return fuzzy
                        if (v.is_Infer()) {
                            return HIRCompare::Fuzzy;
                        }
                        DEBUG("Unequal generic value - " << g << " != " << v);
                        return HIRCompare::Unequal;
                    }
                }

                HIRTypeRef getType(const Span& sp, const HIRGenericRef& ge) const override {
                    //    // TODO: `impl_type` or `des_type`
                    //    //TODO(sp, "[find_impl__check_crate_raw] Self - " << impl_type << " or " << des_type);
                    //}
                    ASSERT_BUG(sp, !ge.isPlaceholder(), "[find_impl__check_crate_raw] Placeholder param seen - " << ge);
                    if (implParams.types.at(ge.binding) != HIRTypeRef()) {
                        return implParams.types.at(ge.binding);
                    }
                    ASSERT_BUG(sp, placeholders.types.size() == implParams.types.size(), "Placeholder size mismatch: " << placeholders.types.size() << " != " << implParams.types.size());
                    return placeholders.types.at(ge.binding);
                }

                HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override {
                    ASSERT_BUG(sp, val.binding < 256, "Generic value binding in " << val << " out of range (>=256)");
                    ASSERT_BUG(sp, val.binding < implParams.values.size(), "Generic value binding in " << val << " out of range (>= " << implParams.values.size() << ")");
                    if (implParams.values.at(val.binding) != HIRConstGeneric()) {
                        return implParams.values.at(val.binding).clone();
                    }
                    ASSERT_BUG(sp, placeholders.values.size() == implParams.values.size(), "Placeholder size mismatch: " << placeholders.values.size() << " != " << implParams.values.size());
                    return placeholders.values.at(val.binding).clone();
                }

            };

            Matcher matcher{crate.types, sp, outImplParams, placeholderName, placeholders};

            //::std::vector<::HIR::TypeRef> saved_ph;

            // Keep looping while placeholders are updated
            int loops = 0;
            HIRPathParams lastPlaceholders;
            do {
                DEBUG(">> LOOP " << loops);
                ASSERT_BUG(sp, loops < 4, "Excessive iterations while resolving bound placeholders");
                loops += 1;
                lastPlaceholders = placeholders.clone();
                // Check bounds for this impl
                // - If a bound fails, then this can't be a valid impl
                for (const auto& bound : implParamsDef.bounds) {
            TU_MATCH_HDRA( (bound), {)
            TU_ARMA(TraitBound, be) {
                    DEBUG("Check bound " << be.type << " : " << be.trait);
                    auto realType = matcher.monomorphType(sp, be.type, false);
                    auto realTrait = matcher.monomorphTraitpath(sp, be.trait, false);
                    realType = this->expandAssociatedTypes(sp, mv$(realType));
                    for (auto& p : realTrait.mPath.mParams.types) {
                        p = this->expandAssociatedTypes(sp, mv$(p));
                    }
                    for (auto& ab : realTrait.typeBounds) {
                        ab.second.type = this->expandAssociatedTypes(sp, mv$(ab.second.type));
                    }
                    const auto& realTraitPath = realTrait.mPath;
                    DEBUG("- bound mono " << realType << " : " << realTrait);
                    bool foundFuzzyMatch = false;
                    // If the type is an unbound UFCS path, assume fuzzy
                    if (TU_TEST1(*realType, Path, .binding.is_Unbound())) {
                        DEBUG("- Bounded type is unbound UFCS, assuming fuzzy match");
                        foundFuzzyMatch = true;
                    }
                    // If the type is an ivar, but not a literal, assume fuzzy
                    if (TU_TEST1(*realType, Infer, .isLit() == false)) {
                        DEBUG("- Bounded type is an ivar, assuming fuzzy match");
                        foundFuzzyMatch = true;
                    }
                    // NOTE: Save the placeholder state and restore if the result was Fuzzy
                    HIRPathParams savedPh = placeholders.clone();
                    HIRPathParams fuzzyPh;
                    unsigned numFuzzy = 0;       //!< Number of detected fuzzy impls
                    bool fuzzyCompatible = true; //!< Indicates that the `fuzzy_ph` applies to all detected fuzzy impls
                    auto rv = this->findTraitImpls(sp, realTraitPath.mPath, realTraitPath.mParams, realType, [&](auto impl, auto implCmp) {
                        // TODO: Save and restore placeholders if this isn't a full match
                        DEBUG("[ftic_check_params] impl_cmp = " << implCmp << ", impl = " << impl);
                        auto cmp = implCmp;
                        if (cmp == HIRCompare::Fuzzy) {
                            // If the match was fuzzy, try again filling in with `cb_match`
                            auto iTy = impl.getImplType(crate.types);
                            this->expandAssociatedTypesInplace(sp, iTy, {});
                            auto iTp = impl.getTraitParams(crate.types);
                            for (auto& t : iTp.types) {
                                this->expandAssociatedTypesInplace(sp, t, {});
                            }
                            DEBUG("[ftic_check_params] " << realType << " ?= " << iTy);
                            cmp &= realType->matchTestGenericsFuzz(sp, iTy, cbInfer, matcher);
                            DEBUG("[ftic_check_params] " << realTraitPath.mParams << " ?= " << iTp);
                            cmp &= realTraitPath.mParams.matchTestGenericsFuzz(sp, iTp, cbInfer, matcher);
                            DEBUG("[ftic_check_params] - Re-check result: " << cmp);
                        }
                        for (const auto& assocBound : realTrait.typeBounds) {
                            HIRTypeRef tmp;
                            const HIRTypeData* ty;

                            tmp = impl.getType(crate.types, assocBound.first.c_str(), assocBound.second.atyParams);
                            if (tmp == HIRTypeRef()) {
                                // This bound isn't from this particular trait, go the slow way of using expand_associated_types
                                tmp = this->expandAssociatedTypes(sp, crate.types.path(HIRPath(HIRPath::Data::Data_UfcsKnown{realType, realTraitPath.clone(), assocBound.first, {}}), {}));
                                ty = tmp;
                            } else {
                                // Expand after extraction, just to make sure.
                                this->expandAssociatedTypesInplace(sp, tmp, {});
                                ty = this->ivars.getType(tmp);
                            }
                            DEBUG("[ftic_check_params] - Compare " << ty << " and " << assocBound.second.type << ", matching generics");
                            // `ty` = Monomorphised actual type (< `be.type` as `be.trait` >::`assoc_bound.first`)
                            // `assoc_bound.second` = Desired type (monomorphised too)
                            auto cmpI = assocBound.second.type->matchTestGenericsFuzz(sp, ty, cbInfer, matcher);
                            switch (cmpI) {
                                case HIRCompare::Equal:
                                    DEBUG("Equal");
                                    break;
                                case HIRCompare::Unequal:
                                    DEBUG("Assoc `" << assocBound.first << "` didn't match - " << ty << " != " << assocBound.second.type);
                                    cmp = HIRCompare::Unequal;
                                    break;
                                case HIRCompare::Fuzzy:
                                    // TODO: When a fuzzy match is encountered on a conditional bound, returning `false` can lead to an false negative (and a compile error)
                                    // BUT, returning `true` could lead to it being selected. (Is this a problem, should a later validation pass check?)
                                    DEBUG("[ftic_check_params] Fuzzy match assoc bound between " << ty << " and " << assocBound.second.type);
                                    cmp = HIRCompare::Fuzzy;
                                    break;
                            }
                            if (cmp == HIRCompare::Unequal) {
                                break;
                            }
                        }

                        DEBUG("[ftic_check_params] impl_cmp = " << implCmp << ", cmp = " << cmp);
                        if (cmp == HIRCompare::Fuzzy) {
                            foundFuzzyMatch |= true;
                            // `fuzzy_ph` is set (num_fuzzy > 0) then check if the PH set is equal, if not then flag not equal
                            if (numFuzzy > 0 && fuzzyPh != placeholders) {
                                DEBUG("Multiple fuzzy matches, placeholders mismatch: " << fuzzyPh << " != " << placeholders);
                                fuzzyCompatible = false;
                            }
                            numFuzzy += 1;

                            fuzzyPh = ::std::move(placeholders);
                            // TODO: Should this do some form of reset?
                            placeholders.types.resize(fuzzyPh.types.size());
                            placeholders.values.resize(fuzzyPh.values.size());
                        }
                        if (cmp != HIRCompare::Equal) {
                            // Restore placeholders
                            // - Maybe save the results for later?
                            DEBUG("[ftic_check_params] Restore placeholders: " << savedPh);
                            DEBUG("[ftic_check_params] OVERWRITTEN placeholders: " << placeholders);
                            placeholders = savedPh.clone();
                        }
                        // If the match isn't a concrete equal, return false (to keep searching)
                        return (cmp == HIRCompare::Equal);
                    });
                    if (rv) {
                        DEBUG("- Bound " << realType << " : " << realTraitPath << " matched");
                    } else if (foundFuzzyMatch) {
                        DEBUG("- Bound " << realType << " : " << realTraitPath << " fuzzed");
                        if (numFuzzy == 0) {
                            DEBUG("No placeholders"); // `real_type` was infer
                        } else if (numFuzzy == 1) {
                            DEBUG("Use placeholders " << fuzzyPh);
                            placeholders = ::std::move(fuzzyPh);
                        } else if (fuzzyCompatible) {
                            DEBUG("Multiple placeholders (" << numFuzzy << "), but all equal " << fuzzyPh);
                            placeholders = ::std::move(fuzzyPh);
                        } else {
                            DEBUG("TODO: Multiple fuzzy matches (" << numFuzzy << "), which placeholder set to use?");
                        }
                        match = HIRCompare::Fuzzy;
                    } else if (TU_TEST1(*realType, Infer, .tyClass == HIRInferClass::None)) {
                        DEBUG("- Bound " << realType << " : " << realTraitPath << " full infer type - make result fuzzy");
                        match = HIRCompare::Fuzzy;
                    } else if (TU_TEST1(*realType, Generic, .isPlaceholder())) {
                        DEBUG("- Bound " << realType << " : " << realTraitPath << " placeholder - make result fuzzy");
                        match = HIRCompare::Fuzzy;
                    } else {
                        DEBUG("- Bound " << realType << " : " << realTraitPath << " failed");
                        return HIRCompare::Unequal;
                    }

                    //}
                }
                TU_ARMA(TypeEquality, be) {
                    TODO(sp, "Check bound " << be.type << " = " << be.otherType);
                }
            }
                }
            } while (placeholders != lastPlaceholders);

            for (size_t i = 0; i < outImplParams.types.size(); i++) {
                if (outImplParams.types[i] == HIRTypeRef()) {
                    outImplParams.types[i] = std::move(placeholders.types[i]);
                }
                ASSERT_BUG(sp, outImplParams.types[i] != HIRTypeRef(), "");
            }
            for (size_t i = 0; i < outImplParams.values.size(); i++) {
                if (outImplParams.values[i] == HIRConstGeneric()) {
                    outImplParams.values[i] = std::move(placeholders.values[i]);
                }
                ASSERT_BUG(sp, outImplParams.values[i] != HIRConstGeneric(), "");
            }

            for (size_t i = 0; i < implParamsDef.types.size(); i++) {
                if (implParamsDef.types.at(i).isSized) {
                    if (outImplParams.types[i] != HIRTypeRef()) {
                        auto cmp = typeIsSized(sp, outImplParams.types[i]);
                        if (cmp == HIRCompare::Unequal) {
                            DEBUG("- Sized bound failed for " << outImplParams.types[i]);
                            return HIRCompare::Unequal;
                        }
                    } else {
                        // TODO: Set match to fuzzy?
                    }
                }
            }

            return match;
        }

        namespace {
            bool traitContainsMethodInner(const HIRTrait& traitPtr, const RcString& name, const HIRFunction*& outFcnPtr) {
                auto it = traitPtr.values.find(name);
                if (it != traitPtr.values.end()) {
                    if (it->second.is_Function()) {
                        const auto& v = it->second.as_Function();
                        outFcnPtr = &v;
                        return true;
                    }
                }
                return false;
            }
        }

        const HIRFunction* TraitResolution::traitContainsMethod(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const HIRTypeData* self, const RcString& name, HIRGenericPath& outPath) const {
            TRACE_FUNCTION_FR("trait_path=" << traitPath << ",name=" << name, outPath);
            const HIRFunction* rv = nullptr;

            if (traitContainsMethodInner(traitPtr, name, rv)) {
                assert(rv);
                outPath = traitPath.clone();
                return rv;
            }

            auto monomorphCb = MonomorphStatePtr(crate.types, self, &traitPath.mParams, nullptr);
            for (const auto& st : traitPtr.allParentTraits) {
                if (traitContainsMethodInner(*st.traitPtr, name, rv)) {
                    assert(rv);
                    outPath.mPath = st.mPath.mPath;
                    outPath.mParams = monomorphCb.monomorphPathParams(sp, st.mPath.mParams, false);
                    return rv;
                }
            }
            return nullptr;
        }

        bool TraitResolution::traitContainsType(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const char* name, HIRGenericPath& outPath) const {
            TRACE_FUNCTION_FR(traitPath << " has " << name, outPath);

            auto it = traitPtr.types.find(name);
            if (it != traitPtr.types.end()) {
                DEBUG("- Found in cur");
                outPath = traitPath.clone();
                return true;
            }

            auto monomorphCb = MonomorphStatePtr(crate.types, nullptr, &traitPath.mParams, nullptr);
            for (const auto& st : traitPtr.allParentTraits) {
                if (st.traitPtr->types.count(name)) {
                    DEBUG("- Found in " << st);
                    outPath.mPath = st.mPath.mPath;
                    outPath.mParams = monomorphCb.monomorphPathParams(sp, st.mPath.mParams, false);
                    return true;
                }
            }
            return false;
        }

        HIRCompare TraitResolution::typeIsSized(const Span& sp, const HIRTypeData* type) const {
            bool isFuzzy = false;
            bool hasEq = false;
            if (!mLangSized.components().empty()) {
                hasEq = findTraitImpls(sp, mLangSized, HIRPathParams{}, type, [&](auto, auto c) -> bool {
                    switch (c) {
                        case HIRCompare::Equal:
                            return true;
                        case HIRCompare::Fuzzy:
                            isFuzzy = true;
                            return false;
                        case HIRCompare::Unequal:
                            return false;
                    }
                    throw "";
                }, /*magic_trait_impls=*/false);
            }
            if (hasEq) {
                return HIRCompare::Equal;
            } else if (isFuzzy) {
                return HIRCompare::Fuzzy;
            } else {
            }

    TU_MATCH_HDRA( (*type), {)
    default:
        // Any unknown - it's sized
    TU_ARMA(Infer, e) {
            switch (e.tyClass) {
                case HIRInferClass::Integer:
                case HIRInferClass::Float:
                    return HIRCompare::Equal;
                default:
                    return HIRCompare::Fuzzy;
            }
        }
        TU_ARMA(Primitive, e) {
            if (e == HIRCoreType::Str) {
                return HIRCompare::Unequal;
            }
        }
        TU_ARMA(Slice, e) {
            return HIRCompare::Unequal;
        }
        TU_ARMA(Path, e) {
            // TODO: Check that only ?Sized parameters are !Sized
            TU_MATCHA(
                (e.binding),
                (pb),
                (Unbound, ),
                (
                    Opaque,
                    // TODO: Check bounds
                ),
                (ExternType,
                 // Is it sized? No.
                 return HIRCompare::Unequal;),
                (
                    Enum,
                    // HAS to be Sized
                ),
                (
                    Union,
                    // Pretty sure unions are Sized
                ),
                (Struct,
                 // Possibly not sized
                 switch (pb->structMarkings.dstType) {
                     case HIRStructMarkings::DstType::None:
                         break;
                     case HIRStructMarkings::DstType::Possible:
                         // Check sized-ness of the unsized param
                         return typeIsSized(sp, e.path.mData.as_Generic().mParams.types.at(pb->structMarkings.unsizedParam));
                     case HIRStructMarkings::DstType::Slice:
                     case HIRStructMarkings::DstType::TraitObject:
                         return HIRCompare::Unequal;
                 })
            )
        }
        TU_ARMA(Generic, e) {
            switch (e.group()) {
                case 0:
                    return this->mImplGenerics->types.at(e.idx()).isSized ? HIRCompare::Equal : HIRCompare::Unequal;
                case 1:
                    return this->mItemGenerics->types.at(e.idx()).isSized ? HIRCompare::Equal : HIRCompare::Unequal;
                default:
                    // Assume sized for anything else?
                    return HIRCompare::Equal;
            }
        }
        TU_ARMA(ErasedType, e) {
            return e.isSized ? HIRCompare::Equal : HIRCompare::Unequal;
        }
        TU_ARMA(TraitObject, e) {
            return HIRCompare::Unequal;
        }
    }
    return HIRCompare::Equal;
        }

        HIRCompare TraitResolution::typeIsCopy(const Span& sp, const HIRTypeData* ty) const {
            const auto& type = this->ivars.getType(ty);
    TU_MATCH_HDRA( (*type), {)
    default: {
            bool isFuzzy = false;
            bool hasEq = findTraitImpls(sp, mLangCopy, HIRPathParams{}, ty, [&](auto, auto c) -> bool {
                switch (c) {
                    case HIRCompare::Equal:
                        return true;
                    case HIRCompare::Fuzzy:
                        isFuzzy = true;
                        return false;
                    case HIRCompare::Unequal:
                        return false;
                }
                throw "";
            }, /*magic_trait_impls=*/false);
            if (hasEq) {
                return HIRCompare::Equal;
            } else if (isFuzzy) {
                return HIRCompare::Fuzzy;
            } else {
                if (type->is_Path() && type->as_Path().binding.is_Unbound()) {
                    return HIRCompare::Fuzzy;
                }
                return HIRCompare::Unequal;
            }
        }
        TU_ARMA(Infer, e) {
            switch (e.tyClass) {
                case HIRInferClass::Integer:
                case HIRInferClass::Float:
                    return HIRCompare::Equal;
                default:
                    DEBUG("Fuzzy Copy impl for ivar?");
                    return HIRCompare::Fuzzy;
            }
        }
        TU_ARMA(Generic, e) {
            // TODO: Store this result - or even pre-calculate it.
            return this->iterateBoundsTraits(
                       sp,
                       ty,
                       mLangCopy,
                       [&](HIRCompare _cmp, const HIRTypeData* beType, const HIRGenericPath& beTrait, const CachedBound& info) -> bool {
                return true;
            }
                   )
                       ? HIRCompare::Equal
                       : HIRCompare::Unequal;
        }
        TU_ARMA(Primitive, e) {
            if (e == HIRCoreType::Str) {
                return HIRCompare::Unequal;
            }
            return HIRCompare::Equal;
        }
        TU_ARMA(Borrow, e) {
            return e.type == HIRBorrowType::Shared ? HIRCompare::Equal : HIRCompare::Unequal;
        }
        TU_ARMA(Pointer, e) {
            return HIRCompare::Equal;
        }
        TU_ARMA(Tuple, e) {
            auto rv = HIRCompare::Equal;
            for (const auto& sty : e) {
                rv &= typeIsCopy(sp, sty);
            }
            return rv;
        }
        TU_ARMA(Slice, e) {
            return HIRCompare::Unequal;
        }
        TU_ARMA(NamedFunction, e) {
            return HIRCompare::Equal;
        }
        TU_ARMA(Function, e) {
            return HIRCompare::Equal;
        }
        TU_ARMA(NodeType, e) {
            // NOTE: This isn't strictly true, we're leaving the actual checking up to the validate pass
            return HIRCompare::Equal;
        }
        TU_ARMA(Array, e) {
            return typeIsCopy(sp, e.inner);
        }
    }
    throw "";
        }

        HIRCompare TraitResolution::typeIsClone(const Span& sp, const HIRTypeData* ty) const {
            TRACE_FUNCTION_F(ty);
            const auto& type = this->ivars.getType(ty);
    TU_MATCH_HDRA( (*type), {)
    default: {
            if (type->is_Path() && type->as_Path().isClosure()) {
                // If it was a closure, assume true (later code can check)
                return HIRCompare::Equal;
            }
            bool isFuzzy = false;
            bool hasEq = findTraitImpls(sp, mLangClone, HIRPathParams{}, ty, [&](auto, auto c) -> bool {
                switch (c) {
                    case HIRCompare::Equal:
                        return true;
                    case HIRCompare::Fuzzy:
                        isFuzzy = true;
                        return false;
                    case HIRCompare::Unequal:
                        return false;
                }
                throw "";
            }, /*magic_trait_impls=*/false);
            if (hasEq) {
                return HIRCompare::Equal;
            } else if (isFuzzy) {
                return HIRCompare::Fuzzy;
            } else {
                return HIRCompare::Unequal;
            }
        }
        TU_ARMA(Infer, e) {
            switch (e.tyClass) {
                case HIRInferClass::Integer:
                case HIRInferClass::Float:
                    return HIRCompare::Equal;
                default:
                    DEBUG("Fuzzy Clone impl for ivar?");
                    return HIRCompare::Fuzzy;
            }
        }
        TU_ARMA(Generic, e) {
            // TODO: Store this result - or even pre-calculate it.
            return this->iterateBoundsTraits(
                       sp,
                       ty,
                       mLangClone,
                       [&](HIRCompare _cmp, const HIRTypeData* beType, const HIRGenericPath& beTrait, const CachedBound& info) -> bool {
                return true;
            }
                   )
                       ? HIRCompare::Equal
                       : HIRCompare::Unequal;
        }
        TU_ARMA(Primitive, e) {
            if (e == HIRCoreType::Str) {
                return HIRCompare::Unequal;
            }
            return HIRCompare::Equal;
        }
        TU_ARMA(Borrow, e) {
            return e.type == HIRBorrowType::Shared ? HIRCompare::Equal : HIRCompare::Unequal;
        }
        TU_ARMA(Pointer, e) {
            return HIRCompare::Equal;
        }
        TU_ARMA(Tuple, e) {
            auto rv = HIRCompare::Equal;
            for (const auto& sty : e) {
                rv &= typeIsClone(sp, sty);
            }
            return rv;
        }
        TU_ARMA(Slice, e) {
            return HIRCompare::Unequal;
        }
        TU_ARMA(NamedFunction, e) {
            return HIRCompare::Equal;
        }
        TU_ARMA(Function, e) {
            return HIRCompare::Equal;
        }
        TU_ARMA(NodeType, e) {
            // NOTE: This isn't strictly true, we're leaving the actual checking up to the validate pass
            // TODO: Determine captures earlier and check captures here
            return HIRCompare::Equal;
        }
        TU_ARMA(Array, e) {
            return typeIsClone(sp, e.inner);
        }
    }
    throw "";
        }

        // Checks if a type can unsize to another
        // - Returns Compare::Equal if the unsize is possible and fully known
        // - Returns Compare::Fuzzy if the unsize is possible, but still unknown.
        // - Returns Compare::Unequal if the unsize is impossibe (for any reason)
        // Closure is called `get_new_type` is true, and the unsize is possible
        // usecases:
        // - Checking for an impl as part of impl selection (return True/False/Maybe with required match for Maybe)
        // - Checking for an impl as part of typeck (return True/False/Maybe with unsize possibility OR required equality)
        HIRCompare TraitResolution::canUnsize(const Span& sp, const HIRTypeData* dstTy, const HIRTypeData* srcTy, ::std::function<void(HIRTypeRef newDst)>* newTypeCallback, ::std::function<void(const HIRTypeData* dst, const HIRTypeData* src)>* inferCallback) const {
            TRACE_FUNCTION_F(dstTy << " <- " << srcTy);

            // 1. Test for type equality
            {
                auto cmp = dstTy->compareWithPlaceholders(sp, srcTy, ivars.callbackResolveInfer());
                if (cmp == HIRCompare::Equal) {
                    return HIRCompare::Unequal;
                }
            }

            // 2. If either side is an ivar, fuzzy.
            if (dstTy->is_Infer() || srcTy->is_Infer()) {
                // Inform the caller that these two types could unsize to each other
                // - This allows the coercions code to move the coercion rule up
                if (inferCallback) {
                    (*inferCallback)(dstTy, srcTy);
                }
                return HIRCompare::Fuzzy;
            }

            {
                bool foundBound = this->iterateBoundsTraits(sp, srcTy, mLangUnsize, [&](HIRCompare cmp, const HIRTypeData* beType, const HIRGenericPath& beTrait, const CachedBound& info) -> bool {
                    const auto& beDst = beTrait.mParams.types.at(0);

                    cmp &= dstTy->compareWithPlaceholders(sp, beDst, ivars.callbackResolveInfer());
                    if (cmp == HIRCompare::Unequal) {
                        return false;
                    }

                    if (cmp != HIRCompare::Equal) {
                        TODO(sp, "Found bound " << dstTy << "=" << beDst << " <- " << srcTy << "=" << beType);
                    }
                    return true;
                });
                if (foundBound) {
                    return HIRCompare::Equal;
                }
            }

            // Associated types, check the bounds in the trait.
            if (srcTy->is_Path() && srcTy->as_Path().path.mData.is_UfcsKnown()) {
                HIRCompare rv = HIRCompare::Equal;
                const auto& pe = srcTy->as_Path().path.mData.as_UfcsKnown();
                auto monomorphCb = MonomorphStatePtr(crate.types, pe.type, &pe.trait.mParams, nullptr);
                auto foundBound = this->iterateAtyBounds(sp, pe, [&](const HIRTraitPath& bound) {
                    if (bound.mPath.mPath != mLangUnsize) {
                        return false;
                    }
                    const auto& beDstTpl = bound.mPath.mParams.types.at(0);
                    HIRTypeRef tmpTy;
                    const auto& beDst = monomorphCb.maybeMonomorphType(sp, tmpTy, beDstTpl);

                    auto cmp = dstTy->compareWithPlaceholders(sp, beDst, ivars.callbackResolveInfer());
                    if (cmp == HIRCompare::Unequal) {
                        return false;
                    }

                    if (cmp != HIRCompare::Equal) {
                        DEBUG("[can_unsize] > Found bound (fuzzy) " << dstTy << "=" << beDst << " <- " << srcTy);
                        rv = HIRCompare::Fuzzy;
                    }
                    return true;
                });
                if (foundBound) {
                    return rv;
                }
            }

            // Struct<..., T, ...>: Unsize<Struct<..., U, ...>>
            if (dstTy->is_Path() && srcTy->is_Path()) {
                bool dstIsUnsizable = dstTy->as_Path().binding.is_Struct() && dstTy->as_Path().binding.as_Struct()->structMarkings.canUnsize;
                bool srcIsUnsizable = srcTy->as_Path().binding.is_Struct() && srcTy->as_Path().binding.as_Struct()->structMarkings.canUnsize;
                if (dstIsUnsizable || srcIsUnsizable) {
                    DEBUG("Struct unsize? " << dstTy << " <- " << srcTy);
                    const auto& str = *dstTy->as_Path().binding.as_Struct();
                    const auto& dstGp = dstTy->as_Path().path.mData.as_Generic();
                    const auto& srcGp = srcTy->as_Path().path.mData.as_Generic();

                    if (dstGp == srcGp) {
                        DEBUG("Can't Unsize, destination and source are identical");
                        return HIRCompare::Unequal;
                    } else if (dstGp.mPath == srcGp.mPath) {
                        DEBUG("Checking for Unsize " << dstGp << " <- " << srcGp);
                        // Structures are equal, add the requirement that the ?Sized parameter also impl Unsize
                        const auto& dstInner = ivars.getType(dstGp.mParams.types.at(str.structMarkings.unsizedParam));
                        const auto& srcInner = ivars.getType(srcGp.mParams.types.at(str.structMarkings.unsizedParam));

                        auto cb = [&](auto d) {
                            assert(newTypeCallback);

                            // Re-create structure with s/d
                            auto dstGpNew = dstGp.clone();
                            dstGpNew.mParams.types.at(str.structMarkings.unsizedParam) = mv$(d);
                            (*newTypeCallback)(crate.types.path(HIRPath(mv$(dstGpNew)), HIRTypePathBinding::make_Struct(&str)));
                        };
                        if (newTypeCallback) {
                            ::std::function<void(HIRTypeRef)> cbP = cb;
                            return this->canUnsize(sp, dstInner, srcInner, &cbP, inferCallback);
                        } else {
                            return this->canUnsize(sp, dstInner, srcInner, nullptr, inferCallback);
                        }
                    } else {
                        DEBUG("Can't Unsize, destination and source are different structs");
                        return HIRCompare::Unequal;
                    }
                }
            }

            // (Trait) <- Foo
            if (const auto* de = dstTy->opt_TraitObject()) {
                // TODO: Check if src_ty is !Sized
                // - Only allowed if the source is a trait object with the same data trait and lesser bounds

                DEBUG("TraitObject unsize? " << dstTy << " <- " << srcTy);

                // (Trait) <- (Trait+Foo)
                if (const auto* se = srcTy->opt_TraitObject()) {
                    auto rv = HIRCompare::Equal;

                    // Project the source principal to the requested
                    // supertrait.  A trait may contain the same supertrait
                    // with different substitutions, so compare the fully
                    // monomorphised parameters instead of only its path.
                    const HIRTraitPath* projected = nullptr;
                    HIRTraitPath projectedStorage;
                    if (de->mTrait.mPath.mPath == se->mTrait.mPath.mPath) {
                        rv &= comparePp(sp, se->mTrait.mPath.mParams, de->mTrait.mPath.mParams);
                        projected = &se->mTrait;
                    } else if (se->mTrait.mPath.mPath != HIRSimplePath()) {
                        findNamedTraitInTrait(sp, de->mTrait.mPath.mPath, de->mTrait.mPath.mParams, *se->mTrait.traitPtr, se->mTrait.mPath.mPath, se->mTrait.mPath.mParams, srcTy, [&](const HIRTraitPath& parent) {
                            const auto cmp = comparePp(sp, parent.mPath.mParams, de->mTrait.mPath.mParams);
                            if (cmp == HIRCompare::Unequal) {
                                return false;
                            }
                            rv &= cmp;
                            projectedStorage = parent.clone();
                            projected = &projectedStorage;
                            return cmp == HIRCompare::Equal;
                        });
                    }
                    if (!projected || rv == HIRCompare::Unequal) {
                        return HIRCompare::Unequal;
                    }

                    // Every associated-type equality required by the
                    // destination object must also hold on the projected
                    // source supertrait.
                    for (const auto& required : de->mTrait.typeBounds) {
                        const auto source = projected->typeBounds.find(required.first);
                        if (source == projected->typeBounds.end()) {
                            return HIRCompare::Unequal;
                        }
                        rv &= source->second.type->compareWithPlaceholders(sp, required.second.type, ivars.callbackResolveInfer());
                        if (rv == HIRCompare::Unequal) {
                            return rv;
                        }
                    }

                    // 2. Destination markers must be a strict subset
                    for (const auto& mt : de->markers) {
                        // TODO: Fuzzy match
                        bool found = false;
                        for (const auto& omt : se->markers) {
                            if (omt == mt) {
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            // Return early.
                            return HIRCompare::Unequal;
                        }
                    }

                    if (rv == HIRCompare::Fuzzy && newTypeCallback) {
                        // TODO: Inner type
                    }
                    return rv;
                }

                bool good;
                HIRCompare totalCmp = HIRCompare::Equal;

                HIRTypeData::Data_TraitObject tmpE;
                tmpE.mTrait.mPath = de->mTrait.mPath.mPath;

                // Check data trait first.
                if (de->mTrait.mPath.mPath == HIRSimplePath()) {
                    ASSERT_BUG(sp, de->markers.size() > 0, "TraitObject with no traits - " << dstTy);
                    good = true;
                } else {
                    good = findTraitImpls(sp, de->mTrait.mPath.mPath, de->mTrait.mPath.mParams, srcTy, [&](const auto impl, auto cmp) {
                        if (cmp == HIRCompare::Unequal) {
                            return false;
                        }

                        auto candidateCmp = cmp;
                        HIRTypeData::Data_TraitObject candidateE;
                        candidateE.mTrait.mPath = de->mTrait.mPath.mPath;
                        candidateE.mTrait.mPath.mParams = impl.getTraitParams(crate.types);

                        // Associated types declared by a supertrait carry the
                        // declaring trait path.  Rebuild that path with the
                        // selected principal-trait response instead of mixing
                        // response parameters with the original goal.
                        auto remapSourceTrait = [&](const HIRGenericPath& sourceTrait) {
                            if (sourceTrait.mPath == de->mTrait.mPath.mPath) {
                                return HIRGenericPath(sourceTrait.mPath, candidateE.mTrait.mPath.mParams.clone());
                            }

                            HIRGenericPath result = sourceTrait.clone();
                            if (!de->mTrait.traitPtr) {
                                candidateCmp = HIRCompare::Fuzzy;
                                return result;
                            }

                            auto goalMonomorph = MonomorphStatePtr(crate.types, srcTy, &de->mTrait.mPath.mParams, nullptr);
                            auto responseMonomorph = MonomorphStatePtr(crate.types, srcTy, &candidateE.mTrait.mPath.mParams, nullptr);
                            bool found = false;
                            bool foundEqual = false;
                            for (const auto& parent : de->mTrait.traitPtr->allParentTraits) {
                                if (parent.mPath.mPath != sourceTrait.mPath) {
                                    continue;
                                }
                                auto goalParent = goalMonomorph.monomorphGenericpath(sp, parent.mPath, false);
                                const auto parentCmp = comparePp(sp, goalParent.mParams, sourceTrait.mParams);
                                if (parentCmp == HIRCompare::Unequal || (foundEqual && parentCmp != HIRCompare::Equal)) {
                                    continue;
                                }

                                auto responseParent = responseMonomorph.monomorphGenericpath(sp, parent.mPath, false);
                                if (!found || parentCmp == HIRCompare::Equal) {
                                    result = ::std::move(responseParent);
                                    found = true;
                                    foundEqual = parentCmp == HIRCompare::Equal;
                                } else if (result != responseParent) {
                                    // Multiple fuzzy supertrait projections
                                    // are a legitimate ambiguous response.
                                    candidateCmp = HIRCompare::Fuzzy;
                                }
                            }
                            if (!found) {
                                candidateCmp = HIRCompare::Fuzzy;
                            } else if (!foundEqual) {
                                candidateCmp = HIRCompare::Fuzzy;
                            }
                            return result;
                        };

                        for (const auto& aty : de->mTrait.typeBounds) {
                            auto atyv = impl.getType(crate.types, aty.first.c_str(), aty.second.atyParams);
                            if (atyv == HIRTypeRef()) {
                                // Get the trait from which this associated type comes.
                                // Insert a UfcsKnown path for that
                                auto p = HIRPath(srcTy, aty.second.sourceTrait.clone(), aty.first, aty.second.atyParams.clone());
                                // Run EAT
                                atyv = this->expandAssociatedTypes(sp, crate.types.path(mv$(p), {}));
                            }

                            auto desired = this->expandAssociatedTypes(sp, aty.second.type);
                            const auto atyCmp = compareTy(sp, atyv, desired);
                            if (atyCmp == HIRCompare::Unequal) {
                                return false;
                            }
                            candidateCmp &= atyCmp;
                            candidateE.mTrait.typeBounds[aty.first] = HIRTraitPath::AtyEqual{remapSourceTrait(aty.second.sourceTrait), aty.second.atyParams.clone(), mv$(atyv)};
                        }

                        totalCmp &= candidateCmp;
                        tmpE = ::std::move(candidateE);
                        return true;
                    });
                }

                // Then markers
                auto cb = [&](const auto impl, auto cmp) {
                    if (cmp == HIRCompare::Unequal) {
                        return false;
                    }
                    totalCmp &= cmp;
                    tmpE.markers.back().mParams = impl.getTraitParams(crate.types);
                    return true;
                };
                for (const auto& marker : de->markers) {
                    if (!good) {
                        break;
                    }
                    tmpE.markers.push_back(marker.mPath);
                    good &= findTraitImpls(sp, marker.mPath, marker.mParams, srcTy, cb);
                }

                if (good && totalCmp == HIRCompare::Fuzzy && newTypeCallback) {
                    (*newTypeCallback)(crate.types.intern(HIRTypeData::make_TraitObject(mv$(tmpE))));
                }
                return totalCmp;
            }

            // [T] <- [T; n]
            if (const auto* de = dstTy->opt_Slice()) {
                if (const auto* se = srcTy->opt_Array()) {
                    DEBUG("Array unsize? " << de->inner << " <- " << se->inner);
                    auto cmp = de->inner->compareWithPlaceholders(sp, se->inner, ivars.callbackResolveInfer());
                    // TODO: Indicate to caller that for this to be true, these two must be the same.
                    // - I.E. if true, equate these types
                    if (cmp == HIRCompare::Fuzzy && newTypeCallback) {
                        (*newTypeCallback)(crate.types.slice(se->inner));
                    }
                    return cmp;
                }
            }

            DEBUG("Can't unsize, no rules matched");
            return HIRCompare::Unequal;
        }

        const HIRTypeData* TraitResolution::typeIsOwnedBox(const Span& sp, const HIRTypeData* ty) const {
            if (const auto* e = ty->opt_Path()) {
                if (const auto* pe = e->path.mData.opt_Generic()) {
                    if (pe->mPath == mLangBox) {
                        return this->ivars.getType(pe->mParams.types.at(0));
                    }
                }
            }
            return nullptr;
        }

        // -------------------------------------------------------------------------------------------------------------------
        // -------------------------------------------------------------------------------------------------------------------
        TraitResolution::AutoderefResult TraitResolution::autoderefStep(const Span& sp, const HIRTypeData* tyIn, HIRTypeRef& target, ::std::optional<HIRTypeRef>* implType) const {
            if (implType) {
                implType->reset();
            }

            const auto& ty = this->ivars.getType(tyIn);
            if (ty->is_Infer()) {
                return AutoderefResult::NoMatch;
            } else if (const auto* e = ty->opt_Borrow()) {
                DEBUG("Deref " << ty << " into " << e->inner);
                target = this->ivars.getType(e->inner);
                return AutoderefResult::Match;
            }
            // Array-to-slice is the final unsize step in an autoderef search.
            // create_autoderef materialises it as borrow -> pointer unsize -> deref.
            else if (const auto* e = ty->opt_Array()) {
                DEBUG("Deref " << ty << " into [" << e->inner << "]");
                target = crate.types.slice(e->inner);
                return AutoderefResult::Match;
            }
            // Shortcut, don't look up a Deref impl for primitives or slices
            else if (ty->is_Slice() || ty->is_Primitive() || ty->is_Tuple() || ty->is_Array()) {
                return AutoderefResult::NoMatch;
            } else {
                ::std::optional<HIRTypeRef> candidateTarget;
                ::std::optional<HIRTypeRef> candidateImplType;
                bool exact = false;
                bool ambiguous = false;

                this->findTraitImpls(sp, mLangDeref, HIRPathParams{}, ty, [&](auto impl, auto match) {
                    auto foundTarget = impl.getType(crate.types, "Target", {});
                    if (foundTarget == HIRTypeRef()) {
                        foundTarget = crate.types.path(HIRPath(ty, mLangDeref, RcString::newInterned("Target")), HIRTypePathBinding::make_Opaque({}));
                    } else {
                        this->expandAssociatedTypesInplace(sp, foundTarget, {});
                    }
                    auto foundImplType = impl.getImplType(crate.types);

                    if (match == HIRCompare::Equal) {
                        candidateTarget = foundTarget;
                        candidateImplType = foundImplType;
                        exact = true;
                        return true;
                    }

                    if (candidateTarget) {
                        ambiguous = true;
                    } else {
                        candidateTarget = foundTarget;
                        candidateImplType = foundImplType;
                    }
                    return false;
                });

                if (!exact && ambiguous) {
                    DEBUG("Ambiguous Deref impl for " << ty);
                    return AutoderefResult::Ambiguous;
                }
                if (!candidateTarget) {
                    return AutoderefResult::NoMatch;
                }

                target = *candidateTarget;
                if (implType) {
                    *implType = *candidateImplType;
                }
                DEBUG("Deref " << ty << " into " << target);
                return AutoderefResult::Match;
            }
        }

        const HIRTypeData* TraitResolution::autoderef(const Span& sp, const HIRTypeData* ty, HIRTypeRef& tmpType) const {
            return autoderefStep(sp, ty, tmpType) == AutoderefResult::Match ? tmpType : nullptr;
        }

        unsigned int TraitResolution::autoderefFindMethod(
            const Span& sp,
            const tTraitList& traits,
            const ::std::vector<unsigned>& ivars,
            unsigned int typeIvarCount,
            const HIRTypeData* topTy,
            const RcString& methodName,
            /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, HIRPath>>& possibilities
        ) const {
            try {
                TRACE_FUNCTION_F("{" << topTy << "}." << methodName);
                unsigned int derefCount = 0;
                HIRTypeRef tmpType; // Temporary type used for handling Deref
                const auto& topTyR = this->ivars.getType(topTy);
                const auto* currentTy = topTyR;

                // Correct algorithm:
                // - Find any available method with a receiver type of `T`
                // - If no, try &T
                // - If no, try &mut T
                // - If no, try &move T
                // - If no, dereference T and try again
                auto curAccess = MethodAccess::Move; // Assume that the input value is movable
                auto collapseToMostSpecificSubtrait = [&]() {
                    if (!crate.featureEnabled("supertrait_item_shadowing") || possibilities.size() < 2) {
                        return;
                    }

                    ::std::vector<HIRSimplePath> candidateTraits;
                    candidateTraits.reserve(possibilities.size());
                    for (const auto& possibility : possibilities) {
                        const auto* path = possibility.second.mData.opt_UfcsKnown();
                        if (!path) {
                            // RFC 3624 only collapses extension-trait picks.
                            return;
                        }
                        candidateTraits.push_back(path->trait.mPath);
                    }

                    const auto selected = crate.findMostSpecificTrait(sp, candidateTraits);
                    if (selected) {
                        auto selectedPossibility = mv$(possibilities[*selected]);
                        possibilities.clear();
                        possibilities.push_back(mv$(selectedPossibility));
                    }
                };
                do {
                    const auto* ty = this->ivars.getType(currentTy);
                    auto shouldPause = [](const auto& ty) -> bool {
                        if (typeIsUnboundedInfer(ty)) {
                            DEBUG("- Ivar" << ty << ", pausing");
                            return true;
                        }
                        if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
                            DEBUG("- Unbound type path " << ty << ", pausing");
                            return true;
                        }
                        return false;
                    };
                    if (shouldPause(ty)) {
                        return ~0u;
                    }
                    if (ty->is_Borrow() && shouldPause(this->ivars.getType(ty->as_Borrow().inner))) {
                        return ~0u;
                    }
                    // TODO: Pause on Box<_>?
                    DEBUG(derefCount << ": " << ty);

                    // Non-referenced
                    if (this->findMethod(sp, traits, ivars, typeIvarCount, ty, methodName, curAccess, AutoderefBorrow::None, possibilities)) {
                        DEBUG("FOUND *{" << derefCount << "}, fcn_path = " << possibilities.back().second);
                    }

                    // Auto-ref
                    auto borrowTy = crate.types.borrow(HIRBorrowType::Shared, ty);
                    if (this->findMethod(sp, traits, ivars, typeIvarCount, borrowTy, methodName, MethodAccess::Move, AutoderefBorrow::Shared, possibilities)) {
                        DEBUG("FOUND & *{" << derefCount << "}, fcn_path = " << possibilities.back().second);
                    }
                    borrowTy = crate.types.borrow(HIRBorrowType::Unique, ty);
                    if (curAccess >= MethodAccess::Unique && this->findMethod(sp, traits, ivars, typeIvarCount, borrowTy, methodName, MethodAccess::Move, AutoderefBorrow::Unique, possibilities)) {
                        DEBUG("FOUND &mut *{" << derefCount << "}, fcn_path = " << possibilities.back().second);
                    }
                    borrowTy = crate.types.borrow(HIRBorrowType::Owned, ty);
                    if (curAccess >= MethodAccess::Move && this->findMethod(sp, traits, ivars, typeIvarCount, borrowTy, methodName, MethodAccess::Move, AutoderefBorrow::Owned, possibilities)) {
                        DEBUG("FOUND &move *{" << derefCount << "}, fcn_path = " << possibilities.back().second);
                    }
                    if (!possibilities.empty()) {
                        collapseToMostSpecificSubtrait();
                        DEBUG("FOUND " << possibilities.size() << " options: " << possibilities);
                        return derefCount;
                    }

                    // Auto-dereference
                    derefCount += 1;
                    if (const auto* typ = this->typeIsOwnedBox(sp, ty)) {
                        // `cur_access` can stay as-is (Box can be moved out of)
                        currentTy = typ;
                    } else {
                        // TODO: Update `cur_access` based on the avaliable Deref impls
                        switch (this->autoderefStep(sp, ty, tmpType)) {
                            case AutoderefResult::NoMatch:
                                currentTy = nullptr;
                                break;
                            case AutoderefResult::Match:
                                currentTy = tmpType;
                                break;
                            case AutoderefResult::Ambiguous:
                                return ~0u;
                        }
                    }
                } while (currentTy);

                // No method found, return an empty list and return 0
                assert(possibilities.empty());
                return 0;
            } catch (const TraitResolution::RecursionDetected&) {
                DEBUG("Recursion detected, deferring");
                return ~0u;
            }
        }

        ::std::ostream& operator<<(::std::ostream& os, const TraitResolution::AutoderefBorrow& x) {
            switch (x) {
                case TraitResolution::AutoderefBorrow::None:
                    os << "None";
                    break;
                case TraitResolution::AutoderefBorrow::Shared:
                    os << "Shared";
                    break;
                case TraitResolution::AutoderefBorrow::Unique:
                    os << "Unique";
                    break;
                case TraitResolution::AutoderefBorrow::Owned:
                    os << "Owned";
                    break;
            }
            return os;
        }

        ::std::ostream& operator<<(::std::ostream& os, const TraitResolution::AllowedReceivers& x) {
            switch (x) {
                case TraitResolution::AllowedReceivers::All:
                    os << "All";
                    break;
                case TraitResolution::AllowedReceivers::AnyBorrow:
                    os << "AnyBorrow";
                    break;
                case TraitResolution::AllowedReceivers::SharedBorrow:
                    os << "SharedBorrow";
                    break;
                case TraitResolution::AllowedReceivers::Value:
                    os << "Value";
                    break;
                case TraitResolution::AllowedReceivers::Box:
                    os << "Box";
                    break;
            }
            return os;
        }

        ::std::ostream& operator<<(::std::ostream& os, const TraitResolution::MethodAccess& x) {
            switch (x) {
                case TraitResolution::MethodAccess::Shared:
                    os << "Shared";
                    break;
                case TraitResolution::MethodAccess::Unique:
                    os << "Unique";
                    break;
                case TraitResolution::MethodAccess::Move:
                    os << "Move";
                    break;
            }
            return os;
        }

        // Checks that a given real receiver type matches a desired receiver type (with the correct access)
        // Returns the matched `Self` type, or nothing if there's a mismatch.
        ::std::optional<HIRTypeRef> TraitResolution::checkMethodReceiver(const Span& sp, const HIRFunction& fcn, const HIRTypeData* ty, TraitResolution::MethodAccess access) const {
            switch (fcn.receiver) {
                case HIRFunction::Receiver::Free:
                    // Free functions are never usable
                    return ::std::nullopt;
                case HIRFunction::Receiver::Value:
                    if (access >= TraitResolution::MethodAccess::Move) {
                        return this->ivars.getType(ty);
                    }
                    break;
                case HIRFunction::Receiver::BorrowOwned:
                    if (!ty->is_Borrow())
                        ;
                    else if (ty->as_Borrow().type != HIRBorrowType::Owned)
                        ;
                    else if (access < TraitResolution::MethodAccess::Move)
                        ;
                    else {
                        return this->ivars.getType(ty->as_Borrow().inner);
                    }
                    break;
                case HIRFunction::Receiver::BorrowUnique:
                    if (!ty->is_Borrow())
                        ;
                    else if (ty->as_Borrow().type != HIRBorrowType::Unique)
                        ;
                    else if (access < TraitResolution::MethodAccess::Unique)
                        ;
                    else {
                        return this->ivars.getType(ty->as_Borrow().inner);
                    }
                    break;
                case HIRFunction::Receiver::BorrowShared:
                    if (!ty->is_Borrow())
                        ;
                    else if (ty->as_Borrow().type != HIRBorrowType::Shared)
                        ;
                    else if (access < TraitResolution::MethodAccess::Shared)
                        ;
                    else {
                        return this->ivars.getType(ty->as_Borrow().inner);
                    }
                    break;
                case HIRFunction::Receiver::Custom: {
                    const auto& receiverType = fcn.mArgs.front().second;
                    ASSERT_BUG(
                        sp,
                        visitTyWith(
                            receiverType,
                            [](const HIRTypeData* v) {
                        return v->is_Generic() && v->as_Generic().isSelf();
                    }
                        ),
                        receiverType
                    );
                    // TODO: Handle custom-receiver functions
                    // - match_test_generics, if it succeeds return the matched Self
                    {
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

                        if (receiverType->matchTestGenerics(sp, ty, this->ivars.callbackResolveInfer(), getself)) {
                            ASSERT_BUG(sp, getself.detectedSelfTy, "Unable to determine receiver type when matching " << receiverType << " and " << ty);
                            return this->ivars.getType(*getself.detectedSelfTy);
                        }
                    }
                    return ::std::nullopt;
                }
                case HIRFunction::Receiver::Box:
                    if (const auto* ity = this->typeIsOwnedBox(sp, ty)) {
                        if (access < TraitResolution::MethodAccess::Move) {
                        } else {
                            return this->ivars.getType(ity);
                        }
                    }
                    break;
            }
            return ::std::nullopt;
        }

        bool TraitResolution::findMethod(const Span& sp, const tTraitList& traits, const ::std::vector<unsigned>& ivars, unsigned int typeIvarCount, const HIRTypeData* ty, const RcString& methodName, MethodAccess access, AutoderefBorrow borrowType, /* Out -> */ ::std::vector<::std::pair<AutoderefBorrow, HIRPath>>& possibilities) const {
            bool rv = false;
            TRACE_FUNCTION_FR("ty=" << ty << ", name=" << methodName << ", access=" << access, rv << " " << possibilities);
            auto cbInfer = this->ivars.callbackResolveInfer();

            auto getIvaredParams = [&](const HIRGenericParams& tpl) -> HIRPathParams {
                unsigned int nParams = tpl.types.size();
                ASSERT_BUG(sp, typeIvarCount <= ivars.size(), "Invalid method ivar split: " << typeIvarCount << " type ivars in a pool of " << ivars.size());
                ASSERT_BUG(sp, nParams <= typeIvarCount, "Not enough type ivars allocated for method: " << nParams << " needed but " << typeIvarCount << " allocated by caller\ntpl = " << tpl.fmtArgs());
                HIRPathParams traitParams;
                traitParams.types.reserve(nParams);
                for (unsigned int i = 0; i < nParams; i++) {
                    traitParams.types.push_back(crate.types.infer(ivars[i], HIRInferClass::None));
                    ASSERT_BUG(sp, this->ivars.getType(traitParams.types.back())->as_Infer().index == ivars[i], "A method selection ivar was bound");
                }
                const unsigned int nValues = tpl.values.size();
                ASSERT_BUG(sp, nValues <= ivars.size() - typeIvarCount, "Not enough value ivars allocated for method: " << nValues << " needed but " << ivars.size() - typeIvarCount << " allocated by caller\ntpl = " << tpl.fmtArgs());
                traitParams.values.reserve(nValues);
                for (unsigned int i = 0; i < nValues; i++) {
                    traitParams.values.push_back(HIRConstGeneric::make_Infer({ivars[typeIvarCount + i]}));
                }
                return traitParams;
            };

            // 1. Search for inherent methods
            // - Inherent methods are searched first.
            // TODO: Have a cache of name+receiver_type to a list of types and impls
            // e.g. `len` `&Self` = `[T]`
            DEBUG("> Inherent methods");
            this->wb.inherentMethods->find(sp, methodName, ty, this->ivars.callbackResolveInfer(), [&](const HIRTypeData* selfTy, const HIRTypeImpl& impl) {
                if (!impl.methods.at(methodName).publicity.isVisible(this->mVisPath)) {
                    // Ignore method: Not visibile
                    return;
                }
                HIRPathParams implParams;
                auto cmp = fticCheckParams(sp, HIRSimplePath(), nullptr, selfTy, impl.mParams, {}, impl.mType, implParams);
                if (cmp != HIRCompare::Unequal) {
                    DEBUG("Found `impl" << impl.mParams.fmtArgs() << " " << impl.mType << "` fn " << methodName /* << " - " << top_ty*/);
                    possibilities.push_back(::std::make_pair(borrowType, HIRPath(selfTy, methodName, {})));
                    DEBUG("++ " << possibilities.back());
                    rv = true;
                }
            });

            // TODO: Handle custom recievers by finding the bottom of a deref chain (or take the top-level reciever as an argument here?)

            // 3. Search generic bounds for a match
            // - If there is a bound on the receiver, then that bound is usable no-matter what
            DEBUG("> Bounds");
            bool foundBound = false;
            for (const auto& tb : traitBounds) {
                const auto& eType = tb.first.first;
                const auto& eTraitGp = tb.first.second;
                const auto& eTraitInfo = tb.second;

                assert(eTraitInfo.traitPtr);
                // 1. Find the named method in the trait.
                HIRGenericPath finalTraitPath;
                const HIRFunction* fcnPtr;
                if (!(fcnPtr = this->traitContainsMethod(sp, eTraitGp, *eTraitInfo.traitPtr, eType, methodName, finalTraitPath))) {
                    DEBUG("- Method '" << methodName << "' missing");
                    continue;
                }
                DEBUG("- Found trait " << finalTraitPath << " (bound)");

                // 2. Compare the receiver of the above to this type and the bound.
                if (auto selfTy = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                    // HRLs - could be some in the path from `trait_contains_method`
                    // - Lazy option, just erase whatever we find
                    struct MonomorphEraseHrls: public Monomorphiser {
                        using Monomorphiser::Monomorphiser;

                        HIRTypeRef getType(const Span& sp, const HIRGenericRef& ty) const override {
                            if (ty.group() == 3) {
                                return types.infer();
                            }
                            return types.generic(ty.name, ty.binding);
                        }

                        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& val) const override {
                            if (val.group() == 3) {
                                return HIRConstGeneric();
                            }
                            return HIRConstGeneric(val);
                        }

                    };

                    finalTraitPath = MonomorphEraseHrls(crate.types).monomorphGenericpath(sp, finalTraitPath, true);

                    // If the type is an unbounded ivar, don't check.
                    if (TU_TEST1(**selfTy, Infer, .isLit() == false)) {
                        return false;
                    }
                    // TODO: Do a fuzzy match here?
                    auto cmp = (*selfTy)->compareWithPlaceholders(sp, eType, cbInfer);
                    if (cmp == HIRCompare::Equal) {
                        // TODO: Re-monomorphise final trait using `ty`?
                        // - Could collide with legitimate uses of `Self`

                        // Found the method, return the UFCS path for it
                        possibilities.push_back(::std::make_pair(borrowType, HIRPath(HIRPath::Data::make_UfcsKnown({*selfTy, mv$(finalTraitPath), methodName, {}}))));
                        DEBUG("++ " << possibilities.back());
                        rv = true;
                        foundBound = true;
                    } else if (cmp == HIRCompare::Fuzzy) {
                        DEBUG("Fuzzy match checking bounded method - " << *selfTy << " != " << eType);

                        // Found the method, return the UFCS path for it
                        possibilities.push_back(::std::make_pair(borrowType, HIRPath(HIRPath::Data::make_UfcsKnown({*selfTy, mv$(finalTraitPath), methodName, {}}))));
                        DEBUG("++ " << possibilities.back());
                        rv = true;
                        foundBound = true;
                    } else {
                        DEBUG("> Type mismatch - " << *selfTy << " != " << eType);
                    }
                } else {
                    DEBUG("> Receiver mismatch");
                }
            }
            if (foundBound) {
                return rv;
            }

            // 2. Search the current trait (if in an impl block)
            if (mCurrentTraitPath) {
                HIRGenericPath finalTraitPath;
                const HIRFunction* fcnPtr;
                if ((fcnPtr = this->traitContainsMethod(sp, *mCurrentTraitPath, *currentTraitPtr, ty, methodName, finalTraitPath))) {
                    DEBUG("- Found trait " << finalTraitPath << " (current)");
                    if (auto selfTy = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                        // If the type is an unbounded ivar, don't check.
                        if (TU_TEST1(**selfTy, Infer, .isLit() == false)) {
                            return false;
                        }

                        // Use the set of ivars we were given to populate the trait parameters
                        const auto& trait = crate.getTraitByPath(sp, finalTraitPath.mPath);
                        auto traitParams = getIvaredParams(trait.mParams);

                        try {
                            bool crateImplFound = false;
                            // Method probing only establishes that some implementation of the
                            // trait can apply to the receiver.  The trait arguments are inference
                            // variables shared with the eventual call signature; constraining
                            // them to the first matching impl here makes impl iteration order
                            // decide calls whose arguments would otherwise disambiguate them.
                            findTraitImplsCrate(sp, finalTraitPath.mPath, nullptr, *selfTy, [&](auto impl, auto cmp) {
                                DEBUG("[find_method] " << impl << ", cmp = " << cmp);
                                crateImplFound = true;
                                return true;
                            });
                            if (crateImplFound) {
                                DEBUG("Found trait impl " << mCurrentTraitPath->mPath << traitParams << " for " << *selfTy << " (" << this->ivars.fmtType(*selfTy) << ")");
                                possibilities.push_back(::std::make_pair(borrowType, HIRPath(*selfTy, HIRGenericPath(finalTraitPath.mPath, mv$(traitParams)), methodName, {})));
                                DEBUG("++ " << possibilities.back());
                                return true;
                            } else {
                            }
                        } catch (const TraitResolution::RecursionDetected&) {
                            DEBUG("Recursion detected, deferring");
                            return false;
                        }
                    }
                }
            }

            auto getInnerType = [this, sp](const HIRTypeData* ty, ::std::function<bool(const HIRTypeData*)> cb) -> const HIRTypeData* {
                if (cb(ty)) {
                    return ty;
                } else if (ty->is_Borrow()) {
                    const auto* ity = this->ivars.getType(ty->as_Borrow().inner);
                    if (cb(ity)) {
                        return ity;
                    } else {
                        return nullptr;
                    }
                } else {
                    auto tp = this->typeIsOwnedBox(sp, ty);
                    if (tp && cb(tp)) {
                        return tp;
                    } else {
                        return nullptr;
                    }
                }
            };

            DEBUG("> Special cases");
            // 4. If the type is a trait object, search for methods on that trait object
            // - NOTE: This isnt mutually exclusive with the below set (an inherent impl of `(Trait)` is valid)
            if (const auto* ityp = getInnerType(ty, [](const auto& t) {
                return t->is_TraitObject();
            })) {
                const auto& e = ityp->as_TraitObject();
                const auto& trait = this->crate.getTraitByPath(sp, e.mTrait.mPath.mPath);

                bool foundTraitObject = false;
                auto addTraitObjectMethod = [&](const HIRFunction& fcn, HIRGenericPath finalTraitPath) {
                    DEBUG("- Found trait " << finalTraitPath << " (trait object)");
                    // - If the receiver is valid, then it's correct (no need to check the type again)
                    if (auto selfTyP = checkMethodReceiver(sp, fcn, ty, access)) {
                        possibilities.push_back(::std::make_pair(borrowType, HIRPath(*selfTyP, mv$(finalTraitPath), methodName, {})));
                        DEBUG("++ " << possibilities.back());
                        rv = true;
                        foundTraitObject = true;
                    }
                };

                const HIRFunction* fcnPtr = nullptr;
                if (traitContainsMethodInner(trait, methodName, fcnPtr)) {
                    assert(fcnPtr);
                    addTraitObjectMethod(*fcnPtr, e.mTrait.mPath.clone());
                } else {
                    const auto selfTy = crate.types.self();
                    auto monomorphCb = MonomorphStatePtr(crate.types, selfTy, &e.mTrait.mPath.mParams, nullptr);
                    for (const auto& st : trait.allParentTraits) {
                        fcnPtr = nullptr;
                        if (!traitContainsMethodInner(*st.traitPtr, methodName, fcnPtr)) {
                            continue;
                        }
                        assert(fcnPtr);
                        auto finalTraitPath = HIRGenericPath(st.mPath.mPath, monomorphCb.monomorphPathParams(sp, st.mPath.mParams, false));
                        addTraitObjectMethod(*fcnPtr, std::move(finalTraitPath));
                    }
                }

                // If the method was found on the trait object, prefer that over all others.
                if (foundTraitObject) {
                    return rv;
                }
            }

            // 5. Mutually exclusive searches
            // - Erased type - `impl Trait`
            if (const auto* ityp = getInnerType(ty, [](const auto& t) {
                return t->is_ErasedType();
            })) {
                const auto& e = ityp->as_ErasedType();
                for (const auto& traitPath : e.traits) {
                    const auto& trait = this->crate.getTraitByPath(sp, traitPath.mPath.mPath);

                    HIRGenericPath finalTraitPath;
                    if (const auto* fcnPtr = this->traitContainsMethod(sp, traitPath.mPath, trait, crate.types.self(), methodName, finalTraitPath)) {
                        DEBUG("- Found trait " << finalTraitPath << " (erased type)");

                        if (auto selfTyP = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                            possibilities.push_back(::std::make_pair(borrowType, HIRPath(*selfTyP, mv$(finalTraitPath), methodName, {})));
                            DEBUG("++ " << possibilities.back());
                            rv = true;
                        }
                    }
                }
            }
            // Generics: Nothing except the bounds (Which have already been checked)
            else if (getInnerType(ty, [](const auto& t) {
                return t->is_Generic();
            })) {
            }
            // UfcsKnown paths: Can have trait bounds added by the definer
            else if (const auto* ityp = getInnerType(ty, [](const auto& t) {
                return t->is_Path() && t->as_Path().path.mData.is_UfcsKnown();
            })) {
                const auto& e = ityp->as_Path().path.mData.as_UfcsKnown();
                DEBUG("UfcsKnown - Search associated type bounds in trait - " << e.trait);

                // UFCS known - Assuming that it's reached the maximum resolvable level (i.e. a type within is generic), search for trait bounds on the type

                // `Self` = `*.type`
                // `/*I:#*/` := `e.trait.m_params`
                auto monomorphCb = MonomorphStatePtr(crate.types, e.type, &e.trait.mParams, &e.params);

                const auto& trait = this->crate.getTraitByPath(sp, e.trait.mPath);
                const auto& assocTy = trait.types.at(e.item);
                // NOTE: The bounds here have 'Self' = the type
                for (const auto& bound : assocTy.traitBounds) {
                    ASSERT_BUG(sp, bound.traitPtr, "Pointer to trait " << bound.mPath << " not set in " << e.trait.mPath);
                    HIRGenericPath finalTraitPath;

                    auto tySelf = crate.types.path(HIRPath(crate.types.self(), bound.mPath.clone(), e.item), HIRTypePathBinding::make_Opaque({}));
                    if (const auto* fcnPtr = this->traitContainsMethod(sp, bound.mPath, *bound.traitPtr, tySelf, methodName, finalTraitPath)) {
                        DEBUG("- Found trait " << finalTraitPath << " (UFCS Known, aty bounds)");

                        if (auto selfTyP = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                            if (*selfTyP == ityp) {
                                auto ppHrb = HIRPathParams();
                                monomorphCb.ppHrb = &ppHrb;
                                finalTraitPath = monomorphCb.monomorphGenericpath(sp, finalTraitPath, false);
                                DEBUG("- Monomorph to " << finalTraitPath);

                                // Found the method, return the UFCS path for it
                                possibilities.push_back(::std::make_pair(borrowType, HIRPath(*selfTyP, mv$(finalTraitPath), methodName, {})));
                                DEBUG("++ " << possibilities.back());
                                rv = true;
                            }
                        }
                    }
                }

                // Search `<Self as Trait>::Name` bounds on the trait itself
                for (const auto& bound : trait.mParams.bounds) {
                    if (!bound.is_TraitBound()) {
                        continue;
                    }
                    const auto& be = bound.as_TraitBound();

                    if (!be.type->is_Path()) {
                        continue;
                    }
                    if (!be.type->as_Path().binding.is_Opaque()) {
                        continue;
                    }

                    const auto& beTypePe = be.type->as_Path().path.mData.as_UfcsKnown();
                    if (beTypePe.type != crate.types.self()) {
                        continue;
                    }
                    if (beTypePe.trait.mPath != e.trait.mPath) {
                        continue;
                    }
                    if (beTypePe.item != e.item) {
                        continue;
                    }

                    // Found such a bound, now to test if it is useful

                    HIRGenericPath finalTraitPath;
                    if (const auto* fcnPtr = this->traitContainsMethod(sp, be.trait.mPath, *be.trait.traitPtr, crate.types.self(), methodName, finalTraitPath)) {
                        DEBUG("- Found trait " << finalTraitPath << " (UFCS Known, trait bounds)");

                        if (auto selfTyP = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                            if (*selfTyP == ityp) {
                                if (monomorphisePathparamsNeeded(finalTraitPath.mParams)) {
                                    finalTraitPath.mParams = monomorphCb.monomorphPathParams(sp, finalTraitPath.mParams, false);
                                    DEBUG("- Monomorph to " << finalTraitPath);
                                }

                                // Found the method, return the UFCS path for it
                                possibilities.push_back(::std::make_pair(borrowType, HIRPath(*selfTyP, mv$(finalTraitPath), methodName, {})));
                                DEBUG("++ " << possibilities.back());
                                rv = true;
                            }
                        }
                    }
                }
            } else {
            }

            // 6. Search for trait methods (using currently in-scope traits)
            DEBUG("> Trait methods");
            for (const auto& traitRef : ::reverse(traits)) {
                if (traitRef.first == nullptr) {
                    break;
                }

                HIRGenericPath finalTraitPath;
                const HIRFunction* fcnPtr;
                if (!(fcnPtr = this->traitContainsMethod(sp, *traitRef.first, *traitRef.second, crate.types.self(), methodName, finalTraitPath))) {
                    continue;
                }
                DEBUG("- Found trait " << finalTraitPath << " (in scope)");

                if (auto selfTyP = checkMethodReceiver(sp, *fcnPtr, ty, access)) {
                    const auto& selfTy = *selfTyP;
                    DEBUG("Search for impl of " << *traitRef.first << " for " << selfTy);

                    // Use the set of ivars we were given to populate the trait parameters
                    HIRPathParams traitParams = getIvaredParams(traitRef.second->mParams);

                    // TODO: Re-monomorphise the trait path!

                    bool magicFound = false;
                    bool crateImplFound = false;

                    crateImplFound = findTraitImplsMagic(sp, *traitRef.first, traitParams, selfTy, [&](auto impl, auto cmp) {
                        return true;
                    });

                    // NOTE: This just detects the presence of a trait impl, not the specifics
                    try {
                        // Keep this an existential probe over the trait arguments.  They are
                        // committed only after the method signature has constrained the shared
                        // inference variables (matching rustc's probe/confirm split).
                        findTraitImplsCrate(sp, *traitRef.first, nullptr, selfTy, [&](auto impl, auto cmp) {
                            DEBUG("[find_method] " << impl << ", cmp = " << cmp);
                            magicFound = true;
                            crateImplFound = true;
                            return true;
                        });
                    } catch (const TraitResolution::RecursionDetected&) {
                        DEBUG("Recursion detected, assuming good");
                        magicFound = true;
                        crateImplFound = true;
                    }
                    if (crateImplFound) {
                        DEBUG("Found trait impl " << *traitRef.first << traitParams << " for " << selfTy << " (" << this->ivars.fmtType(selfTy) << ")");
                        possibilities.push_back(::std::make_pair(borrowType, HIRPath(selfTy, HIRGenericPath(*traitRef.first, mv$(traitParams)), methodName, {})));
                        DEBUG("++ " << possibilities.back());
                        rv = true;
                    }
                } else {
                    DEBUG("> Incorrect receiver");
                }
            }

            return rv;
        }

        unsigned int TraitResolution::autoderefFindField(const Span& sp, const HIRTypeData* topTy, const RcString& fieldName, /* Out -> */ HIRTypeRef& fieldType) const {
            unsigned int derefCount = 0;
            HIRTypeRef tmpType; // Temporary type used for handling Deref
            const auto* currentTy = topTy;
            if (const auto* e = this->ivars.getType(topTy)->opt_Borrow()) {
                currentTy = e->inner;
                derefCount += 1;
            }

            do {
                const auto& ty = this->ivars.getType(currentTy);
                if (ty->is_Infer()) {
                    return ~0u;
                }
                if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
                    return ~0u;
                }

                if (this->findField(sp, ty, fieldName, fieldType)) {
                    return derefCount;
                }

                // 3. Dereference and try again
                derefCount += 1;
                currentTy = this->autoderef(sp, ty, tmpType);
            } while (currentTy);

            if (/*const auto* e =*/this->ivars.getType(topTy)->opt_Borrow()) {
                const auto& ty = this->ivars.getType(topTy);

                if (findField(sp, ty, fieldName, fieldType)) {
                    return 0;
                }
            }

            // Dereference failed! This is a hard error (hitting _ is checked above and returns ~0)
            this->ivars.dump();
            TODO(sp, "Error when no field could be found, but type is known - (: " << topTy << ")." << fieldName);
        }

        bool TraitResolution::findField(const Span& sp, const HIRTypeData* ty, const RcString& name, /* Out -> */ HIRTypeRef& fieldTy) const {
            if (const auto* e = ty->opt_Path()) {
        TU_MATCH_HDRA( (e->binding), {)
        TU_ARMA(Unbound, be) {
                // Wut?
                TODO(sp, "Handle TypePathBinding::Unbound - " << ty);
            }
            TU_ARMA(Opaque, be) {
                // Ignore, no fields on an opaque
            }
            TU_ARMA(Struct, be) {
                // Has fields!
                const auto& str = *be;
                const auto& params = e->path.mData.as_Generic().mParams;
                auto monomorph = MonomorphStatePtr(crate.types, ty, &params, nullptr);
            TU_MATCH_HDRA( (str.mData), {)
            TU_ARMA(Unit, se) {
                        // No fields on a unit struct
                    }
                    TU_ARMA(Tuple, se) {
                        for (unsigned int i = 0; i < se.size(); i++) {
                            DEBUG(i << ": " << se[i].publicity << ", " << this->mVisPath << " : " << se[i].ent);
                            if (se[i].publicity.isVisible(this->mVisPath) && FMT(i) == name) {
                                fieldTy = monomorph.monomorphType(sp, se[i].ent);
                                return true;
                            }
                        }
                    }
                    TU_ARMA(Named, se) {
                        for (const auto& fld : se) {
                            DEBUG(fld.name << ": " << fld.vis << ", " << this->mVisPath << " : " << fld.ty);
                            if (fld.vis.isVisible(this->mVisPath) && fld.name == name) {
                                fieldTy = monomorph.monomorphType(sp, fld.ty);
                                return true;
                            }
                        }
                    }
            }
            }
            TU_ARMA(Enum, be) {
                // No fields on enums either
            }
            TU_ARMA(ExternType, be) {
                // No fields on extern types
            }
            TU_ARMA(Union, be) {
                const auto& unm = *be;
                const auto& params = e->path.mData.as_Generic().mParams;
                auto monomorph = MonomorphStatePtr(crate.types, ty, &params, nullptr);

                for (const auto& fld : unm.mVariants) {
                    if (fld.vis.isVisible(this->mVisPath) && fld.name == name) {
                        fieldTy = monomorph.monomorphType(sp, fld.ty);
                        return true;
                    }
                }
            }
        }
            } else if (const auto* e = ty->opt_Tuple()) {
                for (unsigned int i = 0; i < e->size(); i++) {
                    if (FMT(i) == name) {
                        fieldTy = (*e)[i];
                        return true;
                    }
                }
            } else {
            }
            return false;
        }

        HMTypeInferrence::FmtType::FmtType(const HMTypeInferrence& ctxt, const HIRTypeData* ty)
            : ctxt(ctxt)
            , ty(ty)
        {
        }

        HMTypeInferrence::FmtPP::FmtPP(const HMTypeInferrence& ctxt, const HIRPathParams& pps)
            : ctxt(ctxt)
            , pps(pps)
        {
        }

        // Null only when alias != ~0

        HMTypeInferrence::IVar::IVar(HIRTypeRef type)
            : alias(~0u)
            , type(type)
        {
        }

        HMTypeInferrence::IVarValue::IVarValue()
            : alias(~0u)
            , val(new HIRConstGeneric())
        {
        }

        HMTypeInferrence::HMTypeInferrence(HIRTypeInterner& types)
            : types(types)
            , hasChanged(false)
        {
        }

        bool HMTypeInferrence::takeChanged() {
            bool rv = hasChanged;
            hasChanged = false;
            return rv;
        }

        void HMTypeInferrence::markChange() {
            if (!hasChanged) {
                DEBUG("- CHANGE");
                hasChanged = true;
            }
        }

        HMTypeInferrence::ResolvePlaceholders::ResolvePlaceholders(const HMTypeInferrence& parent)
            : parent(parent)
        {
        }

        TraitResolution::LegacyTraitGoal::LegacyTraitGoal(const HIRSimplePath& trait, const HIRPathParams& params, bool hasParams, const HIRTypeData* type)
            : trait(trait.clone())
            , params(params.clone())
            , type(type)
            , hasParams(hasParams)
        {
        }

        bool TraitResolution::LegacyTraitGoal::matches(const HIRSimplePath& otherTrait, const HIRPathParams& otherParams, bool otherHasParams, const HIRTypeData* otherType) const {
            return trait == otherTrait && hasParams == otherHasParams && (!hasParams || params == otherParams) && type == otherType;
        }

        /// Expand any located associated types in the input, operating in-place and returning the result
        HIRTypeRef TraitResolution::expandAssociatedTypes(const Span& sp, HIRTypeRef input) const {
            expandAssociatedTypesInplace(sp, input, LList<const HIRTypeData*>());
            return input;
        }

        const HIRTypeData* TraitResolution::expandAssociatedTypes(const Span& sp, const HIRTypeData* input, HIRTypeRef& tmp) const {
            if (this->hasAssociatedType(input)) {
                return (tmp = this->expandAssociatedTypes(sp, input));
            } else {
                return input;
            }
        }

        void TraitResolution::expandAssociatedTypesParams(const Span& sp, HIRPathParams& params) const {
            for (auto& type : params.types) {
                if (this->hasAssociatedType(type)) {
                    type = this->expandAssociatedTypes(sp, type);
                }
            }
        }

        bool typeIsUnboundedInfer(const HIRTypeData* ty) {
            if (const auto* te = ty->opt_Infer()) {
                switch (te->tyClass) {
                    case HIRInferClass::Integer:
                        return false;
                    case HIRInferClass::Float:
                        return false;
                    case HIRInferClass::None:
                        return true;
                }
            }
            return false;
        }

        ::std::ostream& operator<<(::std::ostream& os, const HMTypeInferrence::FmtType& x) {
            x.ctxt.printType(os, x.ty);
            return os;
        }

        ::std::ostream& operator<<(::std::ostream& os, const HMTypeInferrence::FmtPP& x) {
            x.ctxt.printPathparams(os, x.pps);
            return os;
        }

        const HIRTypeData* HMTypeInferrence::ResolvePlaceholders::getType(const Span& sp, const HIRTypeData* ty) const {
            if (ty->is_Infer()) {
                return parent.getType(ty);
            } else {
                return ty;
            }
        }

        const HIRConstGeneric& HMTypeInferrence::ResolvePlaceholders::getVal(const Span& sp, const HIRConstGeneric& v) const {
            if (v.is_Infer()) {
                return parent.getValue(v);
            } else {
                return v;
            }
        }
