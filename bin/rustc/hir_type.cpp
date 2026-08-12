#include "hir_type.h"

#include "span.h"
#include "hir_expr.h" // ArraySize::Unevaluated cloning needs the complete expression definition.

#include <std/mem/obj_pool.h>

#include <cstdint>

::std::ostream& operator<<(::std::ostream& os, const HIRTypeData* ty) {
    if (ty) {
        ty->fmt(os);
    } else {
        os << "NULL";
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const HIRCoreType& ct) {
    switch (ct) {
        case HIRCoreType::Usize:
            return os << "usize";
        case HIRCoreType::Isize:
            return os << "isize";
        case HIRCoreType::U8:
            return os << "u8";
        case HIRCoreType::I8:
            return os << "i8";
        case HIRCoreType::U16:
            return os << "u16";
        case HIRCoreType::I16:
            return os << "i16";
        case HIRCoreType::U32:
            return os << "u32";
        case HIRCoreType::I32:
            return os << "i32";
        case HIRCoreType::U64:
            return os << "u64";
        case HIRCoreType::I64:
            return os << "i64";
        case HIRCoreType::U128:
            return os << "u128";
        case HIRCoreType::I128:
            return os << "i128";

        case HIRCoreType::F16:
            return os << "f16";
        case HIRCoreType::F32:
            return os << "f32";
        case HIRCoreType::F64:
            return os << "f64";
        case HIRCoreType::F128:
            return os << "f128";

        case HIRCoreType::Bool:
            return os << "bool";
        case HIRCoreType::Char:
            return os << "char";
        case HIRCoreType::Str:
            return os << "str";
    }
    assert(!"Bad CoreType value");
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const HIRBorrowType& bt) {
    switch (bt) {
        case HIRBorrowType::Owned:
            return os << "Owned";
        case HIRBorrowType::Unique:
            return os << "Unique";
        case HIRBorrowType::Shared:
            return os << "Shared";
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const HIRArraySize& x) {
        TU_MATCH_HDRA( (x), { )
        TU_ARMA(Unevaluated, se) {
            os << se;
        }
        TU_ARMA(Known, se)
        os << se;
        }
        return os;
}

void HIRGenericRef::fmt(std::ostream& os) const {
    os << this->name << "/*";
    if (this->binding == GENERICSelf) {
        os << "";
    } else {
        switch (this->group()) {
            case 0:
                os << "I:" << this->idx();
                break;
            case 1:
                os << "M:" << this->idx();
                break;
            case 2:
                os << "P:" << this->idx();
                break;
            case 3:
                os << "H:" << this->idx();
                break;
            default:
                os << this->binding;
                break;
        }
    }
    os << "*/";
}

Ordering HIRArraySize::ord(const HIRArraySize& x) const {
    if (this->tag() != x.tag()) {
        return ::ord(static_cast<unsigned>(this->tag()), static_cast<unsigned>(x.tag()));
    }
    TU_MATCH_HDRA( (*this, x), {)
    TU_ARMA(Unevaluated, tse, xse)
        return ::ord(tse, xse);
        TU_ARMA(Known, tse, xse)
        return ::ord(tse, xse);
    }
    throw "";
}

HIRArraySize HIRArraySize::clone() const {
    TU_MATCH_HDRA( (*this), {)
    TU_ARMA(Unevaluated, se)
        return se.clone();
        TU_ARMA(Known, se)
        return se;
    }
    throw "";
}

HIRTypeDataErasedTypeAliasInner::HIRTypeDataErasedTypeAliasInner(const HIRItemPath& p, const HIRGenericParams& params)
    : path(p.getSimplePath())
    , type()
{
    this->generics = params.clone();
    this->generics.bounds.clear();
}

bool HIRTypeDataErasedTypeAliasInner::isPublicTo(const HIRSimplePath& p) const {
    return p.startsWith(this->path, /*skip_last=*/true);
}

HIRTypeDataFunctionPointer HIRTypeData::Data_NamedFunction::decay(HIRTypeInterner& types, const Span& sp) const {
    const HIRTypeData* tySelf = nullptr;
    const HIRPathParams* ppImpl = nullptr;
    const HIRPathParams* ppMethod = nullptr;

    TU_MATCH_HDRA( (this->def), { )
    TU_ARMA(Function, fp) {
            ASSERT_BUG(sp, fp, "Non-initialised NamedFunction definition: " << this->path);
        TU_MATCH_HDRA( (this->path.mData), {)
        TU_ARMA(Generic, pe) {
                    ppMethod = &pe.mParams;
                }
                TU_ARMA(UfcsKnown, pe) {
                    tySelf = pe.type;
                    ppImpl = &pe.trait.mParams;
                    ppMethod = &pe.params;
                }
                TU_ARMA(UfcsInherent, pe) {
                    tySelf = pe.type;
                    ppImpl = &pe.implParams;
                    ppMethod = &pe.params;
                }
                TU_ARMA(UfcsUnknown, pe) {
                    BUG(sp, "UfcsUnknown seen");
                }
        }
        MonomorphStatePtr   ms { types, tySelf, ppImpl, ppMethod };
        const auto& f = *fp;
        HIRTypeDataFunctionPointer ft {
            HIRGenericParams(),   // TODO: Get HRLs
            f.unsafe,
            f.variadic,
            f.mAbi,
            ms.monomorphType(sp, f.returnType),
            {}
        };
        for( const auto& arg : f.mArgs )
        {
                ft.argTypes.push_back(ms.monomorphType(sp, arg.second));
        }
        return mv$(ft);
        }
        TU_ARMA(EnumConstructor, ec) {
            const auto& e = this->path.mData.as_Generic();
            MonomorphStatePtr ms{types, nullptr, &e.mParams, nullptr};
            auto enumPath = e.mPath.parent();
            const auto& enm = *ec.e;
            ASSERT_BUG(sp, enm.mData.is_Data(), "Enum " << enumPath << " isn't a data-holding enum");
            const auto& varTy = enm.mData.as_Data()[ec.v].type;
            const auto& str = *varTy->as_Path().binding.as_Struct();
            const auto& varData = str.mData.as_Tuple();

            HIRTypeDataFunctionPointer ft{
                HIRGenericParams(), // TODO: Get HRLs
                false,
                false,
                RcString::newInterned(ABI_RUST),
                types.path(HIRPath(HIRGenericPath(mv$(enumPath), e.mParams.clone())), HIRTypePathBinding::make_Enum(&enm)),
                {}
            };
            for (const auto& arg : varData) {
                ft.argTypes.push_back(ms.monomorphType(sp, arg.ent));
            }
            return ft;
        }
        TU_ARMA(StructConstructor, p) {
            const auto& e = this->path.mData.as_Generic();
            MonomorphStatePtr ms{types, nullptr, &e.mParams, nullptr};
            HIRTypeDataFunctionPointer ft{
                HIRGenericParams(), // TODO: Get HRLs
                false,
                false,
                RcString::newInterned(ABI_RUST),
                types.path(this->path.clone(), HIRTypePathBinding::make_Struct(p)),
                {}
            };
            for (const auto& arg : p->mData.as_Tuple()) {
                ft.argTypes.push_back(ms.monomorphType(sp, arg.ent));
            }
            return ft;
        }
    }
    BUG(sp, "Unreachable code?");
}

void HIRTypeData::fmt(::std::ostream& os) const {
    thread_local static std::vector<const HIRTypeData*> sRecurseStack;
    for (const auto* p : sRecurseStack) {
        if (p == this) {
            os << "RECURSE";
            return;
        }
    }

    struct _ {
        _(const HIRTypeData* ptr) {
            sRecurseStack.push_back(ptr);
        }

        ~_() {
            sRecurseStack.pop_back();
        }
    } h(this);

    TU_MATCH_HDRA( (*this), { )
    TU_ARMA(Infer, e) {
            os << "_";
            if (e.index != ~0u || e.tyClass != HIRInferClass::None) {
                os << "/*";
                if (e.index != ~0u) {
                    os << e.index;
                }
                switch (e.tyClass) {
                    case HIRInferClass::None:
                        break;
                    case HIRInferClass::Float:
                        os << ":f";
                        break;
                    case HIRInferClass::Integer:
                        os << ":i";
                        break;
                }
                os << "*/";
            }
        }
        TU_ARMA(Diverge, e) {
            os << "!";
        }
        TU_ARMA(Primitive, e) {
            os << e;
        }
        TU_ARMA(Path, e) {
            os << e.path;
            TU_MATCH(HIRTypePathBinding, (e.binding), (be), (Unbound, os << "/*?*/";), (Opaque, os << "/*O*/";), (ExternType, os << "/*X*/";), (Struct, os << "/*S*/";), (Union, os << "/*U*/";), (Enum, os << "/*E*/";))
        }
        TU_ARMA(Generic, e) {
            os << e;
        }
        TU_ARMA(TraitObject, e) {
            os << "dyn (";
            if (e.mTrait.mPath != HIRGenericPath()) {
                os << e.mTrait;
            }
            for (const auto& tr : e.markers) {
                os << "+" << tr;
            }
            os << ")";
        }
        TU_ARMA(ErasedType, e) {
            os << "impl ";
            for (const auto& tr : e.traits) {
                if (&tr != &e.traits[0]) {
                    os << "+";
                }
                os << tr;
            }
            os << "+use" << e.use;
            os << "/*";
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Known, ee) {
                    os << "= " << ee;
                }
                TU_ARMA(Fcn, ee) {
                    os << "fn " << ee.origin << "#" << ee.index;
                }
                TU_ARMA(Alias, ee) {
                    os << "type" << ee.params << " " << ee.inner->path;
                }
        }
        os << "*/";
        }
        TU_ARMA(Array, e) {
            os << "[" << e.inner << "; " << e.size << "]";
        }
        TU_ARMA(Slice, e) {
            os << "[" << e.inner << "]";
        }
        TU_ARMA(Tuple, e) {
            os << "(";
            for (const auto& t : e) {
                os << t << ", ";
            }
            os << ")";
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
            os << e.inner;
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
            os << e.inner;
        }
        TU_ARMA(NamedFunction, e) {
            os << "fn{" << (e.def.is_Function() && !e.def.as_Function() ? "!" : "") << e.path << "}";
        }
        TU_ARMA(Function, e) {
            if (e.isUnsafe) {
                os << "unsafe ";
            }
            if (e.mAbi != "") {
                os << "extern \"" << e.mAbi << "\" ";
            }
            os << "fn(";
            for (const auto& t : e.argTypes) {
                os << t << ", ";
            }
            if (e.isVariadic) {
                os << "...";
            }
            os << ") -> " << e.mRettype;
        }
        TU_ARMA(NodeType, e) {
            e.fmt(os);
        }
    }
}

