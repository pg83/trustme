#include "hir_typeck_common.h"

#include "hir_path.h"
#include "wire_board.h"
#include "trans_target.h"
#include "hir_conv_main_bindings.h"

#include <std/alg/defer.h>

namespace {
    template <typename I>
    struct WConst {
        typedef const I T;
    };

    template <typename I>
    struct WMut {
        typedef I T;
    };

    template <template <typename> typename W>
    struct TyVisitor {
        const LList<const HIRTypeData*>* curRecurseStack = nullptr;

        virtual typename W<HIRTypeData>::T& getTyData(const HIRTypeData* ty) const = 0;

        virtual bool visitConstGeneric(typename W<HIRConstGeneric>::T&);

        virtual bool visitPathParams(typename W<HIRPathParams>::T& tpl);

        virtual bool visitTraitPath(typename W<HIRTraitPath>::T& tpl);

        virtual bool visitPath(typename W<HIRPath>::T& path);

        virtual bool visitType(const HIRTypeData* ty);
    };

    struct TyVisitorCbConst: TyVisitor<WConst> {
        HIRTypeVisitorCallback& callback;

        explicit TyVisitorCbConst(HIRTypeVisitorCallback& callback);

        const HIRTypeData& getTyData(const HIRTypeData* ty) const override;

        bool visitType(const HIRTypeData* ty) override;
    };

    struct TyVisitorMonomorphNeeded: TyVisitor<WConst> {
        const HIRTypeData& getTyData(const HIRTypeData* ty) const override;

        bool visitPathParams(const HIRPathParams& pp) override;

        bool visitType(const HIRTypeData* ty) override;
    };

    struct CloneTyWithMonomorph: Monomorphiser {
        HIRTypeCloneCallback& callback;

        CloneTyWithMonomorph(HIRTypeInterner& types, HIRTypeCloneCallback& callback);

        HIRTypeRef getType(const Span& sp, const HIRGenericRef& g) const override;

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override;

        HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* ty, bool allowInfer = true) const override;
    };

    struct TyVisitorGenericGroup final: TyVisitor<WConst> {
        HIRGenericGroup group;

        explicit TyVisitorGenericGroup(HIRGenericGroup group);

        const HIRTypeData& getTyData(const HIRTypeData* ty) const override;

        bool visitConstGeneric(const HIRConstGeneric& value) override;

        bool visitType(const HIRTypeData* ty) override;
    };

    struct TyRewriter {
        HIRTypeInterner& types;
        HIRTypeRewriteCallback& callback;
        std::vector<HIRTypeRef> stack;

        bool rewritePathParams(HIRPathParams& params);

        bool rewriteTraitPath(HIRTraitPath& trait);

        bool rewritePath(HIRPath& path);

        bool rewriteType(HIRTypeRef& type);
    };
}

bool visitTyWithCb(const HIRTypeData* ty, HIRTypeVisitorCallback& callback) {
    TyVisitorCbConst v(callback);
    return v.visitType(ty);
}

bool visitTraitPathTysWithCb(const HIRTraitPath& path, HIRTypeVisitorCallback& callback) {
    TyVisitorCbConst v(callback);
    return v.visitTraitPath(path);
}

bool visitPathTysWithCb(const HIRPath& path, HIRTypeVisitorCallback& callback) {
    TyVisitorCbConst v(callback);
    return v.visitPath(path);
}

bool typeContainsGenericGroup(const HIRTypeData* type, HIRGenericGroup group) {
    TyVisitorGenericGroup visitor(group);
    return visitor.visitType(type);
}

bool pathParamsContainGenericGroup(const HIRPathParams& params, HIRGenericGroup group) {
    TyVisitorGenericGroup visitor(group);
    return visitor.visitPathParams(params);
}

bool rewriteTyWithCb(HIRTypeInterner& types, HIRTypeRef& ty, HIRTypeRewriteCallback& callback) {
    TyRewriter rewriter{types, callback, {}};
    return rewriter.rewriteType(ty);
}

bool rewritePathTysWithCb(HIRTypeInterner& types, HIRPath& path, HIRTypeRewriteCallback& callback) {
    TyRewriter rewriter{types, callback, {}};
    return rewriter.rewritePath(path);
}

bool monomorphisePathparamsNeeded(const HIRPathParams& tpl) {
    TyVisitorMonomorphNeeded v{};
    return v.visitPathParams(tpl);
}

bool monomorphiseTraitpathNeeded(const HIRTraitPath& tpl) {
    TyVisitorMonomorphNeeded v{};
    return v.visitTraitPath(tpl);
}

bool monomorphisePathNeeded(const HIRPath& tpl) {
    TyVisitorMonomorphNeeded v{};
    return v.visitPath(tpl);
}

bool monomorphiseTypeNeeded(const HIRTypeData* tpl) {
    return tpl->needsMonomorphisation();
}

