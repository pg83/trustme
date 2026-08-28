#include "hir_type.h"

#include "span.h"
#include "hir_expr.h"

#include <std/alg/defer.h>
#include <std/mem/obj_pool.h>

#include <cstdint>

using namespace stl;

namespace {
    struct TypeFmtRecursionNode {
        const HIRTypeData* type;
        const TypeFmtRecursionNode* next;
    };

    struct TypeFmtStream final: public std::ostream {
        const TypeFmtRecursionNode* recurseStack = nullptr;

        explicit TypeFmtStream(std::ostream& output);

        static TypeFmtStream* from(std::ostream& output);
    };

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
        switch (a.tag()) {
            case HIRConstGeneric::TAG_Infer: {
                auto& ae = a.as_Infer();
                auto& be = b.as_Infer();
                return ae.index == be.index;
            }
            case HIRConstGeneric::TAG_Generic: {
                auto& ae = a.as_Generic();
                auto& be = b.as_Generic();
                return exactGenericRefEqual(ae, be);
            }
            case HIRConstGeneric::TAG_Evaluated: {
                auto& ae = a.as_Evaluated();
                auto& be = b.as_Evaluated();
                return *ae == *be;
            }
            case HIRConstGeneric::TAG_Unevaluated: {
                auto& ae = a.as_Unevaluated();
                auto& be = b.as_Unevaluated();
                return ae->expr.get() == be->expr.get() && ae->selfType == be->selfType && exactPathParamsEqual(ae->paramsImpl, be->paramsImpl) && exactPathParamsEqual(ae->paramsItem, be->paramsItem);
            }
        }
        UNREACHABLE();
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
        return a.path == b.path && exactPathParamsEqual(a.params, b.params);
    }

    bool exactOptionalGenericParamsEqual(const std::unique_ptr<HIRGenericParams>& a, const std::unique_ptr<HIRGenericParams>& b) {
        return (!a && !b) || (a && b && exactGenericParamsEqual(*a, *b));
    }

    bool exactPathEqual(const HIRPath& a, const HIRPath& b) {
        if (a.data.tag() != b.data.tag()) {
            return false;
        }
        switch (a.data.tag()) {
            case HIRPathData::TAG_Generic: {
                auto& ae = a.data.as_Generic();
                auto& be = b.data.as_Generic();
                return exactGenericPathEqual(ae, be);
            }
            case HIRPathData::TAG_UfcsInherent: {
                auto& ae = a.data.as_UfcsInherent();
                auto& be = b.data.as_UfcsInherent();
                return ae.type == be.type && ae.item == be.item && exactPathParamsEqual(ae.params, be.params) && exactPathParamsEqual(ae.implParams, be.implParams);
            }
            case HIRPathData::TAG_UfcsKnown: {
                auto& ae = a.data.as_UfcsKnown();
                auto& be = b.data.as_UfcsKnown();
                return ae.type == be.type && exactGenericPathEqual(ae.trait, be.trait) && ae.item == be.item && exactPathParamsEqual(ae.params, be.params);
            }
            case HIRPathData::TAG_UfcsUnknown: {
                auto& ae = a.data.as_UfcsUnknown();
                auto& be = b.data.as_UfcsUnknown();
                return ae.type == be.type && ae.item == be.item && exactPathParamsEqual(ae.params, be.params);
            }
        }
        UNREACHABLE();
    }

    bool exactTraitPathEqual(const HIRTraitPath& a, const HIRTraitPath& b) {
        if (!exactGenericPathEqual(a.path, b.path) || a.traitPtr != b.traitPtr || a.typeBounds.size() != b.typeBounds.size() || a.traitBounds.size() != b.traitBounds.size()) {
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
        switch (a.tag()) {
            case HIRGenericBound::TAG_TraitBound: {
                auto& ae = a.as_TraitBound();
                auto& be = b.as_TraitBound();
                return ae.type == be.type && exactTraitPathEqual(ae.trait, be.trait) && ae.isTrivial == be.isTrivial;
            }
            case HIRGenericBound::TAG_TypeEquality: {
                auto& ae = a.as_TypeEquality();
                auto& be = b.as_TypeEquality();
                return ae.type == be.type && ae.otherType == be.otherType;
            }
        }
        UNREACHABLE();
    }

    bool exactGenericParamsEqual(const HIRGenericParams& a, const HIRGenericParams& b) {
        if (a.types.size() != b.types.size() || a.values.size() != b.values.size() || a.bounds.size() != b.bounds.size()) {
            return false;
        }
        for (size_t i = 0; i < a.types.size(); i++) {
            if (a.types[i].name != b.types[i].name || a.types[i].defaultValue != b.types[i].defaultValue || a.types[i].isSized != b.types[i].isSized) {
                return false;
            }
        }
        for (size_t i = 0; i < a.values.size(); i++) {
            if (a.values[i].name != b.values[i].name || a.values[i].type != b.values[i].type || !exactConstGenericEqual(a.values[i].defaultValue, b.values[i].defaultValue)) {
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
        switch (a.tag()) {
            case HIRTypePathBinding::TAG_Unbound: {
                return true;
            }
            case HIRTypePathBinding::TAG_Opaque: {
                return true;
            }
            case HIRTypePathBinding::TAG_ExternType: {
                auto& ae = a.as_ExternType();
                auto& be = b.as_ExternType();
                return ae == be;
            }
            case HIRTypePathBinding::TAG_Struct: {
                auto& ae = a.as_Struct();
                auto& be = b.as_Struct();
                return ae == be;
            }
            case HIRTypePathBinding::TAG_Union: {
                auto& ae = a.as_Union();
                auto& be = b.as_Union();
                return ae == be;
            }
            case HIRTypePathBinding::TAG_Enum: {
                auto& ae = a.as_Enum();
                auto& be = b.as_Enum();
                return ae == be;
            }
        }
        UNREACHABLE();
    }

    bool exactErasedInnerEqual(const TypeDataErasedTypeInner& a, const TypeDataErasedTypeInner& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        switch (a.tag()) {
            case TypeDataErasedTypeInner::TAG_Fcn: {
                auto& ae = a.as_Fcn();
                auto& be = b.as_Fcn();
                return ae.index == be.index && exactPathEqual(ae.origin, be.origin);
            }
            case TypeDataErasedTypeInner::TAG_Known: {
                auto& ae = a.as_Known();
                auto& be = b.as_Known();
                return ae == be;
            }
            case TypeDataErasedTypeInner::TAG_Alias: {
                auto& ae = a.as_Alias();
                auto& be = b.as_Alias();
                return ae.inner->path == be.inner->path && exactPathParamsEqual(ae.params, be.params);
            }
        }
        UNREACHABLE();
    }

    bool exactArraySizeEqual(const HIRArraySize& a, const HIRArraySize& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        switch (a.tag()) {
            case HIRArraySize::TAG_Known: {
                auto& ae = a.as_Known();
                auto& be = b.as_Known();
                return ae == be;
            }
            case HIRArraySize::TAG_Unevaluated: {
                auto& ae = a.as_Unevaluated();
                auto& be = b.as_Unevaluated();
                return exactConstGenericEqual(ae, be);
            }
        }
        UNREACHABLE();
    }

    bool exactTypeDataEqual(const HIRTypeData& a, const HIRTypeData& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        switch (a.tag()) {
            case HIRTypeData::TAG_Infer: {
                auto& ae = a.as_Infer();
                auto& be = b.as_Infer();
                return ae.index == be.index && ae.tyClass == be.tyClass;
            }
            case HIRTypeData::TAG_Diverge: {
                return true;
            }
            case HIRTypeData::TAG_Primitive: {
                auto& ae = a.as_Primitive();
                auto& be = b.as_Primitive();
                return ae == be;
            }
            case HIRTypeData::TAG_Path: {
                auto& ae = a.as_Path();
                auto& be = b.as_Path();
                return exactPathEqual(ae.path, be.path) && exactBindingEqual(ae.binding, be.binding);
            }
            case HIRTypeData::TAG_Generic: {
                auto& ae = a.as_Generic();
                auto& be = b.as_Generic();
                return exactGenericRefEqual(ae, be);
            }
            case HIRTypeData::TAG_TraitObject: {
                auto& ae = a.as_TraitObject();
                auto& be = b.as_TraitObject();
                if (!exactTraitPathEqual(ae.trait, be.trait) || ae.markers.size() != be.markers.size()) {
                    return false;
                }
                for (size_t i = 0; i < ae.markers.size(); i++) {
                    if (!exactGenericPathEqual(ae.markers[i], be.markers[i])) {
                        return false;
                    }
                }
                return ae.lifetimeIdentity == be.lifetimeIdentity && ae.lifetimeIdentityHasFree == be.lifetimeIdentityHasFree;
            }
            case HIRTypeData::TAG_ErasedType: {
                auto& ae = a.as_ErasedType();
                auto& be = b.as_ErasedType();
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
            case HIRTypeData::TAG_Array: {
                auto& ae = a.as_Array();
                auto& be = b.as_Array();
                return ae.inner == be.inner && exactArraySizeEqual(ae.size, be.size);
            }
            case HIRTypeData::TAG_Slice: {
                auto& ae = a.as_Slice();
                auto& be = b.as_Slice();
                return ae.inner == be.inner;
            }
            case HIRTypeData::TAG_Pattern: {
                auto& ae = a.as_Pattern();
                auto& be = b.as_Pattern();
                return ae.inner == be.inner && ae.pattern.ord(be.pattern) == OrdEqual;
            }
            case HIRTypeData::TAG_Tuple: {
                auto& ae = a.as_Tuple();
                auto& be = b.as_Tuple();
                return ae == be;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& ae = a.as_Borrow();
                auto& be = b.as_Borrow();
                return ae.type == be.type && ae.inner == be.inner;
            }
            case HIRTypeData::TAG_Pointer: {
                auto& ae = a.as_Pointer();
                auto& be = b.as_Pointer();
                return ae.type == be.type && ae.inner == be.inner;
            }
            case HIRTypeData::TAG_NamedFunction: {
                auto& ae = a.as_NamedFunction();
                auto& be = b.as_NamedFunction();
                if (!exactPathEqual(ae.path, be.path) || ae.def.tag() != be.def.tag()) {
                    return false;
                }
                switch (ae.def.tag()) {
                    case HIRTypeDataNamedFunctionTy::TAG_Function: {
                        auto& ad = ae.def.as_Function();
                        auto& bd = be.def.as_Function();
                        return ad == bd;
                    }
                    case HIRTypeDataNamedFunctionTy::TAG_EnumConstructor: {
                        auto& ad = ae.def.as_EnumConstructor();
                        auto& bd = be.def.as_EnumConstructor();
                        return ad.e == bd.e && ad.v == bd.v;
                    }
                    case HIRTypeDataNamedFunctionTy::TAG_StructConstructor: {
                        auto& ad = ae.def.as_StructConstructor();
                        auto& bd = be.def.as_StructConstructor();
                        return ad == bd;
                    }
                }
                UNREACHABLE();
            }
            case HIRTypeData::TAG_Function: {
                auto& ae = a.as_Function();
                auto& be = b.as_Function();
                return ae.isUnsafe == be.isUnsafe && ae.isVariadic == be.isVariadic && ae.abi == be.abi && ae.rettype == be.rettype && ae.argTypes == be.argTypes && ae.trackCaller == be.trackCaller && ae.lifetimeIdentity == be.lifetimeIdentity && ae.lifetimeIdentityHasFree == be.lifetimeIdentityHasFree;
            }
            case HIRTypeData::TAG_NodeType: {
                auto& ae = a.as_NodeType();
                auto& be = b.as_NodeType();
                return ae == be;
            }
        }
        UNREACHABLE();
    }

    void addTypeFlags(u32& flags, HIRTypeRef type) {
        if (type) {
            flags |= type->flags;
        }
    }

    u32 typeFlags(const HIRPathParams& params);

    u32 typeFlags(const HIRGenericPath& path) {
        return typeFlags(path.params);
    }

    u32 typeFlags(const HIRTraitPath& trait) {
        auto flags = typeFlags(trait.path);
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

    u32 typeFlags(const HIRPathParams& params) {
        u32 flags = 0;
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

    u32 typeFlags(const HIRPath& path) {
        u32 flags = 0;
        switch (path.data.tag()) {
            case HIRPathData::TAG_Generic: {
                auto& e = path.data.as_Generic();
                flags |= typeFlags(e.params);
                break;
            }
            case HIRPathData::TAG_UfcsInherent: {
                auto& e = path.data.as_UfcsInherent();
                addTypeFlags(flags, e.type);
                flags |= typeFlags(e.params);
                flags |= typeFlags(e.implParams);
                break;
            }
            case HIRPathData::TAG_UfcsKnown: {
                auto& e = path.data.as_UfcsKnown();
                addTypeFlags(flags, e.type);
                flags |= typeFlags(e.trait);
                flags |= typeFlags(e.params);
                break;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                auto& e = path.data.as_UfcsUnknown();
                addTypeFlags(flags, e.type);
                flags |= typeFlags(e.params);
                break;
            }
        }
        return flags;
    }

    u32 typeFlags(const HIRTypeData& type) {
        u32 flags = 0;
        switch (type.tag()) {
            case HIRTypeData::TAG_Infer: {
                flags |= HIRTypeData::HAS_TYPE_INFER;
                break;
            }
            case HIRTypeData::TAG_Diverge: {
                break;
            }
            case HIRTypeData::TAG_Primitive: {
                break;
            }
            case HIRTypeData::TAG_Path: {
                auto& e = type.as_Path();
                flags |= typeFlags(e.path);
                if (e.path.data.is_UfcsKnown() && (e.binding.is_Unbound() || e.binding.is_Opaque())) {
                    flags |= HIRTypeData::HAS_ASSOCIATED_TYPE;
                }
                break;
            }
            case HIRTypeData::TAG_Generic: {
                flags |= HIRTypeData::HAS_TYPE_PARAM;
                break;
            }
            case HIRTypeData::TAG_TraitObject: {
                auto& e = type.as_TraitObject();
                flags |= typeFlags(e.trait);
                for (const auto& marker : e.markers) {
                    flags |= typeFlags(marker);
                }
                break;
            }
            case HIRTypeData::TAG_ErasedType: {
                auto& e = type.as_ErasedType();
                for (const auto& trait : e.traits) {
                    flags |= typeFlags(trait);
                }
                flags |= typeFlags(e.use);
                {
                    auto& tuMatch = e.inner;
                    switch (tuMatch.tag()) {
                        case TypeDataErasedTypeInner::TAG_Fcn: {
                            auto& inner = tuMatch.as_Fcn();
                            flags |= typeFlags(inner.origin);
                            break;
                        }
                        case TypeDataErasedTypeInner::TAG_Known: {
                            auto& inner = tuMatch.as_Known();
                            addTypeFlags(flags, inner);
                            break;
                        }
                        case TypeDataErasedTypeInner::TAG_Alias: {
                            auto& inner = tuMatch.as_Alias();
                            flags |= typeFlags(inner.params);
                            break;
                        }
                    }
                }
                break;
            }
            case HIRTypeData::TAG_Array: {
                auto& e = type.as_Array();
                addTypeFlags(flags, e.inner);
                if (e.size.is_Unevaluated()) {
                    flags |= HIRTypeData::HAS_UNEVALUATED_CONST;
                }
                break;
            }
            case HIRTypeData::TAG_Slice: {
                auto& e = type.as_Slice();
                addTypeFlags(flags, e.inner);
                break;
            }
            case HIRTypeData::TAG_Pattern: {
                auto& e = type.as_Pattern();
                addTypeFlags(flags, e.inner);
                for (const auto& range : e.pattern.alternatives) {
                    const HIRConstGeneric* values[] = {range.hasStart ? &range.start : nullptr, range.hasEnd ? &range.end : nullptr};
                    for (const auto* value : values) {
                        if (!value) {
                            continue;
                        }
                        if (value->is_Generic()) {
                            flags |= HIRTypeData::HAS_TYPE_PARAM;
                        } else if (value->is_Infer() || value->is_Unevaluated()) {
                            flags |= HIRTypeData::HAS_DEFERRED_CONST;
                        }
                    }
                }
                break;
            }
            case HIRTypeData::TAG_Tuple: {
                auto& e = type.as_Tuple();
                for (const auto inner : e) {
                    addTypeFlags(flags, inner);
                }
                break;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& e = type.as_Borrow();
                addTypeFlags(flags, e.inner);
                break;
            }
            case HIRTypeData::TAG_Pointer: {
                auto& e = type.as_Pointer();
                addTypeFlags(flags, e.inner);
                break;
            }
            case HIRTypeData::TAG_NamedFunction: {
                auto& e = type.as_NamedFunction();
                flags |= typeFlags(e.path);
                break;
            }
            case HIRTypeData::TAG_Function: {
                auto& e = type.as_Function();
                addTypeFlags(flags, e.rettype);
                for (const auto argument : e.argTypes) {
                    addTypeFlags(flags, argument);
                }
                break;
            }
            case HIRTypeData::TAG_NodeType: {
                break;
            }
        }
        return flags;
    }

    size_t hashMix(size_t state, size_t value) {
        return state ^ (value + 0x9e3779b97f4a7c15ULL + (state << 6) + (state >> 2));
    }

    size_t hashSimplePath(const HIRSimplePath& path) {
        size_t h = std::hash<RcString>()(path.crateName());
        for (const auto& component : path.components()) {
            h = hashMix(h, std::hash<RcString>()(component));
        }
        return h;
    }

    size_t hashTypeRef(HIRTypeRef type) {
        return std::hash<const void*>()(type);
    }

    size_t hashPathParams(const HIRPathParams& params);

    size_t hashGenericRef(const HIRGenericRef& generic) {
        size_t h = generic.binding;
        if (generic.group() == GENERICPlaceholder) {
            if (generic.isSolverExistential()) {
                h = hashMix(h, generic.solverScope);
            } else {
                h = hashMix(h, std::hash<RcString>()(generic.name));
            }
        }
        return h;
    }

    size_t hashConstGeneric(const HIRConstGeneric& value) {
        size_t h = static_cast<size_t>(value.tag());
        switch (value.tag()) {
            case HIRConstGeneric::TAG_Infer: {
                auto& e = value.as_Infer();
                h = hashMix(h, e.index);
                break;
            }
            case HIRConstGeneric::TAG_Generic: {
                auto& e = value.as_Generic();
                h = hashMix(h, hashGenericRef(e));
                break;
            }
            case HIRConstGeneric::TAG_Evaluated: {
                break;
            }
            case HIRConstGeneric::TAG_Unevaluated: {
                auto& e = value.as_Unevaluated();
                h = hashMix(h, reinterpret_cast<uintptr_t>(e->expr.get()));
                h = hashMix(h, hashTypeRef(e->selfType));
                h = hashMix(h, hashPathParams(e->paramsImpl));
                h = hashMix(h, hashPathParams(e->paramsItem));
                break;
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
        return hashMix(hashSimplePath(path.path), hashPathParams(path.params));
    }

    size_t hashPath(const HIRPath& path) {
        size_t h = static_cast<size_t>(path.data.tag());
        switch (path.data.tag()) {
            case HIRPathData::TAG_Generic: {
                auto& e = path.data.as_Generic();
                h = hashMix(h, hashGenericPath(e));
                break;
            }
            case HIRPathData::TAG_UfcsInherent: {
                auto& e = path.data.as_UfcsInherent();
                h = hashMix(h, hashTypeRef(e.type));
                h = hashMix(h, std::hash<RcString>()(e.item));
                h = hashMix(h, hashPathParams(e.params));
                h = hashMix(h, hashPathParams(e.implParams));
                break;
            }
            case HIRPathData::TAG_UfcsKnown: {
                auto& e = path.data.as_UfcsKnown();
                h = hashMix(h, hashTypeRef(e.type));
                h = hashMix(h, hashGenericPath(e.trait));
                h = hashMix(h, std::hash<RcString>()(e.item));
                h = hashMix(h, hashPathParams(e.params));
                break;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                auto& e = path.data.as_UfcsUnknown();
                h = hashMix(h, hashTypeRef(e.type));
                h = hashMix(h, std::hash<RcString>()(e.item));
                h = hashMix(h, hashPathParams(e.params));
                break;
            }
        }
        return h;
    }

    size_t hashBinding(const HIRTypePathBinding& binding) {
        size_t h = static_cast<size_t>(binding.tag());
        switch (binding.tag()) {
            case HIRTypePathBinding::TAG_Unbound: {
                break;
            }
            case HIRTypePathBinding::TAG_Opaque: {
                break;
            }
            case HIRTypePathBinding::TAG_ExternType: {
                auto& e = binding.as_ExternType();
                h = hashMix(h, reinterpret_cast<uintptr_t>(e));
                break;
            }
            case HIRTypePathBinding::TAG_Struct: {
                auto& e = binding.as_Struct();
                h = hashMix(h, reinterpret_cast<uintptr_t>(e));
                break;
            }
            case HIRTypePathBinding::TAG_Union: {
                auto& e = binding.as_Union();
                h = hashMix(h, reinterpret_cast<uintptr_t>(e));
                break;
            }
            case HIRTypePathBinding::TAG_Enum: {
                auto& e = binding.as_Enum();
                h = hashMix(h, reinterpret_cast<uintptr_t>(e));
                break;
            }
        }
        return h;
    }

    size_t hashTypeData(const HIRTypeData& type) {
        size_t h = static_cast<size_t>(type.tag());
        switch (type.tag()) {
            case HIRTypeData::TAG_Infer: {
                auto& e = type.as_Infer();
                h = hashMix(h, e.index);
                h = hashMix(h, static_cast<size_t>(e.tyClass));
                break;
            }
            case HIRTypeData::TAG_Diverge: {
                break;
            }
            case HIRTypeData::TAG_Primitive: {
                auto& e = type.as_Primitive();
                h = hashMix(h, static_cast<size_t>(e));
                break;
            }
            case HIRTypeData::TAG_Path: {
                auto& e = type.as_Path();
                h = hashMix(h, hashPath(e.path));
                h = hashMix(h, hashBinding(e.binding));
                break;
            }
            case HIRTypeData::TAG_Generic: {
                auto& e = type.as_Generic();
                h = hashMix(h, hashGenericRef(e));
                break;
            }
            case HIRTypeData::TAG_TraitObject: {
                auto& e = type.as_TraitObject();
                h = hashMix(h, hashGenericPath(e.trait.path));
                h = hashMix(h, reinterpret_cast<uintptr_t>(e.trait.traitPtr));
                h = hashMix(h, e.lifetimeIdentity.rawId());
                h = hashMix(h, e.lifetimeIdentityHasFree);
                for (const auto& marker : e.markers) {
                    h = hashMix(h, hashGenericPath(marker));
                }
                for (const auto& bound : e.trait.typeBounds) {
                    h = hashMix(h, std::hash<RcString>()(bound.first));
                    h = hashMix(h, hashGenericPath(bound.second.sourceTrait));
                    h = hashMix(h, hashPathParams(bound.second.atyParams));
                    h = hashMix(h, hashTypeRef(bound.second.type));
                }
                break;
            }
            case HIRTypeData::TAG_ErasedType: {
                auto& e = type.as_ErasedType();
                h = hashMix(h, static_cast<size_t>(e.inner.tag()));
                h = hashMix(h, e.traits.size());
                {
                    auto& tuMatch = e.inner;
                    switch (tuMatch.tag()) {
                        case TypeDataErasedTypeInner::TAG_Fcn: {
                            auto& inner = tuMatch.as_Fcn();
                            h = hashMix(h, hashPath(inner.origin));
                            h = hashMix(h, inner.index);
                            break;
                        }
                        case TypeDataErasedTypeInner::TAG_Known: {
                            auto& inner = tuMatch.as_Known();
                            h = hashMix(h, hashTypeRef(inner));
                            break;
                        }
                        case TypeDataErasedTypeInner::TAG_Alias: {
                            auto& inner = tuMatch.as_Alias();
                            h = hashMix(h, hashSimplePath(inner.inner->path));
                            h = hashMix(h, hashPathParams(inner.params));
                            break;
                        }
                    }
                }
                break;
            }
            case HIRTypeData::TAG_Array: {
                auto& e = type.as_Array();
                h = hashMix(h, hashTypeRef(e.inner));
                h = hashMix(h, static_cast<size_t>(e.size.tag()));
                {
                    auto& tuMatch = e.size;
                    switch (tuMatch.tag()) {
                        case HIRArraySize::TAG_Known: {
                            auto& size = tuMatch.as_Known();
                            h = hashMix(h, size);
                            break;
                        }
                        case HIRArraySize::TAG_Unevaluated: {
                            auto& size = tuMatch.as_Unevaluated();
                            h = hashMix(h, hashConstGeneric(size));
                            break;
                        }
                    }
                }
                break;
            }
            case HIRTypeData::TAG_Slice: {
                auto& e = type.as_Slice();
                h = hashMix(h, hashTypeRef(e.inner));
                break;
            }
            case HIRTypeData::TAG_Pattern: {
                auto& e = type.as_Pattern();
                h = hashMix(h, hashTypeRef(e.inner));
                h = hashMix(h, e.pattern.alternatives.size());
                for (const auto& range : e.pattern.alternatives) {
                    h = hashMix(h, range.hasStart);
                    if (range.hasStart) {
                        h = hashMix(h, hashConstGeneric(range.start));
                    }
                    h = hashMix(h, range.hasEnd);
                    if (range.hasEnd) {
                        h = hashMix(h, hashConstGeneric(range.end));
                    }
                    h = hashMix(h, range.endInclusive);
                }
                break;
            }
            case HIRTypeData::TAG_Tuple: {
                auto& e = type.as_Tuple();
                for (auto t : e) {
                    h = hashMix(h, hashTypeRef(t));
                }
                break;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& e = type.as_Borrow();
                h = hashMix(h, static_cast<size_t>(e.type));
                h = hashMix(h, hashTypeRef(e.inner));
                break;
            }
            case HIRTypeData::TAG_Pointer: {
                auto& e = type.as_Pointer();
                h = hashMix(h, static_cast<size_t>(e.type));
                h = hashMix(h, hashTypeRef(e.inner));
                break;
            }
            case HIRTypeData::TAG_NamedFunction: {
                auto& e = type.as_NamedFunction();
                h = hashMix(h, hashPath(e.path));
                h = hashMix(h, static_cast<size_t>(e.def.tag()));
                break;
            }
            case HIRTypeData::TAG_Function: {
                auto& e = type.as_Function();
                h = hashMix(h, std::hash<RcString>()(e.abi));
                h = hashMix(h, e.lifetimeIdentity.rawId());
                h = hashMix(h, e.lifetimeIdentityHasFree);
                h = hashMix(h, e.isUnsafe);
                h = hashMix(h, e.isVariadic);
                h = hashMix(h, e.trackCaller);
                h = hashMix(h, hashTypeRef(e.rettype));
                for (auto t : e.argTypes) {
                    h = hashMix(h, hashTypeRef(t));
                }
                break;
            }
            case HIRTypeData::TAG_NodeType: {
                auto& e = type.as_NodeType();
                switch (e.tag()) {
                    case HIRTypeDataNodeType::TAG_Closure: {
                        auto& p = e.as_Closure();
                        h = hashMix(h, reinterpret_cast<uintptr_t>(p));
                        break;
                    }
                    case HIRTypeDataNodeType::TAG_Generator: {
                        auto& p = e.as_Generator();
                        h = hashMix(h, reinterpret_cast<uintptr_t>(p));
                        break;
                    }
                    case HIRTypeDataNodeType::TAG_Async: {
                        auto& p = e.as_Async();
                        h = hashMix(h, reinterpret_cast<uintptr_t>(p));
                        break;
                    }
                }
                break;
            }
        }
        return h;
    }

    HIRCompare matchGenericsPp(const Span& sp, const HIRPathParams& t, const HIRPathParams& x, tCbResolveType resolvePlaceholder, HIRMatchGenerics& callback) {
        return t.matchTestGenericsFuzz(sp, x, resolvePlaceholder, callback);
    }

    HIRCompare matchValues(const Span& sp, const HIRConstGeneric& t, const HIRConstGeneric& x, HIRMatchGenerics& callback) {
        if (const auto* e = t.opt_Generic()) {
            return callback.matchVal(*e, x);
        }

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

        switch (t.tag()) {
            case HIRConstGeneric::TAG_Infer: {
                UNREACHABLE();
            }
            case HIRConstGeneric::TAG_Unevaluated: {
                auto& te = t.as_Unevaluated();
                auto& xe = x.as_Unevaluated();
                return te->equivalent(*xe) ? HIRCompare::Equal : HIRCompare::Unequal;
            }
            case HIRConstGeneric::TAG_Generic: {
                UNREACHABLE();
            }
            case HIRConstGeneric::TAG_Evaluated: {
                auto& te = t.as_Evaluated();
                auto& xe = x.as_Evaluated();
                return *te == *xe ? HIRCompare::Equal : HIRCompare::Unequal;
            }
        }
        UNREACHABLE();
    }
}

Ordering ord(const HIRTypeData* l, const HIRTypeData* r) {
    if (l == r) {
        return OrdEqual;
    }
    BUG_ASSERT(l->uid != r->uid);
    return l->uid < r->uid ? OrdLess : OrdGreater;
}

std::ostream& operator<<(std::ostream& os, const HIRTypeData* ty) {
    if (ty) {
        ty->fmt(os);
    } else {
        os << "NULL";
    }
    return os;
}

std::ostream& operator<<(std::ostream& os, const HIRCoreType& ct) {
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
    BUG_ASSERT(!"Bad CoreType value");
    return os;
}

std::ostream& operator<<(std::ostream& os, const HIRBorrowType& bt) {
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

std::ostream& operator<<(std::ostream& os, const HIRArraySize& x) {
    switch (x.tag()) {
        case HIRArraySize::TAG_Unevaluated: {
            auto& se = x.as_Unevaluated();
            os << se;
            break;
        }
        case HIRArraySize::TAG_Known: {
            auto& se = x.as_Known();
            os << se;
            break;
        }
    }
    return os;
}

HIRTypePatternRange HIRTypePatternRange::clone() const {
    return {hasStart, start.clone(), hasEnd, end.clone(), endInclusive};
}

Ordering HIRTypePatternRange::ord(const HIRTypePatternRange& x) const {
    ORD(hasStart, x.hasStart);
    if (hasStart) {
        ORD(start, x.start);
    }
    ORD(hasEnd, x.hasEnd);
    if (hasEnd) {
        ORD(end, x.end);
    }
    ORD(endInclusive, x.endInclusive);
    return OrdEqual;
}

void HIRTypePatternRange::fmt(std::ostream& os) const {
    if (hasStart) {
        os << start;
    }
    os << (endInclusive ? "..=" : "..");
    if (hasEnd) {
        os << end;
    }
}

HIRTypePattern HIRTypePattern::clone() const {
    HIRTypePattern rv;
    rv.alternatives.reserve(alternatives.size());
    for (const auto& range : alternatives) {
        rv.alternatives.push_back(range.clone());
    }
    return rv;
}

Ordering HIRTypePattern::ord(const HIRTypePattern& x) const {
    ORD(alternatives.size(), x.alternatives.size());
    for (size_t i = 0; i < alternatives.size(); i++) {
        auto rv = alternatives[i].ord(x.alternatives[i]);
        if (rv != OrdEqual) {
            return rv;
        }
    }
    return OrdEqual;
}

void HIRTypePattern::fmt(std::ostream& os) const {
    for (size_t i = 0; i < alternatives.size(); i++) {
        if (i != 0) {
            os << " | ";
        }
        alternatives[i].fmt(os);
    }
}

void HIRGenericRef::fmt(std::ostream& os) const {
    os << this->name << "/*";
    if (this->isSolverExistential()) {
        os << "E:" << this->solverScope << ":" << this->idx();
    } else if (this->binding == GENERICSelf) {
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
    switch ((*this).tag()) {
        case HIRArraySize::TAG_Unevaluated: {
            auto& tse = (*this).as_Unevaluated();
            auto& xse = x.as_Unevaluated();
            return ::ord(tse, xse);
        }
        case HIRArraySize::TAG_Known: {
            auto& tse = (*this).as_Known();
            auto& xse = x.as_Known();
            return ::ord(tse, xse);
        }
    }
    UNREACHABLE();
}

HIRArraySize HIRArraySize::clone() const {
    switch ((*this).tag()) {
        case HIRArraySize::TAG_Unevaluated: {
            auto& se = (*this).as_Unevaluated();
            return se.clone();
        }
        case HIRArraySize::TAG_Known: {
            auto& se = (*this).as_Known();
            return se;
        }
    }
    UNREACHABLE();
}

HIRTypeDataErasedTypeAliasInner::HIRTypeDataErasedTypeAliasInner(const HIRItemPath& p, const HIRGenericParams& paramsOuter, const HIRGenericParams* paramsInner)
    : path(p.getSimplePath())
    , type()
{
    this->generics = paramsOuter.clone();
    this->generics.bounds.clear();
    if (!paramsInner) {
        return;
    }

    this->generics.types.reserve(this->generics.types.size() + paramsInner->types.size());
    for (const auto& type : paramsInner->types) {
        this->generics.types.push_back(HIRTypeParamDef{type.name, type.defaultValue, type.isSized});
    }
    this->generics.values.reserve(this->generics.values.size() + paramsInner->values.size());
    for (const auto& value : paramsInner->values) {
        this->generics.values.push_back(HIRValueParamDef{value.name, value.type, value.defaultValue.clone()});
    }

    this->generics.paramKinds.clear();
    this->generics.paramKinds.grow(paramsOuter.paramCount() + paramsInner->paramCount());
    for (size_t i = 0; i < paramsOuter.paramCount(); i++) {
        this->generics.paramKinds.pushBack(paramsOuter.paramKindAt(i));
    }
    for (size_t i = 0; i < paramsInner->paramCount(); i++) {
        this->generics.paramKinds.pushBack(paramsInner->paramKindAt(i));
    }
}

bool HIRTypeDataErasedTypeAliasInner::isLocalTo(const HIRSimplePath& p) const {
    const auto components = path.components();
    bool local = false;
    for (size_t i = 0; i + 1 < components.size(); i++) {
        if (components[i].c_str()[0] == '#') {
            local = true;
            break;
        }
    }
    return local && p.startsWith(path, /*skip_last=*/true);
}

HIRTypeDataFunctionPointer HIRTypeData::Data_NamedFunction::decay(HIRTypeInterner& types, const Span& sp) const {
    const HIRTypeData* tySelf = nullptr;
    const HIRPathParams* ppImpl = nullptr;
    const HIRPathParams* ppMethod = nullptr;

    switch (this->def.tag()) {
        case HIRTypeDataNamedFunctionTy::TAG_Function: {
            auto& fp = this->def.as_Function();
            ASSERT_BUG(sp, fp, "Non-initialised NamedFunction definition: " << this->path);
            switch (this->path.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    auto& pe = this->path.data.as_Generic();
                    ppMethod = &pe.params;
                    break;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    auto& pe = this->path.data.as_UfcsKnown();
                    tySelf = pe.type;
                    ppImpl = &pe.trait.params;
                    ppMethod = &pe.params;
                    break;
                }
                case HIRPathData::TAG_UfcsInherent: {
                    auto& pe = this->path.data.as_UfcsInherent();
                    tySelf = pe.type;
                    ppImpl = &pe.implParams;
                    ppMethod = &pe.params;
                    break;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    BUG(sp, "UfcsUnknown seen");
                    break;
                }
            }
            MonomorphStatePtr ms{types, tySelf, ppImpl, ppMethod};
            const auto& f = *fp;
            HIRTypeDataFunctionPointer ft{f.unsafe, f.variadic, f.abi, ms.monomorphType(sp, f.returnType), {}};
            for (size_t i = 0; i < f.fixedArgCount(); i++) {
                ft.argTypes.push_back(ms.monomorphType(sp, f.args[i].second));
            }
            return mv$(ft);
        }
        case HIRTypeDataNamedFunctionTy::TAG_EnumConstructor: {
            auto& ec = this->def.as_EnumConstructor();
            const auto& e = this->path.data.as_Generic();
            MonomorphStatePtr ms{types, nullptr, &e.params, nullptr};
            auto enumPath = e.path.parent();
            const auto& enm = *ec.e;
            ASSERT_BUG(sp, enm.data.is_Data(), "Enum " << enumPath << " isn't a data-holding enum");
            const auto& varTy = enm.data.as_Data()[ec.v].type;
            const auto& str = *varTy->as_Path().binding.as_Struct();
            const auto& varData = str.data.as_Tuple();

            HIRTypeDataFunctionPointer ft{false, false, RcString::newInterned(ABI_RUST), types.path(HIRPath(HIRGenericPath(mv$(enumPath), e.params.clone())), HIRTypePathBinding::make_Enum(&enm)), {}};
            for (const auto& arg : varData) {
                ft.argTypes.push_back(ms.monomorphType(sp, arg.ent));
            }
            return ft;
        }
        case HIRTypeDataNamedFunctionTy::TAG_StructConstructor: {
            auto& p = this->def.as_StructConstructor();
            const auto& e = this->path.data.as_Generic();
            MonomorphStatePtr ms{types, nullptr, &e.params, nullptr};
            HIRTypeDataFunctionPointer ft{false, false, RcString::newInterned(ABI_RUST), types.path(this->path.clone(), HIRTypePathBinding::make_Struct(p)), {}};
            for (const auto& arg : p->data.as_Tuple()) {
                ft.argTypes.push_back(ms.monomorphType(sp, arg.ent));
            }
            return ft;
        }
    }
    BUG(sp, "Unreachable code?");
}

void HIRTypeData::fmt(std::ostream& os) const {
    auto* context = TypeFmtStream::from(os);
    if (!context) {
        TypeFmtStream fmtStream(os);
        fmt(fmtStream);
        return;
    }

    for (auto* node = context->recurseStack; node; node = node->next) {
        if (node->type == this) {
            os << "RECURSE";
            return;
        }
    }

    const TypeFmtRecursionNode recursionNode{this, context->recurseStack};
    context->recurseStack = &recursionNode;
    STD_DEFER {
        context->recurseStack = recursionNode.next;
    };

    switch ((*this).tag()) {
        case HIRTypeData::TAG_Infer: {
            auto& e = (*this).as_Infer();
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
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            os << "!";
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& e = (*this).as_Primitive();
            os << e;
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*this).as_Path();
            os << e.path;
            switch (e.binding.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    os << "/*?*/";
                    break;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    os << "/*O*/";
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    os << "/*X*/";
                    break;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    os << "/*S*/";
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    os << "/*U*/";
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    os << "/*E*/";
                    break;
                }
            }
            break;
        }
        case HIRTypeData::TAG_Generic: {
            auto& e = (*this).as_Generic();
            os << e;
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = (*this).as_TraitObject();
            os << "dyn (";
            if (e.trait.path != HIRGenericPath()) {
                os << e.trait;
            }
            for (const auto& tr : e.markers) {
                os << "+" << tr;
            }
            os << ")";
            if (e.lifetimeIdentity != "") {
                os << "/*regions:" << e.lifetimeIdentity << "*/";
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = (*this).as_ErasedType();
            os << "impl ";
            for (const auto& tr : e.traits) {
                if (&tr != &e.traits[0]) {
                    os << "+";
                }
                os << tr;
            }
            os << "+use" << e.use;
            os << "/*";
            switch (e.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& ee = e.inner.as_Known();
                    os << "= " << ee;
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& ee = e.inner.as_Fcn();
                    os << "fn " << ee.origin << "#" << ee.index;
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    auto& ee = e.inner.as_Alias();
                    os << "type" << ee.params << " " << ee.inner->path;
                    break;
                }
            }
            os << "*/";
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*this).as_Array();
            os << "[" << e.inner << "; " << e.size << "]";
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = (*this).as_Slice();
            os << "[" << e.inner << "]";
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*this).as_Pattern();
            os << e.inner << " is ";
            e.pattern.fmt(os);
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*this).as_Tuple();
            os << "(";
            for (const auto& t : e) {
                os << t << ", ";
            }
            os << ")";
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*this).as_Borrow();
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
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = (*this).as_Pointer();
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
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& e = (*this).as_NamedFunction();
            os << "fn{" << (e.def.is_Function() && !e.def.as_Function() ? "!" : "") << e.path << "}";
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = (*this).as_Function();
            if (e.trackCaller) {
                os << "#[track_caller] ";
            }
            if (e.isUnsafe) {
                os << "unsafe ";
            }
            if (e.abi != "") {
                os << "extern \"" << e.abi << "\" ";
            }
            os << "fn(";
            for (const auto& t : e.argTypes) {
                os << t << ", ";
            }
            if (e.isVariadic) {
                os << "...";
            }
            os << ") -> " << e.rettype;
            if (e.lifetimeIdentity != "") {
                os << "/*regions:" << e.lifetimeIdentity << "*/";
            }
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            auto& e = (*this).as_NodeType();
            e.fmt(os);
            break;
        }
    }
}

bool HIRTypeDataNodeType::operator==(const HIRTypeDataNodeType& x) const {
    return this->ord(x) == OrdEqual;
}

Ordering HIRTypeDataNodeType::ord(const HIRTypeDataNodeType& x) const {
    ORD(static_cast<int>(this->tag()), static_cast<int>(x.tag()));
    switch ((*this).tag()) {
        case HIRTypeDataNodeType::TAG_Closure: {
            auto& te = (*this).as_Closure();
            auto& xe = x.as_Closure();
            ORD(reinterpret_cast<uintptr_t>(te), reinterpret_cast<uintptr_t>(xe));
            break;
        }
        case HIRTypeDataNodeType::TAG_Generator: {
            auto& te = (*this).as_Generator();
            auto& xe = x.as_Generator();
            ORD(reinterpret_cast<uintptr_t>(te), reinterpret_cast<uintptr_t>(xe));
            break;
        }
        case HIRTypeDataNodeType::TAG_Async: {
            auto& te = (*this).as_Async();
            auto& xe = x.as_Async();
            ORD(reinterpret_cast<uintptr_t>(te), reinterpret_cast<uintptr_t>(xe));
            break;
        }
    }
    return OrdEqual;
}

void HIRTypeDataNodeType::fmt(std::ostream& os) const {
    switch ((*this).tag()) {
        case HIRTypeDataNodeType::TAG_Closure: {
            auto& e = (*this).as_Closure();
            os << "closure[" << e << "]";
            break;
        }
        case HIRTypeDataNodeType::TAG_Generator: {
            auto& e = (*this).as_Generator();
            os << "generator[" << e << "]";
            break;
        }
        case HIRTypeDataNodeType::TAG_Async: {
            auto& e = (*this).as_Async();
            os << "async[" << e << "]";
            break;
        }
    }
}

HIRTypeDataNodeType HIRTypeDataNodeType::clone() const {
    switch ((*this).tag()) {
        case HIRTypeDataNodeType::TAG_Closure: {
            auto& e = (*this).as_Closure();
            return e;
        }
        case HIRTypeDataNodeType::TAG_Generator: {
            auto& e = (*this).as_Generator();
            return e;
        }
        case HIRTypeDataNodeType::TAG_Async: {
            auto& e = (*this).as_Async();
            return e;
        }
    }
    UNREACHABLE();
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
    auto* node = pool.make<HIRTypeData>(mv$(data));
    node->uid = ++id;
    nodes.emplace(hash, node);
    return node;
}

HIRTypeRef HIRTypeInterner::infer(unsigned int idx, HIRInferClass tyClass) {
    return intern(HIRTypeData::make_Infer({idx, tyClass}));
}

unsigned HIRTypeInterner::newAliasInputInfer() {
    return ~++id;
}

HIRTypeRef HIRTypeInterner::primitive(HIRCoreType ct) {
    return intern(HIRTypeData::make_Primitive(ct));
}

HIRTypeRef HIRTypeInterner::generic(HIRGenericRef generic) {
    return intern(HIRTypeData::make_Generic(mv$(generic)));
}

HIRTypeRef HIRTypeInterner::generic(RcString name, unsigned int slot) {
    return generic(HIRGenericRef(mv$(name), slot));
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

HIRTypeRef HIRTypeInterner::tuple(std::vector<HIRTypeRef> types) {
    return intern(HIRTypeData::make_Tuple(mv$(types)));
}

HIRTypeRef HIRTypeInterner::slice(HIRTypeRef inner) {
    return intern(HIRTypeData::make_Slice({inner}));
}

HIRTypeRef HIRTypeInterner::array(HIRTypeRef inner, HIRArraySize size) {
    return intern(HIRTypeData::make_Array({inner, mv$(size)}));
}

HIRTypeRef HIRTypeInterner::array(HIRTypeRef inner, u64 size) {
    BUG_ASSERT(size != ~0u);
    return intern(HIRTypeData::make_Array({inner, size}));
}

HIRTypeRef HIRTypeInterner::array(HIRTypeRef inner, HIRConstGeneric size) {
    return intern(HIRTypeData::make_Array({inner, mv$(size)}));
}

HIRTypeRef HIRTypeInterner::path(HIRPath path, HIRTypePathBinding binding) {
    return intern(HIRTypeData::make_Path({mv$(path), mv$(binding)}));
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
    if (((*this).is_Path() && ((*this).as_Path().path.data.is_Generic()))) {
        return &as_Path().path.data.as_Generic().path;
    }
    if (is_TraitObject()) {
        return &as_TraitObject().trait.path.path;
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

    switch ((*this).tag()) {
        case HIRTypeData::TAG_Infer: {
            auto& te = (*this).as_Infer();
            auto& xe = (*x).as_Infer();
            // TODO: Should comparing inferrence vars be an error?
            return te.index == xe.index;
        }
        case HIRTypeData::TAG_Diverge: {
            return true;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& te = (*this).as_Primitive();
            auto& xe = (*x).as_Primitive();
            return te == xe;
        }
        case HIRTypeData::TAG_Path: {
            auto& te = (*this).as_Path();
            auto& xe = (*x).as_Path();
            return te.path.equalsIgnoringRegions(xe.path);
        }
        case HIRTypeData::TAG_Generic: {
            auto& te = (*this).as_Generic();
            auto& xe = (*x).as_Generic();
            return /*te.name == xe.name &&*/ te.binding == xe.binding;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& te = (*this).as_TraitObject();
            auto& xe = (*x).as_TraitObject();
            if (!te.trait.equalsIgnoringRegions(xe.trait)) {
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
        case HIRTypeData::TAG_ErasedType: {
            auto& te = (*this).as_ErasedType();
            auto& xe = (*x).as_ErasedType();
            return ord(te.inner, xe.inner) == OrdEqual;
        }
        case HIRTypeData::TAG_Array: {
            auto& te = (*this).as_Array();
            auto& xe = (*x).as_Array();
            if (!te.inner->equalsIgnoringRegions(xe.inner)) {
                return false;
            }
            if (xe.size != te.size) {
                return false;
            }
            return true;
        }
        case HIRTypeData::TAG_Slice: {
            auto& te = (*this).as_Slice();
            auto& xe = (*x).as_Slice();
            return te.inner->equalsIgnoringRegions(xe.inner);
        }
        case HIRTypeData::TAG_Pattern: {
            auto& te = (*this).as_Pattern();
            auto& xe = (*x).as_Pattern();
            return te.inner->equalsIgnoringRegions(xe.inner) && te.pattern.ord(xe.pattern) == OrdEqual;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& te = (*this).as_Tuple();
            auto& xe = (*x).as_Tuple();
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
        case HIRTypeData::TAG_Borrow: {
            auto& te = (*this).as_Borrow();
            auto& xe = (*x).as_Borrow();
            if (te.type != xe.type) {
                return false;
            }
            return te.inner->equalsIgnoringRegions(xe.inner);
        }
        case HIRTypeData::TAG_Pointer: {
            auto& te = (*this).as_Pointer();
            auto& xe = (*x).as_Pointer();
            if (te.type != xe.type) {
                return false;
            }
            return te.inner->equalsIgnoringRegions(xe.inner);
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& te = (*this).as_NamedFunction();
            auto& xe = (*x).as_NamedFunction();
            return te.path.equalsIgnoringRegions(xe.path);
        }
        case HIRTypeData::TAG_Function: {
            auto& te = (*this).as_Function();
            auto& xe = (*x).as_Function();
            if (te.isUnsafe != xe.isUnsafe) {
                return false;
            }
            if (te.abi != xe.abi) {
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
            return te.rettype->equalsIgnoringRegions(xe.rettype);
        }
        case HIRTypeData::TAG_NodeType: {
            auto& te = (*this).as_NodeType();
            auto& xe = (*x).as_NodeType();
            return te == xe;
        }
    }
    UNREACHABLE();
}

Ordering ord(const TypeDataErasedTypeInner& l, const TypeDataErasedTypeInner& r) {
    ORD(static_cast<unsigned int>(l.tag()), static_cast<unsigned int>(r.tag()));
    switch (l.tag()) {
        case TypeDataErasedTypeInner::TAG_Known: {
            auto& le = l.as_Known();
            auto& re = r.as_Known();
            return le->ordIgnoringRegions(re);
        }
        case TypeDataErasedTypeInner::TAG_Alias: {
            auto& le = l.as_Alias();
            auto& re = r.as_Alias();
            ORD(le.inner->path, re.inner->path);
            ORD(le.params, re.params);
            break;
        }
        case TypeDataErasedTypeInner::TAG_Fcn: {
            auto& le = l.as_Fcn();
            auto& re = r.as_Fcn();
            ORD(le.origin, re.origin);
            ORD(le.index, re.index);
            break;
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

    switch ((*this).tag()) {
        case HIRTypeData::TAG_Infer: {
            auto& te = (*this).as_Infer();
            auto& xe = (*x).as_Infer();
            // TODO: Should comparing inferrence vars be an error?
            return ::ord(te.index, xe.index);
        }
        case HIRTypeData::TAG_Diverge: {
            return OrdEqual;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& te = (*this).as_Primitive();
            auto& xe = (*x).as_Primitive();
            return ::ord(static_cast<unsigned>(te), static_cast<unsigned>(xe));
        }
        case HIRTypeData::TAG_Path: {
            auto& te = (*this).as_Path();
            auto& xe = (*x).as_Path();
            return ::ord(te.path, xe.path);
        }
        case HIRTypeData::TAG_Generic: {
            auto& te = (*this).as_Generic();
            auto& xe = (*x).as_Generic();
            if ((rv = ::ord(te.binding, xe.binding)) != OrdEqual) {
                return rv;
            }
            return OrdEqual;
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& te = (*this).as_TraitObject();
            auto& xe = (*x).as_TraitObject();
            ORD(te.trait, xe.trait);
            ORD(te.markers, xe.markers);
            return OrdEqual;
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& te = (*this).as_ErasedType();
            auto& xe = (*x).as_ErasedType();
            ORD(te.inner, xe.inner);
            return OrdEqual;
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& te = (*this).as_Array();
            auto& xe = (*x).as_Array();
            ORD(te.inner, xe.inner);
            ORD(te.size, xe.size);
            return OrdEqual;
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& te = (*this).as_Slice();
            auto& xe = (*x).as_Slice();
            return ::ord(te.inner, xe.inner);
        }
        case HIRTypeData::TAG_Pattern: {
            auto& te = (*this).as_Pattern();
            auto& xe = (*x).as_Pattern();
            ORD(te.inner, xe.inner);
            return te.pattern.ord(xe.pattern);
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& te = (*this).as_Tuple();
            auto& xe = (*x).as_Tuple();
            return ::ord(te, xe);
        }
        case HIRTypeData::TAG_Borrow: {
            auto& te = (*this).as_Borrow();
            auto& xe = (*x).as_Borrow();
            ORD(static_cast<unsigned>(te.type), static_cast<unsigned>(xe.type));
            return ::ord(te.inner, xe.inner);
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& te = (*this).as_Pointer();
            auto& xe = (*x).as_Pointer();
            ORD(static_cast<unsigned>(te.type), static_cast<unsigned>(xe.type));
            return ::ord(te.inner, xe.inner);
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& te = (*this).as_NamedFunction();
            auto& xe = (*x).as_NamedFunction();
            return ::ord(te.path, xe.path);
        }
        case HIRTypeData::TAG_Function: {
            auto& te = (*this).as_Function();
            auto& xe = (*x).as_Function();
            ORD(te.isUnsafe, xe.isUnsafe);
            ORD(te.isVariadic, xe.isVariadic);
            ORD(te.trackCaller, xe.trackCaller);
            ORD(te.abi, xe.abi);
            ORD(te.argTypes, xe.argTypes);
            return ::ord(te.rettype, xe.rettype);
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            auto& te = (*this).as_NodeType();
            auto& xe = (*x).as_NodeType();
            return te.ord(xe);
        }
    }
    UNREACHABLE();
}

bool HIRTypeData::matchTestGenerics(const Span& sp, HIRTypeRef xIn, tCbResolveType resolvePlaceholder, HIRMatchGenerics& callback) const {
    return this->matchTestGenericsFuzz(sp, xIn, resolvePlaceholder, callback) == HIRCompare::Equal;
}

HIRCompare HIRTypeData::matchTestGenericsFuzz(const Span& sp, HIRTypeRef xIn, tCbResolveType resolvePlaceholder, HIRMatchGenerics& callback) const {
    const HIRTypeRef self = this;
    return callback.cmpType(sp, self, xIn, resolvePlaceholder);
}

HIRCompare HIRMatchGenerics::cmpPath(const Span& sp, const HIRPath& pathL, const HIRPath& pathR, tCbResolveType resolvePlaceholder) {
    HIRCompare rv = HIRCompare::Unequal;
    if (pathL.data.tag() != pathR.data.tag()) {
        rv = HIRCompare::Unequal;
    } else {
        switch (pathL.data.tag()) {
            case HIRPathData::TAG_Generic: {
                auto& tpe = pathL.data.as_Generic();
                auto& xpe = pathR.data.as_Generic();
                if (tpe.path != xpe.path) {
                    rv = HIRCompare::Unequal;
                } else {
                    rv = matchGenericsPp(sp, tpe.params, xpe.params, resolvePlaceholder, *this);
                }
                break;
            }
            case HIRPathData::TAG_UfcsKnown: {
                auto& tpe = pathL.data.as_UfcsKnown();
                auto& xpe = pathR.data.as_UfcsKnown();
                rv = this->cmpType(sp, tpe.type, xpe.type, resolvePlaceholder);
                if (tpe.trait.path != xpe.trait.path) {
                    rv = HIRCompare::Unequal;
                }
                rv &= matchGenericsPp(sp, tpe.trait.params, xpe.trait.params, resolvePlaceholder, *this);
                if (tpe.item != xpe.item) {
                    rv = HIRCompare::Unequal;
                }
                rv &= matchGenericsPp(sp, tpe.params, xpe.params, resolvePlaceholder, *this);
                break;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                auto& tpe = pathL.data.as_UfcsUnknown();
                auto& xpe = pathR.data.as_UfcsUnknown();
                rv = this->cmpType(sp, tpe.type, xpe.type, resolvePlaceholder);
                if (tpe.item != xpe.item) {
                    rv = HIRCompare::Unequal;
                }
                rv &= matchGenericsPp(sp, tpe.params, xpe.params, resolvePlaceholder, *this);
                break;
            }
            case HIRPathData::TAG_UfcsInherent: {
                auto& tpe = pathL.data.as_UfcsInherent();
                auto& xpe = pathR.data.as_UfcsInherent();
                rv = this->cmpType(sp, tpe.type, xpe.type, resolvePlaceholder);
                if (tpe.item != xpe.item) {
                    rv = HIRCompare::Unequal;
                }
                rv &= matchGenericsPp(sp, tpe.params, xpe.params, resolvePlaceholder, *this);
                break;
            }
        }
    }
    return rv;
}

HIRCompare HIRMatchGenerics::cmpType(const Span& sp, const HIRTypeData* tyL, const HIRTypeData* tyR, tCbResolveType resolvePlaceholder) {
    if (const auto* e = tyL->opt_Generic()) {
        return this->matchTy(*e, tyR, resolvePlaceholder);
    }
    const auto& v = (tyL->is_Infer() ? resolvePlaceholder.getType(sp, tyL) : tyL);
    const auto& x = (tyR->is_Infer() || tyR->is_Generic() ? resolvePlaceholder.getType(sp, tyR) : tyR);
    if (const auto* e = v->opt_Generic()) {
        return this->matchTy(*e, x, resolvePlaceholder);
    }
    if (const auto* xep = x->opt_Infer()) {
        const auto& xe = *xep;
        if (xe.index != ~0u && v->is_Infer() && v->as_Infer().index == xe.index) {
            return HIRCompare::Equal;
        }
        switch (xe.tyClass) {
            case HIRInferClass::None:
                // TODO: Have another callback (optional?) that allows the caller to equate `v` somehow

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
                            return HIRCompare::Unequal;
                    }
                }
                break;
        }
    }

    const auto erasedAlias = [](const HIRTypeData* ty) {
        const auto* erased = ty->opt_ErasedType();
        return erased ? erased->inner.opt_Alias() : nullptr;
    };
    const auto* vAlias = erasedAlias(v);
    const auto* xAlias = erasedAlias(x);
    if (vAlias && xAlias) {
        if (vAlias->inner->path != xAlias->inner->path) {
            return HIRCompare::Unequal;
        }
        return vAlias->params.matchTestGenericsFuzz(sp, xAlias->params, resolvePlaceholder, *this);
    }
    if (vAlias || xAlias) {
        return HIRCompare::Fuzzy;
    }

    if (v->tag() != x->tag()) {
        // HACK: If the path is Opaque, return a fuzzy match.
        // - This works around an impl selection bug.
        if (v->is_Path() && v->as_Path().binding.is_Opaque()) {
            return HIRCompare::Fuzzy;
        }
        // HACK: If RHS is unbound, fuzz it
        if (x->is_Path() && x->as_Path().binding.is_Unbound()) {
            return HIRCompare::Fuzzy;
        }
        if (v->is_Path() && v->as_Path().binding.is_Unbound()) {
            return HIRCompare::Fuzzy;
        }
        // HACK: If the RHS is a placeholder generic, allow it.
        if (x->is_Generic() && (x->as_Generic().binding >> 8) == 2) {
            return HIRCompare::Fuzzy;
        }
        return HIRCompare::Unequal;
    }
    switch ((*v).tag()) {
        case HIRTypeData::TAG_Infer: {
            auto& te = (*v).as_Infer();
            auto& xe = (*x).as_Infer();
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
            break;
        }
        case HIRTypeData::TAG_Generic: {
            UNREACHABLE();
        }
        case HIRTypeData::TAG_Primitive: {
            auto& te = (*v).as_Primitive();
            auto& xe = (*x).as_Primitive();
            return (te == xe ? HIRCompare::Equal : HIRCompare::Unequal);
        }
        case HIRTypeData::TAG_Diverge: {
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_Path: {
            auto& te = (*v).as_Path();
            auto& xe = (*x).as_Path();
            auto rv = this->cmpPath(sp, te.path, xe.path, resolvePlaceholder);

            if (rv == HIRCompare::Unequal) {
                if (te.binding.is_Unbound() || xe.binding.is_Unbound()) {
                    rv = HIRCompare::Fuzzy;
                }
                if (te.binding.is_Opaque()) {
                    return HIRCompare::Fuzzy;
                }
            }
            return rv;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& te = (*v).as_TraitObject();
            auto& xe = (*x).as_TraitObject();
            if (te.trait.path.path != xe.trait.path.path) {
                return HIRCompare::Unequal;
            }
            if (te.markers.size() != xe.markers.size()) {
                return HIRCompare::Unequal;
            }
            auto cmp = matchGenericsPp(sp, te.trait.path.params, xe.trait.path.params, resolvePlaceholder, *this);
            for (unsigned int i = 0; i < te.markers.size(); i++) {
                if (te.markers[i].path != xe.markers[i].path) {
                    return HIRCompare::Unequal;
                }
                cmp &= matchGenericsPp(sp, te.markers[i].params, xe.markers[i].params, resolvePlaceholder, *this);
            }

            auto itL = te.trait.typeBounds.begin();
            auto itR = xe.trait.typeBounds.begin();
            while (itL != te.trait.typeBounds.end() && itR != xe.trait.typeBounds.end()) {
                if (itL->first != itR->first) {
                    return HIRCompare::Unequal;
                }
                cmp &= itL->second.type->matchTestGenericsFuzz(sp, itR->second.type, resolvePlaceholder, *this);
                ++itL;
                ++itR;
            }

            if (itL != te.trait.typeBounds.end() || itR != xe.trait.typeBounds.end()) {
                return HIRCompare::Unequal;
            }

            return cmp;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& te = (*v).as_ErasedType();
            auto& xe = (*x).as_ErasedType();
            if (te.inner.tag() != xe.inner.tag()) {
                return HIRCompare::Unequal;
            }
            switch (te.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& l = te.inner.as_Known();
                    auto& r = xe.inner.as_Known();
                    return l->matchTestGenericsFuzz(sp, r, resolvePlaceholder, *this);
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    auto& l = te.inner.as_Alias();
                    auto& r = xe.inner.as_Alias();
                    return l.inner->path == r.inner->path ? l.params.matchTestGenericsFuzz(sp, r.params, resolvePlaceholder, *this) : HIRCompare::Unequal;
                }
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& l = te.inner.as_Fcn();
                    auto& r = xe.inner.as_Fcn();
                    return l.index == r.index ? this->cmpPath(sp, l.origin, r.origin, resolvePlaceholder) : HIRCompare::Unequal;
                }
            }
            UNREACHABLE();
        }
        case HIRTypeData::TAG_Array: {
            auto& te = (*v).as_Array();
            auto& xe = (*x).as_Array();
            HIRConstGeneric teKnown;
            HIRConstGeneric xeKnown;
            EncodedLiteral teLiteral;
            EncodedLiteral xeLiteral;
            const auto& teValue = te.size.is_Known() ? (teLiteral = EncodedLiteral::makeUsize(te.size.as_Known()), teKnown = retainedValuePool ? freezeEncodedLiteral(*retainedValuePool, mv$(teLiteral)) : &teLiteral) : resolvePlaceholder.getVal(sp, te.size.as_Unevaluated());
            const auto& xeValue = xe.size.is_Known() ? (xeLiteral = EncodedLiteral::makeUsize(xe.size.as_Known()), xeKnown = retainedValuePool ? freezeEncodedLiteral(*retainedValuePool, mv$(xeLiteral)) : &xeLiteral) : resolvePlaceholder.getVal(sp, xe.size.as_Unevaluated());
            auto rv = matchValues(sp, teValue, xeValue, *this);
            rv &= this->cmpType(sp, te.inner, xe.inner, resolvePlaceholder);
            return rv;
        }
        case HIRTypeData::TAG_Slice: {
            auto& te = (*v).as_Slice();
            auto& xe = (*x).as_Slice();
            return this->cmpType(sp, te.inner, xe.inner, resolvePlaceholder);
        }
        case HIRTypeData::TAG_Pattern: {
            auto& te = (*v).as_Pattern();
            auto& xe = (*x).as_Pattern();
            if (te.pattern.alternatives.size() != xe.pattern.alternatives.size()) {
                return HIRCompare::Unequal;
            }
            auto rv = this->cmpType(sp, te.inner, xe.inner, resolvePlaceholder);
            for (size_t i = 0; i < te.pattern.alternatives.size(); i++) {
                const auto& left = te.pattern.alternatives[i];
                const auto& right = xe.pattern.alternatives[i];
                if (left.hasStart != right.hasStart || left.hasEnd != right.hasEnd || left.endInclusive != right.endInclusive) {
                    return HIRCompare::Unequal;
                }
                if (left.hasStart) {
                    rv &= matchValues(sp, left.start, right.start, *this);
                }
                if (left.hasEnd) {
                    rv &= matchValues(sp, left.end, right.end, *this);
                }
                if (rv == HIRCompare::Unequal) {
                    return rv;
                }
            }
            return rv;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& te = (*v).as_Tuple();
            auto& xe = (*x).as_Tuple();
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
        case HIRTypeData::TAG_Pointer: {
            auto& te = (*v).as_Pointer();
            auto& xe = (*x).as_Pointer();
            if (te.type != xe.type) {
                return HIRCompare::Unequal;
            }
            return this->cmpType(sp, te.inner, xe.inner, resolvePlaceholder);
        }
        case HIRTypeData::TAG_Borrow: {
            auto& te = (*v).as_Borrow();
            auto& xe = (*x).as_Borrow();
            if (te.type != xe.type) {
                return HIRCompare::Unequal;
            }
            auto rv = HIRCompare::Equal;
            rv &= this->cmpType(sp, te.inner, xe.inner, resolvePlaceholder);
            return rv;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& te = (*v).as_NamedFunction();
            auto& xe = (*x).as_NamedFunction();
            return this->cmpPath(sp, te.path, xe.path, resolvePlaceholder);
        }
        case HIRTypeData::TAG_Function: {
            auto& te = (*v).as_Function();
            auto& xe = (*x).as_Function();
            if (te.isUnsafe != xe.isUnsafe) {
                return HIRCompare::Unequal;
            }
            if (te.abi != xe.abi) {
                return HIRCompare::Unequal;
            }
            if (te.argTypes.size() != xe.argTypes.size()) {
                return HIRCompare::Unequal;
            }
            auto rv = HIRCompare::Equal;
            for (unsigned int i = 0; i < te.argTypes.size(); i++) {
                rv &= this->cmpType(sp, te.argTypes[i], xe.argTypes[i], resolvePlaceholder);
                if (rv == HIRCompare::Unequal) {
                    return rv;
                }
            }
            rv &= this->cmpType(sp, te.rettype, xe.rettype, resolvePlaceholder);
            return rv;
        }
        case HIRTypeData::TAG_NodeType: {
            auto& te = (*v).as_NodeType();
            auto& xe = (*x).as_NodeType();
            return te == xe ? HIRCompare::Equal : HIRCompare::Unequal;
        }
    }
    UNREACHABLE();
}

HIRTypePathBinding HIRTypePathBinding::clone() const {
    switch ((*this).tag()) {
        case HIRTypePathBinding::TAG_Unbound: {
            return HIRTypePathBinding::make_Unbound({});
        }
        case HIRTypePathBinding::TAG_Opaque: {
            return HIRTypePathBinding::make_Opaque({});
        }
        case HIRTypePathBinding::TAG_ExternType: {
            auto& e = (*this).as_ExternType();
            return HIRTypePathBinding(e);
        }
        case HIRTypePathBinding::TAG_Struct: {
            auto& e = (*this).as_Struct();
            return HIRTypePathBinding(e);
        }
        case HIRTypePathBinding::TAG_Union: {
            auto& e = (*this).as_Union();
            return HIRTypePathBinding(e);
        }
        case HIRTypePathBinding::TAG_Enum: {
            auto& e = (*this).as_Enum();
            return HIRTypePathBinding(e);
        }
    }
    BUG_ASSERT(!"Fell off end of clone_binding");
    UNREACHABLE();
}

bool HIRTypePathBinding::operator==(const HIRTypePathBinding& x) const {
    if (this->tag() != x.tag()) {
        return false;
    }
    switch ((*this).tag()) {
        case HIRTypePathBinding::TAG_Unbound: {
            return true;
        }
        case HIRTypePathBinding::TAG_Opaque: {
            return true;
        }
        case HIRTypePathBinding::TAG_ExternType: {
            auto& te = (*this).as_ExternType();
            auto& xe = x.as_ExternType();
            return te == xe;
        }
        case HIRTypePathBinding::TAG_Struct: {
            auto& te = (*this).as_Struct();
            auto& xe = x.as_Struct();
            return te == xe;
        }
        case HIRTypePathBinding::TAG_Union: {
            auto& te = (*this).as_Union();
            auto& xe = x.as_Union();
            return te == xe;
        }
        case HIRTypePathBinding::TAG_Enum: {
            auto& te = (*this).as_Enum();
            auto& xe = x.as_Enum();
            return te == xe;
        }
    }
    UNREACHABLE();
}

const HIRTraitMarkings* HIRTypePathBinding::getTraitMarkings() const {
    const HIRTraitMarkings* markingsPtr = nullptr;
    switch ((*this).tag()) {
        case HIRTypePathBinding::TAG_Unbound: {
            break;
        }
        case HIRTypePathBinding::TAG_Opaque: {
            break;
        }
        case HIRTypePathBinding::TAG_ExternType: {
            auto& tpb = (*this).as_ExternType();
            if (tpb) {
                markingsPtr = &tpb->markings;
            }
            break;
        }
        case HIRTypePathBinding::TAG_Struct: {
            auto& tpb = (*this).as_Struct();
            if (tpb) {
                markingsPtr = &tpb->markings;
            }
            break;
        }
        case HIRTypePathBinding::TAG_Union: {
            auto& tpb = (*this).as_Union();
            if (tpb) {
                markingsPtr = &tpb->markings;
            }
            break;
        }
        case HIRTypePathBinding::TAG_Enum: {
            auto& tpb = (*this).as_Enum();
            if (tpb) {
                markingsPtr = &tpb->markings;
            }
            break;
        }
    }
    return markingsPtr;
}

const HIRGenericParams* HIRTypePathBinding::getGenerics() const {
    const HIRGenericParams* rv = nullptr;
    switch ((*this).tag()) {
        case HIRTypePathBinding::TAG_Unbound: {
            break;
        }
        case HIRTypePathBinding::TAG_Opaque: {
            break;
        }
        case HIRTypePathBinding::TAG_ExternType: {
            break;
        }
        case HIRTypePathBinding::TAG_Struct: {
            auto& tpb = (*this).as_Struct();
            if (tpb) {
                rv = &tpb->params;
            }
            break;
        }
        case HIRTypePathBinding::TAG_Union: {
            auto& tpb = (*this).as_Union();
            if (tpb) {
                rv = &tpb->params;
            }
            break;
        }
        case HIRTypePathBinding::TAG_Enum: {
            auto& tpb = (*this).as_Enum();
            if (tpb) {
                rv = &tpb->params;
            }
            break;
        }
    }
    return rv;
}

HIRTypeDataNamedFunctionTy HIRTypeDataNamedFunctionTy::clone() const {
    switch ((*this).tag()) {
        case HIRTypeDataNamedFunctionTy::TAG_Function: {
            auto& e = (*this).as_Function();
            return e;
        }
        case HIRTypeDataNamedFunctionTy::TAG_EnumConstructor: {
            auto& e = (*this).as_EnumConstructor();
            return e;
        }
        case HIRTypeDataNamedFunctionTy::TAG_StructConstructor: {
            auto& e = (*this).as_StructConstructor();
            return e;
        }
    }
    UNREACHABLE();
}

HIRTypeData HIRTypeData::cloneData() const {
    switch ((*this).tag()) {
        case HIRTypeData::TAG_Infer: {
            auto& e = (*this).as_Infer();
            return HIRTypeData::make_Infer(e);
        }
        case HIRTypeData::TAG_Diverge: {
            return HIRTypeData::make_Diverge({});
        }
        case HIRTypeData::TAG_Primitive: {
            auto& e = (*this).as_Primitive();
            return HIRTypeData::make_Primitive(e);
        }
        case HIRTypeData::TAG_Path: {
            auto& e = (*this).as_Path();
            return HIRTypeData::make_Path({e.path.clone(), e.binding.clone()});
        }
        case HIRTypeData::TAG_Generic: {
            auto& e = (*this).as_Generic();
            return HIRTypeData::make_Generic(e);
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = (*this).as_TraitObject();
            HIRTypeData::Data_TraitObject rv;
            rv.trait = e.trait.clone();
            rv.lifetimeIdentity = e.lifetimeIdentity;
            rv.lifetimeIdentityHasFree = e.lifetimeIdentityHasFree;
            for (const auto& trait : e.markers) {
                rv.markers.push_back(trait.clone());
            }
            return HIRTypeData::make_TraitObject(mv$(rv));
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = (*this).as_ErasedType();
            std::vector<HIRTraitPath> traits;
            traits.reserve(e.traits.size());
            for (const auto& trait : e.traits) {
                traits.push_back(trait.clone());
            }

            TypeDataErasedTypeInner inner;
            switch (e.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& ee = e.inner.as_Fcn();
                    inner = TypeDataErasedTypeInner::Data_Fcn{ee.origin.clone(), ee.index};
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& ee = e.inner.as_Known();
                    inner = ee;
                    break;
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    auto& ee = e.inner.as_Alias();
                    inner = TypeDataErasedTypeInner::Data_Alias{ee.params.clone(), ee.inner};
                    break;
                }
            }
            return HIRTypeData::make_ErasedType({e.isSized, mv$(traits), mv$(inner), e.use.clone(), e.usePresent});
        }
        case HIRTypeData::TAG_Array: {
            auto& e = (*this).as_Array();
            return HIRTypeData::make_Array({e.inner, e.size.clone()});
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = (*this).as_Slice();
            return HIRTypeData::make_Slice({e.inner});
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = (*this).as_Pattern();
            return HIRTypeData::make_Pattern({e.inner, e.pattern.clone()});
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = (*this).as_Tuple();
            std::vector<HIRTypeRef> types;
            for (const auto& t : e) {
                types.push_back(t);
            }
            return HIRTypeData::make_Tuple(mv$(types));
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = (*this).as_Borrow();
            return HIRTypeData::make_Borrow({e.type, e.inner});
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = (*this).as_Pointer();
            return HIRTypeData::make_Pointer({e.type, e.inner});
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& e = (*this).as_NamedFunction();
            return HIRTypeData::make_NamedFunction({e.path.clone(), e.def.clone()});
        }
        case HIRTypeData::TAG_Function: {
            auto& e = (*this).as_Function();
            HIRTypeDataFunctionPointer ft{e.isUnsafe, e.isVariadic, e.abi, e.rettype, {}, e.trackCaller};
            ft.lifetimeIdentity = e.lifetimeIdentity;
            ft.lifetimeIdentityHasFree = e.lifetimeIdentityHasFree;
            for (const auto& a : e.argTypes) {
                ft.argTypes.push_back(a);
            }
            return HIRTypeData::make_Function(mv$(ft));
        }
        case HIRTypeData::TAG_NodeType: {
            auto& e = (*this).as_NodeType();
            return HIRTypeData::make_NodeType(e.clone());
        }
    }
    UNREACHABLE();
}

HIRCompare HIRTypeData::compareWithPlaceholders(const Span& sp, HIRTypeRef x, tCbResolveType resolvePlaceholder) const {
    const HIRTypeRef self = this;
    const auto& left = resolvePlaceholder.getType(sp, self);
    const auto& right = resolvePlaceholder.getType(sp, x);

    if (left->is_Infer() && left == right) {
        return HIRCompare::Equal;
    }

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

    if (const auto* e = left->opt_Infer()) {
        switch (e->tyClass) {
            case HIRInferClass::None:
                return HIRCompare::Fuzzy;
            case HIRInferClass::Integer:
                switch ((*right).tag()) {
                    default:
                        return HIRCompare::Unequal;
                    case HIRTypeData::TAG_Primitive: {
                        auto& re = (*right).as_Primitive();
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
                        break;
                    }
                    case HIRTypeData::TAG_Infer: {
                        auto& re = (*right).as_Infer();
                        switch (re.tyClass) {
                            case HIRInferClass::None:
                            case HIRInferClass::Integer:
                                return HIRCompare::Fuzzy;
                            case HIRInferClass::Float:
                                return HIRCompare::Unequal;
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Path: {
                        auto& re = (*right).as_Path();
                        return re.binding.is_Unbound() ? HIRCompare::Fuzzy : HIRCompare::Unequal;
                    }
                }
            case HIRInferClass::Float:
                switch ((*right).tag()) {
                    default:
                        return HIRCompare::Unequal;
                    case HIRTypeData::TAG_Primitive: {
                        auto& re = (*right).as_Primitive();
                        switch (re) {
                            case HIRCoreType::F16:
                            case HIRCoreType::F32:
                            case HIRCoreType::F64:
                            case HIRCoreType::F128:
                                return HIRCompare::Fuzzy;
                            default:
                                return HIRCompare::Unequal;
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Infer: {
                        auto& re = (*right).as_Infer();
                        switch (re.tyClass) {
                            case HIRInferClass::None:
                            case HIRInferClass::Float:
                                return HIRCompare::Fuzzy;
                            case HIRInferClass::Integer:
                                return HIRCompare::Unequal;
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Path: {
                        auto& re = (*right).as_Path();
                        return re.binding.is_Unbound() ? HIRCompare::Fuzzy : HIRCompare::Unequal;
                    }
                }
        }
        UNREACHABLE();
    }

    if (const auto* re = right->opt_Infer()) {
        switch (re->tyClass) {
            case HIRInferClass::None:
                return HIRCompare::Fuzzy;
            case HIRInferClass::Integer:
                switch ((*left).tag()) {
                    default:
                        return HIRCompare::Unequal;
                    case HIRTypeData::TAG_Primitive: {
                        auto& le = (*left).as_Primitive();
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
                        break;
                    }
                    case HIRTypeData::TAG_Path: {
                        auto& le = (*left).as_Path();
                        return le.binding.is_Unbound() ? HIRCompare::Fuzzy : HIRCompare::Unequal;
                    }
                }
            case HIRInferClass::Float:
                switch ((*left).tag()) {
                    default:
                        return HIRCompare::Unequal;
                    case HIRTypeData::TAG_Primitive: {
                        auto& le = (*left).as_Primitive();
                        switch (le) {
                            case HIRCoreType::F16:
                            case HIRCoreType::F32:
                            case HIRCoreType::F64:
                            case HIRCoreType::F128:
                                return HIRCompare::Fuzzy;
                            default:
                                return HIRCompare::Unequal;
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Path: {
                        auto& le = (*left).as_Path();
                        return le.binding.is_Unbound() ? HIRCompare::Fuzzy : HIRCompare::Unequal;
                    }
                }
        }
        UNREACHABLE();
    }

    if (left->tag() != right->tag()) {
        return HIRCompare::Unequal;
    }
    switch ((*left).tag()) {
        case HIRTypeData::TAG_Infer: {
            BUG_ASSERT(!"infer");
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& le = (*left).as_Primitive();
            auto& re = (*right).as_Primitive();
            return (le == re ? HIRCompare::Equal : HIRCompare::Unequal);
        }
        case HIRTypeData::TAG_Path: {
            auto& le = (*left).as_Path();
            auto& re = (*right).as_Path();
            auto rv = le.path.compareWithPlaceholders(sp, re.path, resolvePlaceholder);
            if (rv == HIRCompare::Unequal) {
                if (le.binding.is_Unbound() || re.binding.is_Unbound()) {
                    rv = HIRCompare::Fuzzy;
                }
            }
            return rv;
        }
        case HIRTypeData::TAG_Generic: {
            auto& le = (*left).as_Generic();
            auto& re = (*right).as_Generic();
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
        case HIRTypeData::TAG_TraitObject: {
            auto& le = (*left).as_TraitObject();
            auto& re = (*right).as_TraitObject();
            if (le.markers.size() != re.markers.size()) {
                return HIRCompare::Unequal;
            }
            auto rv = le.trait.compareWithPlaceholders(sp, re.trait, resolvePlaceholder);
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
        case HIRTypeData::TAG_ErasedType: {
            auto& le = (*left).as_ErasedType();
            auto& re = (*right).as_ErasedType();
            if (le.inner.tag() != re.inner.tag()) {
                return HIRCompare::Unequal;
            }
            switch (le.inner.tag()) {
                case TypeDataErasedTypeInner::TAG_Known: {
                    auto& l = le.inner.as_Known();
                    auto& r = re.inner.as_Known();
                    return l->compareWithPlaceholders(sp, r, resolvePlaceholder);
                }
                case TypeDataErasedTypeInner::TAG_Alias: {
                    auto& l = le.inner.as_Alias();
                    auto& r = re.inner.as_Alias();
                    if (l.inner->path != r.inner->path) {
                        return HIRCompare::Unequal;
                    }
                    return l.params.compareWithPlaceholders(sp, r.params, resolvePlaceholder);
                }
                case TypeDataErasedTypeInner::TAG_Fcn: {
                    auto& l = le.inner.as_Fcn();
                    auto& r = re.inner.as_Fcn();
                    if (l.index != r.index) {
                        return HIRCompare::Unequal;
                    }
                    return l.origin.compareWithPlaceholders(sp, r.origin, resolvePlaceholder);
                }
            }
            return HIRCompare::Equal;
        }
        case HIRTypeData::TAG_Array: {
            auto& le = (*left).as_Array();
            auto& re = (*right).as_Array();
            auto rv = HIRCompare::Equal;
            if (le.size.is_Unevaluated() && le.size.as_Unevaluated().is_Infer()) {
                rv &= HIRCompare::Fuzzy;
            } else if (re.size.is_Unevaluated() && re.size.as_Unevaluated().is_Infer()) {
                rv &= HIRCompare::Fuzzy;
            } else if (le.size != re.size) {
                return HIRCompare::Unequal;
            } else {
            }
            rv &= le.inner->compareWithPlaceholders(sp, re.inner, resolvePlaceholder);
            return rv;
        }
        case HIRTypeData::TAG_Slice: {
            auto& le = (*left).as_Slice();
            auto& re = (*right).as_Slice();
            return le.inner->compareWithPlaceholders(sp, re.inner, resolvePlaceholder);
        }
        case HIRTypeData::TAG_Pattern: {
            auto& le = (*left).as_Pattern();
            auto& re = (*right).as_Pattern();
            if (le.pattern.alternatives.size() != re.pattern.alternatives.size()) {
                return HIRCompare::Unequal;
            }
            auto rv = le.inner->compareWithPlaceholders(sp, re.inner, resolvePlaceholder);
            auto compareValue = [&](const HIRConstGeneric& left, const HIRConstGeneric& right) {
                const auto& leftResolved = resolvePlaceholder.getVal(sp, left);
                const auto& rightResolved = resolvePlaceholder.getVal(sp, right);
                if (leftResolved.is_Infer() || rightResolved.is_Infer()) {
                    return HIRCompare::Fuzzy;
                }
                if (leftResolved == rightResolved) {
                    return HIRCompare::Equal;
                }
                return leftResolved.is_Unevaluated() || rightResolved.is_Unevaluated() ? HIRCompare::Fuzzy : HIRCompare::Unequal;
            };
            for (size_t i = 0; i < le.pattern.alternatives.size(); i++) {
                const auto& left = le.pattern.alternatives[i];
                const auto& right = re.pattern.alternatives[i];
                if (left.hasStart != right.hasStart || left.hasEnd != right.hasEnd || left.endInclusive != right.endInclusive) {
                    return HIRCompare::Unequal;
                }
                if (left.hasStart) {
                    rv &= compareValue(left.start, right.start);
                }
                if (left.hasEnd) {
                    rv &= compareValue(left.end, right.end);
                }
                if (rv == HIRCompare::Unequal) {
                    return rv;
                }
            }
            return rv;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& le = (*left).as_Tuple();
            auto& re = (*right).as_Tuple();
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
        case HIRTypeData::TAG_Borrow: {
            auto& le = (*left).as_Borrow();
            auto& re = (*right).as_Borrow();
            if (le.type != re.type) {
                return HIRCompare::Unequal;
            }
            return le.inner->compareWithPlaceholders(sp, re.inner, resolvePlaceholder);
        }
        case HIRTypeData::TAG_Pointer: {
            auto& le = (*left).as_Pointer();
            auto& re = (*right).as_Pointer();
            if (le.type != re.type) {
                return HIRCompare::Unequal;
            }
            return le.inner->compareWithPlaceholders(sp, re.inner, resolvePlaceholder);
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& le = (*left).as_NamedFunction();
            auto& re = (*right).as_NamedFunction();
            return le.path.compareWithPlaceholders(sp, re.path, resolvePlaceholder);
        }
        case HIRTypeData::TAG_Function: {
            auto& le = (*left).as_Function();
            auto& re = (*right).as_Function();
            if (le.abi != re.abi || le.isUnsafe != re.isUnsafe || le.isVariadic != re.isVariadic || le.trackCaller != re.trackCaller) {
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
            rv &= le.rettype->compareWithPlaceholders(sp, re.rettype, resolvePlaceholder);
            return rv;
        }
        case HIRTypeData::TAG_NodeType: {
            auto& le = (*left).as_NodeType();
            auto& re = (*right).as_NodeType();
            return le == re ? HIRCompare::Equal : HIRCompare::Unequal;
        }
    }
    UNREACHABLE();
}

HIRTypeInterner::HIRTypeInterner(ObjPool& pool, u32& id)
    : pool(pool)
    , id(id)
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

TypeFmtStream::TypeFmtStream(std::ostream& output)
    : std::ostream(output.rdbuf())
{
    copyfmt(output);
    clear(output.rdstate());
    pword(0) = this;
}

auto TypeFmtStream::from(std::ostream& output) -> TypeFmtStream* {
    return output.pword(0) == &output ? static_cast<TypeFmtStream*>(&output) : nullptr;
}