bool HIRTypeDataNodeType::operator==(const HIRTypeDataNodeType& x) const {
    return this->ord(x) == OrdEqual;
}

Ordering HIRTypeDataNodeType::ord(const HIRTypeDataNodeType& x) const {
    ORD(static_cast<int>(this->tag()), static_cast<int>(x.tag()));
    TU_MATCH_HDRA((*this, x), {)
    TU_ARMA(Closure, te, xe) {
            ORD(reinterpret_cast<uintptr_t>(te), reinterpret_cast<uintptr_t>(xe));
        }
        TU_ARMA(Generator, te, xe) {
            ORD(reinterpret_cast<uintptr_t>(te), reinterpret_cast<uintptr_t>(xe));
        }
        TU_ARMA(Async, te, xe) {
            ORD(reinterpret_cast<uintptr_t>(te), reinterpret_cast<uintptr_t>(xe));
        }
    }
    return OrdEqual;
}

void HIRTypeDataNodeType::fmt(::std::ostream& os) const {
    TU_MATCH_HDRA((*this), {)
    TU_ARMA(Closure, e) {
            os << "closure[" << e << "]";
        }
        TU_ARMA(Generator, e) {
            os << "generator[" << e << "]";
        }
        TU_ARMA(Async, e) {
            os << "async[" << e << "]";
        }
    }
}

HIRTypeDataNodeType HIRTypeDataNodeType::clone() const {
    TU_MATCH_HDRA((*this), {)
    TU_ARMA(Closure, e) {
            return e;
        }
        TU_ARMA(Generator, e) {
            return e;
        }
        TU_ARMA(Async, e) {
            return e;
        }
    }
    throw "";
}

namespace {

    bool exactPathParamsEqual(const HIRPathParams& a, const HIRPathParams& b);
    bool exactGenericParamsEqual(const HIRGenericParams& a, const HIRGenericParams& b);
    bool exactTraitPathEqual(const HIRTraitPath& a, const HIRTraitPath& b);

    bool exactGenericRefEqual(const HIRGenericRef& a, const HIRGenericRef& b) {
        return a == b;
    }

    bool exactConstGenericEqual(const HIRConstGeneric& a, const HIRConstGeneric& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        TU_MATCH_HDRA((a, b), {)
        TU_ARMA(Infer, ae, be) return ae.index == be.index;
            TU_ARMA(Generic, ae, be) return exactGenericRefEqual(ae, be);
            TU_ARMA(Evaluated, ae, be) return *ae == *be;
            TU_ARMA(Unevaluated, ae, be) {
                return ae->expr.get() == be->expr.get() && exactPathParamsEqual(ae->paramsImpl, be->paramsImpl) && exactPathParamsEqual(ae->paramsItem, be->paramsItem);
            }
        }
        throw "";
    }

    bool exactPathParamsEqual(const HIRPathParams& a, const HIRPathParams& b) {
        if (a.types.size() != b.types.size() || a.values.size() != b.values.size()) {
            return false;
        }
        for (size_t i = 0; i < a.types.size(); i++) {
            if (a.types[i] != b.types[i]) {
                return false;
            }
        }
        for (size_t i = 0; i < a.values.size(); i++) {
            if (!exactConstGenericEqual(a.values[i], b.values[i])) {
                return false;
            }
        }
        return true;
    }

    bool exactGenericPathEqual(const HIRGenericPath& a, const HIRGenericPath& b) {
        return a.mPath == b.mPath && exactPathParamsEqual(a.mParams, b.mParams);
    }

    bool exactOptionalGenericParamsEqual(const ::std::unique_ptr<HIRGenericParams>& a, const ::std::unique_ptr<HIRGenericParams>& b) {
        return (!a && !b) || (a && b && exactGenericParamsEqual(*a, *b));
    }

    bool exactPathEqual(const HIRPath& a, const HIRPath& b) {
        if (a.mData.tag() != b.mData.tag()) {
            return false;
        }
        TU_MATCH_HDRA((a.mData, b.mData), {)
        TU_ARMA(Generic, ae, be) return exactGenericPathEqual(ae, be);
            TU_ARMA(UfcsInherent, ae, be) {
                return ae.type == be.type && ae.item == be.item && exactPathParamsEqual(ae.params, be.params) && exactPathParamsEqual(ae.implParams, be.implParams);
            }
            TU_ARMA(UfcsKnown, ae, be) {
                return ae.type == be.type && exactGenericPathEqual(ae.trait, be.trait) && ae.item == be.item && exactPathParamsEqual(ae.params, be.params) && exactOptionalGenericParamsEqual(ae.hrtbs, be.hrtbs);
            }
            TU_ARMA(UfcsUnknown, ae, be) {
                return ae.type == be.type && ae.item == be.item && exactPathParamsEqual(ae.params, be.params);
            }
        }
        throw "";
    }

    bool exactTraitPathEqual(const HIRTraitPath& a, const HIRTraitPath& b) {
        if (!exactOptionalGenericParamsEqual(a.hrtbs, b.hrtbs) || !exactGenericPathEqual(a.mPath, b.mPath) || a.traitPtr != b.traitPtr || a.typeBounds.size() != b.typeBounds.size() || a.traitBounds.size() != b.traitBounds.size()) {
            return false;
        }
        auto ai = a.typeBounds.begin();
        auto bi = b.typeBounds.begin();
        for (; ai != a.typeBounds.end(); ++ai, ++bi) {
            if (ai->first != bi->first || !exactGenericPathEqual(ai->second.sourceTrait, bi->second.sourceTrait) || !exactPathParamsEqual(ai->second.atyParams, bi->second.atyParams) || ai->second.type != bi->second.type) {
                return false;
            }
        }
        auto ati = a.traitBounds.begin();
        auto bti = b.traitBounds.begin();
        for (; ati != a.traitBounds.end(); ++ati, ++bti) {
            if (ati->first != bti->first || !exactGenericPathEqual(ati->second.sourceTrait, bti->second.sourceTrait) || !exactPathParamsEqual(ati->second.atyParams, bti->second.atyParams) || ati->second.traits.size() != bti->second.traits.size()) {
                return false;
            }
            for (size_t i = 0; i < ati->second.traits.size(); i++) {
                if (!exactTraitPathEqual(ati->second.traits[i], bti->second.traits[i])) {
                    return false;
                }
            }
        }
        return true;
    }

    bool exactGenericBoundEqual(const HIRGenericBound& a, const HIRGenericBound& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        TU_MATCH_HDRA((a, b), {)
        TU_ARMA(TraitBound, ae, be) {
                return exactOptionalGenericParamsEqual(ae.hrtbs, be.hrtbs) && ae.type == be.type && exactTraitPathEqual(ae.trait, be.trait);
            }
            TU_ARMA(TypeEquality, ae, be) return ae.type == be.type && ae.otherType == be.otherType;
        }
        throw "";
    }

    bool exactGenericParamsEqual(const HIRGenericParams& a, const HIRGenericParams& b) {
        if (a.types.size() != b.types.size() || a.values.size() != b.values.size() || a.bounds.size() != b.bounds.size()) {
            return false;
        }
        for (size_t i = 0; i < a.types.size(); i++) {
            if (a.types[i].mName != b.types[i].mName || a.types[i].defaultValue != b.types[i].defaultValue || a.types[i].isSized != b.types[i].isSized) {
                return false;
            }
        }
        for (size_t i = 0; i < a.values.size(); i++) {
            if (a.values[i].mName != b.values[i].mName || a.values[i].mType != b.values[i].mType || !exactConstGenericEqual(a.values[i].defaultValue, b.values[i].defaultValue)) {
                return false;
            }
        }
        for (size_t i = 0; i < a.bounds.size(); i++) {
            if (!exactGenericBoundEqual(a.bounds[i], b.bounds[i])) {
                return false;
            }
        }
        return true;
    }

    bool exactBindingEqual(const HIRTypePathBinding& a, const HIRTypePathBinding& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        TU_MATCH_HDRA((a, b), {)
        TU_ARMA(Unbound, ae, be) return true;
            TU_ARMA(Opaque, ae, be) return true;
            TU_ARMA(ExternType, ae, be) return ae == be;
            TU_ARMA(Struct, ae, be) return ae == be;
            TU_ARMA(Union, ae, be) return ae == be;
            TU_ARMA(Enum, ae, be) return ae == be;
        }
        throw "";
    }

    bool exactErasedInnerEqual(const TypeDataErasedTypeInner& a, const TypeDataErasedTypeInner& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        TU_MATCH_HDRA((a, b), {)
        TU_ARMA(Fcn, ae, be) return ae.index == be.index && exactPathEqual(ae.origin, be.origin);
            TU_ARMA(Known, ae, be) return ae == be;
            TU_ARMA(Alias, ae, be) return ae.inner.get() == be.inner.get() && exactPathParamsEqual(ae.params, be.params);
        }
        throw "";
    }

    bool exactArraySizeEqual(const HIRArraySize& a, const HIRArraySize& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        TU_MATCH_HDRA((a, b), {)
        TU_ARMA(Known, ae, be) return ae == be;
            TU_ARMA(Unevaluated, ae, be) return exactConstGenericEqual(ae, be);
        }
        throw "";
    }