HIRTypeRef Monomorphiser::monomorphType(const Span& sp, const HIRTypeData* tpl, bool allowInfer /*=true*/) const {
    switch ((*tpl).tag()) {
        case HIRTypeData::TAG_Infer: {
            ASSERT_BUG(sp, allowInfer, "Unexpected ivar seen - " << tpl);
            return tpl;
        }
        case HIRTypeData::TAG_Diverge: {
            return tpl;
        }
        case HIRTypeData::TAG_Primitive: {
            return tpl;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*tpl).as_Path();
            auto binding = e.binding.is_Opaque() ? HIRTypePathBinding() : e.binding.clone();
            return types.intern(HIRTypeData::make_Path({this->monomorphPath(sp, e.path, allowInfer), mv$(binding)}));
        }
        case HIRTypeData::TAG_Generic: {
            auto& e = (*tpl).as_Generic();
            return this->getType(sp, e);
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = (*tpl).as_TraitObject();
            HIRTypeData::Data_TraitObject to;
            to.lifetimeIdentity = e.lifetimeIdentity;
            to.lifetimeIdentityHasFree = e.lifetimeIdentityHasFree;
            {
                to.trait = this->monomorphTraitpath(sp, e.trait, allowInfer);
                for (const auto& trait : e.markers) {
                    to.markers.push_back(this->monomorphGenericpath(sp, trait, allowInfer));
                }
            }
            return types.intern(HIRTypeData::make_TraitObject(mv$(to)));
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = (*tpl).as_ErasedType();
            std::vector<HIRTraitPath> traits;
            traits.reserve(e.traits.size());
            for (const auto& trait : e.traits) {
                traits.push_back(this->monomorphTraitpath(sp, trait, allowInfer));
            }

            TypeDataErasedTypeInner inner;
            switch (e.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& ee = e.inner.as_Fcn();
                    inner = TypeDataErasedTypeInner::Data_Fcn{this->monomorphPath(sp, ee.origin, allowInfer), ee.index};
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    auto& ee = e.inner.as_Alias();
                    inner = TypeDataErasedTypeInner::Data_Alias{this->monomorphPathParams(sp, ee.params, allowInfer), ee.inner};
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& ee = e.inner.as_Known();
                    inner = this->monomorphType(sp, ee, allowInfer);
                    break;
                }
            }

            return types.intern(HIRTypeData::make_ErasedType(HIRTypeData::Data_ErasedType{e.isSized, mv$(traits), mv$(inner), this->monomorphPathParams(sp, e.use, allowInfer), e.usePresent}));
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*tpl).as_Array();
            return types.intern(HIRTypeData::make_Array({this->monomorphType(sp, e.inner, allowInfer), this->monomorphArraysize(sp, e.size)}));
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = (*tpl).as_Slice();
            return types.slice(this->monomorphType(sp, e.inner, allowInfer));
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*tpl).as_Pattern();
            HIRTypePattern pattern;
            pattern.alternatives.reserve(e.pattern.alternatives.size());
            for (const auto& range : e.pattern.alternatives) {
                HIRTypePatternRange out{
                    range.hasStart,
                    range.hasStart ? this->monomorphConstgeneric(sp, range.start, allowInfer) : HIRConstGeneric(),
                    range.hasEnd,
                    range.hasEnd ? this->monomorphConstgeneric(sp, range.end, allowInfer) : HIRConstGeneric(),
                    range.endInclusive,
                };
                pattern.alternatives.push_back(mv$(out));
            }
            return types.intern(HIRTypeData::make_Pattern({this->monomorphType(sp, e.inner, allowInfer), mv$(pattern)}));
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*tpl).as_Tuple();
            std::vector<HIRTypeRef> types;
            for (const auto& ty : e) {
                types.push_back(this->monomorphType(sp, ty, allowInfer));
            }
            return this->types.tuple(mv$(types));
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*tpl).as_Borrow();
            return types.borrow(e.type, this->monomorphType(sp, e.inner, allowInfer));
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = (*tpl).as_Pointer();
            return types.pointer(e.type, this->monomorphType(sp, e.inner, allowInfer));
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& e = (*tpl).as_NamedFunction();
            return types.intern(HIRTypeData::make_NamedFunction(HIRTypeData::Data_NamedFunction{this->monomorphPath(sp, e.path, allowInfer), e.def.clone()}));
        }
        case HIRTypeData::TAG_Function: {
            auto& e = (*tpl).as_Function();
            HIRTypeDataFunctionPointer ft;
            ft.isUnsafe = e.isUnsafe;
            ft.isVariadic = e.isVariadic;
            ft.trackCaller = e.trackCaller;
            ft.abi = e.abi;
            ft.lifetimeIdentity = e.lifetimeIdentity;
            ft.lifetimeIdentityHasFree = e.lifetimeIdentityHasFree;
            ft.rettype = this->monomorphType(sp, e.rettype, allowInfer);
            for (const auto& arg : e.argTypes) {
                ft.argTypes.push_back(this->monomorphType(sp, arg, allowInfer));
            }
            return types.function(mv$(ft));
        }
        case HIRTypeData::TAG_NodeType: {
            return tpl;
        }
    }
    UNREACHABLE();
}

