#pragma once

#include <cstdint>
#include <memory>
#include <vector>
#include "rc_string.h"
#include "span.h"

namespace HIR {

    class TypeData;
    using TypeRef = const TypeData*;
    class TypeInterner;

    struct GenericRef;
    struct LifetimeRef;
    struct SimplePath;
    class Path;
    class ConstGeneric;
    class GenericParams;

    class ExprPtr;
    struct ExprNodeClosure;
    struct ExprNodeGenerator;
    struct ExprNodeAsyncBlock;

    enum Compare {
        Equal,
        Fuzzy,
        Unequal,
    };

    class ResolvePlaceholders {
    public:
        virtual const ::HIR::TypeData* getType(const Span& sp, const HIR::TypeData* ty) const = 0;
        virtual const ::HIR::ConstGeneric& getVal(const Span& sp, const HIR::ConstGeneric& v) const = 0;
    };

    class ResolvePlaceholdersNop: public ResolvePlaceholders {
        const ::HIR::TypeData* getType(const Span&, const ::HIR::TypeData* ty) const override {
            return ty;
        }

        const ::HIR::ConstGeneric& getVal(const Span&, const ::HIR::ConstGeneric& v) const override {
            return v;
        }
    };

    using t_cb_resolve_type = const ResolvePlaceholders&;

    class TrackHrbStack {
        mutable std::vector<const HIR::GenericParams*> hrbStack;

    public:
        class PopOnDrop {
            friend class TrackHrbStack;
            std::vector<const HIR::GenericParams*>* v;

            PopOnDrop();
            explicit PopOnDrop(std::vector<const HIR::GenericParams*>& v);

        public:
            ~PopOnDrop();

            PopOnDrop(const PopOnDrop&) = delete;
            PopOnDrop(PopOnDrop&& x);
        };

        PopOnDrop pushHrb(const std::unique_ptr<HIR::GenericParams>& params) const;
        PopOnDrop pushHrb(const HIR::GenericParams& params) const;
        const HIR::GenericParams* hasHrb() const {
            return hrbStack.empty() ? nullptr : hrbStack.back();
        }
    };

    class MatchGenerics: virtual public TrackHrbStack {
    public:
        ::HIR::Compare cmpPath(const Span& sp, const ::HIR::Path& ty_l, const ::HIR::Path& ty_r, t_cb_resolve_type resolveCb);
        virtual ::HIR::Compare cmpType(const Span& sp, const ::HIR::TypeData* ty_l, const ::HIR::TypeData* ty_r, t_cb_resolve_type resolveCb);

        virtual ::HIR::Compare matchTy(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, t_cb_resolve_type resolveCb) = 0;
        virtual ::HIR::Compare matchVal(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) = 0;
        virtual ::HIR::Compare matchLft(const ::HIR::GenericRef&, const ::HIR::LifetimeRef&) {
            return HIR::Compare::Equal;
        }
    };

    enum class InferClass {
        None,
        Integer,
        Float,
    };

    enum class CoreType;
    enum class BorrowType;
    struct TypeDataFunctionPointer;
    class TypePathBinding;

} // namespace HIR