    bool exactTypeDataEqual(const HIRTypeData& a, const HIRTypeData& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        TU_MATCH_HDRA((a, b), {)
        TU_ARMA(Infer, ae, be) return ae.index == be.index && ae.tyClass == be.tyClass;
            TU_ARMA(Diverge, ae, be) return true;
            TU_ARMA(Primitive, ae, be) return ae == be;
            TU_ARMA(Path, ae, be) {
                return exactPathEqual(ae.path, be.path) && exactBindingEqual(ae.binding, be.binding) && exactOptionalGenericParamsEqual(ae.hrtbs, be.hrtbs);
            }
            TU_ARMA(Generic, ae, be) return exactGenericRefEqual(ae, be);
            TU_ARMA(TraitObject, ae, be) {
                if (!exactTraitPathEqual(ae.mTrait, be.mTrait) || ae.markers.size() != be.markers.size()) {
                    return false;
                }
                for (size_t i = 0; i < ae.markers.size(); i++) {
                    if (!exactGenericPathEqual(ae.markers[i], be.markers[i])) {
                        return false;
                    }
                }
                return true;
            }
            TU_ARMA(ErasedType, ae, be) {
                if (ae.isSized != be.isSized || ae.usePresent != be.usePresent || ae.traits.size() != be.traits.size() || !exactErasedInnerEqual(ae.inner, be.inner) || !exactPathParamsEqual(ae.use, be.use)) {
                    return false;
                }
                for (size_t i = 0; i < ae.traits.size(); i++) {
                    if (!exactTraitPathEqual(ae.traits[i], be.traits[i])) {
                        return false;
                    }
                }
                return true;
            }
            TU_ARMA(Array, ae, be) return ae.inner == be.inner && exactArraySizeEqual(ae.size, be.size);
            TU_ARMA(Slice, ae, be) return ae.inner == be.inner;
            TU_ARMA(Tuple, ae, be) return ae == be;
            TU_ARMA(Borrow, ae, be) return ae.type == be.type && ae.inner == be.inner;
            TU_ARMA(Pointer, ae, be) return ae.type == be.type && ae.inner == be.inner;
            TU_ARMA(NamedFunction, ae, be) {
                if (!exactPathEqual(ae.path, be.path) || ae.def.tag() != be.def.tag()) {
                    return false;
                }
            TU_MATCH_HDRA((ae.def, be.def), {)
            TU_ARMA(Function, ad, bd) return ad == bd;
                    TU_ARMA(EnumConstructor, ad, bd) return ad.e == bd.e && ad.v == bd.v;
                    TU_ARMA(StructConstructor, ad, bd) return ad == bd;
            }
            throw "";
            }
            TU_ARMA(Function, ae, be) {
                return exactGenericParamsEqual(ae.hrls, be.hrls) && ae.isUnsafe == be.isUnsafe && ae.isVariadic == be.isVariadic && ae.mAbi == be.mAbi && ae.mRettype == be.mRettype && ae.argTypes == be.argTypes;
            }
            TU_ARMA(NodeType, ae, be) return ae == be;
        }
        throw "";
    }

    void addTypeFlags(uint32_t& flags, HIRTypeRef type) {
        flags |= type->flags;
    }

    void addLifetimeFlags(uint32_t& flags, HIRLifetimeRef lifetime) {
        if (lifetime.isParam() && lifetime.asParam().group() != GENERICHrtb) {
            flags |= HIRTypeData::HAS_LIFETIME_PARAM;
        }
    }

    uint32_t typeFlags(const HIRPathParams& params);

    uint32_t typeFlags(const HIRGenericPath& path) {
        return typeFlags(path.mParams);
    }

    uint32_t typeFlags(const HIRTraitPath& trait) {
        auto flags = typeFlags(trait.mPath);
        for (const auto& bound : trait.typeBounds) {
            flags |= typeFlags(bound.second.sourceTrait);
            flags |= typeFlags(bound.second.atyParams);
            addTypeFlags(flags, bound.second.type);
        }
        for (const auto& bound : trait.traitBounds) {
            flags |= typeFlags(bound.second.sourceTrait);
            flags |= typeFlags(bound.second.atyParams);
            for (const auto& nested : bound.second.traits) {
                flags |= typeFlags(nested);
            }
        }
        return flags;
    }

    uint32_t typeFlags(const HIRPathParams& params) {
        uint32_t flags = 0;
        for (const auto type : params.types) {
            addTypeFlags(flags, type);
        }
        for (const auto& value : params.values) {
            if (value.is_Generic()) {
                flags |= HIRTypeData::HAS_TYPE_PARAM;
            } else if (value.is_Infer() || value.is_Unevaluated()) {
                flags |= HIRTypeData::HAS_DEFERRED_CONST;
            }
        }
        return flags;
    }

    uint32_t typeFlags(const HIRPath& path) {
        uint32_t flags = 0;
        TU_MATCH_HDRA((path.mData), {)
        TU_ARMA(Generic, e) {
                flags |= typeFlags(e.mParams);
            }
            TU_ARMA(UfcsInherent, e) {
                addTypeFlags(flags, e.type);
                flags |= typeFlags(e.params);
                flags |= typeFlags(e.implParams);
            }
            TU_ARMA(UfcsKnown, e) {
                addTypeFlags(flags, e.type);
                flags |= typeFlags(e.trait);
                flags |= typeFlags(e.params);
            }
            TU_ARMA(UfcsUnknown, e) {
                addTypeFlags(flags, e.type);
                flags |= typeFlags(e.params);
            }
        }
        return flags;
    }

    uint32_t typeFlags(const HIRTypeData& type) {
        uint32_t flags = 0;
        TU_MATCH_HDRA((type), {)
        TU_ARMA(Infer, e) {
                flags |= HIRTypeData::HAS_TYPE_INFER;
            }
            TU_ARMA(Diverge, e) {
            }
            TU_ARMA(Primitive, e) {
            }
            TU_ARMA(Path, e) {
                flags |= typeFlags(e.path);
                if (e.path.mData.is_UfcsKnown() && (e.binding.is_Unbound() || e.binding.is_Opaque())) {
                    flags |= HIRTypeData::HAS_ASSOCIATED_TYPE;
                }
            }
            TU_ARMA(Generic, e) {
                flags |= HIRTypeData::HAS_TYPE_PARAM;
            }
            TU_ARMA(TraitObject, e) {
                flags |= typeFlags(e.mTrait);
                for (const auto& marker : e.markers) {
                    flags |= typeFlags(marker);
                }
            }
            TU_ARMA(ErasedType, e) {
                for (const auto& trait : e.traits) {
                    flags |= typeFlags(trait);
                }
                flags |= typeFlags(e.use);
            TU_MATCH_HDRA((e.inner), {)
            TU_ARMA(Fcn, inner) flags |= typeFlags(inner.origin);
                    TU_ARMA(Known, inner) addTypeFlags(flags, inner);
                    TU_ARMA(Alias, inner) flags |= typeFlags(inner.params);
            }
            }
            TU_ARMA(Array, e) {
                addTypeFlags(flags, e.inner);
                if (e.size.is_Unevaluated()) {
                    flags |= HIRTypeData::HAS_UNEVALUATED_CONST;
                }
            }
            TU_ARMA(Slice, e) addTypeFlags(flags, e.inner);
            TU_ARMA(Tuple, e) for (const auto inner : e) addTypeFlags(flags, inner);
            TU_ARMA(Borrow, e) {
                addTypeFlags(flags, e.inner);
            }
            TU_ARMA(Pointer, e) addTypeFlags(flags, e.inner);
            TU_ARMA(NamedFunction, e) flags |= typeFlags(e.path);
            TU_ARMA(Function, e) {
                addTypeFlags(flags, e.mRettype);
                for (const auto argument : e.argTypes) {
                    addTypeFlags(flags, argument);
                }
            }
            TU_ARMA(NodeType, e) {
            }
        }
        return flags;
    }