HIRPath Monomorphiser::monomorphPath(const Span& sp, const HIRPath& tpl, bool allowInfer /*=true*/) const {
    switch (tpl.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e2 = tpl.data.as_Generic();
            return HIRPath(this->monomorphGenericpath(sp, e2, allowInfer));
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e2 = tpl.data.as_UfcsKnown();
            auto rv = HIRPath(HIRPath::Data::make_UfcsKnown({this->monomorphType(sp, e2.type, allowInfer), this->monomorphGenericpath(sp, e2.trait, allowInfer), e2.item, this->monomorphPathParams(sp, e2.params, allowInfer)}));
            return rv;
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& e2 = tpl.data.as_UfcsUnknown();
            return HIRPath::Data::make_UfcsUnknown({this->monomorphType(sp, e2.type, allowInfer), e2.item, this->monomorphPathParams(sp, e2.params, allowInfer)});
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& e2 = tpl.data.as_UfcsInherent();
            return HIRPath::Data::make_UfcsInherent({this->monomorphType(sp, e2.type, allowInfer), e2.item, this->monomorphPathParams(sp, e2.params, allowInfer), this->monomorphPathParams(sp, e2.implParams, allowInfer)});
        }
    }
    UNREACHABLE();
}

HIRTraitPath Monomorphiser::monomorphTraitpath(const Span& sp, const HIRTraitPath& tpl, bool allowInfer) const {
    HIRTraitPath rv{this->monomorphGenericpath(sp, tpl.path, allowInfer), {}, {}, tpl.traitPtr, tpl.constness};

    for (const auto& assoc : tpl.typeBounds) {
        rv.typeBounds.insert(std::make_pair(assoc.first, this->monomorphTpAtyEqual(sp, assoc.second, allowInfer)));
    }
    for (const auto& assoc : tpl.traitBounds) {
        auto v = HIRTraitPath::AtyBound{this->monomorphGenericpath(sp, assoc.second.sourceTrait, allowInfer), this->monomorphPathParams(sp, assoc.second.atyParams, allowInfer), {}};
        for (const auto& trait : assoc.second.traits) {
            v.traits.push_back(monomorphTraitpath(sp, trait, allowInfer));
        }
        rv.traitBounds.insert(std::make_pair(assoc.first, std::move(v)));
    }

    return rv;
}

HIRTraitPath::AtyEqual Monomorphiser::monomorphTpAtyEqual(const Span& sp, const HIRTraitPath::AtyEqual& tpl, bool allowInfer) const {
    return HIRTraitPath::AtyEqual{this->monomorphGenericpath(sp, tpl.sourceTrait, allowInfer), this->monomorphPathParams(sp, tpl.atyParams, allowInfer), this->monomorphType(sp, tpl.type, allowInfer)};
}

HIRConstGeneric Monomorphiser::monomorphConstgeneric(const Span& sp, const HIRConstGeneric& val, bool allowInfer) const {
    if (const auto* ge = val.opt_Generic()) {
        return this->getValue(sp, *ge);
    } else if (const auto* ge = val.opt_Unevaluated()) {
        auto rv = HIRConstGeneric(std::make_unique<HIRConstGenericUnevaluated>((*ge)->monomorph(sp, *this, true)));
        // TODO: Evaluate this constant (if possible), but that requires knowing the target type :/
        return rv;
    } else {
        return val.clone();
    }
}

HIRPathParams Monomorphiser::monomorphPathParams(const Span& sp, const HIRPathParams& tpl, bool allowInfer) const {
    HIRPathParams rv;

    rv.types.reserve(tpl.types.size());
    for (const auto& ty : tpl.types) {
        rv.types.push_back(this->monomorphType(sp, ty, allowInfer));
    }

    rv.values.reserve(tpl.values.size());
    for (const auto& val : tpl.values) {
        rv.values.push_back(monomorphConstgeneric(sp, val, allowInfer));
    }

    return rv;
}

HIRGenericPath Monomorphiser::monomorphGenericpath(const Span& sp, const HIRGenericPath& tpl, bool allowInfer) const {
    return HIRGenericPath(tpl.path, this->monomorphPathParams(sp, tpl.params, allowInfer));
}

HIRArraySize Monomorphiser::monomorphArraysize(const Span& sp, const HIRArraySize& tpl) const {
    if (auto* se = tpl.opt_Unevaluated()) {
        HIRArraySize sz;
        if (se->is_Generic()) {
            sz = this->getValue(sp, se->as_Generic());
        } else if (se->is_Unevaluated()) {
            sz = HIRConstGeneric(std::make_unique<HIRConstGenericUnevaluated>(se->as_Unevaluated()->monomorph(sp, *this, true)));
        } else {
            sz = this->monomorphConstgeneric(sp, *se, true);
        }
        se = sz.opt_Unevaluated();
        assert(se);

        if (se->is_Unevaluated()) {
            if (this->constevalWb) {
                ConvertHIRConstantEvaluateConstGeneric(sp, *this->constevalWb, *this->constevalWb->crate, types.primitive(HIRCoreType::Usize), sz.as_Unevaluated());
            } else {
            }
        }

        if (const auto* e = se->opt_Evaluated()) {
            return (*e)->readUsize(0);
        }
        return sz;
    } else {
        return tpl.clone();
    }
}

