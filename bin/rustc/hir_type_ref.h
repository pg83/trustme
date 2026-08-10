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
    struct ExprNode_Closure;
    struct ExprNode_Generator;
    struct ExprNode_AsyncBlock;

    enum Compare {
        Equal,
        Fuzzy,
        Unequal,
    };

    class ResolvePlaceholders {
    public:
        virtual const ::HIR::TypeRef& get_type(const Span& sp, const HIR::TypeRef& ty) const = 0;
        virtual const ::HIR::ConstGeneric& get_val(const Span& sp, const HIR::ConstGeneric& v) const = 0;
    };

    class ResolvePlaceholdersNop: public ResolvePlaceholders {
        const ::HIR::TypeRef& get_type(const Span&, const ::HIR::TypeRef& ty) const override {
            return ty;
        }

        const ::HIR::ConstGeneric& get_val(const Span&, const ::HIR::ConstGeneric& v) const override {
            return v;
        }
    };

    using t_cb_resolve_type = const ResolvePlaceholders&;

    class TrackHrbStack {
        mutable std::vector<const HIR::GenericParams*> m_hrb_stack;

    public:
        class PopOnDrop {
            friend class TrackHrbStack;
            std::vector<const HIR::GenericParams*>* v;

            PopOnDrop(): v(nullptr) {}
            explicit PopOnDrop(std::vector<const HIR::GenericParams*>& v): v(&v) {}

        public:
            ~PopOnDrop() {
                if (v) {
                    assert(!v->empty());
                    v->pop_back();
                }
            }

            PopOnDrop(const PopOnDrop&) = delete;
            PopOnDrop(PopOnDrop&& x): v(x.v) { x.v = nullptr; }
        };

        PopOnDrop push_hrb(const std::unique_ptr<HIR::GenericParams>& params) const;
        PopOnDrop push_hrb(const HIR::GenericParams& params) const {
            m_hrb_stack.push_back(&params);
            return PopOnDrop(m_hrb_stack);
        }
        const HIR::GenericParams* has_hrb() const {
            return m_hrb_stack.empty() ? nullptr : m_hrb_stack.back();
        }
    };

    class MatchGenerics: virtual public TrackHrbStack {
    public:
        ::HIR::Compare cmp_path(const Span& sp, const ::HIR::Path& ty_l, const ::HIR::Path& ty_r, t_cb_resolve_type resolve_cb);
        virtual ::HIR::Compare cmp_type(const Span& sp, const ::HIR::TypeRef& ty_l, const ::HIR::TypeRef& ty_r, t_cb_resolve_type resolve_cb);

        virtual ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeRef& ty, t_cb_resolve_type resolve_cb) = 0;
        virtual ::HIR::Compare match_val(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) = 0;
        virtual ::HIR::Compare match_lft(const ::HIR::GenericRef&, const ::HIR::LifetimeRef&) {
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
    struct TypeData_FunctionPointer;
    class TypePathBinding;

} // namespace HIR