    size_t hashMix(size_t state, size_t value) {
        return state ^ (value + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2));
    }

    size_t hashSimplePath(const HIRSimplePath& path) {
        size_t h = ::std::hash<RcString>()(path.crateName());
        for (const auto& component : path.components()) {
            h = hashMix(h, ::std::hash<RcString>()(component));
        }
        return h;
    }

    size_t hashTypeRef(HIRTypeRef type) {
        return ::std::hash<const void*>()(type);
    }

    size_t hashPathParams(const HIRPathParams& params);

    size_t hashGenericRef(const HIRGenericRef& generic) {
        size_t h = generic.binding;
        if (generic.group() == GENERICPlaceholder) {
            h = hashMix(h, ::std::hash<RcString>()(generic.name));
        }
        return h;
    }

    size_t hashConstGeneric(const HIRConstGeneric& value) {
        size_t h = static_cast<size_t>(value.tag());
        TU_MATCH_HDRA((value), {)
        TU_ARMA(Infer, e) {
                h = hashMix(h, e.index);
            }
            TU_ARMA(Generic, e) {
                h = hashMix(h, hashGenericRef(e));
            }
            TU_ARMA(Evaluated, e) {
                // The evaluated value does not expose a cheap scalar hash for
                // every representation.  Its tag still separates it from the
                // overwhelmingly more common generic and inferred constants.
            }
            TU_ARMA(Unevaluated, e) {
                h = hashMix(h, reinterpret_cast<uintptr_t>(e->expr.get()));
                h = hashMix(h, hashPathParams(e->paramsImpl));
                h = hashMix(h, hashPathParams(e->paramsItem));
            }
        }
        return h;
    }

    size_t hashPathParams(const HIRPathParams& params) {
        size_t h = hashMix(params.types.size(), params.values.size());
        for (const auto type : params.types) {
            h = hashMix(h, hashTypeRef(type));
        }
        for (const auto& value : params.values) {
            h = hashMix(h, hashConstGeneric(value));
        }
        return h;
    }

    size_t hashGenericPath(const HIRGenericPath& path) {
        return hashMix(hashSimplePath(path.mPath), hashPathParams(path.mParams));
    }

    size_t hashPath(const HIRPath& path) {
        size_t h = static_cast<size_t>(path.mData.tag());
        TU_MATCH_HDRA((path.mData), {)
        TU_ARMA(Generic, e) {
                h = hashMix(h, hashGenericPath(e));
            }
            TU_ARMA(UfcsInherent, e) {
                h = hashMix(h, hashTypeRef(e.type));
                h = hashMix(h, ::std::hash<RcString>()(e.item));
                h = hashMix(h, hashPathParams(e.params));
                h = hashMix(h, hashPathParams(e.implParams));
            }
            TU_ARMA(UfcsKnown, e) {
                h = hashMix(h, hashTypeRef(e.type));
                h = hashMix(h, hashGenericPath(e.trait));
                h = hashMix(h, ::std::hash<RcString>()(e.item));
                h = hashMix(h, hashPathParams(e.params));
                h = hashMix(h, static_cast<bool>(e.hrtbs));
            }
            TU_ARMA(UfcsUnknown, e) {
                h = hashMix(h, hashTypeRef(e.type));
                h = hashMix(h, ::std::hash<RcString>()(e.item));
                h = hashMix(h, hashPathParams(e.params));
            }
        }
        return h;
    }

    size_t hashBinding(const HIRTypePathBinding& binding) {
        size_t h = static_cast<size_t>(binding.tag());
        TU_MATCH_HDRA((binding), {)
        TU_ARMA(Unbound, e) {
            }
            TU_ARMA(Opaque, e) {
            }
            TU_ARMA(ExternType, e) {
                h = hashMix(h, reinterpret_cast<uintptr_t>(e));
            }
            TU_ARMA(Struct, e) {
                h = hashMix(h, reinterpret_cast<uintptr_t>(e));
            }
            TU_ARMA(Union, e) {
                h = hashMix(h, reinterpret_cast<uintptr_t>(e));
            }
            TU_ARMA(Enum, e) {
                h = hashMix(h, reinterpret_cast<uintptr_t>(e));
            }
        }
        return h;
    }

    size_t hashTypeData(const HIRTypeData& type) {
        size_t h = static_cast<size_t>(type.tag());
        TU_MATCH_HDRA((type), {)
        TU_ARMA(Infer, e) {
                h = hashMix(h, e.index);
                h = hashMix(h, static_cast<size_t>(e.tyClass));
            }
            TU_ARMA(Diverge, e) {
            }
            TU_ARMA(Primitive, e) h = hashMix(h, static_cast<size_t>(e));
            TU_ARMA(Path, e) {
                h = hashMix(h, hashPath(e.path));
                h = hashMix(h, hashBinding(e.binding));
                h = hashMix(h, static_cast<bool>(e.hrtbs));
            }
            TU_ARMA(Generic, e) {
                h = hashMix(h, hashGenericRef(e));
            }
            TU_ARMA(TraitObject, e) {
                h = hashMix(h, hashGenericPath(e.mTrait.mPath));
                h = hashMix(h, reinterpret_cast<uintptr_t>(e.mTrait.traitPtr));
                for (const auto& marker : e.markers) {
                    h = hashMix(h, hashGenericPath(marker));
                }
                for (const auto& bound : e.mTrait.typeBounds) {
                    h = hashMix(h, ::std::hash<RcString>()(bound.first));
                    h = hashMix(h, hashGenericPath(bound.second.sourceTrait));
                    h = hashMix(h, hashPathParams(bound.second.atyParams));
                    h = hashMix(h, hashTypeRef(bound.second.type));
                }
            }
            TU_ARMA(ErasedType, e) {
                h = hashMix(h, static_cast<size_t>(e.inner.tag()));
                h = hashMix(h, e.traits.size());
            }
            TU_ARMA(Array, e) {
                h = hashMix(h, hashTypeRef(e.inner));
                h = hashMix(h, static_cast<size_t>(e.size.tag()));
            TU_MATCH_HDRA((e.size), {)
            TU_ARMA(Known, size) {
                        h = hashMix(h, size);
                    }
                    TU_ARMA(Unevaluated, size) {
                        h = hashMix(h, hashConstGeneric(size));
                    }
            }
            }
            TU_ARMA(Slice, e) h = hashMix(h, hashTypeRef(e.inner));
            TU_ARMA(Tuple, e) {
                for (auto t : e) {
                    h = hashMix(h, hashTypeRef(t));
                }
            }
            TU_ARMA(Borrow, e) {
                h = hashMix(h, static_cast<size_t>(e.type));
                h = hashMix(h, hashTypeRef(e.inner));
            }
            TU_ARMA(Pointer, e) {
                h = hashMix(h, static_cast<size_t>(e.type));
                h = hashMix(h, hashTypeRef(e.inner));
            }
            TU_ARMA(NamedFunction, e) {
                h = hashMix(h, hashPath(e.path));
                h = hashMix(h, static_cast<size_t>(e.def.tag()));
            }
            TU_ARMA(Function, e) {
                h = hashMix(h, ::std::hash<RcString>()(e.mAbi));
                h = hashMix(h, e.isUnsafe);
                h = hashMix(h, e.isVariadic);
                h = hashMix(h, hashTypeRef(e.mRettype));
                for (auto t : e.argTypes) {
                    h = hashMix(h, hashTypeRef(t));
                }
            }
            TU_ARMA(NodeType, e) { TU_MATCH_HDRA((e), {) TU_ARMA(Closure, p) h = hashMix(h, reinterpret_cast<uintptr_t>(p));
                    TU_ARMA(Generator, p) h = hashMix(h, reinterpret_cast<uintptr_t>(p));
                    TU_ARMA(Async, p) h = hashMix(h, reinterpret_cast<uintptr_t>(p)); }
            }
        }
        return h;
    }
}

HIRTypeRef HIRTypeInterner::intern(HIRTypeData data) {
    data.flags = typeFlags(data);
    const auto hash = hashTypeData(data);
    const auto range = nodes.equal_range(hash);
    for (auto it = range.first; it != range.second; ++it) {
        if (exactTypeDataEqual(*it->second, data)) {
            return it->second;
        }
    }
    const auto* node = pool.make<HIRTypeData>(mv$(data));
    nodes.emplace(hash, node);
    return node;
}

HIRTypeRef HIRTypeInterner::infer(unsigned int idx, HIRInferClass tyClass) {
    return intern(HIRTypeData::make_Infer({idx, tyClass}));
}

HIRTypeRef HIRTypeInterner::primitive(HIRCoreType ct) {
    return intern(HIRTypeData::make_Primitive(ct));
}

HIRTypeRef HIRTypeInterner::generic(RcString name, unsigned int slot) {
    return intern(HIRTypeData::make_Generic({mv$(name), slot}));
}

HIRTypeRef HIRTypeInterner::self() {
    return generic(RcString::newInterned("Self"), GENERICSelf);
}

HIRTypeRef HIRTypeInterner::unit() {
    return intern(HIRTypeData::make_Tuple({}));
}

HIRTypeRef HIRTypeInterner::diverge() {
    return intern(HIRTypeData::make_Diverge({}));
}

HIRTypeRef HIRTypeInterner::borrow(HIRBorrowType bt, HIRTypeRef inner) {
    return intern(HIRTypeData::make_Borrow({bt, inner}));
}

HIRTypeRef HIRTypeInterner::pointer(HIRBorrowType bt, HIRTypeRef inner) {
    return intern(HIRTypeData::make_Pointer({bt, inner}));
}

HIRTypeRef HIRTypeInterner::tuple(::std::vector<HIRTypeRef> types) {
    return intern(HIRTypeData::make_Tuple(mv$(types)));
}

HIRTypeRef HIRTypeInterner::slice(HIRTypeRef inner) {
    return intern(HIRTypeData::make_Slice({inner}));
}

HIRTypeRef HIRTypeInterner::array(HIRTypeRef inner, HIRArraySize size) {
    return intern(HIRTypeData::make_Array({inner, mv$(size)}));
}

HIRTypeRef HIRTypeInterner::array(HIRTypeRef inner, uint64_t size) {
    assert(size != ~0u);
    return intern(HIRTypeData::make_Array({inner, size}));
}

HIRTypeRef HIRTypeInterner::array(HIRTypeRef inner, HIRConstGeneric size) {
    return intern(HIRTypeData::make_Array({inner, mv$(size)}));
}

HIRTypeRef HIRTypeInterner::path(HIRPath path, HIRTypePathBinding binding, ::std::unique_ptr<HIRGenericParams> hrtbs) {
    return intern(HIRTypeData::make_Path({mv$(path), mv$(binding), mv$(hrtbs)}));
}

HIRTypeRef HIRTypeInterner::function(HIRTypeDataFunctionPointer ft) {
    return intern(HIRTypeData::make_Function(mv$(ft)));
}

HIRTypeRef HIRTypeInterner::closure(HIRExprNodeClosure* node) {
    return intern(HIRTypeData::make_NodeType(HIRTypeDataNodeType::make_Closure(node)));
}

HIRTypeRef HIRTypeInterner::generator(HIRExprNodeGenerator* node) {
    return intern(HIRTypeData::make_NodeType(HIRTypeDataNodeType::make_Generator(node)));
}

HIRTypeRef HIRTypeInterner::asyncBlock(HIRExprNodeAsyncBlock* node) {
    return intern(HIRTypeData::make_NodeType(HIRTypeDataNodeType::make_Async(node)));
}

const HIRSimplePath* HIRTypeData::getSortPath() const {
    if (TU_TEST1(*this, Path, .path.mData.is_Generic())) {
        return &as_Path().path.mData.as_Generic().mPath;
    }
    if (is_TraitObject()) {
        return &as_TraitObject().mTrait.mPath.mPath;
    }
    return nullptr;
}

Ordering ord(const TypeDataErasedTypeInner& l, const TypeDataErasedTypeInner& r);