HIRPathParams clonePathParamsWithCb(HIRTypeInterner& types, const Span& sp, const HIRPathParams& tpl, HIRTypeCloneCallback& callback) {
    HIRPathParams rv;
    rv.types.reserve(tpl.types.size());
    for (const auto& ty : tpl.types) {
        rv.types.push_back(cloneTyWithCb(types, sp, ty, callback));
    }
    for (const auto& v : tpl.values) {
        rv.values.push_back(v.clone());
    }
    return rv;
}

HIRTypeRef cloneTyWithCb(HIRTypeInterner& types, const Span& sp, const HIRTypeData* tpl, HIRTypeCloneCallback& callback) {
    CloneTyWithMonomorph m(types, callback);
    return m.monomorphType(sp, tpl, true);
}

HIRTypeRef MonomorphiserPP::getType(const Span& sp, const HIRGenericRef& ty) const /*override*/
{
    if (ty.isSelf()) {
        if (const auto* s = this->getSelfType()) {
            return s;
        } else {
            BUG(sp, "Unexpected Self");
        }
    } else {
        switch (ty.group()) {
            case 0:
                if (const auto* p = this->getImplParams()) {
                    ASSERT_BUG(sp, ty.idx() < p->types.size(), "Type param " << ty << " out of range for (max " << p->types.size() << ")");
                    return p->types[ty.idx()];
                } else {
                    BUG(sp, "Impl parameters were not expected (got " << ty << ")");
                }
                break;
            case 1:
                if (const auto* p = this->getMethodParams()) {
                    ASSERT_BUG(sp, ty.idx() < p->types.size(), "Type param " << ty << " out of range for (max " << p->types.size() << ")");
                    return p->types[ty.idx()];
                } else {
                    BUG(sp, "Method parameters were not expected (got " << ty << ")");
                }
                break;
            case GENERICHrtb:
                if (const auto* p = this->getHrbParams()) {
                    ASSERT_BUG(sp, ty.idx() < p->types.size(), "HRTB type param " << ty << " out of range for (max " << p->types.size() << ")");
                    return p->types[ty.idx()];
                }
                return types.generic(ty.name, ty.binding);
            default:
                BUG(sp, "Unexpected type param " << ty);
        }
    }
}

HIRConstGeneric MonomorphiserPP::getValue(const Span& sp, const HIRGenericRef& val) const /*override*/
{
    switch (val.group()) {
        case 0:
            if (const auto* p = this->getImplParams()) {
                ASSERT_BUG(sp, val.idx() < p->values.size(), "Value param " << val << " out of range for (max " << p->values.size() << ")");
                return p->values[val.idx()].clone();
            } else {
                BUG(sp, "Impl parameters were not expected (got " << val << ")");
            }
            break;
        case 1:
            if (const auto* p = this->getMethodParams()) {
                ASSERT_BUG(sp, val.idx() < p->values.size(), "Value param " << val << " out of range for (max " << p->values.size() << ")");
                return p->values[val.idx()].clone();
            } else {
                BUG(sp, "Method parameters were not expected (got " << val << ")");
            }
            break;
        case GENERICHrtb:
            if (const auto* p = this->getHrbParams()) {
                ASSERT_BUG(sp, val.idx() < p->values.size(), "HRTB value param " << val << " out of range for (max " << p->values.size() << ")");
                return p->values[val.idx()].clone();
            }
            return val;
        default:
            BUG(sp, "Unexpected value param " << val);
    }
}

std::ostream& operator<<(std::ostream& os, const MonomorphState& ms) {
    os << "MonomorphState {";
    if (ms.selfTy != HIRTypeRef()) {
        os << " self=" << ms.selfTy;
    }
    if (ms.ppImpl) {
        os << " I=" << *ms.ppImpl;
    }
    if (ms.ppMethod) {
        os << " M=" << *ms.ppMethod;
    }
    os << " }";
    return os;
}

bool typeClassPrimitiveCompatible(HIRInferClass ic, HIRCoreType ct) {
    switch (ic) {
        case HIRInferClass::None:
            return true;
        case HIRInferClass::Float:
            switch (ct) {
                case HIRCoreType::F16:
                case HIRCoreType::F32:
                case HIRCoreType::F64:
                case HIRCoreType::F128:
                    return true;
                default:
                    return false;
            }
        case HIRInferClass::Integer:
            switch (ct) {
                case HIRCoreType::I8:
                case HIRCoreType::U8:
                case HIRCoreType::I16:
                case HIRCoreType::U16:
                case HIRCoreType::I32:
                case HIRCoreType::U32:
                case HIRCoreType::I64:
                case HIRCoreType::U64:
                case HIRCoreType::I128:
                case HIRCoreType::U128:
                case HIRCoreType::Isize:
                case HIRCoreType::Usize:
                    return true;
                default:
                    return false;
            }
    }
    return false;
}

void checkTypeClassPrimitive(const Span& sp, const HIRTypeData* type, HIRInferClass ic, HIRCoreType ct) {
    if (!typeClassPrimitiveCompatible(ic, ct)) {
        if (ic == HIRInferClass::Float) {
            ERROR(sp, E0000, "Type unificiation of float literal with non-float - " << type);
        }
        ERROR(sp, E0000, "Type unificiation of integer literal with non-integer - " << type);
    }
}

