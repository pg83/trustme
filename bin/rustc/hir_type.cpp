#include "hir_type.h"
#include "span.h"
#include "hir_expr.h" // ArraySize::Unevaluated cloning needs the complete expression definition.
#include <std/mem/obj_pool.h>
#include <cstdint>

namespace HIR {

    ::std::ostream& operator<<(::std::ostream& os, const ::HIR::TypeData* ty) {
        if (ty) {
            ty->fmt(os);
        } else {
            os << "NULL";
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const CoreType& ct) {
        switch (ct) {
            case CoreType::Usize:
                return os << "usize";
            case CoreType::Isize:
                return os << "isize";
            case CoreType::U8:
                return os << "u8";
            case CoreType::I8:
                return os << "i8";
            case CoreType::U16:
                return os << "u16";
            case CoreType::I16:
                return os << "i16";
            case CoreType::U32:
                return os << "u32";
            case CoreType::I32:
                return os << "i32";
            case CoreType::U64:
                return os << "u64";
            case CoreType::I64:
                return os << "i64";
            case CoreType::U128:
                return os << "u128";
            case CoreType::I128:
                return os << "i128";

            case CoreType::F16:
                return os << "f16";
            case CoreType::F32:
                return os << "f32";
            case CoreType::F64:
                return os << "f64";
            case CoreType::F128:
                return os << "f128";

            case CoreType::Bool:
                return os << "bool";
            case CoreType::Char:
                return os << "char";
            case CoreType::Str:
                return os << "str";
        }
        assert(!"Bad CoreType value");
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const BorrowType& bt) {
        switch (bt) {
            case BorrowType::Owned:
                return os << "Owned";
            case BorrowType::Unique:
                return os << "Unique";
            case BorrowType::Shared:
                return os << "Shared";
        }
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const ArraySize& x) {
        TU_MATCH_HDRA( (x), { )
        TU_ARMA(Unevaluated, se) {
                os << se;
            }
            TU_ARMA(Known, se)
            os << se;
        }
        return os;
    }
}

void HIR::GenericRef::fmt(std::ostream& os) const {
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

Ordering HIR::ArraySize::ord(const HIR::ArraySize& x) const {
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

HIR::ArraySize HIR::ArraySize::clone() const {
    TU_MATCH_HDRA( (*this), {)
    TU_ARMA(Unevaluated, se)
        return se.clone();
        TU_ARMA(Known, se)
        return se;
    }
    throw "";
}

::HIR::TypeDataErasedTypeAliasInner::TypeDataErasedTypeAliasInner(const HIR::ItemPath& p, const HIR::GenericParams& params)
    : path(p.getSimplePath())
    , type()
{
    this->generics = params.clone();
    this->generics.bounds.clear();
}

bool ::HIR::TypeDataErasedTypeAliasInner::isPublicTo(const HIR::SimplePath& p) const {
    return p.starts_with(this->path, /*skip_last=*/true);
}

::HIR::TypeDataFunctionPointer HIR::TypeData::Data_NamedFunction::decay(TypeInterner& types, const Span& sp) const {
    const ::HIR::TypeData* ty_self = nullptr;
    const ::HIR::PathParams* pp_impl = nullptr;
    const ::HIR::PathParams* pp_method = nullptr;

    TU_MATCH_HDRA( (this->def), { )
    TU_ARMA(Function, fp) {
            ASSERT_BUG(sp, fp, "Non-initialised NamedFunction definition: " << this->path);
        TU_MATCH_HDRA( (this->path.mData), {)
        TU_ARMA(Generic, pe) {
                    pp_method = &pe.mParams;
                }
                TU_ARMA(UfcsKnown, pe) {
                    ty_self = pe.type;
                    pp_impl = &pe.trait.mParams;
                    pp_method = &pe.params;
                }
                TU_ARMA(UfcsInherent, pe) {
                    ty_self = pe.type;
                    pp_impl = &pe.impl_params;
                    pp_method = &pe.params;
                }
                TU_ARMA(UfcsUnknown, pe) {
                    BUG(sp, "UfcsUnknown seen");
                }
        }
        MonomorphStatePtr   ms { types, ty_self, pp_impl, pp_method };
        const auto& f = *fp;
        ::HIR::TypeDataFunctionPointer ft {
            HIR::GenericParams(),   // TODO: Get HRLs
            f.unsafe,
            f.variadic,
            f.mAbi,
            ms.monomorphType(sp, f.returnType),
            {}
        };
        HIR::PathParams methodPpTrimmed;
        if( !f.mParams.mLifetimes.empty() )
        {
                ft.hrls.mLifetimes = f.mParams.mLifetimes;
                methodPpTrimmed = ms.pp_method->clone();
                methodPpTrimmed.mLifetimes = std::move(ft.hrls.makeNopParams(types, 3, /*lifetimes_only*/ true).mLifetimes);
                ms.pp_method = &methodPpTrimmed;
        }
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
            const auto& var_ty = enm.mData.as_Data()[ec.v].type;
            const auto& str = *var_ty->as_Path().binding.as_Struct();
            const auto& var_data = str.mData.as_Tuple();

            ::HIR::TypeDataFunctionPointer ft{
                HIR::GenericParams(), // TODO: Get HRLs
                false,
                false,
                RcString::newInterned(ABI_RUST),
                types.path(::HIR::Path(::HIR::GenericPath(mv$(enumPath), e.mParams.clone())), ::HIR::TypePathBinding::make_Enum(&enm)),
                {}
            };
            for (const auto& arg : var_data) {
                ft.argTypes.push_back(ms.monomorphType(sp, arg.ent));
            }
            return ft;
        }
        TU_ARMA(StructConstructor, p) {
            const auto& e = this->path.mData.as_Generic();
            MonomorphStatePtr ms{types, nullptr, &e.mParams, nullptr};
            ::HIR::TypeDataFunctionPointer ft{
                HIR::GenericParams(), // TODO: Get HRLs
                false,
                false,
                RcString::newInterned(ABI_RUST),
                types.path(this->path.clone(), ::HIR::TypePathBinding::make_Struct(p)),
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

void ::HIR::TypeData::fmt(::std::ostream& os) const {
    thread_local static std::vector<const HIR::TypeData*> s_recurse_stack;
    for (const auto* p : s_recurse_stack) {
        if (p == this) {
            os << "RECURSE";
            return;
        }
    }

    struct _ {
        _(const HIR::TypeData* ptr) {
            s_recurse_stack.push_back(ptr);
        }

        ~_() {
            s_recurse_stack.pop_back();
        }
    } h(this);

    TU_MATCH_HDRA( (*this), { )
    TU_ARMA(Infer, e) {
            os << "_";
            if (e.index != ~0u || e.ty_class != ::HIR::InferClass::None) {
                os << "/*";
                if (e.index != ~0u) {
                    os << e.index;
                }
                switch (e.ty_class) {
                    case ::HIR::InferClass::None:
                        break;
                    case ::HIR::InferClass::Float:
                        os << ":f";
                        break;
                    case ::HIR::InferClass::Integer:
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
            TU_MATCH(::HIR::TypePathBinding, (e.binding), (be), (Unbound, os << "/*?*/";), (Opaque, os << "/*O*/";), (ExternType, os << "/*X*/";), (Struct, os << "/*S*/";), (Union, os << "/*U*/";), (Enum, os << "/*E*/";))
        }
        TU_ARMA(Generic, e) {
            os << e;
        }
        TU_ARMA(TraitObject, e) {
            os << "dyn (";
            if (e.mTrait.mPath != ::HIR::GenericPath()) {
                os << e.mTrait;
            }
            for (const auto& tr : e.markers) {
                os << "+" << tr;
            }
            if (e.lifetime != LifetimeRef::new_static()) {
                os << "+" << e.lifetime;
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
            if (!e.lifetimeBounds.empty()) {
                for (const auto& lft : e.lifetimeBounds) {
                    os << "+" << lft;
                }
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
            os << e.lifetime << " ";
            switch (e.type) {
                case ::HIR::BorrowType::Shared:
                    os << "";
                    break;
                case ::HIR::BorrowType::Unique:
                    os << "mut ";
                    break;
                case ::HIR::BorrowType::Owned:
                    os << "move ";
                    break;
            }
            os << e.inner;
        }
        TU_ARMA(Pointer, e) {
            switch (e.type) {
                case ::HIR::BorrowType::Shared:
                    os << "*const ";
                    break;
                case ::HIR::BorrowType::Unique:
                    os << "*mut ";
                    break;
                case ::HIR::BorrowType::Owned:
                    os << "*move ";
                    break;
            }
            os << e.inner;
        }
        TU_ARMA(NamedFunction, e) {
            os << "fn{" << (e.def.is_Function() && !e.def.as_Function() ? "!" : "") << e.path << "}";
        }
        TU_ARMA(Function, e) {
            if (!e.hrls.mLifetimes.empty()) {
                os << "for" << e.hrls.fmtArgs() << " ";
            }
            if (e.is_unsafe) {
                os << "unsafe ";
            }
            if (e.mAbi != "") {
                os << "extern \"" << e.mAbi << "\" ";
            }
            os << "fn(";
            for (const auto& t : e.argTypes) {
                os << t << ", ";
            }
            if (e.is_variadic) {
                os << "...";
            }
            os << ") -> " << e.mRettype;
        }
        TU_ARMA(NodeType, e) {
            e.fmt(os);
        }
    }
}

bool HIR::TypeDataNodeType::operator==(const ::HIR::TypeDataNodeType& x) const {
    return this->ord(x) == OrdEqual;
}

Ordering HIR::TypeDataNodeType::ord(const ::HIR::TypeDataNodeType& x) const {
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

void ::HIR::TypeDataNodeType::fmt(::std::ostream& os) const {
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

::HIR::TypeDataNodeType HIR::TypeDataNodeType::clone() const {
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
    using namespace HIR;

    bool exactPathParamsEqual(const PathParams& a, const PathParams& b);
    bool exactGenericParamsEqual(const GenericParams& a, const GenericParams& b);
    bool exactTraitPathEqual(const TraitPath& a, const TraitPath& b);

    bool exactGenericRefEqual(const GenericRef& a, const GenericRef& b) {
        return a == b;
    }

    bool exactConstGenericEqual(const ConstGeneric& a, const ConstGeneric& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        TU_MATCH_HDRA((a, b), {)
        TU_ARMA(Infer, ae, be) return ae.index == be.index;
        TU_ARMA(Generic, ae, be) return exactGenericRefEqual(ae, be);
        TU_ARMA(Evaluated, ae, be) return *ae == *be;
        TU_ARMA(Unevaluated, ae, be) {
            return ae->expr.get() == be->expr.get()
                && exactPathParamsEqual(ae->params_impl, be->params_impl)
                && exactPathParamsEqual(ae->params_item, be->params_item);
        }
        }
        throw "";
    }

    bool exactPathParamsEqual(const PathParams& a, const PathParams& b) {
        if (a.mLifetimes.size() != b.mLifetimes.size()
            || a.types.size() != b.types.size()
            || a.values.size() != b.values.size()) {
            return false;
        }
        for (size_t i = 0; i < a.mLifetimes.size(); i++) {
            if (a.mLifetimes[i] != b.mLifetimes[i]) return false;
        }
        for (size_t i = 0; i < a.types.size(); i++) {
            if (a.types[i] != b.types[i]) return false;
        }
        for (size_t i = 0; i < a.values.size(); i++) {
            if (!exactConstGenericEqual(a.values[i], b.values[i])) return false;
        }
        return true;
    }

    bool exactGenericPathEqual(const GenericPath& a, const GenericPath& b) {
        return a.mPath == b.mPath && exactPathParamsEqual(a.mParams, b.mParams);
    }

    bool exactOptionalGenericParamsEqual(const ::std::unique_ptr<GenericParams>& a, const ::std::unique_ptr<GenericParams>& b) {
        return (!a && !b) || (a && b && exactGenericParamsEqual(*a, *b));
    }

    bool exactPathEqual(const Path& a, const Path& b) {
        if (a.mData.tag() != b.mData.tag()) return false;
        TU_MATCH_HDRA((a.mData, b.mData), {)
        TU_ARMA(Generic, ae, be) return exactGenericPathEqual(ae, be);
        TU_ARMA(UfcsInherent, ae, be) {
            return ae.type == be.type && ae.item == be.item
                && exactPathParamsEqual(ae.params, be.params)
                && exactPathParamsEqual(ae.impl_params, be.impl_params);
        }
        TU_ARMA(UfcsKnown, ae, be) {
            return ae.type == be.type && exactGenericPathEqual(ae.trait, be.trait)
                && ae.item == be.item && exactPathParamsEqual(ae.params, be.params)
                && exactOptionalGenericParamsEqual(ae.hrtbs, be.hrtbs);
        }
        TU_ARMA(UfcsUnknown, ae, be) {
            return ae.type == be.type && ae.item == be.item
                && exactPathParamsEqual(ae.params, be.params);
        }
        }
        throw "";
    }

    bool exactTraitPathEqual(const TraitPath& a, const TraitPath& b) {
        if (!exactOptionalGenericParamsEqual(a.hrtbs, b.hrtbs)
            || !exactGenericPathEqual(a.mPath, b.mPath)
            || a.lifetimeElision != b.lifetimeElision
            || a.traitPtr != b.traitPtr
            || a.typeBounds.size() != b.typeBounds.size()
            || a.traitBounds.size() != b.traitBounds.size()) {
            return false;
        }
        auto ai = a.typeBounds.begin();
        auto bi = b.typeBounds.begin();
        for (; ai != a.typeBounds.end(); ++ai, ++bi) {
            if (ai->first != bi->first
                || !exactGenericPathEqual(ai->second.source_trait, bi->second.source_trait)
                || !exactPathParamsEqual(ai->second.atyParams, bi->second.atyParams)
                || ai->second.type != bi->second.type) return false;
        }
        auto ati = a.traitBounds.begin();
        auto bti = b.traitBounds.begin();
        for (; ati != a.traitBounds.end(); ++ati, ++bti) {
            if (ati->first != bti->first
                || !exactGenericPathEqual(ati->second.source_trait, bti->second.source_trait)
                || !exactPathParamsEqual(ati->second.atyParams, bti->second.atyParams)
                || ati->second.traits.size() != bti->second.traits.size()) return false;
            for (size_t i = 0; i < ati->second.traits.size(); i++) {
                if (!exactTraitPathEqual(ati->second.traits[i], bti->second.traits[i])) return false;
            }
        }
        return true;
    }

    bool exactGenericBoundEqual(const GenericBound& a, const GenericBound& b) {
        if (a.tag() != b.tag()) return false;
        TU_MATCH_HDRA((a, b), {)
        TU_ARMA(Lifetime, ae, be) return ae.test == be.test && ae.valid_for == be.valid_for;
        TU_ARMA(TypeLifetime, ae, be) return ae.type == be.type && ae.valid_for == be.valid_for;
        TU_ARMA(TraitBound, ae, be) {
            return exactOptionalGenericParamsEqual(ae.hrtbs, be.hrtbs)
                && ae.type == be.type && exactTraitPathEqual(ae.trait, be.trait);
        }
        TU_ARMA(TypeEquality, ae, be) return ae.type == be.type && ae.otherType == be.otherType;
        }
        throw "";
    }

    bool exactGenericParamsEqual(const GenericParams& a, const GenericParams& b) {
        if (a.types.size() != b.types.size()
            || a.mLifetimes.size() != b.mLifetimes.size()
            || a.values.size() != b.values.size()
            || a.bounds.size() != b.bounds.size()) return false;
        for (size_t i = 0; i < a.types.size(); i++) {
            if (a.types[i].mName != b.types[i].mName
                || a.types[i].defaultValue != b.types[i].defaultValue
                || a.types[i].isSized != b.types[i].isSized) return false;
        }
        for (size_t i = 0; i < a.mLifetimes.size(); i++) {
            if (a.mLifetimes[i].mName != b.mLifetimes[i].mName) return false;
        }
        for (size_t i = 0; i < a.values.size(); i++) {
            if (a.values[i].mName != b.values[i].mName
                || a.values[i].mType != b.values[i].mType
                || !exactConstGenericEqual(a.values[i].defaultValue, b.values[i].defaultValue)) return false;
        }
        for (size_t i = 0; i < a.bounds.size(); i++) {
            if (!exactGenericBoundEqual(a.bounds[i], b.bounds[i])) return false;
        }
        return true;
    }

    bool exactBindingEqual(const TypePathBinding& a, const TypePathBinding& b) {
        if (a.tag() != b.tag()) return false;
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
        if (a.tag() != b.tag()) return false;
        TU_MATCH_HDRA((a, b), {)
        TU_ARMA(Fcn, ae, be) return ae.index == be.index && exactPathEqual(ae.origin, be.origin);
        TU_ARMA(Known, ae, be) return ae == be;
        TU_ARMA(Alias, ae, be) return ae.inner.get() == be.inner.get() && exactPathParamsEqual(ae.params, be.params);
        }
        throw "";
    }

    bool exactArraySizeEqual(const ArraySize& a, const ArraySize& b) {
        if (a.tag() != b.tag()) return false;
        TU_MATCH_HDRA((a, b), {)
        TU_ARMA(Known, ae, be) return ae == be;
        TU_ARMA(Unevaluated, ae, be) return exactConstGenericEqual(ae, be);
        }
        throw "";
    }

    bool exactTypeDataEqual(const TypeData& a, const TypeData& b) {
        if (a.tag() != b.tag()) return false;
        TU_MATCH_HDRA((a, b), {)
        TU_ARMA(Infer, ae, be) return ae.index == be.index && ae.ty_class == be.ty_class;
        TU_ARMA(Diverge, ae, be) return true;
        TU_ARMA(Primitive, ae, be) return ae == be;
        TU_ARMA(Path, ae, be) {
            return exactPathEqual(ae.path, be.path) && exactBindingEqual(ae.binding, be.binding)
                && exactOptionalGenericParamsEqual(ae.hrtbs, be.hrtbs);
        }
        TU_ARMA(Generic, ae, be) return exactGenericRefEqual(ae, be);
        TU_ARMA(TraitObject, ae, be) {
            if (!exactTraitPathEqual(ae.mTrait, be.mTrait)
                || ae.lifetime != be.lifetime || ae.markers.size() != be.markers.size()) return false;
            for (size_t i = 0; i < ae.markers.size(); i++) {
                if (!exactGenericPathEqual(ae.markers[i], be.markers[i])) return false;
            }
            return true;
        }
        TU_ARMA(ErasedType, ae, be) {
            if (ae.isSized != be.isSized || ae.usePresent != be.usePresent
                || ae.traits.size() != be.traits.size()
                || ae.lifetimeBounds != be.lifetimeBounds
                || !exactErasedInnerEqual(ae.inner, be.inner)
                || !exactPathParamsEqual(ae.use, be.use)) return false;
            for (size_t i = 0; i < ae.traits.size(); i++) {
                if (!exactTraitPathEqual(ae.traits[i], be.traits[i])) return false;
            }
            return true;
        }
        TU_ARMA(Array, ae, be) return ae.inner == be.inner && exactArraySizeEqual(ae.size, be.size);
        TU_ARMA(Slice, ae, be) return ae.inner == be.inner;
        TU_ARMA(Tuple, ae, be) return ae == be;
        TU_ARMA(Borrow, ae, be) return ae.lifetime == be.lifetime && ae.type == be.type && ae.inner == be.inner;
        TU_ARMA(Pointer, ae, be) return ae.type == be.type && ae.inner == be.inner;
        TU_ARMA(NamedFunction, ae, be) {
            if (!exactPathEqual(ae.path, be.path) || ae.def.tag() != be.def.tag()) return false;
            TU_MATCH_HDRA((ae.def, be.def), {)
            TU_ARMA(Function, ad, bd) return ad == bd;
            TU_ARMA(EnumConstructor, ad, bd) return ad.e == bd.e && ad.v == bd.v;
            TU_ARMA(StructConstructor, ad, bd) return ad == bd;
            }
            throw "";
        }
        TU_ARMA(Function, ae, be) {
            return exactGenericParamsEqual(ae.hrls, be.hrls)
                && ae.is_unsafe == be.is_unsafe && ae.is_variadic == be.is_variadic
                && ae.mAbi == be.mAbi && ae.mRettype == be.mRettype
                && ae.argTypes == be.argTypes;
        }
        TU_ARMA(NodeType, ae, be) return ae == be;
        }
        throw "";
    }

    void addTypeFlags(uint32_t& flags, TypeRef type) {
        flags |= type->flags;
    }

    void addLifetimeFlags(uint32_t& flags, LifetimeRef lifetime) {
        if (lifetime.isParam() && lifetime.asParam().group() != GENERICHrtb) {
            flags |= TypeData::HAS_LIFETIME_PARAM;
        }
    }

    uint32_t type_flags(const PathParams& params);

    uint32_t type_flags(const GenericPath& path) {
        return type_flags(path.mParams);
    }

    uint32_t type_flags(const TraitPath& trait) {
        auto flags = type_flags(trait.mPath);
        for (const auto& bound : trait.typeBounds) {
            flags |= type_flags(bound.second.source_trait);
            flags |= type_flags(bound.second.atyParams);
            addTypeFlags(flags, bound.second.type);
        }
        for (const auto& bound : trait.traitBounds) {
            flags |= type_flags(bound.second.source_trait);
            flags |= type_flags(bound.second.atyParams);
            for (const auto& nested : bound.second.traits) {
                flags |= type_flags(nested);
            }
        }
        return flags;
    }

    uint32_t type_flags(const PathParams& params) {
        uint32_t flags = 0;
        for (const auto lifetime : params.mLifetimes) {
            addLifetimeFlags(flags, lifetime);
        }
        for (const auto type : params.types) {
            addTypeFlags(flags, type);
        }
        for (const auto& value : params.values) {
            if (value.is_Generic()) {
                flags |= TypeData::HAS_TYPE_PARAM;
            } else if (value.is_Infer() || value.is_Unevaluated()) {
                flags |= TypeData::HAS_DEFERRED_CONST;
            }
        }
        return flags;
    }

    uint32_t type_flags(const Path& path) {
        uint32_t flags = 0;
        TU_MATCH_HDRA((path.mData), {)
        TU_ARMA(Generic, e) {
            flags |= type_flags(e.mParams);
        }
        TU_ARMA(UfcsInherent, e) {
            addTypeFlags(flags, e.type);
            flags |= type_flags(e.params);
            flags |= type_flags(e.impl_params);
        }
        TU_ARMA(UfcsKnown, e) {
            addTypeFlags(flags, e.type);
            flags |= type_flags(e.trait);
            flags |= type_flags(e.params);
        }
        TU_ARMA(UfcsUnknown, e) {
            addTypeFlags(flags, e.type);
            flags |= type_flags(e.params);
        }
        }
        return flags;
    }

    uint32_t type_flags(const TypeData& type) {
        uint32_t flags = 0;
        TU_MATCH_HDRA((type), {)
        TU_ARMA(Infer, e) {
            flags |= TypeData::HAS_TYPE_INFER;
        }
        TU_ARMA(Diverge, e) {}
        TU_ARMA(Primitive, e) {}
        TU_ARMA(Path, e) {
            flags |= type_flags(e.path);
            if (e.path.mData.is_UfcsKnown()
                && (e.binding.is_Unbound() || e.binding.is_Opaque())) {
                flags |= TypeData::HAS_ASSOCIATED_TYPE;
            }
        }
        TU_ARMA(Generic, e) {
            flags |= TypeData::HAS_TYPE_PARAM;
        }
        TU_ARMA(TraitObject, e) {
            flags |= type_flags(e.mTrait);
            for (const auto& marker : e.markers) {
                flags |= type_flags(marker);
            }
            addLifetimeFlags(flags, e.lifetime);
        }
        TU_ARMA(ErasedType, e) {
            for (const auto& trait : e.traits) {
                flags |= type_flags(trait);
            }
            flags |= type_flags(e.use);
            for (const auto lifetime : e.lifetimeBounds) {
                addLifetimeFlags(flags, lifetime);
            }
            TU_MATCH_HDRA((e.inner), {)
            TU_ARMA(Fcn, inner) flags |= type_flags(inner.origin);
            TU_ARMA(Known, inner) addTypeFlags(flags, inner);
            TU_ARMA(Alias, inner) flags |= type_flags(inner.params);
            }
        }
        TU_ARMA(Array, e) {
            addTypeFlags(flags, e.inner);
            if (e.size.is_Unevaluated()) {
                flags |= TypeData::HAS_UNEVALUATED_CONST;
            }
        }
        TU_ARMA(Slice, e) addTypeFlags(flags, e.inner);
        TU_ARMA(Tuple, e) for (const auto inner : e) addTypeFlags(flags, inner);
        TU_ARMA(Borrow, e) {
            addTypeFlags(flags, e.inner);
            addLifetimeFlags(flags, e.lifetime);
        }
        TU_ARMA(Pointer, e) addTypeFlags(flags, e.inner);
        TU_ARMA(NamedFunction, e) flags |= type_flags(e.path);
        TU_ARMA(Function, e) {
            addTypeFlags(flags, e.mRettype);
            for (const auto argument : e.argTypes) {
                addTypeFlags(flags, argument);
            }
        }
        TU_ARMA(NodeType, e) {}
        }
        return flags;
    }

    size_t hashMix(size_t state, size_t value) {
        return state ^ (value + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2));
    }

    size_t hashSimplePath(const SimplePath& path) {
        size_t h = ::std::hash<RcString>()(path.crate_name());
        for (const auto& component : path.components()) h = hashMix(h, ::std::hash<RcString>()(component));
        return h;
    }

    size_t hashTypeRef(TypeRef type) {
        return ::std::hash<const void*>()(type);
    }

    size_t hashPathParams(const PathParams& params);

    size_t hashGenericRef(const GenericRef& generic) {
        size_t h = generic.binding;
        if (generic.group() == GENERICPlaceholder) {
            h = hashMix(h, ::std::hash<RcString>()(generic.name));
        }
        return h;
    }

    size_t hashConstGeneric(const ConstGeneric& value) {
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
            h = hashMix(h, hashPathParams(e->params_impl));
            h = hashMix(h, hashPathParams(e->params_item));
        }
        }
        return h;
    }

    size_t hashPathParams(const PathParams& params) {
        size_t h = params.mLifetimes.size();
        h = hashMix(h, params.types.size());
        h = hashMix(h, params.values.size());
        for (const auto& lifetime : params.mLifetimes) {
            h = hashMix(h, lifetime.binding);
        }
        for (const auto type : params.types) {
            h = hashMix(h, hashTypeRef(type));
        }
        for (const auto& value : params.values) {
            h = hashMix(h, hashConstGeneric(value));
        }
        return h;
    }

    size_t hashGenericPath(const GenericPath& path) {
        return hashMix(hashSimplePath(path.mPath), hashPathParams(path.mParams));
    }

    size_t hashPath(const Path& path) {
        size_t h = static_cast<size_t>(path.mData.tag());
        TU_MATCH_HDRA((path.mData), {)
        TU_ARMA(Generic, e) {
            h = hashMix(h, hashGenericPath(e));
        }
        TU_ARMA(UfcsInherent, e) {
            h = hashMix(h, hashTypeRef(e.type));
            h = hashMix(h, ::std::hash<RcString>()(e.item));
            h = hashMix(h, hashPathParams(e.params));
            h = hashMix(h, hashPathParams(e.impl_params));
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

    size_t hashBinding(const TypePathBinding& binding) {
        size_t h = static_cast<size_t>(binding.tag());
        TU_MATCH_HDRA((binding), {)
        TU_ARMA(Unbound, e) {}
        TU_ARMA(Opaque, e) {}
        TU_ARMA(ExternType, e) { h = hashMix(h, reinterpret_cast<uintptr_t>(e)); }
        TU_ARMA(Struct, e) { h = hashMix(h, reinterpret_cast<uintptr_t>(e)); }
        TU_ARMA(Union, e) { h = hashMix(h, reinterpret_cast<uintptr_t>(e)); }
        TU_ARMA(Enum, e) { h = hashMix(h, reinterpret_cast<uintptr_t>(e)); }
        }
        return h;
    }

    size_t hashTypeData(const TypeData& type) {
        size_t h = static_cast<size_t>(type.tag());
        TU_MATCH_HDRA((type), {)
        TU_ARMA(Infer, e) { h = hashMix(h, e.index); h = hashMix(h, static_cast<size_t>(e.ty_class)); }
        TU_ARMA(Diverge, e) {}
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
            h = hashMix(h, e.lifetime.binding);
            for (const auto& marker : e.markers) {
                h = hashMix(h, hashGenericPath(marker));
            }
            for (const auto& bound : e.mTrait.typeBounds) {
                h = hashMix(h, ::std::hash<RcString>()(bound.first));
                h = hashMix(h, hashGenericPath(bound.second.source_trait));
                h = hashMix(h, hashPathParams(bound.second.atyParams));
                h = hashMix(h, hashTypeRef(bound.second.type));
            }
        }
        TU_ARMA(ErasedType, e) { h = hashMix(h, static_cast<size_t>(e.inner.tag())); h = hashMix(h, e.traits.size()); }
        TU_ARMA(Array, e) {
            h = hashMix(h, hashTypeRef(e.inner));
            h = hashMix(h, static_cast<size_t>(e.size.tag()));
            TU_MATCH_HDRA((e.size), {)
            TU_ARMA(Known, size) { h = hashMix(h, size); }
            TU_ARMA(Unevaluated, size) { h = hashMix(h, hashConstGeneric(size)); }
            }
        }
        TU_ARMA(Slice, e) h = hashMix(h, hashTypeRef(e.inner));
        TU_ARMA(Tuple, e) { for (auto t : e) h = hashMix(h, hashTypeRef(t)); }
        TU_ARMA(Borrow, e) { h = hashMix(h, e.lifetime.binding); h = hashMix(h, static_cast<size_t>(e.type)); h = hashMix(h, hashTypeRef(e.inner)); }
        TU_ARMA(Pointer, e) { h = hashMix(h, static_cast<size_t>(e.type)); h = hashMix(h, hashTypeRef(e.inner)); }
        TU_ARMA(NamedFunction, e) {
            h = hashMix(h, hashPath(e.path));
            h = hashMix(h, static_cast<size_t>(e.def.tag()));
        }
        TU_ARMA(Function, e) { h = hashMix(h, ::std::hash<RcString>()(e.mAbi)); h = hashMix(h, e.is_unsafe); h = hashMix(h, e.is_variadic); h = hashMix(h, hashTypeRef(e.mRettype)); for (auto t : e.argTypes) h = hashMix(h, hashTypeRef(t)); }
        TU_ARMA(NodeType, e) { TU_MATCH_HDRA((e), {) TU_ARMA(Closure, p) h = hashMix(h, reinterpret_cast<uintptr_t>(p)); TU_ARMA(Generator, p) h = hashMix(h, reinterpret_cast<uintptr_t>(p)); TU_ARMA(Async, p) h = hashMix(h, reinterpret_cast<uintptr_t>(p)); }
        }
        }
        return h;
    }
}

::HIR::TypeRef HIR::TypeInterner::intern(TypeData data) {
    data.flags = type_flags(data);
    const auto hash = hashTypeData(data);
    const auto range = nodes.equal_range(hash);
    for (auto it = range.first; it != range.second; ++it) {
        if (exactTypeDataEqual(*it->second, data)) {
            return it->second;
        }
    }
    const auto* node = pool.make<TypeData>(mv$(data));
    nodes.emplace(hash, node);
    return node;
}

::HIR::TypeRef HIR::TypeInterner::infer(unsigned int idx, InferClass ty_class) {
    return intern(TypeData::make_Infer({idx, ty_class}));
}

::HIR::TypeRef HIR::TypeInterner::primitive(CoreType ct) {
    return intern(TypeData::make_Primitive(ct));
}

::HIR::TypeRef HIR::TypeInterner::generic(RcString name, unsigned int slot) {
    return intern(TypeData::make_Generic({mv$(name), slot}));
}

::HIR::TypeRef HIR::TypeInterner::self() {
    return generic(RcString::newInterned("Self"), GENERICSelf);
}

::HIR::TypeRef HIR::TypeInterner::unit() {
    return intern(TypeData::make_Tuple({}));
}

::HIR::TypeRef HIR::TypeInterner::diverge() {
    return intern(TypeData::make_Diverge({}));
}

::HIR::TypeRef HIR::TypeInterner::borrow(BorrowType bt, TypeRef inner, LifetimeRef lft) {
    return intern(TypeData::make_Borrow({lft, bt, inner}));
}

::HIR::TypeRef HIR::TypeInterner::pointer(BorrowType bt, TypeRef inner) {
    return intern(TypeData::make_Pointer({bt, inner}));
}

::HIR::TypeRef HIR::TypeInterner::tuple(::std::vector<TypeRef> types) {
    return intern(TypeData::make_Tuple(mv$(types)));
}

::HIR::TypeRef HIR::TypeInterner::slice(TypeRef inner) {
    return intern(TypeData::make_Slice({inner}));
}

::HIR::TypeRef HIR::TypeInterner::array(TypeRef inner, ArraySize size) {
    return intern(TypeData::make_Array({inner, mv$(size)}));
}

::HIR::TypeRef HIR::TypeInterner::array(TypeRef inner, uint64_t size) {
    assert(size != ~0u);
    return intern(TypeData::make_Array({inner, size}));
}

::HIR::TypeRef HIR::TypeInterner::array(TypeRef inner, ConstGeneric size) {
    return intern(TypeData::make_Array({inner, mv$(size)}));
}

::HIR::TypeRef HIR::TypeInterner::path(Path path, TypePathBinding binding, ::std::unique_ptr<GenericParams> hrtbs) {
    return intern(TypeData::make_Path({mv$(path), mv$(binding), mv$(hrtbs)}));
}

::HIR::TypeRef HIR::TypeInterner::function(TypeDataFunctionPointer ft) {
    return intern(TypeData::make_Function(mv$(ft)));
}

::HIR::TypeRef HIR::TypeInterner::closure(ExprNodeClosure* node) {
    return intern(TypeData::make_NodeType(TypeDataNodeType::make_Closure(node)));
}

::HIR::TypeRef HIR::TypeInterner::generator(ExprNodeGenerator* node) {
    return intern(TypeData::make_NodeType(TypeDataNodeType::make_Generator(node)));
}

::HIR::TypeRef HIR::TypeInterner::asyncBlock(ExprNodeAsyncBlock* node) {
    return intern(TypeData::make_NodeType(TypeDataNodeType::make_Async(node)));
}

const ::HIR::SimplePath* HIR::TypeData::getSortPath() const {
    if (TU_TEST1(*this, Path, .path.mData.is_Generic())) {
        return &as_Path().path.mData.as_Generic().mPath;
    }
    if (is_TraitObject()) {
        return &as_TraitObject().mTrait.mPath.mPath;
    }
    return nullptr;
}

Ordering ord(const HIR::TypeDataErasedTypeInner& l, const HIR::TypeDataErasedTypeInner& r);

bool ::HIR::TypeData::equalsIgnoringRegions(::HIR::TypeRef x) const {
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
            //return te.m_lifetime == xe.m_lifetime;
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
            //    return false;
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
            if (te.is_unsafe != xe.is_unsafe) {
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

Ordering ord(const HIR::TypeDataErasedTypeInner& l, const HIR::TypeDataErasedTypeInner& r) {
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

Ordering HIR::TypeData::ordIgnoringRegions(::HIR::TypeRef x) const {
    Ordering rv;

    if (this == x) {
        return OrdEqual;
    }
    ORD(static_cast<unsigned int>(tag()), static_cast<unsigned int>(x->tag()));

    TU_MATCH(
        ::HIR::TypeData,
        (*this, *x),
        (te, xe),
        (Infer,
         // TODO: Should comparing inferrence vars be an error?
         return ::ord(te.index, xe.index);),
        (Diverge, return OrdEqual;),
        (Primitive, return ::ord(static_cast<unsigned>(te), static_cast<unsigned>(xe));),
        (Path, return ::ord(te.path, xe.path);),
        (Generic,
         //ORD(te.name, xe.name);
         if ((rv = ::ord(te.binding, xe.binding)) != OrdEqual) return rv;
         return OrdEqual;),
        (TraitObject, ORD(te.mTrait, xe.mTrait); ORD(te.markers, xe.markers);
         //ORD(te.m_lifetime, xe.m_lifetime);
         return OrdEqual;),
        (ErasedType,
         //ORD(te.m_traits, xe.m_traits);
         ORD(te.inner, xe.inner);
         return OrdEqual;),
        (Array, ORD(te.inner, xe.inner); ORD(te.size, xe.size); return OrdEqual;),
        (Slice, return ::ord(te.inner, xe.inner);),
        (Tuple, return ::ord(te, xe);),
        (Borrow, ORD(static_cast<unsigned>(te.type), static_cast<unsigned>(xe.type)); return ::ord(te.inner, xe.inner);),
        (Pointer, ORD(static_cast<unsigned>(te.type), static_cast<unsigned>(xe.type)); return ::ord(te.inner, xe.inner);),
        (NamedFunction, return ::ord(te.path, xe.path);),
        (Function, ORD(te.is_unsafe, xe.is_unsafe); ORD(te.mAbi, xe.mAbi); ORD(te.argTypes, xe.argTypes); return ::ord(te.mRettype, xe.mRettype);),
        (NodeType, return te.ord(xe);)
    )
    throw "";
}

namespace {
    ::HIR::Compare matchGenericsPp(const Span& sp, const ::HIR::PathParams& t, const ::HIR::PathParams& x, ::HIR::t_cb_resolve_type resolve_placeholder, ::HIR::MatchGenerics& callback) {
        return t.matchTestGenericsFuzz(sp, x, resolve_placeholder, callback);
    }

    ::HIR::Compare matchValues(const Span& sp, const ::HIR::ConstGeneric& t, const ::HIR::ConstGeneric& x, ::HIR::MatchGenerics& callback) {
        // LHS generic: call callback
        if (const auto* e = t.opt_Generic()) {
            return callback.matchVal(*e, x);
        }

        // Either are infer, check for exact match or return fuzzy
        if (const auto* xep = x.opt_Infer()) {
            const auto& xe = *xep;

            if (xe.index != ~0u && t.is_Infer() && t.as_Infer().index == xe.index) {
                return ::HIR::Compare::Equal;
            }

            return ::HIR::Compare::Fuzzy;
        }
        if (const auto* tep = t.opt_Infer()) {
            const auto& te = *tep;
            ASSERT_BUG(sp, te.index != ~0u, "Encountered ivar for `this` - " << t);
            return ::HIR::Compare::Fuzzy;
        }

        if (t.tag() != x.tag()) {
            return ::HIR::Compare::Unequal;
        }

        TU_MATCH_HDRA( (t,x), { )
            TU_ARMA(Infer, te,xe) throw "Unreachable";
            TU_ARMA(Unevaluated, te, xe) {
                return te->equivalent(*xe) ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
            }
            TU_ARMA(Generic, te, xe) throw "Unreachable";
            TU_ARMA(Evaluated, te, xe)
            return *te == *xe ? ::HIR::Compare::Equal : ::HIR::Compare::Unequal;
        }
        throw "Unreachable";
    }
}

bool ::HIR::TypeData::matchTestGenerics(const Span& sp, ::HIR::TypeRef x_in, t_cb_resolve_type resolve_placeholder, ::HIR::MatchGenerics& callback) const {
    return this->matchTestGenericsFuzz(sp, x_in, resolve_placeholder, callback) == ::HIR::Compare::Equal;
}

::HIR::Compare HIR::TypeData::matchTestGenericsFuzz(const Span& sp, ::HIR::TypeRef x_in, t_cb_resolve_type resolve_placeholder, ::HIR::MatchGenerics& callback) const {
    const TypeRef self = this;
    return callback.cmpType(sp, self, x_in, resolve_placeholder);
}

HIR::TrackHrbStack::PopOnDrop HIR::TrackHrbStack::push_hrb(const std::unique_ptr<HIR::GenericParams>& params) const {
    static HIR::GenericParams emptyParams;
    return params ? push_hrb(*params) : PopOnDrop();
}

::HIR::Compare HIR::MatchGenerics::cmpPath(const Span& sp, const ::HIR::Path& path_l, const ::HIR::Path& path_r, t_cb_resolve_type resolve_placeholder) {
    ::HIR::Compare rv = Compare::Unequal;
    if (path_l.mData.tag() != path_r.mData.tag()) {
        rv = Compare::Unequal;
    } else {
        TU_MATCH_HDRA((path_l.mData, path_r.mData), {)
        TU_ARMA(Generic, tpe, xpe) {
                if (tpe.mPath != xpe.mPath) {
                    rv = Compare::Unequal;
                } else {
                    rv = matchGenericsPp(sp, tpe.mParams, xpe.mParams, resolve_placeholder, *this);
                }
            }
            TU_ARMA(UfcsKnown, tpe, xpe) {
                rv = this->cmpType(sp, tpe.type, xpe.type, resolve_placeholder);
                if (tpe.trait.mPath != xpe.trait.mPath) {
                    rv = Compare::Unequal;
                }
                rv &= matchGenericsPp(sp, tpe.trait.mParams, xpe.trait.mParams, resolve_placeholder, *this);
                if (tpe.item != xpe.item) {
                    rv = Compare::Unequal;
                }
                rv &= matchGenericsPp(sp, tpe.params, xpe.params, resolve_placeholder, *this);
            }
            TU_ARMA(UfcsUnknown, tpe, xpe) {
                rv = this->cmpType(sp, tpe.type, xpe.type, resolve_placeholder);
                if (tpe.item != xpe.item) {
                    rv = Compare::Unequal;
                }
                rv &= matchGenericsPp(sp, tpe.params, xpe.params, resolve_placeholder, *this);
            }
            TU_ARMA(UfcsInherent, tpe, xpe) {
                rv = this->cmpType(sp, tpe.type, xpe.type, resolve_placeholder);
                if (tpe.item != xpe.item) {
                    rv = Compare::Unequal;
                }
                rv &= matchGenericsPp(sp, tpe.params, xpe.params, resolve_placeholder, *this);
            }
        }
    }
    DEBUG("rv = " << rv);
    return rv;
}

::HIR::Compare HIR::MatchGenerics::cmpType(const Span& sp, const ::HIR::TypeData* ty_l, const ::HIR::TypeData* ty_r, t_cb_resolve_type resolve_placeholder) {
    if (const auto* e = ty_l->opt_Generic()) {
        return this->matchTy(*e, ty_r, resolve_placeholder);
    }
    const auto& v = (ty_l->is_Infer() ? resolve_placeholder.getType(sp, ty_l) : ty_l);
    const auto& x = (ty_r->is_Infer() || ty_r->is_Generic() ? resolve_placeholder.getType(sp, ty_r) : ty_r);
    TRACE_FUNCTION_F(ty_l << ", " << ty_r << " -- " << v << ", " << x);
    // If `x` is an ivar - This can be a fuzzy match.
    if (const auto* xep = x->opt_Infer()) {
        const auto& xe = *xep;
        // - If type inferrence is active (i.e. this ivar has an index), AND both `v` and `x` refer to the same ivar slot
        if (xe.index != ~0u && v->is_Infer() && v->as_Infer().index == xe.index) {
            // - They're equal (no fuzzyness about it)
            return Compare::Equal;
        }
        switch (xe.ty_class) {
            case ::HIR::InferClass::None:
                // TODO: Have another callback (optional?) that allows the caller to equate `v` somehow
                // - Very niche?
                return Compare::Fuzzy;
            case ::HIR::InferClass::Integer:
                if (const auto* te = v->opt_Primitive()) {
                    switch (*te) {
                        case ::HIR::CoreType::I8:
                        case ::HIR::CoreType::U8:
                        case ::HIR::CoreType::I16:
                        case ::HIR::CoreType::U16:
                        case ::HIR::CoreType::I32:
                        case ::HIR::CoreType::U32:
                        case ::HIR::CoreType::I64:
                        case ::HIR::CoreType::U64:
                        case ::HIR::CoreType::I128:
                        case ::HIR::CoreType::U128:
                        case ::HIR::CoreType::Isize:
                        case ::HIR::CoreType::Usize:
                            return Compare::Fuzzy;
                            //return true;
                        default:
                            DEBUG("- Fuzz fail");
                            return Compare::Unequal;
                    }
                }
                break;
            case ::HIR::InferClass::Float:
                if (const auto* te = v->opt_Primitive()) {
                    switch (*te) {
                        case ::HIR::CoreType::F16:
                        case ::HIR::CoreType::F32:
                        case ::HIR::CoreType::F64:
                        case ::HIR::CoreType::F128:
                            return Compare::Fuzzy;
                            //return true;
                        default:
                            DEBUG("- Fuzz fail");
                            return Compare::Unequal;
                    }
                }
                break;
        }
    }

    if (const auto* tep = v->opt_Infer()) {
        const auto& te = *tep;
        // TODO: Restrict this block with a flag so it panics if an ivar is seen when not expected
        ASSERT_BUG(sp, te.index != ~0u, "Encountered ivar for `this` - " << v);

        switch (te.ty_class) {
            case ::HIR::InferClass::None:
                // TODO: Have another callback (optional?) that allows the caller to equate `v` somehow
                // - Very niche?
                return Compare::Fuzzy;
            case ::HIR::InferClass::Integer:
                if (const auto* xe = x->opt_Primitive()) {
                    switch (*xe) {
                        case ::HIR::CoreType::I8:
                        case ::HIR::CoreType::U8:
                        case ::HIR::CoreType::I16:
                        case ::HIR::CoreType::U16:
                        case ::HIR::CoreType::I32:
                        case ::HIR::CoreType::U32:
                        case ::HIR::CoreType::I64:
                        case ::HIR::CoreType::U64:
                        case ::HIR::CoreType::I128:
                        case ::HIR::CoreType::U128:
                        case ::HIR::CoreType::Isize:
                        case ::HIR::CoreType::Usize:
                            return Compare::Fuzzy;
                        default:
                            DEBUG("- Fuzz fail");
                            return Compare::Unequal;
                    }
                }
                break;
            case ::HIR::InferClass::Float:
                if (const auto* xe = x->opt_Primitive()) {
                    switch (*xe) {
                        case ::HIR::CoreType::F16:
                        case ::HIR::CoreType::F32:
                        case ::HIR::CoreType::F64:
                        case ::HIR::CoreType::F128:
                            return Compare::Fuzzy;
                        default:
                            DEBUG("- Fuzz fail");
                            return Compare::Unequal;
                    }
                }
                break;
        }
    }

    const auto unresolved_erased_alias = [](const ::HIR::TypeData* ty) {
        const auto* erased = ty->opt_ErasedType();
        const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
        return alias && !alias->inner->type;
    };
    if (unresolved_erased_alias(v) || unresolved_erased_alias(x)) {
        DEBUG("- Fuzzy match due to unresolved opaque alias - " << v << " = " << x);
        return Compare::Fuzzy;
    }

    // MatchGenerics is a relation, not plain type equality.  Its callbacks can
    // bind lifetimes and generic parameters while walking two identical
    // interned types, so pointer identity must not bypass the structural walk.
    if (v->tag() != x->tag()) {
        // HACK: If the path is Opaque, return a fuzzy match.
        // - This works around an impl selection bug.
        if (v->is_Path() && v->as_Path().binding.is_Opaque()) {
            DEBUG("- Fuzzy match due to opaque - " << v << " = " << x);
            return Compare::Fuzzy;
        }
        // HACK: If RHS is unbound, fuzz it
        if (x->is_Path() && x->as_Path().binding.is_Unbound()) {
            DEBUG("- Fuzzy match due to unbound - " << v << " = " << x);
            return Compare::Fuzzy;
        }
        if (v->is_Path() && v->as_Path().binding.is_Unbound()) {
            DEBUG("- Fuzzy match due to unbound - " << v << " = " << x);
            return Compare::Fuzzy;
        }
        // HACK: If the RHS is a placeholder generic, allow it.
        if (x->is_Generic() && (x->as_Generic().binding >> 8) == 2) {
            DEBUG("- Fuzzy match due to placeholder - " << v << " = " << x);
            return Compare::Fuzzy;
        }
        DEBUG("- Tag mismatch " << v << " and " << x);
        return Compare::Unequal;
    }
    TU_MATCH_HDRA( (*v, *x), { )
    TU_ARMA(Infer, te, xe) {
            // Both sides are infer
            switch (te.ty_class) {
                case ::HIR::InferClass::None:
                    return Compare::Fuzzy;
                default:
                    switch (xe.ty_class) {
                        case ::HIR::InferClass::None:
                            return Compare::Fuzzy;
                        default:
                            if (te.ty_class != xe.ty_class) {
                                return Compare::Unequal;
                            }
                            return Compare::Fuzzy;
                    }
            }
        }
        TU_ARMA(Generic, te, xe) throw "";
        TU_ARMA(Primitive, te, xe) {
            return (te == xe ? Compare::Equal : Compare::Unequal);
        }
        TU_ARMA(Diverge, te, xe) {
            return Compare::Equal;
        }
        TU_ARMA(Path, te, xe) {
            auto rv = this->cmpPath(sp, te.path, xe.path, resolve_placeholder);

            if (rv == ::HIR::Compare::Unequal) {
                if (te.binding.is_Unbound() || xe.binding.is_Unbound()) {
                    rv = ::HIR::Compare::Fuzzy;
                }
                if (te.binding.is_Opaque()) {
                    DEBUG("- Fuzzy match due to opaque");
                    return Compare::Fuzzy;
                }
            }
            return rv;
        }
        TU_ARMA(TraitObject, te, xe) {
            if (te.mTrait.mPath.mPath != xe.mTrait.mPath.mPath) {
                return Compare::Unequal;
            }
            if (te.markers.size() != xe.markers.size()) {
                return Compare::Unequal;
            }
            static const HIR::GenericParams emptyParams;
            auto _ = push_hrb(te.mTrait.hrtbs ? *te.mTrait.hrtbs : emptyParams);
            auto cmp = matchGenericsPp(sp, te.mTrait.mPath.mParams, xe.mTrait.mPath.mParams, resolve_placeholder, *this);
            for (unsigned int i = 0; i < te.markers.size(); i++) {
                if (te.markers[i].mPath != xe.markers[i].mPath) {
                    return Compare::Unequal;
                }
                cmp &= matchGenericsPp(sp, te.markers[i].mParams, xe.markers[i].mParams, resolve_placeholder, *this);
            }

            auto itL = te.mTrait.typeBounds.begin();
            auto itR = xe.mTrait.typeBounds.begin();
            while (itL != te.mTrait.typeBounds.end() && itR != xe.mTrait.typeBounds.end()) {
                if (itL->first != itR->first) {
                    return Compare::Unequal;
                }
                cmp &= itL->second.type->matchTestGenericsFuzz(sp, itR->second.type, resolve_placeholder, *this);
                ++itL;
                ++itR;
            }

            if (itL != te.mTrait.typeBounds.end() || itR != xe.mTrait.typeBounds.end()) {
                return Compare::Unequal;
            }

            if (te.lifetime.isParam()) {
                /*cmp &= */ this->matchLft(HIR::GenericRef("", te.lifetime.binding), xe.lifetime);
            }

            return cmp;
        }
        TU_ARMA(ErasedType, te, xe) {
            if (te.inner.tag() != xe.inner.tag()) {
                return Compare::Unequal;
            }
            TU_MATCH_HDRA((te.inner, xe.inner), {)
            TU_ARMA(Known, l, r) return l->matchTestGenericsFuzz(sp, r, resolve_placeholder, *this);
            TU_ARMA(Alias, l, r) {
                return l.inner == r.inner
                    ? l.params.matchTestGenericsFuzz(sp, r.params, resolve_placeholder, *this)
                    : Compare::Unequal;
            }
            TU_ARMA(Fcn, l, r) {
                return l.index == r.index
                    ? this->cmpPath(sp, l.origin, r.origin, resolve_placeholder)
                    : Compare::Unequal;
            }
            }
            throw "";
        }
        TU_ARMA(Array, te, xe) {
            auto rv = Compare::Equal;
            if (const auto* tse = te.size.opt_Unevaluated()) {
                HIR::ConstGeneric v;
                if (xe.size.opt_Known()) {
                    rv &= matchValues(sp, *tse, EncodedLiteralPtr(EncodedLiteral::makeUsize(xe.size.as_Known())), *this);
                } else {
                    rv &= matchValues(sp, *tse, xe.size.as_Unevaluated(), *this);
                }
            } else if (const auto* xse = xe.size.opt_Unevaluated()) {
                // `te.size` must be known here, all we need to handle is `Infer`?
                if (xse->is_Infer()) {
                    rv &= Compare::Fuzzy;
                } else {
                    ASSERT_BUG(sp, !xse->is_Evaluated(), "TODO: Handle " << te.size << " ?= " << xe.size);
                    // - Evaluated? (TODO - could use `EncodedLiteralPtr( EncodedLiteral::make_usize(te.size.as_Known()) )`)
                    // - Generic - could only match with another generic, i.e. `tse` must have been `Unevaluated,Generic`
                    // - Unevaluated - could only match with another Unevaluated, i.e. `tse` must have been `Unevaluated,Unevaluated`
                    return Compare::Unequal;
                }
            } else if (te.size != xe.size) {
                return Compare::Unequal;
            }
            return this->cmpType(sp, te.inner, xe.inner, resolve_placeholder);
        }
        TU_ARMA(Slice, te, xe) {
            return this->cmpType(sp, te.inner, xe.inner, resolve_placeholder);
        }
        TU_ARMA(Tuple, te, xe) {
            if (te.size() != xe.size()) {
                return Compare::Unequal;
            }
            auto rv = Compare::Equal;
            for (unsigned int i = 0; i < te.size(); i++) {
                rv &= this->cmpType(sp, te[i], xe[i], resolve_placeholder);
                if (rv == Compare::Unequal) {
                    return Compare::Unequal;
                }
            }
            return rv;
        }
        TU_ARMA(Pointer, te, xe) {
            if (te.type != xe.type) {
                return Compare::Unequal;
            }
            return this->cmpType(sp, te.inner, xe.inner, resolve_placeholder);
        }
        TU_ARMA(Borrow, te, xe) {
            if (te.type != xe.type) {
                return Compare::Unequal;
            }
            auto rv = Compare::Equal;
            if (te.lifetime.isParam()) {
                /*rv &=*/this->matchLft(te.lifetime.asParam(), xe.lifetime);
            } else {
                //if( te.lifetime != xe.lifetime )
                //    return Compare::Unequal;
            }
            rv &= this->cmpType(sp, te.inner, xe.inner, resolve_placeholder);
            return rv;
        }
        TU_ARMA(NamedFunction, te, xe) {
            return this->cmpPath(sp, te.path, xe.path, resolve_placeholder);
        }
        TU_ARMA(Function, te, xe) {
            if (te.is_unsafe != xe.is_unsafe) {
                return Compare::Unequal;
            }
            if (te.mAbi != xe.mAbi) {
                return Compare::Unequal;
            }
            if (te.argTypes.size() != xe.argTypes.size()) {
                return Compare::Unequal;
            }
            auto _ = push_hrb(te.hrls);
            auto rv = Compare::Equal;
            for (unsigned int i = 0; i < te.argTypes.size(); i++) {
                rv &= this->cmpType(sp, te.argTypes[i], xe.argTypes[i], resolve_placeholder);
                if (rv == Compare::Unequal) {
                    return rv;
                }
            }
            rv &= this->cmpType(sp, te.mRettype, xe.mRettype, resolve_placeholder);
            return rv;
        }
        TU_ARMA(NodeType, te, xe) {
            return te == xe ? Compare::Equal : Compare::Unequal;
        }
    }
    throw "";
}

::HIR::TypePathBinding HIR::TypePathBinding::clone() const {
    TU_MATCH(::HIR::TypePathBinding, (*this), (e), (Unbound, return ::HIR::TypePathBinding::make_Unbound({});), (Opaque, return ::HIR::TypePathBinding::make_Opaque({});), (ExternType, return ::HIR::TypePathBinding(e);), (Struct, return ::HIR::TypePathBinding(e);), (Union, return ::HIR::TypePathBinding(e);), (Enum, return ::HIR::TypePathBinding(e);))
    assert(!"Fell off end of clone_binding");
    throw "";
}

bool HIR::TypePathBinding::operator==(const HIR::TypePathBinding& x) const {
    if (this->tag() != x.tag()) {
        return false;
    }
    TU_MATCH(::HIR::TypePathBinding, (*this, x), (te, xe), (Unbound, return true;), (Opaque, return true;), (ExternType, return te == xe;), (Struct, return te == xe;), (Union, return te == xe;), (Enum, return te == xe;))
    throw "";
}

const ::HIR::TraitMarkings* HIR::TypePathBinding::getTraitMarkings() const {
    const ::HIR::TraitMarkings* markingsPtr = nullptr;
    TU_MATCHA((*this), (tpb), (Unbound, ), (Opaque, ), (ExternType, markingsPtr = &tpb->markings;), (Struct, markingsPtr = &tpb->markings;), (Union, markingsPtr = &tpb->markings;), (Enum, markingsPtr = &tpb->markings;))
    return markingsPtr;
}

const ::HIR::GenericParams* HIR::TypePathBinding::getGenerics() const {
    const ::HIR::GenericParams* rv = nullptr;
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

HIR::TypeDataNamedFunctionTy HIR::TypeDataNamedFunctionTy::clone() const {
    TU_MATCH_HDRA( (*this), { )
    TU_ARMA(Function, e)    return e;
        TU_ARMA(EnumConstructor, e) return e;
        TU_ARMA(StructConstructor, e) return e;
    }
    throw "";
}

::HIR::TypeData HIR::TypeData::cloneData() const {
    TU_MATCH_HDRA( (*this), {)
    TU_ARMA(Infer, e) {
            return TypeData::make_Infer(e);
        }
        TU_ARMA(Diverge, e) {
            return TypeData::make_Diverge({});
        }
        TU_ARMA(Primitive, e) {
            return TypeData::make_Primitive(e);
        }
        TU_ARMA(Path, e) {
            return TypeData::make_Path({e.path.clone(), e.binding.clone(), e.hrtbs ? ::std::make_unique<GenericParams>(e.hrtbs->clone()) : nullptr});
        }
        TU_ARMA(Generic, e) {
            return TypeData::make_Generic(e);
        }
        TU_ARMA(TraitObject, e) {
            TypeData::Data_TraitObject rv;
            rv.mTrait = e.mTrait.clone();
            for (const auto& trait : e.markers) {
                rv.markers.push_back(trait.clone());
            }
            rv.lifetime = e.lifetime;
            return TypeData::make_TraitObject(mv$(rv));
        }
        TU_ARMA(ErasedType, e) {
            ::std::vector<::HIR::TraitPath> traits;
            traits.reserve(e.traits.size());
            for (const auto& trait : e.traits) {
                traits.push_back(trait.clone());
            }

            HIR::TypeDataErasedTypeInner inner;
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Fcn, ee) {
                    inner = HIR::TypeDataErasedTypeInner::Data_Fcn{ee.origin.clone(), ee.index};
                }
                TU_ARMA(Known, ee) inner = ee;
                TU_ARMA(Alias, ee) {
                    inner = HIR::TypeDataErasedTypeInner::Data_Alias{ee.params.clone(), ee.inner};
                }
        }
        return TypeData::make_ErasedType({
            e.isSized,
            mv$(traits),
            e.lifetimeBounds,
            mv$(inner),
            e.use.clone(),
            e.usePresent
            });
        }
        TU_ARMA(Array, e) {
            return TypeData::make_Array({e.inner, e.size.clone()});
        }
        TU_ARMA(Slice, e) {
            return TypeData::make_Slice({e.inner});
        }
        TU_ARMA(Tuple, e) {
            ::std::vector<::HIR::TypeRef> types;
            for (const auto& t : e) {
                types.push_back(t);
            }
            return TypeData::make_Tuple(mv$(types));
        }
        TU_ARMA(Borrow, e) {
            return TypeData::make_Borrow({e.lifetime, e.type, e.inner});
        }
        TU_ARMA(Pointer, e) {
            return TypeData::make_Pointer({e.type, e.inner});
        }
        TU_ARMA(NamedFunction, e) {
            return TypeData::make_NamedFunction({e.path.clone(), e.def.clone()});
        }
        TU_ARMA(Function, e) {
            TypeDataFunctionPointer ft{e.hrls.clone(), e.is_unsafe, e.is_variadic, e.mAbi, e.mRettype, {}};
            for (const auto& a : e.argTypes) {
                ft.argTypes.push_back(a);
            }
            return TypeData::make_Function(mv$(ft));
        }
        TU_ARMA(NodeType, e) {
            return TypeData::make_NodeType(e.clone());
        }
    }
    throw "";
}

::HIR::Compare HIR::TypeData::compareWithPlaceholders(const Span& sp, ::HIR::TypeRef x, t_cb_resolve_type resolve_placeholder) const {
    //TRACE_FUNCTION_F(*this << " ?= " << x);
    const TypeRef self = this;
    const auto& left = resolve_placeholder.getType(sp, self);
    //const auto& left = *this;
    const auto& right = resolve_placeholder.getType(sp, x);

    // If the two types are the same ivar, return equal
    if (left->is_Infer() && left == right) {
        return Compare::Equal;
    }

    // Unbound paths and placeholder generics
    if (left->tag() != right->tag()) {
        if (left->is_Path() && left->as_Path().binding.is_Unbound()) {
            return Compare::Fuzzy;
        }
        if (right->is_Path() && right->as_Path().binding.is_Unbound()) {
            return Compare::Fuzzy;
        }
        if (left->is_Generic() && (left->as_Generic().binding >> 8) == 2) {
            return Compare::Fuzzy;
        }
        if (right->is_Generic() && (right->as_Generic().binding >> 8) == 2) {
            return Compare::Fuzzy;
        }
    }

    // If left is infer
    if (const auto* e = left->opt_Infer()) {
        switch (e->ty_class) {
            case ::HIR::InferClass::None:
                return Compare::Fuzzy;
            case ::HIR::InferClass::Integer:
            TU_MATCH_HDRA( (*right), {)
            default:
                return Compare::Unequal;
                    TU_ARMA(Primitive, re) {
                        switch (re) {
                            case ::HIR::CoreType::I8:
                            case ::HIR::CoreType::U8:
                            case ::HIR::CoreType::I16:
                            case ::HIR::CoreType::U16:
                            case ::HIR::CoreType::I32:
                            case ::HIR::CoreType::U32:
                            case ::HIR::CoreType::I64:
                            case ::HIR::CoreType::U64:
                            case ::HIR::CoreType::I128:
                            case ::HIR::CoreType::U128:
                            case ::HIR::CoreType::Isize:
                            case ::HIR::CoreType::Usize:
                                return Compare::Fuzzy;
                            default:
                                return Compare::Unequal;
                        }
                    }
                    TU_ARMA(Infer, re) {
                        switch (re.ty_class) {
                            case ::HIR::InferClass::None:
                            case ::HIR::InferClass::Integer:
                                return Compare::Fuzzy;
                            case ::HIR::InferClass::Float:
                                return Compare::Unequal;
                        }
                    }
                    TU_ARMA(Path, re) {
                        return re.binding.is_Unbound() ? Compare::Fuzzy : Compare::Unequal;
                    }
            }
        case ::HIR::InferClass::Float:
            TU_MATCH_HDRA( (*right), {)
            default:
                return Compare::Unequal;
                    TU_ARMA(Primitive, re) {
                        switch (re) {
                            case ::HIR::CoreType::F16:
                            case ::HIR::CoreType::F32:
                            case ::HIR::CoreType::F64:
                            case ::HIR::CoreType::F128:
                                return Compare::Fuzzy;
                            default:
                                return Compare::Unequal;
                        }
                    }
                    TU_ARMA(Infer, re) {
                        switch (re.ty_class) {
                            case ::HIR::InferClass::None:
                            case ::HIR::InferClass::Float:
                                return Compare::Fuzzy;
                            case ::HIR::InferClass::Integer:
                                return Compare::Unequal;
                        }
                    }
                    TU_ARMA(Path, re) {
                        return re.binding.is_Unbound() ? Compare::Fuzzy : Compare::Unequal;
                    }
            }
        }
        throw "";
    }

    // If righthand side is infer, it's a fuzzy match (or not a match)
    if (const auto* re = right->opt_Infer()) {
        switch (re->ty_class) {
            case ::HIR::InferClass::None:
                return Compare::Fuzzy;
            case ::HIR::InferClass::Integer:
            TU_MATCH_HDRA( (*left), {)
            default:
                return Compare::Unequal;
                    TU_ARMA(Primitive, le) {
                        switch (le) {
                            case ::HIR::CoreType::I8:
                            case ::HIR::CoreType::U8:
                            case ::HIR::CoreType::I16:
                            case ::HIR::CoreType::U16:
                            case ::HIR::CoreType::I32:
                            case ::HIR::CoreType::U32:
                            case ::HIR::CoreType::I64:
                            case ::HIR::CoreType::U64:
                            case ::HIR::CoreType::I128:
                            case ::HIR::CoreType::U128:
                            case ::HIR::CoreType::Isize:
                            case ::HIR::CoreType::Usize:
                                return Compare::Fuzzy;
                            default:
                                return Compare::Unequal;
                        }
                    }
                    TU_ARMA(Path, le) {
                        return le.binding.is_Unbound() ? Compare::Fuzzy : Compare::Unequal;
                    }
            }
        case ::HIR::InferClass::Float:
            TU_MATCH_HDRA( (*left), {)
            default:
                return Compare::Unequal;
                    TU_ARMA(Primitive, le) {
                        switch (le) {
                            case ::HIR::CoreType::F16:
                            case ::HIR::CoreType::F32:
                            case ::HIR::CoreType::F64:
                            case ::HIR::CoreType::F128:
                                return Compare::Fuzzy;
                            default:
                                return Compare::Unequal;
                        }
                    }
                    TU_ARMA(Path, le) {
                        return le.binding.is_Unbound() ? Compare::Fuzzy : Compare::Unequal;
                    }
            }
        }
        throw "";
    }

    // If righthand is a type parameter, it can only match another type parameter
    // - See `(Generic,` below

    if (left->tag() != right->tag()) {
        return Compare::Unequal;
    }
    TU_MATCH_HDRA( (*left, *right), {)
    TU_ARMA(Infer, le, re) {
            assert(!"infer");
        }
        TU_ARMA(Diverge, le, re) {
            return Compare::Equal;
        }
        TU_ARMA(Primitive, le, re) {
            return (le == re ? Compare::Equal : Compare::Unequal);
        }
        TU_ARMA(Path, le, re) {
            auto rv = le.path.compareWithPlaceholders(sp, re.path, resolve_placeholder);
            if (rv == ::HIR::Compare::Unequal) {
                if (le.binding.is_Unbound() || re.binding.is_Unbound()) {
                    rv = ::HIR::Compare::Fuzzy;
                }
            }
            return rv;
        }
        TU_ARMA(Generic, le, re) {
            if (le.binding != re.binding) {
                if ((le.binding >> 8) == 2) {
                    return Compare::Fuzzy;
                }
                if ((re.binding >> 8) == 2) {
                    return Compare::Fuzzy;
                }
                return Compare::Unequal;
            }
            return Compare::Equal;
        }
        TU_ARMA(TraitObject, le, re) {
            if (le.markers.size() != re.markers.size()) {
                return Compare::Unequal;
            }
            auto rv = le.mTrait.compareWithPlaceholders(sp, re.mTrait, resolve_placeholder);
            if (rv == Compare::Unequal) {
                return rv;
            }
            for (unsigned int i = 0; i < le.markers.size(); i++) {
                auto rv2 = le.markers[i].compareWithPlaceholders(sp, re.markers[i], resolve_placeholder);
                if (rv2 == Compare::Unequal) {
                    return Compare::Unequal;
                }
                if (rv2 == Compare::Fuzzy) {
                    rv = Compare::Fuzzy;
                }
            }
            return rv;
        }
        TU_ARMA(ErasedType, le, re) {
            if (le.inner.tag() != re.inner.tag()) {
                return Compare::Unequal;
            }
        TU_MATCH_HDRA( (le.inner, re.inner), {)
        TU_ARMA(Known, l,r) {
                    return l->compareWithPlaceholders(sp, r, resolve_placeholder);
                }
                TU_ARMA(Alias, l, r) {
                    if (l.inner != r.inner) {
                        return Compare::Unequal;
                    }
                    return l.params.compareWithPlaceholders(sp, r.params, resolve_placeholder);
                }
                TU_ARMA(Fcn, l, r) {
                    if (l.index != r.index) {
                        return Compare::Unequal;
                    }
                    return l.origin.compareWithPlaceholders(sp, r.origin, resolve_placeholder);
                }
        }
        return Compare::Equal;
        }
        TU_ARMA(Array, le, re) {
            auto rv = Compare::Equal;
            if (le.size.is_Unevaluated() && le.size.as_Unevaluated().is_Infer()) {
                rv &= Compare::Fuzzy;
            } else if (re.size.is_Unevaluated() && re.size.as_Unevaluated().is_Infer()) {
                rv &= Compare::Fuzzy;
            } else if (le.size != re.size) {
                return Compare::Unequal;
            } else {
                // Sizes equal
            }
            rv &= le.inner->compareWithPlaceholders(sp, re.inner, resolve_placeholder);
            return rv;
        }
        TU_ARMA(Slice, le, re) {
            return le.inner->compareWithPlaceholders(sp, re.inner, resolve_placeholder);
        }
        TU_ARMA(Tuple, le, re) {
            if (le.size() != re.size()) {
                return Compare::Unequal;
            }
            auto rv = Compare::Equal;
            for (unsigned int i = 0; i < le.size(); i++) {
                auto rv2 = le[i]->compareWithPlaceholders(sp, re[i], resolve_placeholder);
                if (rv2 == Compare::Unequal) {
                    return Compare::Unequal;
                }
                if (rv2 == Compare::Fuzzy) {
                    rv = Compare::Fuzzy;
                }
            }
            return rv;
        }
        TU_ARMA(Borrow, le, re) {
            if (le.type != re.type) {
                return Compare::Unequal;
            }
            return le.inner->compareWithPlaceholders(sp, re.inner, resolve_placeholder);
        }
        TU_ARMA(Pointer, le, re) {
            if (le.type != re.type) {
                return Compare::Unequal;
            }
            return le.inner->compareWithPlaceholders(sp, re.inner, resolve_placeholder);
        }
        TU_ARMA(NamedFunction, le, re) {
            return le.path.compareWithPlaceholders(sp, re.path, resolve_placeholder);
        }
        TU_ARMA(Function, le, re) {
            if (le.mAbi != re.mAbi || le.is_unsafe != re.is_unsafe) {
                return Compare::Unequal;
            }
            if (le.argTypes.size() != re.argTypes.size()) {
                return Compare::Unequal;
            }
            auto rv = Compare::Equal;
            for (unsigned int i = 0; i < le.argTypes.size(); i++) {
                rv &= le.argTypes[i]->compareWithPlaceholders(sp, re.argTypes[i], resolve_placeholder);
                if (rv == Compare::Unequal) {
                    return Compare::Unequal;
                }
            }
            rv &= le.mRettype->compareWithPlaceholders(sp, re.mRettype, resolve_placeholder);
            return rv;
        }
        TU_ARMA(NodeType, le, re) {
            return le == re ? Compare::Equal : Compare::Unequal;
        }
    }
    throw "";
}

namespace HIR {

TypeInterner::TypeInterner(stl::ObjPool& pool): pool(pool) {}
}

namespace HIR {

bool is_integer(const CoreType& v) {
    switch (v) {
        case CoreType::Usize:
        case CoreType::Isize:
        case CoreType::U8:
        case CoreType::I8:
        case CoreType::U16:
        case CoreType::I16:
        case CoreType::U32:
        case CoreType::I32:
        case CoreType::U64:
        case CoreType::I64:
        case CoreType::U128:
        case CoreType::I128:
            return true;
        default:
            return false;
    }
}
bool isFloat(const CoreType& v) {
    switch (v) {
        case CoreType::F16:
        case CoreType::F32:
        case CoreType::F64:
        case CoreType::F128:
            return true;
        default:
            return false;
    }
}
}