bool HIRTypeData::equalsIgnoringRegions(HIRTypeRef x) const {
    if (this == x) {
        return true;
    }
    if (tag() != x->tag()) {
        return false;
    }

    TU_MATCH_HDRA( (*this, *x), {)
    TU_ARMA(Infer, te, xe) {
            // TODO: Should comparing inferrence vars be an error?
            return te.index == xe.index;
        }
        TU_ARMA(Diverge, te, xe) {
            return true;
        }
        TU_ARMA(Primitive, te, xe) {
            return te == xe;
        }
        TU_ARMA(Path, te, xe) {
            return te.path.equalsIgnoringRegions(xe.path);
        }
        TU_ARMA(Generic, te, xe) {
            return /*te.name == xe.name &&*/ te.binding == xe.binding;
        }
        TU_ARMA(TraitObject, te, xe) {
            if (!te.mTrait.equalsIgnoringRegions(xe.mTrait)) {
                return false;
            }
            if (te.markers.size() != xe.markers.size()) {
                return false;
            }
            for (unsigned int i = 0; i < te.markers.size(); i++) {
                if (!te.markers[i].equalsIgnoringRegions(xe.markers[i])) {
                    return false;
                }
            }
            return true;
        }
        TU_ARMA(ErasedType, te, xe) {
            return ord(te.inner, xe.inner) == OrdEqual;
        }
        TU_ARMA(Array, te, xe) {
            if (!te.inner->equalsIgnoringRegions(xe.inner)) {
                return false;
            }
            if (xe.size != te.size) {
                return false;
            }
            return true;
        }
        TU_ARMA(Slice, te, xe) {
            return te.inner->equalsIgnoringRegions(xe.inner);
        }
        TU_ARMA(Tuple, te, xe) {
            if (te.size() != xe.size()) {
                return false;
            }
            for (unsigned int i = 0; i < te.size(); i++) {
                if (!te[i]->equalsIgnoringRegions(xe[i])) {
                    return false;
                }
            }
            return true;
        }
        TU_ARMA(Borrow, te, xe) {
            if (te.type != xe.type) {
                return false;
            }
            //if( te.lifetime != xe.lifetime )
            return te.inner->equalsIgnoringRegions(xe.inner);
        }
        TU_ARMA(Pointer, te, xe) {
            if (te.type != xe.type) {
                return false;
            }
            return te.inner->equalsIgnoringRegions(xe.inner);
        }
        TU_ARMA(NamedFunction, te, xe) {
            return te.path.equalsIgnoringRegions(xe.path);
        }
        TU_ARMA(Function, te, xe) {
            if (te.isUnsafe != xe.isUnsafe) {
                return false;
            }
            if (te.mAbi != xe.mAbi) {
                return false;
            }
            if (te.argTypes.size() != xe.argTypes.size()) {
                return false;
            }
            for (unsigned int i = 0; i < te.argTypes.size(); i++) {
                if (!te.argTypes[i]->equalsIgnoringRegions(xe.argTypes[i])) {
                    return false;
                }
            }
            return te.mRettype->equalsIgnoringRegions(xe.mRettype);
        }
        TU_ARMA(NodeType, te, xe) {
            return te == xe;
        }
    }
    throw "";
}

Ordering ord(const TypeDataErasedTypeInner& l, const TypeDataErasedTypeInner& r) {
    ORD(static_cast<unsigned int>(l.tag()), static_cast<unsigned int>(r.tag()));
    TU_MATCH_HDRA( (l, r), {)
    TU_ARMA(Known, le, re) {
            return le->ordIgnoringRegions(re);
        }
        TU_ARMA(Alias, le, re) {
            if (le.inner.get() != re.inner.get()) {
                if (reinterpret_cast<uintptr_t>(le.inner.get()) < reinterpret_cast<uintptr_t>(re.inner.get())) {
                    return OrdLess;
                } else {
                    return OrdGreater;
                }
            }
            ORD(le.params, re.params);
        }
        TU_ARMA(Fcn, le, re) {
            ORD(le.origin, re.origin);
            ORD(le.index, re.index);
        }
    }
    return OrdEqual;
}

Ordering HIRTypeData::ordIgnoringRegions(HIRTypeRef x) const {
    Ordering rv;

    if (this == x) {
        return OrdEqual;
    }
    ORD(static_cast<unsigned int>(tag()), static_cast<unsigned int>(x->tag()));

    TU_MATCH(
        HIRTypeData,
        (*this, *x),
        (te, xe),
        (Infer,
         // TODO: Should comparing inferrence vars be an error?
         return ::ord(te.index, xe.index);),
        (Diverge, return OrdEqual;),
        (Primitive, return ::ord(static_cast<unsigned>(te), static_cast<unsigned>(xe));),
        (Path, return ::ord(te.path, xe.path);),
        (Generic,
         if ((rv = ::ord(te.binding, xe.binding)) != OrdEqual) return rv;
         return OrdEqual;),
        (TraitObject, ORD(te.mTrait, xe.mTrait); ORD(te.markers, xe.markers);
         return OrdEqual;),
        (ErasedType,
         ORD(te.inner, xe.inner);
         return OrdEqual;),
        (Array, ORD(te.inner, xe.inner); ORD(te.size, xe.size); return OrdEqual;),
        (Slice, return ::ord(te.inner, xe.inner);),
        (Tuple, return ::ord(te, xe);),
        (Borrow, ORD(static_cast<unsigned>(te.type), static_cast<unsigned>(xe.type)); return ::ord(te.inner, xe.inner);),
        (Pointer, ORD(static_cast<unsigned>(te.type), static_cast<unsigned>(xe.type)); return ::ord(te.inner, xe.inner);),
        (NamedFunction, return ::ord(te.path, xe.path);),
        (Function, ORD(te.isUnsafe, xe.isUnsafe); ORD(te.mAbi, xe.mAbi); ORD(te.argTypes, xe.argTypes); return ::ord(te.mRettype, xe.mRettype);),
        (NodeType, return te.ord(xe);)
    )
    throw "";
}

namespace {
    HIRCompare matchGenericsPp(const Span& sp, const HIRPathParams& t, const HIRPathParams& x, tCbResolveType resolvePlaceholder, HIRMatchGenerics& callback) {
        return t.matchTestGenericsFuzz(sp, x, resolvePlaceholder, callback);
    }

    HIRCompare matchValues(const Span& sp, const HIRConstGeneric& t, const HIRConstGeneric& x, HIRMatchGenerics& callback) {
        // LHS generic: call callback
        if (const auto* e = t.opt_Generic()) {
            return callback.matchVal(*e, x);
        }

        // Either are infer, check for exact match or return fuzzy
        if (const auto* xep = x.opt_Infer()) {
            const auto& xe = *xep;

            if (xe.index != ~0u && t.is_Infer() && t.as_Infer().index == xe.index) {
                return HIRCompare::Equal;
            }

            return HIRCompare::Fuzzy;
        }
        if (const auto* tep = t.opt_Infer()) {
            const auto& te = *tep;
            ASSERT_BUG(sp, te.index != ~0u, "Encountered ivar for `this` - " << t);
            return HIRCompare::Fuzzy;
        }

        if (t.tag() != x.tag()) {
            return HIRCompare::Unequal;
        }

        TU_MATCH_HDRA( (t,x), { )
            TU_ARMA(Infer, te,xe) throw "Unreachable";
            TU_ARMA(Unevaluated, te, xe) {
                return te->equivalent(*xe) ? HIRCompare::Equal : HIRCompare::Unequal;
            }
            TU_ARMA(Generic, te, xe) throw "Unreachable";
            TU_ARMA(Evaluated, te, xe)
            return *te == *xe ? HIRCompare::Equal : HIRCompare::Unequal;
        }
        throw "Unreachable";
    }
}

bool HIRTypeData::matchTestGenerics(const Span& sp, HIRTypeRef xIn, tCbResolveType resolvePlaceholder, HIRMatchGenerics& callback) const {
    return this->matchTestGenericsFuzz(sp, xIn, resolvePlaceholder, callback) == HIRCompare::Equal;
}

HIRCompare HIRTypeData::matchTestGenericsFuzz(const Span& sp, HIRTypeRef xIn, tCbResolveType resolvePlaceholder, HIRMatchGenerics& callback) const {
    const HIRTypeRef self = this;
    return callback.cmpType(sp, self, xIn, resolvePlaceholder);
}

HIRTrackHrbStack::PopOnDrop HIRTrackHrbStack::pushHrb(const std::unique_ptr<HIRGenericParams>& params) const {
    static HIRGenericParams emptyParams;
    return params ? pushHrb(*params) : PopOnDrop();
}

HIRCompare HIRMatchGenerics::cmpPath(const Span& sp, const HIRPath& pathL, const HIRPath& pathR, tCbResolveType resolvePlaceholder) {
    HIRCompare rv = HIRCompare::Unequal;
    if (pathL.mData.tag() != pathR.mData.tag()) {
        rv = HIRCompare::Unequal;
    } else {
        TU_MATCH_HDRA((pathL.mData, pathR.mData), {)
        TU_ARMA(Generic, tpe, xpe) {
                if (tpe.mPath != xpe.mPath) {
                    rv = HIRCompare::Unequal;
                } else {
                    rv = matchGenericsPp(sp, tpe.mParams, xpe.mParams, resolvePlaceholder, *this);
                }
            }
            TU_ARMA(UfcsKnown, tpe, xpe) {
                rv = this->cmpType(sp, tpe.type, xpe.type, resolvePlaceholder);
                if (tpe.trait.mPath != xpe.trait.mPath) {
                    rv = HIRCompare::Unequal;
                }
                rv &= matchGenericsPp(sp, tpe.trait.mParams, xpe.trait.mParams, resolvePlaceholder, *this);
                if (tpe.item != xpe.item) {
                    rv = HIRCompare::Unequal;
                }
                rv &= matchGenericsPp(sp, tpe.params, xpe.params, resolvePlaceholder, *this);
            }
            TU_ARMA(UfcsUnknown, tpe, xpe) {
                rv = this->cmpType(sp, tpe.type, xpe.type, resolvePlaceholder);
                if (tpe.item != xpe.item) {
                    rv = HIRCompare::Unequal;
                }
                rv &= matchGenericsPp(sp, tpe.params, xpe.params, resolvePlaceholder, *this);
            }
            TU_ARMA(UfcsInherent, tpe, xpe) {
                rv = this->cmpType(sp, tpe.type, xpe.type, resolvePlaceholder);
                if (tpe.item != xpe.item) {
                    rv = HIRCompare::Unequal;
                }
                rv &= matchGenericsPp(sp, tpe.params, xpe.params, resolvePlaceholder, *this);
            }
        }
    }
    DEBUG("rv = " << rv);
    return rv;
}