bool primitiveOperatorHasBuiltin(TypeckPrimitiveOperator op, const HIRTypeData* left, const HIRTypeData* right) {
    const auto* leftPrimitive = left->opt_Primitive();
    const auto* rightPrimitive = right->opt_Primitive();

    const auto sameNumeric = [&]() {
        return left == right && leftPrimitive && (isInteger(*leftPrimitive) || isFloat(*leftPrimitive));
    };
    const auto sameBitwise = [&]() {
        return left == right && leftPrimitive && (isInteger(*leftPrimitive) || *leftPrimitive == HIRCoreType::Bool);
    };
    const auto shift = [&]() {
        return leftPrimitive && rightPrimitive && isInteger(*leftPrimitive) && isInteger(*rightPrimitive);
    };
    const auto comparison = [&]() {
        if (left != right) {
            return false;
        }
        return left->is_Pointer() || (leftPrimitive && *leftPrimitive != HIRCoreType::Str);
    };

    switch (op) {
        case TypeckPrimitiveOperator::Add:
        case TypeckPrimitiveOperator::Sub:
        case TypeckPrimitiveOperator::Mul:
        case TypeckPrimitiveOperator::Div:
        case TypeckPrimitiveOperator::Rem:
        case TypeckPrimitiveOperator::AddAssign:
        case TypeckPrimitiveOperator::SubAssign:
        case TypeckPrimitiveOperator::MulAssign:
        case TypeckPrimitiveOperator::DivAssign:
        case TypeckPrimitiveOperator::RemAssign:
            return sameNumeric();

        case TypeckPrimitiveOperator::BitAnd:
        case TypeckPrimitiveOperator::BitOr:
        case TypeckPrimitiveOperator::BitXor:
        case TypeckPrimitiveOperator::BitAndAssign:
        case TypeckPrimitiveOperator::BitOrAssign:
        case TypeckPrimitiveOperator::BitXorAssign:
            return sameBitwise();

        case TypeckPrimitiveOperator::Shl:
        case TypeckPrimitiveOperator::Shr:
        case TypeckPrimitiveOperator::ShlAssign:
        case TypeckPrimitiveOperator::ShrAssign:
            return shift();

        case TypeckPrimitiveOperator::Equal:
        case TypeckPrimitiveOperator::Order:
            return comparison();

        case TypeckPrimitiveOperator::None:
        case TypeckPrimitiveOperator::Not:
        case TypeckPrimitiveOperator::Neg:
        case TypeckPrimitiveOperator::Deref:
            return false;
    }
    UNREACHABLE();
}

bool primitiveOperatorLhsDeterminesRhs(TypeckPrimitiveOperator op, const HIRTypeData* left) {
    const auto* primitive = left->opt_Primitive();
    const auto numeric = primitive && (isInteger(*primitive) || isFloat(*primitive));
    const auto bitwise = primitive && (isInteger(*primitive) || *primitive == HIRCoreType::Bool);
    const auto comparison = left->is_Pointer() || (primitive && *primitive != HIRCoreType::Str);

    switch (op) {
        case TypeckPrimitiveOperator::Add:
        case TypeckPrimitiveOperator::Sub:
        case TypeckPrimitiveOperator::Mul:
        case TypeckPrimitiveOperator::Div:
        case TypeckPrimitiveOperator::Rem:
        case TypeckPrimitiveOperator::AddAssign:
        case TypeckPrimitiveOperator::SubAssign:
        case TypeckPrimitiveOperator::MulAssign:
        case TypeckPrimitiveOperator::DivAssign:
        case TypeckPrimitiveOperator::RemAssign:
            return numeric;

        case TypeckPrimitiveOperator::BitAnd:
        case TypeckPrimitiveOperator::BitOr:
        case TypeckPrimitiveOperator::BitXor:
        case TypeckPrimitiveOperator::BitAndAssign:
        case TypeckPrimitiveOperator::BitOrAssign:
        case TypeckPrimitiveOperator::BitXorAssign:
            return bitwise;

        case TypeckPrimitiveOperator::Equal:
        case TypeckPrimitiveOperator::Order:
            return comparison;

        case TypeckPrimitiveOperator::Shl:
        case TypeckPrimitiveOperator::Shr:
        case TypeckPrimitiveOperator::ShlAssign:
        case TypeckPrimitiveOperator::ShrAssign:
        case TypeckPrimitiveOperator::None:
        case TypeckPrimitiveOperator::Not:
        case TypeckPrimitiveOperator::Neg:
        case TypeckPrimitiveOperator::Deref:
            return false;
    }
    UNREACHABLE();
}

bool primitiveOperatorHasLanguageCandidate(TypeckPrimitiveOperator op, const HIRTypeData* left, const HIRTypeData* right) {
    return primitiveOperatorHasBuiltin(op, left, right) || (right->is_Infer() && primitiveOperatorLhsDeterminesRhs(op, left));
}