HIRCompare HIRMatchGenerics::cmpType(const Span& sp, const HIRTypeData* tyL, const HIRTypeData* tyR, tCbResolveType resolvePlaceholder) {
    if (const auto* e = tyL->opt_Generic()) {
        return this->matchTy(*e, tyR, resolvePlaceholder);
    }
    const auto& v = (tyL->is_Infer() ? resolvePlaceholder.getType(sp, tyL) : tyL);
    const auto& x = (tyR->is_Infer() || tyR->is_Generic() ? resolvePlaceholder.getType(sp, tyR) : tyR);
    TRACE_FUNCTION_F(tyL << ", " << tyR << " -- " << v << ", " << x);
    // If `x` is an ivar - This can be a fuzzy match.
    if (const auto* xep = x->opt_Infer()) {
        const auto& xe = *xep;
        // - If type inferrence is active (i.e. this ivar has an index), AND both `v` and `x` refer to the same ivar slot
        if (xe.index != ~0u && v->is_Infer() && v->as_Infer().index == xe.index) {
            // - They're equal (no fuzzyness about it)
            return HIRCompare::Equal;
        }
        switch (xe.tyClass) {
            case HIRInferClass::None:
                // TODO: Have another callback (optional?) that allows the caller to equate `v` somehow
                // - Very niche?
                return HIRCompare::Fuzzy;
            case HIRInferClass::Integer:
                if (const auto* te = v->opt_Primitive()) {
                    switch (*te) {
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
                            return HIRCompare::Fuzzy;
                        default:
                            DEBUG("- Fuzz fail");
                            return HIRCompare::Unequal;
                    }
                }
                break;
            case HIRInferClass::Float:
                if (const auto* te = v->opt_Primitive()) {
                    switch (*te) {
                        case HIRCoreType::F16:
                        case HIRCoreType::F32:
                        case HIRCoreType::F64:
                        case HIRCoreType::F128:
                            return HIRCompare::Fuzzy;
                        default:
                            DEBUG("- Fuzz fail");
                            return HIRCompare::Unequal;
                    }
                }
                break;
        }
    }

    if (const auto* tep = v->opt_Infer()) {
        const auto& te = *tep;
        // TODO: Restrict this block with a flag so it panics if an ivar is seen when not expected
        ASSERT_BUG(sp, te.index != ~0u, "Encountered ivar for `this` - " << v);

        switch (te.tyClass) {
            case HIRInferClass::None:
                // TODO: Have another callback (optional?) that allows the caller to equate `v` somehow
                // - Very niche?
                return HIRCompare::Fuzzy;
            case HIRInferClass::Integer:
                if (const auto* xe = x->opt_Primitive()) {
                    switch (*xe) {
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
                            return HIRCompare::Fuzzy;
                        default:
                            DEBUG("- Fuzz fail");
                            return HIRCompare::Unequal;
                    }
                }
                break;
            case HIRInferClass::Float:
                if (const auto* xe = x->opt_Primitive()) {
                    switch (*xe) {
                        case HIRCoreType::F16:
                        case HIRCoreType::F32:
                        case HIRCoreType::F64:
                        case HIRCoreType::F128:
                            return HIRCompare::Fuzzy;
                        default:
                            DEBUG("- Fuzz fail");
                            return HIRCompare::Unequal;
                    }
                }
                break;
        }
    }

    const auto unresolvedErasedAlias = [](const HIRTypeData* ty) {
        const auto* erased = ty->opt_ErasedType();
        const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
        return alias && !alias->inner->type;
    };
    if (unresolvedErasedAlias(v) || unresolvedErasedAlias(x)) {
        DEBUG("- Fuzzy match due to unresolved opaque alias - " << v << " = " << x);
        return HIRCompare::Fuzzy;
    }

    // MatchGenerics is a relation, not plain type equality.  Its callbacks can
    // bind lifetimes and generic parameters while walking two identical
    // interned types, so pointer identity must not bypass the structural walk.
    if (v->tag() != x->tag()) {
        // HACK: If the path is Opaque, return a fuzzy match.
        // - This works around an impl selection bug.
        if (v->is_Path() && v->as_Path().binding.is_Opaque()) {
            DEBUG("- Fuzzy match due to opaque - " << v << " = " << x);
            return HIRCompare::Fuzzy;
        }
        // HACK: If RHS is unbound, fuzz it
        if (x->is_Path() && x->as_Path().binding.is_Unbound()) {
            DEBUG("- Fuzzy match due to unbound - " << v << " = " << x);
            return HIRCompare::Fuzzy;
        }
        if (v->is_Path() && v->as_Path().binding.is_Unbound()) {
            DEBUG("- Fuzzy match due to unbound - " << v << " = " << x);
            return HIRCompare::Fuzzy;
        }
        // HACK: If the RHS is a placeholder generic, allow it.
        if (x->is_Generic() && (x->as_Generic().binding >> 8) == 2) {
            DEBUG("- Fuzzy match due to placeholder - " << v << " = " << x);
            return HIRCompare::Fuzzy;
        }
        DEBUG("- Tag mismatch " << v << " and " << x);
        return HIRCompare::Unequal;
    }
    TU_MATCH_HDRA( (*v, *x), { )
    TU_ARMA(Infer, te, xe) {
            // Both sides are infer
            switch (te.tyClass) {
                case HIRInferClass::None:
                    return HIRCompare::Fuzzy;
                default:
                    switch (xe.tyClass) {
                        case HIRInferClass::None:
                            return HIRCompare::Fuzzy;
                        default:
                            if (te.tyClass != xe.tyClass) {
                                return HIRCompare::Unequal;
                            }
                            return HIRCompare::Fuzzy;
                    }
            }
        }
        TU_ARMA(Generic, te, xe) throw "";
        TU_ARMA(Primitive, te, xe) {
            return (te == xe ? HIRCompare::Equal : HIRCompare::Unequal);
        }
        TU_ARMA(Diverge, te, xe) {
            return HIRCompare::Equal;
        }
        TU_ARMA(Path, te, xe) {
            auto rv = this->cmpPath(sp, te.path, xe.path, resolvePlaceholder);

            if (rv == HIRCompare::Unequal) {
                if (te.binding.is_Unbound() || xe.binding.is_Unbound()) {
                    rv = HIRCompare::Fuzzy;
                }
                if (te.binding.is_Opaque()) {
                    DEBUG("- Fuzzy match due to opaque");
                    return HIRCompare::Fuzzy;
                }
            }
            return rv;
        }
        TU_ARMA(TraitObject, te, xe) {
            if (te.mTrait.mPath.mPath != xe.mTrait.mPath.mPath) {
                return HIRCompare::Unequal;
            }
            if (te.markers.size() != xe.markers.size()) {
                return HIRCompare::Unequal;
            }
            static const HIRGenericParams emptyParams;
            auto _ = pushHrb(te.mTrait.hrtbs ? *te.mTrait.hrtbs : emptyParams);
            auto cmp = matchGenericsPp(sp, te.mTrait.mPath.mParams, xe.mTrait.mPath.mParams, resolvePlaceholder, *this);
            for (unsigned int i = 0; i < te.markers.size(); i++) {
                if (te.markers[i].mPath != xe.markers[i].mPath) {
                    return HIRCompare::Unequal;
                }
                cmp &= matchGenericsPp(sp, te.markers[i].mParams, xe.markers[i].mParams, resolvePlaceholder, *this);
            }

            auto itL = te.mTrait.typeBounds.begin();
            auto itR = xe.mTrait.typeBounds.begin();
            while (itL != te.mTrait.typeBounds.end() && itR != xe.mTrait.typeBounds.end()) {
                if (itL->first != itR->first) {
                    return HIRCompare::Unequal;
                }
                cmp &= itL->second.type->matchTestGenericsFuzz(sp, itR->second.type, resolvePlaceholder, *this);
                ++itL;
                ++itR;
            }

            if (itL != te.mTrait.typeBounds.end() || itR != xe.mTrait.typeBounds.end()) {
                return HIRCompare::Unequal;
            }

            return cmp;
        }
        TU_ARMA(ErasedType, te, xe) {
            if (te.inner.tag() != xe.inner.tag()) {
                return HIRCompare::Unequal;
            }
            TU_MATCH_HDRA((te.inner, xe.inner), {)
            TU_ARMA(Known, l, r) return l->matchTestGenericsFuzz(sp, r, resolvePlaceholder, *this);
                TU_ARMA(Alias, l, r) {
                    return l.inner == r.inner ? l.params.matchTestGenericsFuzz(sp, r.params, resolvePlaceholder, *this) : HIRCompare::Unequal;
                }
                TU_ARMA(Fcn, l, r) {
                    return l.index == r.index ? this->cmpPath(sp, l.origin, r.origin, resolvePlaceholder) : HIRCompare::Unequal;
                }
            }
            throw "";
        }
        TU_ARMA(Array, te, xe) {
            auto rv = HIRCompare::Equal;
            if (const auto* tse = te.size.opt_Unevaluated()) {
                HIRConstGeneric v;
                if (xe.size.opt_Known()) {
                    rv &= matchValues(sp, *tse, HIREncodedLiteralPtr(EncodedLiteral::makeUsize(xe.size.as_Known())), *this);
                } else {
                    rv &= matchValues(sp, *tse, xe.size.as_Unevaluated(), *this);
                }
            } else if (const auto* xse = xe.size.opt_Unevaluated()) {
                // `te.size` must be known here, all we need to handle is `Infer`?
                if (xse->is_Infer()) {
                    rv &= HIRCompare::Fuzzy;
                } else {
                    ASSERT_BUG(sp, !xse->is_Evaluated(), "TODO: Handle " << te.size << " ?= " << xe.size);
                    // - Evaluated? (TODO - could use `EncodedLiteralPtr( EncodedLiteral::make_usize(te.size.as_Known()) )`)
                    // - Generic - could only match with another generic, i.e. `tse` must have been `Unevaluated,Generic`
                    // - Unevaluated - could only match with another Unevaluated, i.e. `tse` must have been `Unevaluated,Unevaluated`
                    return HIRCompare::Unequal;
                }
            } else if (te.size != xe.size) {
                return HIRCompare::Unequal;
            }
            return this->cmpType(sp, te.inner, xe.inner, resolvePlaceholder);
        }
        TU_ARMA(Slice, te, xe) {
            return this->cmpType(sp, te.inner, xe.inner, resolvePlaceholder);
        }
        TU_ARMA(Tuple, te, xe) {
            if (te.size() != xe.size()) {
                return HIRCompare::Unequal;
            }
            auto rv = HIRCompare::Equal;
            for (unsigned int i = 0; i < te.size(); i++) {
                rv &= this->cmpType(sp, te[i], xe[i], resolvePlaceholder);
                if (rv == HIRCompare::Unequal) {
                    return HIRCompare::Unequal;
                }
            }
            return rv;
        }
        TU_ARMA(Pointer, te, xe) {
            if (te.type != xe.type) {
                return HIRCompare::Unequal;
            }
            return this->cmpType(sp, te.inner, xe.inner, resolvePlaceholder);
        }
        TU_ARMA(Borrow, te, xe) {
            if (te.type != xe.type) {
                return HIRCompare::Unequal;
            }
            auto rv = HIRCompare::Equal;
            rv &= this->cmpType(sp, te.inner, xe.inner, resolvePlaceholder);
            return rv;
        }
        TU_ARMA(NamedFunction, te, xe) {
            return this->cmpPath(sp, te.path, xe.path, resolvePlaceholder);
        }
        TU_ARMA(Function, te, xe) {
            if (te.isUnsafe != xe.isUnsafe) {
                return HIRCompare::Unequal;
            }
            if (te.mAbi != xe.mAbi) {
                return HIRCompare::Unequal;
            }
            if (te.argTypes.size() != xe.argTypes.size()) {
                return HIRCompare::Unequal;
            }
            auto _ = pushHrb(te.hrls);
            auto rv = HIRCompare::Equal;
            for (unsigned int i = 0; i < te.argTypes.size(); i++) {
                rv &= this->cmpType(sp, te.argTypes[i], xe.argTypes[i], resolvePlaceholder);
                if (rv == HIRCompare::Unequal) {
                    return rv;
                }
            }
            rv &= this->cmpType(sp, te.mRettype, xe.mRettype, resolvePlaceholder);
            return rv;
        }
        TU_ARMA(NodeType, te, xe) {
            return te == xe ? HIRCompare::Equal : HIRCompare::Unequal;
        }
    }
    throw "";
}