bool primitiveOperatorHasBuiltin(TypeckPrimitiveOperator op, const HIRTypeData* value) {
    if (op == TypeckPrimitiveOperator::Deref) {
        return value->is_Borrow() || value->is_Pointer();
    }

    const auto* primitive = value->opt_Primitive();
    if (!primitive) {
        return false;
    }

    switch (op) {
        case TypeckPrimitiveOperator::Not:
            return *primitive == HIRCoreType::Bool || isInteger(*primitive);
        case TypeckPrimitiveOperator::Neg:
            if (isFloat(*primitive)) {
                return true;
            }
            switch (*primitive) {
                case HIRCoreType::Isize:
                case HIRCoreType::I8:
                case HIRCoreType::I16:
                case HIRCoreType::I32:
                case HIRCoreType::I64:
                case HIRCoreType::I128:
                    return true;
                default:
                    return false;
            }
        case TypeckPrimitiveOperator::Deref:
            return false;
        default:
            return false;
    }
}

template <template <typename> typename W>
auto TyVisitor<W>::visitConstGeneric(typename W<HIRConstGeneric>::T&) -> bool {
    return false;
}

template <template <typename> typename W>
auto TyVisitor<W>::visitPathParams(typename W<HIRPathParams>::T& tpl) -> bool {
    for (auto& ty : tpl.types) {
        if (visitType(ty)) {
            return true;
        }
    }
    for (auto& value : tpl.values) {
        if (visitConstGeneric(value)) {
            return true;
        }
    }
    return false;
}

template <template <typename> typename W>
auto TyVisitor<W>::visitTraitPath(typename W<HIRTraitPath>::T& tpl) -> bool {
    if (visitPathParams(tpl.path.params)) {
        return true;
    }
    for (auto& assoc : tpl.typeBounds) {
        if (visitPathParams(assoc.second.sourceTrait.params) || visitPathParams(assoc.second.atyParams) || visitType(assoc.second.type)) {
            return true;
        }
    }
    for (auto& assoc : tpl.traitBounds) {
        if (visitPathParams(assoc.second.sourceTrait.params) || visitPathParams(assoc.second.atyParams)) {
            return true;
        }
        for (auto& t : assoc.second.traits) {
            if (visitTraitPath(t)) {
                return true;
            }
        }
    }
    return false;
}

template <template <typename> typename W>
auto TyVisitor<W>::visitPath(typename W<HIRPath>::T& path) -> bool {
    switch (path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e = path.data.as_Generic();
            return visitPathParams(e.params);
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& e = path.data.as_UfcsInherent();
            return visitType(e.type) || visitPathParams(e.params);
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e = path.data.as_UfcsKnown();
            return visitType(e.type) || visitPathParams(e.trait.params) || visitPathParams(e.params);
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& e = path.data.as_UfcsUnknown();
            return visitType(e.type) || visitPathParams(e.params);
        }
    }
    UNREACHABLE();
}

template <template <typename> typename W>
auto TyVisitor<W>::visitType(const HIRTypeData* ty) -> bool {
    if (curRecurseStack) {
        for (const auto* p : *curRecurseStack) {
            if (p == ty) {
                return false;
            }
        }
    }

    LList<const HIRTypeData*> recurseNode(curRecurseStack, ty);
    curRecurseStack = &recurseNode;
    STD_DEFER {
        curRecurseStack = recurseNode.prev;
    };

    {
        auto& tuMatch = (this->getTyData(ty));
        switch (tuMatch.tag()) {
            case HIRTypeData::TAG_Infer: {
                break;
            }
            case HIRTypeData::TAG_Diverge: {
                break;
            }
            case HIRTypeData::TAG_Primitive: {
                break;
            }
            case HIRTypeData::TAG_Generic: {
                break;
            }
            case HIRTypeData::TAG_Path: {
                auto& e = tuMatch.as_Path();
                return visitPath(e.path);
            }
            case HIRTypeData::TAG_TraitObject: {
                auto& e = tuMatch.as_TraitObject();
                if (visitTraitPath(e.trait)) {
                    return true;
                }
                for (auto& trait : e.markers) {
                    if (visitPathParams(trait.params)) {
                        return true;
                    }
                }
                return false;
            }
            case HIRTypeData::TAG_ErasedType: {
                auto& e = tuMatch.as_ErasedType();
                for (auto& trait : e.traits) {
                    if (visitTraitPath(trait)) {
                        return true;
                    }
                }
                visitPathParams(e.use);
                switch (e.inner.tag()) {
                    case TypeDataErasedTypeInner::TAG_Fcn: {
                        auto& ee = e.inner.as_Fcn();
                        if (visitPath(ee.origin)) {
                            return true;
                        }
                        break;
                    }
                    case TypeDataErasedTypeInner::TAG_Known: {
                        auto& ee = e.inner.as_Known();
                        if (visitType(ee)) {
                            return true;
                        }
                        break;
                    }
                    case TypeDataErasedTypeInner::TAG_Alias: {
                        auto& ee = e.inner.as_Alias();
                        visitPathParams(ee.params);
                        break;
                    }
                }
                return false;
            }
            case HIRTypeData::TAG_Array: {
                auto& e = tuMatch.as_Array();
                if (visitType(e.inner)) {
                    return true;
                }
                if (auto* size = e.size.opt_Unevaluated()) {
                    return visitConstGeneric(*size);
                }
                return false;
            }
            case HIRTypeData::TAG_Slice: {
                auto& e = tuMatch.as_Slice();
                return visitType(e.inner);
            }
            case HIRTypeData::TAG_Pattern: {
                auto& e = tuMatch.as_Pattern();
                if (visitType(e.inner)) {
                    return true;
                }
                for (auto& range : e.pattern.alternatives) {
                    if ((range.hasStart && visitConstGeneric(range.start)) || (range.hasEnd && visitConstGeneric(range.end))) {
                        return true;
                    }
                }
                return false;
            }
            case HIRTypeData::TAG_Tuple: {
                auto& e = tuMatch.as_Tuple();
                for (auto& ty : e) {
                    if (visitType(ty)) {
                        return true;
                    }
                }
                return false;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& e = tuMatch.as_Borrow();
                return visitType(e.inner);
            }
            case HIRTypeData::TAG_Pointer: {
                auto& e = tuMatch.as_Pointer();
                return visitType(e.inner);
            }
            case HIRTypeData::TAG_NamedFunction: {
                auto& e = tuMatch.as_NamedFunction();
                return visitPath(e.path);
            }
            case HIRTypeData::TAG_Function: {
                auto& e = tuMatch.as_Function();
                for (auto& ty : e.argTypes) {
                    if (visitType(ty)) {
                        return true;
                    }
                }
                return visitType(e.rettype);
            }
            case HIRTypeData::TAG_NodeType: {
                break;
            }
        }
    }
    return false;
}