HIRTypePathBinding HIRTypePathBinding::clone() const {
    TU_MATCH(HIRTypePathBinding, (*this), (e), (Unbound, return HIRTypePathBinding::make_Unbound({});), (Opaque, return HIRTypePathBinding::make_Opaque({});), (ExternType, return HIRTypePathBinding(e);), (Struct, return HIRTypePathBinding(e);), (Union, return HIRTypePathBinding(e);), (Enum, return HIRTypePathBinding(e);))
    assert(!"Fell off end of clone_binding");
    throw "";
}

bool HIRTypePathBinding::operator==(const HIRTypePathBinding& x) const {
    if (this->tag() != x.tag()) {
        return false;
    }
    TU_MATCH(HIRTypePathBinding, (*this, x), (te, xe), (Unbound, return true;), (Opaque, return true;), (ExternType, return te == xe;), (Struct, return te == xe;), (Union, return te == xe;), (Enum, return te == xe;))
    throw "";
}

const HIRTraitMarkings* HIRTypePathBinding::getTraitMarkings() const {
    const HIRTraitMarkings* markingsPtr = nullptr;
    TU_MATCHA((*this), (tpb), (Unbound, ), (Opaque, ), (ExternType, markingsPtr = &tpb->markings;), (Struct, markingsPtr = &tpb->markings;), (Union, markingsPtr = &tpb->markings;), (Enum, markingsPtr = &tpb->markings;))
    return markingsPtr;
}

const HIRGenericParams* HIRTypePathBinding::getGenerics() const {
    const HIRGenericParams* rv = nullptr;
    TU_MATCH_HDRA( (*this), {)
    TU_ARMA(Unbound, tpb) {
        }
        TU_ARMA(Opaque, tpb) {
        }
        TU_ARMA(ExternType, tpb) {
        }
        TU_ARMA(Struct, tpb) rv = &tpb->mParams;
        TU_ARMA(Union, tpb) rv = &tpb->mParams;
        TU_ARMA(Enum, tpb) rv = &tpb->mParams;
    }
    return rv;
}

HIRTypeDataNamedFunctionTy HIRTypeDataNamedFunctionTy::clone() const {
    TU_MATCH_HDRA( (*this), { )
    TU_ARMA(Function, e)    return e;
        TU_ARMA(EnumConstructor, e) return e;
        TU_ARMA(StructConstructor, e) return e;
    }
    throw "";
}

HIRTypeData HIRTypeData::cloneData() const {
    TU_MATCH_HDRA( (*this), {)
    TU_ARMA(Infer, e) {
            return HIRTypeData::make_Infer(e);
        }
        TU_ARMA(Diverge, e) {
            return HIRTypeData::make_Diverge({});
        }
        TU_ARMA(Primitive, e) {
            return HIRTypeData::make_Primitive(e);
        }
        TU_ARMA(Path, e) {
            return HIRTypeData::make_Path({e.path.clone(), e.binding.clone(), e.hrtbs ? ::std::make_unique<HIRGenericParams>(e.hrtbs->clone()) : nullptr});
        }
        TU_ARMA(Generic, e) {
            return HIRTypeData::make_Generic(e);
        }
        TU_ARMA(TraitObject, e) {
            HIRTypeData::Data_TraitObject rv;
            rv.mTrait = e.mTrait.clone();
            for (const auto& trait : e.markers) {
                rv.markers.push_back(trait.clone());
            }
            return HIRTypeData::make_TraitObject(mv$(rv));
        }
        TU_ARMA(ErasedType, e) {
            ::std::vector<HIRTraitPath> traits;
            traits.reserve(e.traits.size());
            for (const auto& trait : e.traits) {
                traits.push_back(trait.clone());
            }

            TypeDataErasedTypeInner inner;
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Fcn, ee) {
                    inner = TypeDataErasedTypeInner::Data_Fcn{ee.origin.clone(), ee.index};
                }
                TU_ARMA(Known, ee) inner = ee;
                TU_ARMA(Alias, ee) {
                    inner = TypeDataErasedTypeInner::Data_Alias{ee.params.clone(), ee.inner};
                }
        }
        return HIRTypeData::make_ErasedType({
            e.isSized,
            mv$(traits),
            mv$(inner),
            e.use.clone(),
            e.usePresent
            });
        }
        TU_ARMA(Array, e) {
            return HIRTypeData::make_Array({e.inner, e.size.clone()});
        }
        TU_ARMA(Slice, e) {
            return HIRTypeData::make_Slice({e.inner});
        }
        TU_ARMA(Tuple, e) {
            ::std::vector<HIRTypeRef> types;
            for (const auto& t : e) {
                types.push_back(t);
            }
            return HIRTypeData::make_Tuple(mv$(types));
        }
        TU_ARMA(Borrow, e) {
            return HIRTypeData::make_Borrow({e.type, e.inner});
        }
        TU_ARMA(Pointer, e) {
            return HIRTypeData::make_Pointer({e.type, e.inner});
        }
        TU_ARMA(NamedFunction, e) {
            return HIRTypeData::make_NamedFunction({e.path.clone(), e.def.clone()});
        }
        TU_ARMA(Function, e) {
            HIRTypeDataFunctionPointer ft{e.hrls.clone(), e.isUnsafe, e.isVariadic, e.mAbi, e.mRettype, {}};
            for (const auto& a : e.argTypes) {
                ft.argTypes.push_back(a);
            }
            return HIRTypeData::make_Function(mv$(ft));
        }
        TU_ARMA(NodeType, e) {
            return HIRTypeData::make_NodeType(e.clone());
        }
    }
    throw "";
}