TyVisitorCbConst::TyVisitorCbConst(HIRTypeVisitorCallback& callback)
    : callback(callback)
{
}

auto TyVisitorCbConst::getTyData(const HIRTypeData* ty) const -> const HIRTypeData& {
    return *ty;
}

auto TyVisitorCbConst::visitType(const HIRTypeData* ty) -> bool {
    if (callback.visit(ty)) {
        return true;
    }
    return TyVisitor::visitType(ty);
}

TyVisitorGenericGroup::TyVisitorGenericGroup(HIRGenericGroup group)
    : group(group)
{
}

auto TyVisitorGenericGroup::getTyData(const HIRTypeData* ty) const -> const HIRTypeData& {
    return *ty;
}

auto TyVisitorGenericGroup::visitConstGeneric(const HIRConstGeneric& value) -> bool {
    if (const auto* generic = value.opt_Generic()) {
        return generic->group() == group;
    }
    if (const auto* unevaluated = value.opt_Unevaluated()) {
        return ((*unevaluated)->selfType && visitType((*unevaluated)->selfType)) || visitPathParams((*unevaluated)->paramsImpl) || visitPathParams((*unevaluated)->paramsItem);
    }
    return false;
}

auto TyVisitorGenericGroup::visitType(const HIRTypeData* ty) -> bool {
    if (const auto* generic = ty->opt_Generic(); generic && generic->group() == group) {
        return true;
    }
    return TyVisitor::visitType(ty);
}

auto TyRewriter::rewritePathParams(HIRPathParams& params) -> bool {
    for (auto& type : params.types) {
        if (rewriteType(type)) {
            return true;
        }
    }
    return false;
}

auto TyRewriter::rewriteTraitPath(HIRTraitPath& trait) -> bool {
    if (rewritePathParams(trait.path.params)) {
        return true;
    }
    for (auto& assoc : trait.typeBounds) {
        if (rewritePathParams(assoc.second.sourceTrait.params) || rewritePathParams(assoc.second.atyParams) || rewriteType(assoc.second.type)) {
            return true;
        }
    }
    for (auto& assoc : trait.traitBounds) {
        if (rewritePathParams(assoc.second.sourceTrait.params) || rewritePathParams(assoc.second.atyParams)) {
            return true;
        }
        for (auto& bound : assoc.second.traits) {
            if (rewriteTraitPath(bound)) {
                return true;
            }
        }
    }
    return false;
}

auto TyRewriter::rewritePath(HIRPath& path) -> bool {
    switch (path.data.tag()) {
        case HIRPathData::TAG_Generic: {
            auto& e = path.data.as_Generic();
            return rewritePathParams(e.params);
        }
        case HIRPathData::TAG_UfcsInherent: {
            auto& e = path.data.as_UfcsInherent();
            return rewriteType(e.type) || rewritePathParams(e.params) || rewritePathParams(e.implParams);
        }
        case HIRPathData::TAG_UfcsKnown: {
            auto& e = path.data.as_UfcsKnown();
            return rewriteType(e.type) || rewritePathParams(e.trait.params) || rewritePathParams(e.params);
        }
        case HIRPathData::TAG_UfcsUnknown: {
            auto& e = path.data.as_UfcsUnknown();
            return rewriteType(e.type) || rewritePathParams(e.params);
        }
    }
    UNREACHABLE();
}

auto TyRewriter::rewriteType(HIRTypeRef& type) -> bool {
    if (!type || std::find(stack.begin(), stack.end(), type) != stack.end()) {
        return false;
    }
    const auto original = type;
    auto data = original->cloneData();
    HIRTypeRef rewritten = original;
    const bool stop = callback.rewrite(rewritten, data);
    if (rewritten != original) {
        type = rewritten;
        return stop;
    }

    stack.push_back(original);
    bool childStop = false;
    if (!stop) {
        switch (data.tag()) {
            case HIRTypeData::TAG_Infer: {
                break;
            }
            case HIRTypeData::TAG_Diverge: {
                break;
            }
            case HIRTypeData::TAG_Primitive: {
                break;
            }
            case HIRTypeData::TAG_Generic: {
                break;
            }
            case HIRTypeData::TAG_Path: {
                auto& e = data.as_Path();
                childStop = rewritePath(e.path);
                break;
            }
            case HIRTypeData::TAG_TraitObject: {
                auto& e = data.as_TraitObject();
                childStop = rewriteTraitPath(e.trait);
                for (auto& marker : e.markers) {
                    if (!childStop) {
                        childStop = rewritePathParams(marker.params);
                    }
                }
                break;
            }
            case HIRTypeData::TAG_ErasedType: {
                auto& e = data.as_ErasedType();
                for (auto& trait : e.traits) {
                    if (!childStop) {
                        childStop = rewriteTraitPath(trait);
                    }
                }
                if (!childStop) {
                    childStop = rewritePathParams(e.use);
                }
                if (!childStop) {
                    {
                        auto& tuMatch = e.inner;
                        switch (tuMatch.tag()) {
                            case TypeDataErasedTypeInner::TAG_Fcn: {
                                auto& inner = tuMatch.as_Fcn();
                                childStop = rewritePath(inner.origin);
                                break;
                            }
                            case TypeDataErasedTypeInner::TAG_Known: {
                                auto& inner = tuMatch.as_Known();
                                childStop = rewriteType(inner);
                                break;
                            }
                            case TypeDataErasedTypeInner::TAG_Alias: {
                                auto& inner = tuMatch.as_Alias();
                                childStop = rewritePathParams(inner.params);
                                break;
                            }
                        }
                    }
                }
                break;
            }
            case HIRTypeData::TAG_Array: {
                auto& e = data.as_Array();
                childStop = rewriteType(e.inner);
                break;
            }
            case HIRTypeData::TAG_Slice: {
                auto& e = data.as_Slice();
                childStop = rewriteType(e.inner);
                break;
            }
            case HIRTypeData::TAG_Pattern: {
                auto& e = data.as_Pattern();
                childStop = rewriteType(e.inner);
                break;
            }
            case HIRTypeData::TAG_Tuple: {
                auto& e = data.as_Tuple();
                for (auto& inner : e) {
                    if (!childStop) {
                        childStop = rewriteType(inner);
                    }
                }
                break;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& e = data.as_Borrow();
                childStop = rewriteType(e.inner);
                break;
            }
            case HIRTypeData::TAG_Pointer: {
                auto& e = data.as_Pointer();
                childStop = rewriteType(e.inner);
                break;
            }
            case HIRTypeData::TAG_NamedFunction: {
                auto& e = data.as_NamedFunction();
                childStop = rewritePath(e.path);
                break;
            }
            case HIRTypeData::TAG_Function: {
                auto& e = data.as_Function();
                for (auto& arg : e.argTypes) {
                    if (!childStop) {
                        childStop = rewriteType(arg);
                    }
                }
                if (!childStop) {
                    childStop = rewriteType(e.rettype);
                }
                break;
            }
            case HIRTypeData::TAG_NodeType: {
                break;
            }
        }
    }
    stack.pop_back();
    type = types.intern(mv$(data));
    return stop || childStop;
}

auto TyVisitorMonomorphNeeded::getTyData(const HIRTypeData* ty) const -> const HIRTypeData& {
    return *ty;
}

auto TyVisitorMonomorphNeeded::visitPathParams(const HIRPathParams& pp) -> bool {
    for (const auto& v : pp.values) {
        if (v.is_Generic()) {
            return true;
        }
    }
    return TyVisitor::visitPathParams(pp);
}

auto TyVisitorMonomorphNeeded::visitType(const HIRTypeData* ty) -> bool {
    if (ty->is_Generic()) {
        return true;
    }
    if (ty->is_Array() && ty->as_Array().size.is_Unevaluated() /*&& ty->as_Array().size.as_Unevaluated().*/) {
        return true;
    }
    return TyVisitor::visitType(ty);
}

CloneTyWithMonomorph::CloneTyWithMonomorph(HIRTypeInterner& types, HIRTypeCloneCallback& callback)
    : Monomorphiser(types)
    , callback(callback)
{
}

auto CloneTyWithMonomorph::getType(const Span& sp, const HIRGenericRef& g) const -> HIRTypeRef {
    return types.generic(g.name, g.binding);
}

auto CloneTyWithMonomorph::getValue(const Span& sp, const HIRGenericRef& g) const -> HIRConstGeneric {
    return g;
}

auto CloneTyWithMonomorph::monomorphType(const Span& sp, const HIRTypeData* ty, bool allowInfer) const -> HIRTypeRef {
    HIRTypeRef rv;

    if (callback.clone(ty, rv)) {
        return rv;
    }
    return Monomorphiser::monomorphType(sp, ty, allowInfer);
}