HIRCompare HIRTypeData::compareWithPlaceholders(const Span& sp, HIRTypeRef x, tCbResolveType resolvePlaceholder) const {
    const HIRTypeRef self = this;
    const auto& left = resolvePlaceholder.getType(sp, self);
    const auto& right = resolvePlaceholder.getType(sp, x);

    // If the two types are the same ivar, return equal
    if (left->is_Infer() && left == right) {
        return HIRCompare::Equal;
    }

    // Unbound paths and placeholder generics
    if (left->tag() != right->tag()) {
        if (left->is_Path() && left->as_Path().binding.is_Unbound()) {
            return HIRCompare::Fuzzy;
        }
        if (right->is_Path() && right->as_Path().binding.is_Unbound()) {
            return HIRCompare::Fuzzy;
        }
        if (left->is_Generic() && (left->as_Generic().binding >> 8) == 2) {
            return HIRCompare::Fuzzy;
        }
        if (right->is_Generic() && (right->as_Generic().binding >> 8) == 2) {
            return HIRCompare::Fuzzy;
        }
    }

    // If left is infer
    if (const auto* e = left->opt_Infer()) {
        switch (e->tyClass) {
            case HIRInferClass::None:
                return HIRCompare::Fuzzy;
            case HIRInferClass::Integer:
            TU_MATCH_HDRA( (*right), {)
            default:
                return HIRCompare::Unequal;
                    TU_ARMA(Primitive, re) {
                        switch (re) {
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
                                return HIRCompare::Fuzzy;
                            default:
                                return HIRCompare::Unequal;
                        }
                    }
                    TU_ARMA(Infer, re) {
                        switch (re.tyClass) {
                            case HIRInferClass::None:
                            case HIRInferClass::Integer:
                                return HIRCompare::Fuzzy;
                            case HIRInferClass::Float:
                                return HIRCompare::Unequal;
                        }
                    }
                    TU_ARMA(Path, re) {
                        return re.binding.is_Unbound() ? HIRCompare::Fuzzy : HIRCompare::Unequal;
                    }
            }
        case HIRInferClass::Float:
            TU_MATCH_HDRA( (*right), {)
            default:
                return HIRCompare::Unequal;
                    TU_ARMA(Primitive, re) {
                        switch (re) {
                            case HIRCoreType::F16:
                            case HIRCoreType::F32:
                            case HIRCoreType::F64:
                            case HIRCoreType::F128:
                                return HIRCompare::Fuzzy;
                            default:
                                return HIRCompare::Unequal;
                        }
                    }
                    TU_ARMA(Infer, re) {
                        switch (re.tyClass) {
                            case HIRInferClass::None:
                            case HIRInferClass::Float:
                                return HIRCompare::Fuzzy;
                            case HIRInferClass::Integer:
                                return HIRCompare::Unequal;
                        }
                    }
                    TU_ARMA(Path, re) {
                        return re.binding.is_Unbound() ? HIRCompare::Fuzzy : HIRCompare::Unequal;
                    }
            }
        }
        throw "";
    }

    // If righthand side is infer, it's a fuzzy match (or not a match)
    if (const auto* re = right->opt_Infer()) {
        switch (re->tyClass) {
            case HIRInferClass::None:
                return HIRCompare::Fuzzy;
            case HIRInferClass::Integer:
            TU_MATCH_HDRA( (*left), {)
            default:
                return HIRCompare::Unequal;
                    TU_ARMA(Primitive, le) {
                        switch (le) {
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
                                return HIRCompare::Fuzzy;
                            default:
                                return HIRCompare::Unequal;
                        }
                    }
                    TU_ARMA(Path, le) {
                        return le.binding.is_Unbound() ? HIRCompare::Fuzzy : HIRCompare::Unequal;
                    }
            }
        case HIRInferClass::Float:
            TU_MATCH_HDRA( (*left), {)
            default:
                return HIRCompare::Unequal;
                    TU_ARMA(Primitive, le) {
                        switch (le) {
                            case HIRCoreType::F16:
                            case HIRCoreType::F32:
                            case HIRCoreType::F64:
                            case HIRCoreType::F128:
                                return HIRCompare::Fuzzy;
                            default:
                                return HIRCompare::Unequal;
                        }
                    }
                    TU_ARMA(Path, le) {
                        return le.binding.is_Unbound() ? HIRCompare::Fuzzy : HIRCompare::Unequal;
                    }
            }
        }
        throw "";
    }

    // If righthand is a type parameter, it can only match another type parameter
    // - See `(Generic,` below

    if (left->tag() != right->tag()) {
        return HIRCompare::Unequal;
    }
    TU_MATCH_HDRA( (*left, *right), {)
    TU_ARMA(Infer, le, re) {
            assert(!"infer");
        }
        TU_ARMA(Diverge, le, re) {
            return HIRCompare::Equal;
        }
        TU_ARMA(Primitive, le, re) {
            return (le == re ? HIRCompare::Equal : HIRCompare::Unequal);
        }
        TU_ARMA(Path, le, re) {
            auto rv = le.path.compareWithPlaceholders(sp, re.path, resolvePlaceholder);
            if (rv == HIRCompare::Unequal) {
                if (le.binding.is_Unbound() || re.binding.is_Unbound()) {
                    rv = HIRCompare::Fuzzy;
                }
            }
            return rv;
        }
        TU_ARMA(Generic, le, re) {
            if (le.binding != re.binding) {
                if ((le.binding >> 8) == 2) {
                    return HIRCompare::Fuzzy;
                }
                if ((re.binding >> 8) == 2) {
                    return HIRCompare::Fuzzy;
                }
                return HIRCompare::Unequal;
            }
            return HIRCompare::Equal;
        }
        TU_ARMA(TraitObject, le, re) {
            if (le.markers.size() != re.markers.size()) {
                return HIRCompare::Unequal;
            }
            auto rv = le.mTrait.compareWithPlaceholders(sp, re.mTrait, resolvePlaceholder);
            if (rv == HIRCompare::Unequal) {
                return rv;
            }
            for (unsigned int i = 0; i < le.markers.size(); i++) {
                auto rv2 = le.markers[i].compareWithPlaceholders(sp, re.markers[i], resolvePlaceholder);
                if (rv2 == HIRCompare::Unequal) {
                    return HIRCompare::Unequal;
                }
                if (rv2 == HIRCompare::Fuzzy) {
                    rv = HIRCompare::Fuzzy;
                }
            }
            return rv;
        }
        TU_ARMA(ErasedType, le, re) {
            if (le.inner.tag() != re.inner.tag()) {
                return HIRCompare::Unequal;
            }
        TU_MATCH_HDRA( (le.inner, re.inner), {)
        TU_ARMA(Known, l,r) {
                    return l->compareWithPlaceholders(sp, r, resolvePlaceholder);
                }
                TU_ARMA(Alias, l, r) {
                    if (l.inner != r.inner) {
                        return HIRCompare::Unequal;
                    }
                    return l.params.compareWithPlaceholders(sp, r.params, resolvePlaceholder);
                }
                TU_ARMA(Fcn, l, r) {
                    if (l.index != r.index) {
                        return HIRCompare::Unequal;
                    }
                    return l.origin.compareWithPlaceholders(sp, r.origin, resolvePlaceholder);
                }
        }
        return HIRCompare::Equal;
        }
        TU_ARMA(Array, le, re) {
            auto rv = HIRCompare::Equal;
            if (le.size.is_Unevaluated() && le.size.as_Unevaluated().is_Infer()) {
                rv &= HIRCompare::Fuzzy;
            } else if (re.size.is_Unevaluated() && re.size.as_Unevaluated().is_Infer()) {
                rv &= HIRCompare::Fuzzy;
            } else if (le.size != re.size) {
                return HIRCompare::Unequal;
            } else {
                // Sizes equal
            }
            rv &= le.inner->compareWithPlaceholders(sp, re.inner, resolvePlaceholder);
            return rv;
        }
        TU_ARMA(Slice, le, re) {
            return le.inner->compareWithPlaceholders(sp, re.inner, resolvePlaceholder);
        }
        TU_ARMA(Tuple, le, re) {
            if (le.size() != re.size()) {
                return HIRCompare::Unequal;
            }
            auto rv = HIRCompare::Equal;
            for (unsigned int i = 0; i < le.size(); i++) {
                auto rv2 = le[i]->compareWithPlaceholders(sp, re[i], resolvePlaceholder);
                if (rv2 == HIRCompare::Unequal) {
                    return HIRCompare::Unequal;
                }
                if (rv2 == HIRCompare::Fuzzy) {
                    rv = HIRCompare::Fuzzy;
                }
            }
            return rv;
        }
        TU_ARMA(Borrow, le, re) {
            if (le.type != re.type) {
                return HIRCompare::Unequal;
            }
            return le.inner->compareWithPlaceholders(sp, re.inner, resolvePlaceholder);
        }
        TU_ARMA(Pointer, le, re) {
            if (le.type != re.type) {
                return HIRCompare::Unequal;
            }
            return le.inner->compareWithPlaceholders(sp, re.inner, resolvePlaceholder);
        }
        TU_ARMA(NamedFunction, le, re) {
            return le.path.compareWithPlaceholders(sp, re.path, resolvePlaceholder);
        }
        TU_ARMA(Function, le, re) {
            if (le.mAbi != re.mAbi || le.isUnsafe != re.isUnsafe) {
                return HIRCompare::Unequal;
            }
            if (le.argTypes.size() != re.argTypes.size()) {
                return HIRCompare::Unequal;
            }
            auto rv = HIRCompare::Equal;
            for (unsigned int i = 0; i < le.argTypes.size(); i++) {
                rv &= le.argTypes[i]->compareWithPlaceholders(sp, re.argTypes[i], resolvePlaceholder);
                if (rv == HIRCompare::Unequal) {
                    return HIRCompare::Unequal;
                }
            }
            rv &= le.mRettype->compareWithPlaceholders(sp, re.mRettype, resolvePlaceholder);
            return rv;
        }
        TU_ARMA(NodeType, le, re) {
            return le == re ? HIRCompare::Equal : HIRCompare::Unequal;
        }
    }
    throw "";
}

HIRTypeInterner::HIRTypeInterner(stl::ObjPool& pool)
    : pool(pool)
{
}

bool isInteger(const HIRCoreType& v) {
    switch (v) {
        case HIRCoreType::Usize:
        case HIRCoreType::Isize:
        case HIRCoreType::U8:
        case HIRCoreType::I8:
        case HIRCoreType::U16:
        case HIRCoreType::I16:
        case HIRCoreType::U32:
        case HIRCoreType::I32:
        case HIRCoreType::U64:
        case HIRCoreType::I64:
        case HIRCoreType::U128:
        case HIRCoreType::I128:
            return true;
        default:
            return false;
    }
}

bool isFloat(const HIRCoreType& v) {
    switch (v) {
        case HIRCoreType::F16:
        case HIRCoreType::F32:
        case HIRCoreType::F64:
        case HIRCoreType::F128:
            return true;
        default:
            return false;
    }
}
