#include "hir_typeck_expr_cs.h"

#include "hir_hir.h"
#include "hir_expr.h"
#include "hir_expr_state.h"
#include "settings.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "hir_typeck_static.h"
#include "hir_typeck_helpers.h"
#include "hir_typeck_expr_visit.h"
#include "hir_conv_main_bindings.h"
#include "hir_typeck_main_bindings.h"

#include <std/mem/obj_pool.h>

#include <optional>
#include <algorithm> // std::find_if

namespace {
    inline HIRExprNodeP mkExprnodep(HIRExprNode* en, HIRTypeRef ty) {
        en->resType = mv$(ty);
        return HIRExprNodeP(en);
    }

    inline HIRSimplePath getParentPath(const HIRSimplePath& sp) {
        return sp.parent();
    }

    inline HIRGenericPath getParentPath(const HIRGenericPath& gp) {
        return HIRGenericPath(getParentPath(gp.path), gp.params.clone());
    }

    bool typeContainsImplPlaceholder(HIRTypeInterner& types, const HIRTypeData* t) {
        struct HasPlaceholder {};

        struct V: public HIRVisitor {
            explicit V(HIRTypeInterner& types)
                : HIRVisitor(nullptr, types)
            {
            }

            void visitConstgeneric(const HIRConstGeneric& v) {
                if (v.is_Generic() && v.as_Generic().isPlaceholder()) {
                    throw HasPlaceholder{};
                }
            }

            void visitPathParams(HIRPathParams& pp) override {
                for (const auto& v : pp.values) {
                    visitConstgeneric(v);
                }
                HIRVisitor::visitPathParams(pp);
            }

            [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
                if (ty->is_Generic() && ty->as_Generic().isPlaceholder()) {
                    throw HasPlaceholder{};
                }
                if (const auto* e = ty->opt_Array()) {
                    if (const auto* ase = e->size.opt_Unevaluated()) {
                        visitConstgeneric(*ase);
                    }
                }
                return visitTypeDefaultViaHooks(ty);
            }
        } v(types);

        try {
            auto _discard = v.visitType(t);
            (void)_discard;
            return false;
        } catch (const HasPlaceholder&) {
            return true;
        }
    }

    struct MonomorphEraseHrls: public Monomorphiser {
        explicit MonomorphEraseHrls(HIRTypeInterner& types)
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
}

#define NEWNODE(TY, SP, CLASS, ...) mkExprnodep(context.crate.pool->make<HIRExprNode##CLASS>(SP, ##__VA_ARGS__), TY)

void applyBoundsAsRules(Context& context, const Span& sp, const HIRGenericParams& paramsDef, const Monomorphiser& ms, bool isImplLevel);

namespace {

    // -----------------------------------------------------------------------
    // Revisit Class
    // Handles visiting nodes during inferrence passes
    // -----------------------------------------------------------------------
    // TODO: Convert these to `Revisitor` instances
    class ExprVisitorRevisit: public HIRExprVisitor {
        Context& context;
        bool completed;
        /// Tells vistors that inferrence has stalled, and that they can take
        /// more extreme actions (e.g. ignoring an ambigious inherent method
        /// and using a trait method instead)
        bool isFallback;

        bool nodeDiverges(const HIRExprNode& node) const {
            return node.diverges || this->context.getType(node.resType)->is_Diverge();
        }

    public:
        ExprVisitorRevisit(Context& context, bool fallback = false)
            : context(context)
            , completed(false)
            , isFallback(fallback)
        {
        }

        bool nodeCompleted() const {
            return completed;
        }

        void visit(HIRExprNodeBlock& node) override {
            assert(!node.nodes.empty());
            const auto& lastNode = *node.nodes.back();
            const auto& lastTy = this->context.getType(lastNode.resType);
            DEBUG("_Block: last_ty = " << lastTy);

            bool diverges = false;
            // NOTE: If the final statement in the block diverges, mark this as diverging
            if (lastNode.diverges) {
                diverges = true;
            } else if (const auto* e = lastTy->opt_Infer()) {
                switch (e->tyClass) {
                    case HIRInferClass::Integer:
                    case HIRInferClass::Float:
                        diverges = false;
                        break;
                    default:
                        this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::From);
                        return;
                }
            } else if (lastTy->is_Diverge()) {
                diverges = true;
            } else {
                diverges = false;
            }
            // If a statement in this block diverges
            if (diverges) {
                DEBUG("_Block: diverges, yield !");
                this->context.equateTypes(node.span(), node.resType, context.crate.types.diverge());
            } else {
                DEBUG("_Block: doesn't diverge but doesn't yield a value, yield ()");
                this->context.equateTypes(node.span(), node.resType, context.crate.types.unit());
            }
            node.diverges = diverges;
            this->completed = true;
        }

        void visit(HIRExprNodeConstBlock& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeAsm& node) override {
            // TODO: Revisit for validation
            noRevisit(node);
        }

        void visit(HIRExprNodeAsm2& node) override {
            // TODO: Revisit for validation
            noRevisit(node);
        }

        void visit(HIRExprNodeReturn& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeYield& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeAWait& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeUse& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeLet& node) override {
            if (!node.value) {
                this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
                this->completed = true;
                return;
            }
            const auto* valueType = this->context.getType(node.value->resType);
            const auto* infer = valueType->opt_Infer();
            if (!node.value->diverges && infer && infer->tyClass == HIRInferClass::None) {
                this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::From);
                return;
            }
            node.diverges = this->nodeDiverges(*node.value);
            this->context.equateTypes(node.span(), node.resType,
                node.diverges ? this->context.crate.types.diverge() : this->context.crate.types.unit());
            this->completed = true;
        }

        void visit(HIRExprNodeLoop& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeLoopControl& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeMatch& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeAssign& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeBinOp& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeUniOp& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeBorrow& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeRawBorrow& node) override {
            noRevisit(node);
        }

        void bad_cast(const Span& sp, const HIRTypeData* srcTy, const HIRTypeData* tgtTy, const char* where) {
            ERROR(
                sp,
                E0000,
                "Invalid cast [" << where << "]:\n"
                                 << "from " << this->context.ivars.fmtType(srcTy) << "\n"
                                 << " to  " << this->context.ivars.fmtType(tgtTy)
            );
        }

        /// Equate what a cast between function pointers must agree on. The types
        /// themselves may still differ: a safe function casts to an unsafe one.
        void equateFunctionSignature(const Span& sp, const HIRTypeDataFunctionPointer& dst, const HIRTypeDataFunctionPointer& src) {
            this->context.equateTypes(sp, dst.rettype, src.rettype);
            for (size_t i = 0; i < dst.argTypes.size(); i++) {
                this->context.equateTypes(sp, dst.argTypes[i], src.argTypes[i]);
            }
        }

        void visit(HIRExprNodeCast& node) override {
            const auto& sp = node.span();
            const auto& tgtTy = this->context.getType(node.resType);
            const auto& srcTy = this->context.getType(node.value->resType);
            TRACE_FUNCTION_F(srcTy << " as " << tgtTy);

            if (this->context.ivars.typesEqual(srcTy, tgtTy)) {
                this->completed = true;
                return;
            }

            switch ((*tgtTy).tag()) {
                case HIRTypeData::TAG_Infer: {
                    // Can't know anything
                    DEBUG("- Target type is still _");
                    break;
                }
                case HIRTypeData::TAG_Diverge: {
                    BUG(sp, "");
                    break;
                }
                case HIRTypeData::TAG_Primitive: {
                    auto& e = (*tgtTy).as_Primitive();
                    // Don't have anything to contribute
                    // EXCEPT: `char` can only be casted from `u8` (but what about no-op casts?)
                    // - Hint the input (default) to be `u8`
                    if (e == HIRCoreType::Char) {
                        if (this->isFallback) {
                            this->context.equateTypes(sp, srcTy, context.crate.types.primitive(HIRCoreType::U8));
                            this->completed = true;
                        } else if (!this->context.getType(srcTy)->is_Infer()) {
                            this->completed = true;
                        } else {
                            // Not fallback, and still infer - not complete
                        }
                    } else {
                        this->completed = true;
                    }
                    break;
                }
                case HIRTypeData::TAG_Path: {
                    this->context.equateTypesCoerce(sp, tgtTy, node.value);
                    this->completed = true;
                    return;
                }
                case HIRTypeData::TAG_Generic: {
                    TODO(sp, "_Cast Generic");
                    break;
                }
                case HIRTypeData::TAG_TraitObject: {
                    bad_cast(sp, srcTy, tgtTy, "dst");
                    break;
                }
                case HIRTypeData::TAG_ErasedType: {
                    bad_cast(sp, srcTy, tgtTy, "dst");
                    break;
                }
                case HIRTypeData::TAG_Array: {
                    // A cast is a coercion site.  In particular, its target
                    // supplies the otherwise unconstrained element type of
                    // an empty array literal and the element coercion for a
                    // non-empty literal.
                    this->context.equateTypesCoerce(sp, tgtTy, node.value);
                    this->completed = true;
                    return;
                }
                case HIRTypeData::TAG_Slice: {
                    bad_cast(sp, srcTy, tgtTy, "dst");
                    break;
                }
                case HIRTypeData::TAG_Pattern: {
                    bad_cast(sp, srcTy, tgtTy, "dst");
                    break;
                }
                case HIRTypeData::TAG_Tuple: {
                    bad_cast(sp, srcTy, tgtTy, "dst");
                    break;
                }
                case HIRTypeData::TAG_Borrow: {
                    // An `as &[T]` cast is an unsizing operation on the
                    // already-typed source.  Do not let its destination pick
                    // the type of an otherwise pending source variable: that
                    // would turn a later `&[T; N]` into `&[T]` globally.
                    if (!this->isFallback && (srcTy->is_Infer()
                        || (srcTy->is_Borrow() && this->context.getType(srcTy->as_Borrow().inner)->is_Infer()))) {
                        return;
                    }
                    // Emit a coercion and delete this revisit
                    this->context.equateTypesCoerce(sp, tgtTy, node.value);
                    this->completed = true;
                    return;
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& e = (*tgtTy).as_Pointer();
                    const auto& ity = this->context.getType(e.inner);
                    switch ((*srcTy).tag()) {
default:
                        ERROR(sp, E0000, "Invalid cast to pointer from " << srcTy);
                            case HIRTypeData::TAG_Function:
                            case HIRTypeData::TAG_NamedFunction:
                                // TODO: What is the valid set? *const () and *const u8 at least are allowed
                                if (ity == context.crate.types.unit() || ity == HIRCoreType::U8 || ity == HIRCoreType::I8) {
                                    this->completed = true;
                                } else if (ity->is_Infer()) {
                                    // Keep around.
                                } else {
                                    // TODO: Only allow thin pointers? `c_void` is used in 1.74 libstd
                                    this->completed = true;
                                }
                                break;
                        case HIRTypeData::TAG_Primitive: {
                            auto& sE = (*srcTy).as_Primitive();
                            switch (sE) {
                                case HIRCoreType::Bool:
                                case HIRCoreType::Char:
                                case HIRCoreType::Str:
                                case HIRCoreType::F32:
                                case HIRCoreType::F64:
                                    ERROR(sp, E0000, "Invalid cast to pointer from " << srcTy);
                                default:
                                    break;
                            }
                            // NOTE: Can't be to a fat pointer though - This is checked by the later pass (once all types are known and thus sized-ness is known)
                            this->completed = true;
                            break;
                        }
                        case HIRTypeData::TAG_Infer: {
                            auto& sE = (*srcTy).as_Infer();
                            switch (sE.tyClass) {
                                case HIRInferClass::Float:
                                    ERROR(sp, E0000, "Invalid cast to pointer from floating point literal");
                                case HIRInferClass::Integer:
                                    this->context.equateTypes(sp, srcTy, context.crate.types.primitive(HIRCoreType::Usize));
                                    this->completed = true;
                                    break;
                                case HIRInferClass::None:
                                    break;
                            }
                            break;
                        }
                        case HIRTypeData::TAG_Borrow: {
                            auto& sE = (*srcTy).as_Borrow();
                            // Check class (destination must be weaker) and type
                            if (!(sE.type >= e.type)) {
                                ERROR(sp, E0000, "Invalid cast from " << srcTy << " to " << tgtTy);
                            }
                            const auto& srcInner = this->context.getType(sE.inner);

                            // This is also a semi-coercion point, so need to run the same sort of rules

                            // If either the source or the destination inner tyes are infer - add back rules
                            if (const auto* sEI = srcInner->opt_Infer()) {
                                this->context.possibleEquateIvar(sp, sEI->index, ity, Context::PossibleTypeSource::UnsizeTo);
                            }
                            if (const auto* dEI = ity->opt_Infer()) {
                                this->context.possibleEquateIvar(sp, dEI->index, sE.inner, Context::PossibleTypeSource::UnsizeFrom);
                                if (!this->isFallback) {
                                    // `&value as *const _` may get its pointee
                                    // type from the surrounding expression.  Keep
                                    // the source pointee as the final fallback
                                    // instead of selecting it before that context
                                    // has been resolved.
                                    this->context.possibleEquateIvarUnknown(sp, dEI->index, Context::IvarUnknownType::From);
                                }
                            }

                            // If this looks like `&mut [?; N]` -> `*mut ?` then do a possible equate between the two types
                            if (srcInner->is_Array()) {
                                if (const auto* sEI = this->context.getType(srcInner->as_Array().inner)->opt_Infer()) {
                                    this->context.possibleEquateIvar(sp, sEI->index, ity, Context::PossibleTypeSource::UnsizeTo);
                                    return;
                                }
                            }

                            // NOTE: &mut T -> *mut U where T: Unsize<U> is allowed
                            // TODO: Wouldn't this be better served by a coercion point?

                            if (srcInner->is_Infer() || ity->is_Infer()) {
                                // Either side is infer, keep going
                            }
                            // - NOTE: Crude, and likely to break if ether inner isn't known.
                            else if (srcInner->is_Array() && srcInner->as_Array().inner == ity) {
                                // Allow &[T; n] -> *const T - Convert into two casts
                                auto ty = context.crate.types.pointer(e.type, srcInner);
                                node.value = NEWNODE(ty, sp, Cast, mv$(node.value), ty);
                                this->completed = true;
                            } else {
                                bool found = !this->context.resolve.langUnsize().components().empty() && this->context.resolve.findTraitImpls(sp, this->context.resolve.langUnsize(), HIRPathParams(e.inner), sE.inner, [](auto, auto) {
                                    return true;
                                });
                                if (found) {
                                    auto ty = context.crate.types.borrow(e.type, e.inner);
                                    node.value = NEWNODE(ty, sp, Unsize, mv$(node.value), ty);
                                    this->context.addTraitBound(sp, sE.inner, this->context.resolve.langUnsize(), HIRPathParams(e.inner));
                                } else {
                                    this->context.equateTypes(sp, e.inner, sE.inner);
                                }
                                this->completed = true;
                            }
                            break;
                        }
                        case HIRTypeData::TAG_Pointer: {
                            auto& sE = (*srcTy).as_Pointer();
                            // Allow with no link?
                            // TODO: In some rare cases, this ivar could be completely
                            // unrestricted. If in fallback mode
                            const auto& dstInner = this->context.getType(e.inner);
                            const auto& srcInner = this->context.getType(sE.inner);
                            if (dstInner->is_Infer()) {
                                // NOTE: Don't equate on fallback, to avoid cast chains breaking
                                // - Instead, leave the bounds present (which will hopefully get used by ivar_poss)
                                ::std::vector<HIRTypeRef> tys;
                                tys.push_back(dstInner);
                                tys.push_back(srcInner);
                                this->context.possibleEquateIvarBounds(sp, dstInner->as_Infer().index, std::move(tys));
                                return;
                            } else if (srcInner->is_Infer()) {
                                if (!this->isFallback) {
                                    ::std::vector<HIRTypeRef> tys;
                                    tys.push_back(dstInner);
                                    tys.push_back(srcInner);
                                    this->context.possibleEquateIvarBounds(sp, srcInner->as_Infer().index, std::move(tys));
                                    return;
                                }
                                // A pointer cast doesn't constrain its source. If a real *coercion*
                                // is still pending on this ivar, defer so the later ivar_poss passes
                                // can resolve it instead of us forcing the cast target (bytemuck's
                                // checked slice casts). Bounds-only ivars (e.g. `p as *mut _ as *mut
                                // u8` in alloc's rc.rs) have no coercion to wait on, so equate now -
                                // deferring those leaves spare rules and aborts typecheck.
                                auto srcIdx = srcInner->as_Infer().index;
                                if (srcIdx < this->context.possibleIvarVals.size()) {
                                    const auto& poss = this->context.possibleIvarVals[srcIdx];
                                    if (!poss.typesCoerceTo.empty() || !poss.typesCoerceFrom.empty()) {
                                        return;
                                    }
                                }
                                this->context.equateTypes(sp, dstInner, srcInner);
                            } else {
                            }
                            this->completed = true;
                            break;
                        }
                    }
                    break;
                }
                case HIRTypeData::TAG_Function: {
                    auto& e = (*tgtTy).as_Function();
                    // NOTE: Valid if it's causing a fn item -> fn pointer coercion
                    switch ((*srcTy).tag()) {
default:
                        bad_cast(sp, srcTy, tgtTy, "fcn src");
                        case HIRTypeData::TAG_Infer: {
                            auto& sE = (*srcTy).as_Infer();
                            if (sE.tyClass != HIRInferClass::None) {
                                bad_cast(sp, srcTy, tgtTy, "fcn src");
                            }
                            if (this->isFallback) {
                                this->context.equateTypes(sp, srcTy, tgtTy);
                                this->completed = true;
                            }
                            break;
                        }
                        case HIRTypeData::TAG_NodeType: {
                            auto& sE = (*srcTy).as_NodeType();
                            if (const auto* const* snPp = sE.opt_Closure()) {
                                // Valid cast here, downstream code will check if its a non-capturing closure
                                if ((*snPp)->args.size() != e.argTypes.size()) {
                                    bad_cast(sp, srcTy, tgtTy, "fcn nargs");
                                }
                                this->context.equateTypes(sp, e.rettype, (*snPp)->returnType);
                                for (size_t i = 0; i < e.argTypes.size(); i++) {
                                    this->context.equateTypes(sp, e.argTypes[i], (*snPp)->args[i].second);
                                }
                                this->completed = true;
                            } else {
                                bad_cast(sp, srcTy, tgtTy, "fcn src");
                            }
                            break;
                        }
                        case HIRTypeData::TAG_Function: {
                            auto& sE = (*srcTy).as_Function();
                            // Check that the ABI and unsafety is correct
                            if (sE.abi != e.abi || (sE.isUnsafe && sE.isUnsafe != e.isUnsafe) || sE.argTypes.size() != e.argTypes.size()) {
                                bad_cast(sp, srcTy, tgtTy, "fcn nargs");
                            }
                            equateFunctionSignature(sp, e, sE);
                            this->completed = true;
                            break;
                        }
                        case HIRTypeData::TAG_NamedFunction: {
                            auto& f = (*srcTy).as_NamedFunction();
                            auto ft = context.resolve.expandAssociatedTypes(sp, context.crate.types.function(f.decay(context.crate.types, sp)));
                            const auto& sE = ft->as_Function();
                            // Check that the ABI and unsafety is correct
                            if (sE.abi != e.abi || (sE.isUnsafe && sE.isUnsafe != e.isUnsafe) || sE.argTypes.size() != e.argTypes.size()) {
                                bad_cast(sp, srcTy, tgtTy, "fcn nargs");
                            }
                            equateFunctionSignature(sp, e, sE);
                            this->completed = true;
                            break;
                        }
                    }
                    break;
                }
                case HIRTypeData::TAG_NamedFunction: {
                    BUG(sp, "Attempting to cast to a named-function type - impossible");
                    break;
                }
                case HIRTypeData::TAG_NodeType: {
                    BUG(sp, "Attempting to cast to a magic type type - impossible");
                    break;
                }
            }
        }

        void visit(HIRExprNodeUnsize& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeIndex& node) override {
            const auto& langIndex = this->context.crate.getLangItemPath(node.span(), "index"); // TODO: Pre-load
            const auto& valTy = this->context.getType(node.value->resType);
            const auto& idxTy = this->context.getType(node.cache.indexTy);
            TRACE_FUNCTION_F(&node << " Index: val=" << valTy << ", idx=" << idxTy << "");

            this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::From);

            // NOTE: Indexing triggers autoderef
            unsigned int derefCount = 0;
            HIRTypeRef tmpType; // Temporary type used for handling Deref
            const auto* currentTy = node.value->resType;
            ::std::vector<HIRTypeRef> derefResTypes;

            // TODO: (CHECK) rustc doesn't use the index value type when finding the indexable item, trustme does.
            HIRPathParams traitPp;
            traitPp.types.push_back(idxTy);
            do {
                const auto& ty = this->context.getType(currentTy);
                DEBUG("(Index): (: " << ty << ")[: " << traitPp.types[0] << "]");
                if (ty->is_Infer()) {
                    return;
                }

                HIRTypeRef possibleIndexType;
                HIRTypeRef possibleResType;
                unsigned int count = 0;
                bool rv = this->context.resolve.findTraitImpls(node.span(), langIndex, traitPp, ty, [&](auto impl, auto cmp) {
                    DEBUG("[visit(_Index)] cmp=" << cmp << " - " << impl);
                    possibleResType = impl.getType(context.crate.types, "Output", {});
                    count += 1;
                    if (cmp == HIRCompare::Equal) {
                        return true;
                    }
                    possibleIndexType = impl.getTraitTyParam(context.crate.types, 0);
                    return false;
                });
                if (rv) {
                    // If a non-fuzzy impl was found, but there was no result type - then the result must be opaque
                    if (possibleResType == HIRTypeRef()) {
                        possibleResType = context.crate.types.path(HIRPath(ty, HIRGenericPath(langIndex, mv$(traitPp)), "Output"), HIRTypePathBinding::make_Opaque({}));
                    }
                    // TODO: Node's result type could be an &-ptr?
                    this->context.equateTypes(node.span(), node.resType, possibleResType);
                    break;
                } else if (count == 1) {
                    assert(possibleIndexType != HIRTypeRef());
                    this->context.equateTypesAssoc(node.span(), node.resType, langIndex, mv$(traitPp), ty, "Output", {}, false);
                    break;
                } else if (count > 1) {
                    const auto* indexInfer = this->context.getType(node.cache.indexTy)->opt_Infer();
                    if (indexInfer && indexInfer->tyClass == HIRInferClass::Integer) {
                        // Trait selection constrains an integer literal before its i32 fallback.
                        // Select a concrete index type only when exactly one integer satisfies
                        // the complete Index obligation, including candidate where-clauses.
                        const HIRCoreType integerTypes[] = {
                            HIRCoreType::Usize,
                            HIRCoreType::Isize,
                            HIRCoreType::U8,
                            HIRCoreType::I8,
                            HIRCoreType::U16,
                            HIRCoreType::I16,
                            HIRCoreType::U32,
                            HIRCoreType::I32,
                            HIRCoreType::U64,
                            HIRCoreType::I64,
                            HIRCoreType::U128,
                            HIRCoreType::I128,
                        };
                        const HIRTypeData* possibleType = nullptr;
                        for (const auto integerType : integerTypes) {
                            const auto* candidateType = this->context.crate.types.primitive(integerType);
                            const bool isViable = this->context.resolve.findTraitImpls(node.span(), langIndex, HIRPathParams(candidateType), ty, [](ImplRef, HIRCompare cmp) {
                                return cmp == HIRCompare::Equal;
                            });
                            if (!isViable) {
                                continue;
                            }
                            if (possibleType) {
                                possibleType = nullptr;
                                break;
                            }
                            possibleType = candidateType;
                        }
                        if (possibleType) {
                            this->context.equateTypes(node.span(), node.cache.indexTy, possibleType);
                            return;
                        }
                    }
                    // Multiple fuzzy matches, don't keep dereferencing until we know.
                    currentTy = nullptr;
                    break;
                } else {
                    // Either no matches, or multiple fuzzy matches
                }

                derefCount += 1;
                currentTy = this->context.resolve.autoderef(node.span(), ty, tmpType);
                if (currentTy) {
                    derefResTypes.push_back(currentTy);
                }
            } while (currentTy);

            if (currentTy) {
                DEBUG("Found impl on type " << currentTy << " with " << derefCount << " derefs");
                assert(derefCount == derefResTypes.size());
                for (auto& tyR : derefResTypes) {
                    auto ty = mv$(tyR);

                    node.value = this->context.createAutoderef(mv$(node.value), mv$(ty));
                    context.ivars.getType(node.value->resType);
                }

                completed = true;
            }
        }

        void visit(HIRExprNodeDeref& node) override {
            const auto& ty = this->context.getType(node.value->resType);
            TRACE_FUNCTION_F("Deref: ty=" << ty);

            const auto& opTrait = this->context.crate.getLangItemPathOpt("deref");
            auto useBuiltin = [&](const HIRTypeData* inner) {
                node.traitUsed = HIRExprNodeDeref::TraitUsed::Builtin;
                this->context.equateTypes(node.span(), node.resType, inner);
            };
            auto useTrait = [&]() {
                node.traitUsed = HIRExprNodeDeref::TraitUsed::Trait;
                this->context.equateTypesAssoc(node.span(), node.resType, opTrait, {}, node.value->resType, "Target", {}, true, TypeckPrimitiveOperator::Deref);
            };

            switch ((*ty).tag()) {
default: {
                    if (const auto* inner = this->context.resolve.typeIsOwnedBox(node.span(), ty)) {
                        useBuiltin(inner);
                    } else {
                        ASSERT_BUG(node.span(), !opTrait.components().empty(), "Deref trait missing for non-builtin dereference of " << ty);
                        useTrait();
                    }
                }
                break;
                case HIRTypeData::TAG_Infer: {
                    // Keep trying
                    this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::From);
                    return;
                }
                case HIRTypeData::TAG_Borrow: {
                    auto& e = (*ty).as_Borrow();
                    useBuiltin(e.inner);
                    break;
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& e = (*ty).as_Pointer();
                    // TODO: Figure out if this node is in an unsafe block.
                    useBuiltin(e.inner);
                    break;
                }
            }
            this->completed = true;
        }

        void visitEmplace129(HIRExprNodeEmplace& node) {
            const auto& sp = node.span();
            const auto& expTy = this->context.getType(node.resType);
            const auto& dataTy = this->context.getType(node.value->resType);
            const auto& placerTy = this->context.getType(node.place->resType);
            const auto& langBoxed = this->context.langBox;
            TRACE_FUNCTION_F("exp_ty=" << expTy << ", data_ty=" << dataTy << ", placer_ty" << placerTy);
            ASSERT_BUG(sp, node.type == HIRExprNodeEmplace::Type::Boxer, "1.29 mode with non-box _Emplace node");
            ASSERT_BUG(sp, placerTy == context.crate.types.unit(), "1.29 mode with box in syntax - placer type is " << placerTy);

            ASSERT_BUG(sp, !langBoxed.components().empty(), "`owned_box` not present when `box` operator used");

            // NOTE: `owned_box` shouldn't point to anything but a struct
            const auto& str = this->context.crate.getStructByPath(sp, langBoxed);
            // TODO: Store this type to avoid having to construct it every pass
            auto p = HIRGenericPath(langBoxed, {dataTy});
            p.params.types.push_back(MonomorphStatePtr(context.crate.types, nullptr, &p.params, nullptr).monomorphType(sp, str.params.types.at(1).defaultValue));
            this->context.addIvars(p.params.types.back());
            auto boxedTy = context.crate.types.path(mv$(p), &str);

            // TODO: is there anyting special about this node that might need revisits?

            context.equateTypes(sp, expTy, boxedTy);
            this->completed = true;
        }

        void visit(HIRExprNodeEmplace& node) override {
            return visitEmplace129(node);
        }

        void visit(HIRExprNodeTupleVariant& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeCallPath& node) override {
            TRACE_FUNCTION_F(node.path << "(...)");
            // Retry the cache now inference may have advanced; the main pass deferred here because the callee was ambiguous.
            if (!visitCallPopulateCache(this->context, node.span(), node.path, node.cache)) {
                DEBUG("- CallPath still ambiguous - trying again later");
                return;
            }
            assert(node.cache.argTypes.size() >= 1);
            unsigned int expArgc = node.cache.argTypes.size() - 1;
            if (node.args.size() != expArgc) {
                if (node.cache.fcn->variadic && node.args.size() > expArgc) {
                } else {
                    ERROR(node.span(), E0000, "Incorrect number of arguments to " << node.path << " - exp " << expArgc << " got " << node.args.size());
                }
            }
            for (unsigned int i = 0; i < node.cache.argTypes.size() - 1; i++) {
                this->context.equateTypesCoerce(node.span(), node.cache.argTypes[i], node.args[i]);
            }
            this->context.equateTypes(node.span(), node.resType, node.cache.argTypes.back());
            this->context.requireSized(node.span(), node.resType);
            this->completed = true;
        }

        /// A value may be callable only through the async callable traits (a
        /// generic bounded by `AsyncFnOnce()`, say). The call then evaluates to
        /// the future the trait names, and the most permissive of the three
        /// traits decides how the callee is passed.
        bool callAsyncCallable(HIRExprNodeCallValue& node, HIRTypeRef ty, const HIRPathParams& traitPp) {
            struct Candidate {
                const HIRSimplePath& trait;
                const char* future;
                HIRExprNodeCallValue::TraitUsed used;
            };
            const Candidate candidates[] = {
                {this->context.resolve.langAsyncFn(), "CallRefFuture", HIRExprNodeCallValue::TraitUsed::AsyncFn},
                {this->context.resolve.langAsyncFnMut(), "CallRefFuture", HIRExprNodeCallValue::TraitUsed::AsyncFnMut},
                {this->context.resolve.langAsyncFnOnce(), "CallOnceFuture", HIRExprNodeCallValue::TraitUsed::AsyncFnOnce},
            };
            for (const auto& candidate : candidates) {
                if (candidate.trait.components().empty()) {
                    continue;
                }
                HIRTypeRef fcnArgsTup;
                unsigned int count = 0;
                this->context.resolve.findTraitImpls(node.span(), candidate.trait, traitPp, ty, [&](auto impl, auto cmp) -> bool {
                    count++;
                    auto tup = impl.getTraitTyParam(context.crate.types, 0);
                    if (!tup->is_Tuple()) {
                        ERROR(node.span(), E0000, "AsyncFn* expects a tuple argument, got " << tup);
                    }
                    fcnArgsTup = mv$(tup);
                    return cmp == HIRCompare::Equal;
                });
                if (count != 1) {
                    continue;
                }
                DEBUG("-- Using " << candidate.trait << " for " << ty);
                // `CallRefFuture` is a generic associated type, but its only
                // parameter is a lifetime -- which HIR does not carry.
                const auto futureName = RcString::newInterned(candidate.future);
                const auto& futureTrait = candidate.used == HIRExprNodeCallValue::TraitUsed::AsyncFnOnce
                    ? this->context.resolve.langAsyncFnOnce()
                    : this->context.resolve.langAsyncFnMut();
                this->context.equateTypesAssoc(node.span(), node.resType, futureTrait, HIRPathParams(fcnArgsTup), ty, futureName.c_str(), {});
                node.argTypes = fcnArgsTup->as_Tuple();
                node.argTypes.push_back(node.resType);
                node.traitUsed = candidate.used;
                return true;
            }
            return false;
        }

        void visit(HIRExprNodeCallValue& node) override {
            // A for-loop (and other desugarings) can leave the callee as an
            // associated projection until the surrounding IntoIterator/
            // Iterator obligations select their input types.  Normalise what
            // is currently known, but don't diagnose the still-dependent
            // projection as a non-callable concrete type.
            node.value->resType = this->context.resolve.expandAssociatedTypes(node.span(), node.value->resType);
            const auto& tyO = this->context.getType(node.value->resType);
            TRACE_FUNCTION_F("CallValue: ty=" << tyO);

            this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::Bound);
            // - Shadow (prevent ivar guessing) every parameter
            for (const auto& argTy : node.argIvars) {
                this->context.possibleEquateTypeUnknown(node.span(), argTy, Context::IvarUnknownType::To);
            }

            if (tyO->is_Infer()) {
                // - Don't even bother
                return;
            }
            if (const auto* path = tyO->opt_Path()) {
                if (path->binding.is_Unbound() && path->path.data.is_UfcsKnown() && this->context.resolve.typeContainsIvars(tyO)) {
                    return;
                }
            }
            const auto& langFnOnce = this->context.resolve.langFnOnce();

            // 1. Create a param set with a single tuple (of all argument types)
            HIRPathParams traitPp;
            {
                ::std::vector<HIRTypeRef> argTypes;
                for (const auto& argTy : node.argIvars) {
                    argTypes.push_back(this->context.getType(argTy));
                }
                traitPp.types.push_back(context.crate.types.tuple(mv$(argTypes)));
            }

            unsigned int derefCount = 0;
            HIRTypeRef tmpType; // for autoderef
            const auto* ty = tyO;

            bool keepLooping = false;
            do // while( keep_looping );
            {
                // Reset at the start of each loop
                keepLooping = false;

                DEBUG("- ty = " << ty);
                if (ty->is_NodeType() && ty->as_NodeType().is_Closure()) {
                    const auto* nodeP = ty->as_NodeType().as_Closure();
                    for (const auto& arg : nodeP->args) {
                        node.argTypes.push_back(arg.second);
                    }
                    node.argTypes.push_back(nodeP->returnType);
                    node.traitUsed = HIRExprNodeCallValue::TraitUsed::Unknown;
                } else if (ty->is_Function() || ty->is_NamedFunction()) {
                    HIRTypeRef tmpFt;
                    const auto* e = ty->opt_Function();
                    if (!e) {
                        tmpFt = context.crate.types.function(ty->as_NamedFunction().decay(context.crate.types, node.span()));
                        tmpFt = this->context.resolve.expandAssociatedTypes(node.span(), tmpFt);
                        e = &tmpFt->as_Function();
                    }
                    for (const auto& arg : e->argTypes) {
                        node.argTypes.push_back(arg);
                    }
                    if (e->isVariadic) {
                        for (size_t i = e->argTypes.size(); i < node.args.size(); i++) {
                            node.argTypes.push_back(node.argIvars[i]);
                        }
                    }
                    node.argTypes.push_back(e->rettype);
                    node.traitUsed = HIRExprNodeCallValue::TraitUsed::Fn;
                } else if (ty->is_Infer()) {
                    // No idea yet
                    return;
                } else if (const auto* e = ty->opt_Borrow()) {
                    derefCount++;
                    ty = this->context.getType(e->inner);
                    DEBUG("Deref -> " << ty);
                    keepLooping = true;
                    continue;
                }
                // TODO: If autoderef is possible, do it and continue. Only look for impls once autoderef fails
                else {
                    HIRTypeRef fcnArgsTup;
                    HIRTypeRef fcnRet;

                    // TODO: Use `find_trait_impls` instead of two different calls
                    // - This will get the TraitObject impl search too

                    // Locate an impl of FnOnce (exists for all other Fn* traits)
                    // TODO: Sometimes there's impls that just forward for wrappers, which can lead to incorrect rules
                    // e.g. `&mut _` (where `_ = Box<...>`) later will pick the FnMut impl for `&mut T: FnMut` - but Box doesn't have those forwarding impls
                    // - Maybe just keep applying auto-deref until it's no longer possible?
                    unsigned int count = 0;
                    this->context.resolve.findTraitImpls(node.span(), langFnOnce, traitPp, ty, [&](auto impl, auto cmp) -> bool {
                        // TODO: Don't accept if too fuzzy
                        count++;

                        auto tup = impl.getTraitTyParam(context.crate.types, 0);
                        if (!tup->is_Tuple()) {
                            ERROR(node.span(), E0000, "FnOnce expects a tuple argument, got " << tup);
                        }
                        fcnArgsTup = mv$(tup);

                        fcnRet = impl.getType(context.crate.types, "Output", {});
                        DEBUG("[visit:_CallValue] fcn_args_tup=" << fcnArgsTup << ", fcn_ret=" << fcnRet);
                        return cmp == HIRCompare::Equal;
                    });
                    DEBUG("Found " << count << " impls of FnOnce");
                    if (count > 1) {
                        return;
                    }
                    if (count == 1) {
                        this->context.equateTypesAssoc(node.span(), node.resType, langFnOnce, HIRPathParams(fcnArgsTup), ty, "Output", {});

                        // If the return type wasn't found in the impls, emit it as a UFCS
                        if (fcnRet == HIRTypeRef()) {
                            fcnRet = context.crate.types.path(
                                HIRPath(
                                    HIRPath::Data::make_UfcsKnown(
                                        {ty,
                                         // - Clone argument tuple, as it's stolen into cache below
                                         HIRGenericPath(langFnOnce, HIRPathParams(fcnArgsTup)),
                                         "Output",
                                         {}}
                                    )
                                ),
                                {}
                            );
                        }
                    } else if (const auto* e = ty->opt_Borrow()) {
                        derefCount++;
                        ty = this->context.getType(e->inner);
                        DEBUG("Deref -> " << ty);
                        keepLooping = true;
                        continue;
                    } else {
                        if (!ty->is_Generic()) {
                            bool found = this->context.resolve.findTraitImplsCrate(node.span(), langFnOnce, traitPp, ty, [&](auto impl, auto cmp) -> bool {
                                if (cmp == HIRCompare::Fuzzy) {
                                    TODO(node.span(), "Handle fuzzy match - " << impl);
                                }

                                auto tup = impl.getTraitTyParam(context.crate.types, 0);
                                if (!tup->is_Tuple()) {
                                    ERROR(node.span(), E0000, "FnOnce expects a tuple argument, got " << tup);
                                }
                                fcnArgsTup = mv$(tup);
                                fcnRet = impl.getType(context.crate.types, "Output", {});
                                ASSERT_BUG(node.span(), fcnRet != HIRTypeRef(), "Impl didn't have a type for Output - " << impl);
                                return true;
                            });
                            if (found) {
                                // Fill cache and leave the TU_MATCH
                                node.argTypes = fcnArgsTup->as_Tuple();
                                node.argTypes.push_back(mv$(fcnRet));
                                node.traitUsed = HIRExprNodeCallValue::TraitUsed::Unknown;
                                break; // leaves TU_MATCH
                            }
                        }
                        if (const auto* nextTyP = this->context.resolve.autoderef(node.span(), ty, tmpType)) {
                            DEBUG("Deref (autoderef) " << ty << " -> " << nextTyP);
                            derefCount++;
                            ty = nextTyP;
                            keepLooping = true;
                            continue;
                        }

                        // A value may be callable through the async callable
                        // traits only (`impl AsyncFnOnce()`); the call then
                        // evaluates to the future, not to the output.
                        if (this->callAsyncCallable(node, ty, traitPp)) {
                            break; // leaves TU_MATCH
                        }

                        // Didn't find anything. Error?
                        ERROR(node.span(), E0000, "Unable to find an implementation of Fn*" << traitPp << " for " << this->context.ivars.fmtType(ty));
                    }

                    node.argTypes = fcnArgsTup->as_Tuple();
                    node.argTypes.push_back(mv$(fcnRet));
                }
            } while (keepLooping);

            if (derefCount > 0) {
                ty = tyO;
                while (derefCount-- > 0) {
                    const auto* nextTy = this->context.resolve.autoderef(node.span(), ty, tmpType);
                    assert(nextTy);
                    ty = nextTy;
                    node.value = this->context.createAutoderef(mv$(node.value), ty);
                }
            }

            ASSERT_BUG(node.span(), node.argTypes.size() == node.args.size() + 1, "Malformed cache in CallValue: " << node.argTypes.size() << " != 1+" << node.args.size());
            for (unsigned int i = 0; i < node.args.size(); i++) {
                this->context.equateTypes(node.span(), node.argTypes[i], node.argIvars[i]);
            }
            this->context.equateTypes(node.span(), node.resType, node.argTypes.back());
            this->completed = true;
        }

        void visit(HIRExprNodeCallMethod& node) override {
            const auto& sp = node.span();

            const auto& ty = this->context.getType(node.value->resType);
            TRACE_FUNCTION_F(
                "(CallMethod) {" << this->context.ivars.fmtType(ty) << "}." << node.method << node.params << "(" << FMT_CB(os, for (const auto& argNode : node.args) os << this->context.ivars.fmtType(argNode->resType) << ", ";) << ")"
                                 << " -> " << this->context.ivars.fmtType(node.resType)
            );

            // Make sure that no mentioned types are inferred until this method is known
            this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::From);
            for (const auto& argNode : node.args) {
                this->context.possibleEquateTypeUnknown(node.span(), argNode->resType, Context::IvarUnknownType::To);
            }

            // Using autoderef, locate this method on the type
            // TODO: Obtain a list of avaliable methods at that level?
            // - If running in a mode after stablise (before defaults), fall
            //   back to trait if the inherent is still ambigious.
            ::std::vector<::std::pair<TraitResolution::AutoderefBorrow, HIRPath>> possibleMethods;
            // Once type checking has stabilised there is nothing left to wait
            // for, so the probe answers from what is known.
            this->context.resolve.methodProbeMustDecide = this->isFallback;
            unsigned int derefCount = this->context.resolve.autoderefFindMethod(node.span(), node.traits, node.traitParamIvars, node.traitParamTypeIvars, ty, node.method, possibleMethods);
            this->context.resolve.methodProbeMustDecide = false;
        tryAgain:
            if (derefCount != ~0u) {
                DEBUG("possible_methods = " << possibleMethods);
                // HACK: In fallback mode, remove inherent impls from bounded ivars
                if (ty->is_Infer() && this->isFallback) {
                    auto newEnd = std::remove_if(possibleMethods.begin(), possibleMethods.end(), [](const auto& e) {
                        return e.second.data.is_UfcsInherent();
                    });
                    if (newEnd != possibleMethods.begin()) {
                        possibleMethods.erase(newEnd, possibleMethods.end());
                    }
                }
                if (possibleMethods.empty()) {
                    ERROR(sp, E0000, "No applicable methods for {" << this->context.ivars.fmtType(ty) << "}." << node.method);
                }
                if (possibleMethods.size() > 1) {
                    // TODO: What do do when there's multiple possibilities?
                    // - Should use available information to strike them down
                    // > Try and equate the return type and the arguments, if any fail then move on to the next possibility?
                    // > ONLY if those arguments/return are generic
                    // Possible causes of multiple entries
                    // - Multiple distinct traits with the same method
                    //   > If `self` is concretely known, this is an error (and shouldn't happen in well-formed code).
                    // - Multiple inherent methods on a type
                    //   > These would have to have different type parmeters
                    // - Multiple trait bounds (same trait, different type params)
                    //   > Guess at the type params, then discard if there's a conflict?
                    //   > De-duplicate same traits?
                    // So: To be able to prune the list, we need to check the type parameters for the trait/type/impl

                    // Remove anything except for the highest autoref level
                    for (auto it1 = possibleMethods.begin(); it1 != possibleMethods.end(); ++it1) {
                        if (it1->first != possibleMethods.front().first) {
                            it1 = possibleMethods.erase(it1) - 1;
                        }
                    }
                    // De-duplcate traits in this list.
                    // - If the self type and the trait name are the same, replace with an entry using placeholder
                    //   ivars (node.m_trait_param_ivars)
                    for (auto it1 = possibleMethods.begin(); it1 != possibleMethods.end(); ++it1) {
                        // Only consider trait impls (UfcsKnown path)
                        if (!it1->second.data.is_UfcsKnown()) {
                            continue;
                        }

                        auto& e1 = it1->second.data.as_UfcsKnown();
                        for (auto it2 = it1 + 1; it2 != possibleMethods.end(); ++it2) {
                            if (!it2->second.data.is_UfcsKnown()) {
                                continue;
                            }
                            // If it's a complete duplicate, immediately ignore.
                            if (it2->second == it1->second) {
                                it2 = possibleMethods.erase(it2) - 1;
                                continue;
                            }
                            const auto& e2 = it2->second.data.as_UfcsKnown();

                            // TODO: If the trait is the same, but the type differs, pick the first?
                            if (e1.trait == e2.trait) {
                                // NOTE: The trait is identical, but the full path comparison above failed. Ergo the type is different.
                                DEBUG("Duplicate trait, different type - " << e1.trait << " for " << e1.type << " or " << e2.type << ", picking the first");
                                it2 = possibleMethods.erase(it2) - 1;
                                continue;
                            }
                            if (e1.type != e2.type) {
                                continue;
                            }
                            // Compare the simplepath.
                            if (e1.trait.path != e2.trait.path) {
                                continue;
                            }
                            assert(!(e1.trait.params == e2.trait.params));

                            DEBUG("Duplicate trait in possible_methods - " << it1->second << " and " << it2->second);

                            // Remove the second entry, after re-creating the params using the ivar list
                            // TODO: If `Into<Foo>` and `Into<_>` is seen, we want to pick the solid type, BUT
                            //       For `Into<Foo>` and `Into<Bar>` this needs to be collapsed into `Into<_>` and propagated
                            //if( e1.trait .compare_with_placeholders(sp, e2.trait, context.m_ivars.callback_resolve_infer()) == ::HIR::Compare::Unequal )
                            {
                                auto& ivars = node.traitParamIvars;
                                unsigned int nParams = e1.trait.params.types.size();
                                ASSERT_BUG(sp, node.traitParamTypeIvars <= ivars.size(), "Invalid method ivar split");
                                ASSERT_BUG(sp, nParams <= node.traitParamTypeIvars, "Not enough type ivars for duplicate trait method");
                                HIRPathParams traitParams;
                                traitParams.types.reserve(nParams);
                                for (unsigned int i = 0; i < nParams; i++) {
                                    traitParams.types.push_back(context.crate.types.infer(ivars[i], HIRInferClass::None));
                                }
                                const unsigned int nValues = e1.trait.params.values.size();
                                ASSERT_BUG(sp, nValues <= ivars.size() - node.traitParamTypeIvars, "Not enough value ivars for duplicate trait method");
                                traitParams.values.reserve(nValues);
                                for (unsigned int i = 0; i < nValues; i++) {
                                    traitParams.values.push_back(HIRConstGeneric::make_Infer({ivars[node.traitParamTypeIvars + i]}));
                                }
                                // If one of these was already using the placeholder ivars, then maintain the one with the palceholders
                                if (e1.trait.params != traitParams) {
                                    e1.trait.params = mv$(traitParams);
                                }
                            }

                            it2 = possibleMethods.erase(it2) - 1;
                        }
                    }

                    // If tjhis is fallback mode, and we're in a trait impl - grab the trait
                    if (possibleMethods.size() > 1 && this->context.resolve.currentTraitPath()) {
                        const auto& tp = *this->context.resolve.currentTraitPath();
                        auto found = possibleMethods.end();
                        bool hadInherent = false;
                        for (auto it = possibleMethods.begin(); it != possibleMethods.end(); ++it) {
                            hadInherent |= it->second.data.is_UfcsInherent();
                            if (it->second.data.is_UfcsKnown() && it->second.data.as_UfcsKnown().trait.path == tp.path) {
                                found = it;
                            }
                        }
                        if (!hadInherent && found != possibleMethods.end()) {
                            DEBUG("Multiple options - Restricted to just current trait");
                            possibleMethods.erase(found + 1, possibleMethods.end());
                            possibleMethods.erase(possibleMethods.begin(), found);
                        }
                    }

                    DEBUG("possible_methods = " << possibleMethods);
                }
                assert(!possibleMethods.empty());
                if (possibleMethods.size() != 1 && possibleMethods.front().second.data.is_UfcsKnown()) {
                    DEBUG("- Multiple options, deferring");
                    // TODO: If the type is fully known, then this is an error.
                    return;
                }
                auto& adBorrow = possibleMethods.front().first;
                auto& fcnPath = possibleMethods.front().second;
                DEBUG("- deref_count = " << derefCount << ", fcn_path = " << fcnPath);

                // Inside a trait impl, unconstrained trait arguments on a
                // call through the current Self default to this impl's
                // arguments.  They remain defaults: the method signature can
                // still select a different impl (for example
                // PartialOrd<Ipv4Addr> from inside PartialOrd<IpAddr>).
                if (context.currentTraitImpl) {
                    const auto* selected = fcnPath.data.opt_UfcsKnown();
                    const auto* currentTrait = context.resolve.currentTraitPath();
                    const auto* selectedSelf = selected ? context.ivars.getType(selected->type) : nullptr;
                    const auto* currentSelf = context.ivars.getType(context.currentTraitImpl->type);
                    if (selected && currentTrait && (selectedSelf == currentSelf || selectedSelf->equalsIgnoringRegions(currentSelf))) {
                        HIRGenericPath exactTrait;
                        const auto& trait = context.crate.getTraitByPath(sp, currentTrait->path);
                        if (context.resolve.traitContainsMethod(sp, *currentTrait, trait, selectedSelf, node.method, exactTrait)
                            && selected->trait.path == exactTrait.path
                            && selected->trait.params.types.size() == exactTrait.params.types.size()) {
                            for (unsigned int i = 0; i < selected->trait.params.types.size(); i++) {
                                const auto* selectedParam = context.ivars.getType(selected->trait.params.types[i]);
                                if (const auto* infer = selectedParam->opt_Infer()) {
                                    if (auto* possible = context.getIvarPossibilities(sp, infer->index)) {
                                        possible->typesDefault.insert(exactTrait.params.types[i]);
                                    }
                                }
                            }
                        }
                    }
                }

                node.methodPath = mv$(fcnPath);
                // NOTE: Steals the params from the node
                switch (node.methodPath.data.tag()) {
                    case HIRPath::Data::TAG_Generic: {
                        break;
                    }
                    case HIRPath::Data::TAG_UfcsUnknown: {
                        break;
                    }
                    case HIRPath::Data::TAG_UfcsKnown: {
                        auto& e = node.methodPath.data.as_UfcsKnown();
                        e.params = mv$(node.params);
                        break;
                    }
                    case HIRPath::Data::TAG_UfcsInherent: {
                        auto& e = node.methodPath.data.as_UfcsInherent();
                        e.params = mv$(node.params);
                        break;
                    }
                }

                // TODO: If this is ambigious, and it's an inherent, and in fallback mode - fall down to the next trait method.
                if (!visitCallPopulateCache(this->context, node.span(), node.methodPath, node.cache)) {
                    // Move the params back
                    switch (node.methodPath.data.tag()) {
                        case HIRPath::Data::TAG_Generic: {
                            break;
                        }
                        case HIRPath::Data::TAG_UfcsUnknown: {
                            break;
                        }
                        case HIRPath::Data::TAG_UfcsKnown: {
                            auto& e = node.methodPath.data.as_UfcsKnown();
                            node.params = mv$(e.params);
                            break;
                        }
                        case HIRPath::Data::TAG_UfcsInherent: {
                            auto& e = node.methodPath.data.as_UfcsInherent();
                            node.params = mv$(e.params);
                            break;
                        }
                    }
                    if (this->isFallback && node.methodPath.data.is_UfcsInherent()) {
                        unsigned nRemove = 1;
                        while (nRemove < possibleMethods.size() && possibleMethods[nRemove].second.data.is_UfcsInherent()) {
                            nRemove += 1;
                        }
                        if (nRemove < possibleMethods.size()) {
                            possibleMethods.erase(possibleMethods.begin() + nRemove);
                            DEBUG("Inference stall (remove " << nRemove << ") try again with " << possibleMethods.front().second);
                            goto tryAgain;
                        } else {
                            DEBUG("AMBIGUOUS and removed all " << nRemove << " possibilities");
                        }
                    } else {
                        DEBUG("- AMBIGUOUS - Trying again later");
                    }
                    return;
                }
                DEBUG("> m_method_path = " << node.methodPath);

                assert(node.cache.argTypes.size() >= 1);

                if (node.args.size() + 1 != node.cache.argTypes.size() - 1) {
                    ERROR(node.span(), E0000, "Incorrect number of arguments to " << node.methodPath << " - exp " << node.cache.argTypes.size() - 2 << " got " << node.args.size());
                }
                DEBUG("- fcn_path=" << node.methodPath);

                // --- Check and equate self/arguments/return
                DEBUG("node.m_cache.m_arg_types = " << node.cache.argTypes);
                // NOTE: `Self` is equated after autoderef and autoref
                for (unsigned int i = 0; i < node.args.size(); i++) {
                    // 1+ because it's a method call (#0 is Self)
                    DEBUG("> ARG " << i << " : " << node.cache.argTypes[1 + i]);
                    this->context.equateTypesCoerce(sp, node.cache.argTypes[1 + i], node.args[i]);
                }
                DEBUG("> Ret : " << node.cache.argTypes.back());
                this->context.equateTypes(sp, node.resType, node.cache.argTypes.back());

                // Add derefs
                if (derefCount > 0) {
                    assert(derefCount < (1 << 16)); // Just some sanity.
                    DEBUG("- Inserting " << derefCount << " dereferences");
                    // Get dereferencing!
                    auto& nodePtr = node.value;
                    HIRTypeRef tmpTy;
                    const HIRTypeData* curTy = nodePtr->resType;
                    while (derefCount--) {
                        auto span = nodePtr->span();
                        auto sourceTy = curTy;
                        ::std::optional<HIRTypeRef> implType;
                        auto result = this->context.resolve.autoderefStep(span, sourceTy, tmpTy, &implType);
                        ASSERT_BUG(span, result == TraitResolution::AutoderefResult::Match, "Selected autoderef step no longer has a unique response for " << sourceTy);
                        if (implType) {
                            this->context.equateTypes(span, sourceTy, *implType);
                            this->context.equateTypesAssoc(span, tmpTy, this->context.crate.getLangItemPath(span, "deref"), {}, sourceTy, "Target", {}, true, TypeckPrimitiveOperator::Deref);
                        }
                        curTy = tmpTy;
                        auto ty = tmpTy;

                        node.value = this->context.createAutoderef(mv$(node.value), mv$(ty));
                    }
                }

                // A `*mut T` receiver taken as `*const T` is a cast, not a borrow.
                if (adBorrow == TraitResolution::AutoderefBorrow::RawShared) {
                    const auto& srcTy = this->context.getType(node.value->resType);
                    ASSERT_BUG(sp, srcTy->is_Pointer(), "RawShared adjustment on " << srcTy);
                    auto ty = context.crate.types.pointer(HIRBorrowType::Shared, srcTy->as_Pointer().inner);
                    DEBUG("- Raw cast (cmd) " << &*node.value << " -> " << ty);
                    auto span = node.value->span();
                    node.value = NEWNODE(ty, span, Cast, mv$(node.value), ty);
                }
                // `Pin<&mut T>` is reborrowed as the `Pin<&T>` receiver found
                // by the pin-ergonomics probe. The ordinary coercion lowering
                // already knows how to rebuild this transparent wrapper.
                else if (adBorrow == TraitResolution::AutoderefBorrow::PinShared) {
                    auto ty = node.cache.argTypes[0];
                    DEBUG("- Pin reborrow (cmd) " << &*node.value << " -> " << ty);
                    auto span = node.value->span();
                    node.value = NEWNODE(ty, span, Unsize, mv$(node.value), ty);
                }
                // Autoref
                else if (adBorrow != TraitResolution::AutoderefBorrow::None) {
                    HIRBorrowType bt = HIRBorrowType::Shared;
                    switch (adBorrow) {
                        case TraitResolution::AutoderefBorrow::None:
                        case TraitResolution::AutoderefBorrow::RawShared:
                        case TraitResolution::AutoderefBorrow::PinShared:
                            throw "";
                        case TraitResolution::AutoderefBorrow::Shared:
                            bt = HIRBorrowType::Shared;
                            break;
                        case TraitResolution::AutoderefBorrow::Unique:
                            bt = HIRBorrowType::Unique;
                            break;
                        case TraitResolution::AutoderefBorrow::Owned:
                            bt = HIRBorrowType::Owned;
                            break;
                    }

                    auto ty = context.crate.types.borrow(bt, node.value->resType);
                    DEBUG("- Ref (cmd) " << &*node.value << " -> " << ty);
                    auto span = node.value->span();
                    node.value = NEWNODE(mv$(ty), span, Borrow, bt, mv$(node.value));
                } else {
                }

                // Equate the type for `self` (to ensure that Self's type params infer correctly)
                this->context.equateTypes(sp, node.cache.argTypes[0], node.value->resType);

                this->completed = true;
            }
        }

        void visit(HIRExprNodeField& node) override {
            const auto& fieldName = node.field;
            TRACE_FUNCTION_F("(Field) name=" << fieldName << ", ty = " << this->context.ivars.fmtType(node.value->resType));

            this->context.possibleEquateTypeUnknown(node.span(), node.resType, Context::IvarUnknownType::Bound);

            HIRTypeRef outType;

            // Using autoderef, locate this field
            unsigned int derefCount = 0;
            HIRTypeRef tmpType; // Temporary type used for handling Deref
            const auto* currentTy = node.value->resType;
            ::std::vector<HIRTypeRef> derefResTypes;

            // TODO: autoderef_find_field?
            do {
                const auto* ty = this->context.revealOpaqueType(currentTy);
                if (ty->is_Infer()) {
                    DEBUG("Hit ivar, returning early");
                    return;
                }
                if (ty->is_Path() && ty->as_Path().binding.is_Unbound()) {
                    DEBUG("Hit unbound path, returning early");
                    return;
                }
                if (this->context.resolve.findField(node.span(), ty, fieldName, outType)) {
                    this->context.equateTypes(node.span(), node.resType, outType);
                    break;
                }

                derefCount += 1;
                currentTy = this->context.resolve.autoderef(node.span(), ty, tmpType);
                if (currentTy) {
                    derefResTypes.push_back(currentTy);
                }
            } while (currentTy);

            if (!currentTy) {
                ERROR(node.span(), E0000, "Couldn't find the field " << fieldName << " in " << this->context.ivars.fmtType(node.value->resType));
            }

            assert(derefCount == derefResTypes.size());
            for (unsigned int i = 0; i < derefResTypes.size(); i++) {
                auto ty = mv$(derefResTypes[i]);
                DEBUG("- Deref " << &*node.value << " -> " << ty);
                if (node.value->resType->is_Array()) {
                    BUG(node.span(), "Field access from array/slice?");
                }
                node.value = NEWNODE(mv$(ty), node.span(), Deref, mv$(node.value));
                context.ivars.getType(node.value->resType);
            }

            completed = true;
        }

        void visit(HIRExprNodeLiteral& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeUnitVariant& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodePathValue& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeVariable& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeConstParam& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeStructLiteral& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeTuple& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeArrayList& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeArraySized& node) override {
            if (cast<HIRExprNodeConstBlock>(node.val.get())) {
                completed = true;
                return;
            }
            if (const auto* path = cast<HIRExprNodePathValue>(node.val.get())) {
                if (path->target == HIRExprNodePathValue::CONSTANT) {
                    completed = true;
                    return;
                }
                // An associated const keeps `target == UNKNOWN`: only generic
                // paths are bound before type checking. A UFCS path in value
                // position is either such a const — exempt from `Copy` exactly
                // like a free const item — or a function item, whose type is
                // `Copy` anyway.
                if (!path->path.data.is_Generic()) {
                    completed = true;
                    return;
                }
            }

            bool requireCopy = false;
            if (node.size.is_Known()) {
                requireCopy = node.size.as_Known() > 1;
            } else {
                const auto& count = this->context.ivars.getValue(node.size.as_Unevaluated());
                if (count.is_Infer()) {
                    return;
                }
                if (count.is_Evaluated()) {
                    requireCopy = count.as_Evaluated()->readUsize(0) > 1;
                } else {
                    // A generic or unevaluated count can exceed one after
                    // monomorphisation, so its element must be Copy.
                    requireCopy = true;
                }
            }

            if (requireCopy) {
                this->context.addTraitBound(node.span(), node.val->resType, this->context.resolve.langCopy(), {});
            }
            completed = true;
        }

        void visit(HIRExprNodeClosure& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeGenerator& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeGeneratorWrapper& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeAsyncBlock& node) override {
            noRevisit(node);
        }

    private:
        void noRevisit(HIRExprNode& node) {
            BUG(node.span(), "Node revisit unexpected - " << typeid(node).name());
        }
    }; // class ExprVisitor_Revisit

    // -----------------------------------------------------------------------
    // Post-inferrence visitor
    // Saves the inferred types into the HIR expression tree, and ensures that
    // all types were inferred.
    // -----------------------------------------------------------------------
    class ExprVisitorApply: public HIRExprVisitorDef {
        const Context& context;
        const HMTypeInferrence& ivars;
        HIRPathParams nopImpl;
        HIRPathParams nopItem;

    public:
        ExprVisitorApply(const Context& context)
            : HIRExprVisitorDef(context.crate.types)
            , context(context)
            , ivars(context.ivars)
        {
            nopImpl = context.resolve.implGenerics().makeNopParams(context.crate.types, 0);
            nopItem = context.resolve.itemGenerics().makeNopParams(context.crate.types, 1);
        }

        void visitNodePtr(HIRExprPtr& nodePtr) {
            auto& node = *nodePtr;
            const char* nodeTy = typeid(node).name();

            TRACE_FUNCTION_FR(&node << " " << &node << " " << nodeTy << " : " << node.resType, nodeTy);
            this->checkTypeResolvedTop(node.span(), node.resType);
            DEBUG(nodeTy << " : = " << node.resType);

            nodePtr->visit(*this);

            for (auto& ty : nodePtr.bindings) {
                this->checkTypeResolvedTop(node.span(), ty);
            }

            for (auto& ty : nodePtr.erasedTypes) {
                this->checkTypeResolvedTop(node.span(), ty);
            }

            for (auto& ent : context.erasedTypeAliases) {
                auto t = ent.second.ourType;
                checkTypeResolved(node.span(), t, t);
                // If the type is for the same type alias, then ignore
                if (t->is_ErasedType() && t->as_ErasedType().inner.is_Alias() && t->as_ErasedType().inner.as_Alias().inner.get() == ent.first) {
                    continue;
                }
                // TODO: Enforce/validate that the parmas match this function's params, then convert method-level to type-level
                // - Get the path params used to construct this path in the first place, and then do a `clone_ty_with`
                auto ty = cloneTyWith(context.crate.types, node.span(), t, [&](const HIRTypeData* tpl, HIRTypeRef& outTy) -> bool {
                    for (size_t i = 0; i < ent.second.params.types.size(); i++) {
                        if (tpl == ent.second.params.types[i]) {
                            outTy = context.crate.types.generic(ent.first->generics.types.at(i).name, i);
                            return true;
                        }
                    }
                    return false;
                });
                {
                    auto p = ent.first->generics.makeNopParams(context.crate.types, 0);
                    MonomorphStatePtr(context.crate.types, nullptr, &p, nullptr).monomorphType(node.span(), ty);
                }
                if (ent.first->type == HIRTypeRef()) {
                    DEBUG("type " << ent.first->path << " = " << ty);
                    ent.first->type = std::move(ty);
                } else {
                    if (ent.first->type != ty) {
                        ERROR(node.span(), E0000, "Disagreement on type for " << ent.first->path << ": " << ent.first->type << " or " << ty);
                    }
                }
            }
        }

        void visitNodePtr(HIRExprNodeP& nodePtr) override {
            auto& node = *nodePtr;
            const char* nodeTy = typeid(node).name();
            TRACE_FUNCTION_FR(&nodePtr << " " << &node << " " << nodeTy << " : " << node.resType, &node << " " << nodeTy);
            this->checkTypeResolvedTop(node.span(), node.resType);
            DEBUG(nodeTy << " : = " << node.resType);
            HIRExprVisitorDef::visitNodePtr(nodePtr);
        }

        void visitPattern(const Span& sp, HIRPattern& pat) override {
            if (auto* deref = pat.data.opt_Deref()) {
                ASSERT_BUG(sp, deref->targetType, "Untyped deref pattern");
                this->checkTypeResolvedTop(sp, deref->targetType);
            }
            switch (pat.data.tag()) {
default:
                break;
                case HIRPatternData::TAG_Value: {
                    auto& e = pat.data.as_Value();
                    if (e.val.is_Named()) {
                        auto& ve = e.val.as_Named();
                        this->checkTypeResolvedPath(sp, ve.path);
                    }
                    break;
                }
                case HIRPatternData::TAG_Range: {
                    auto& e = pat.data.as_Range();
                    if (e.start && e.start->is_Named()) {
                        this->checkTypeResolvedPath(sp, e.start->as_Named().path);
                    }
                    if (e.end && e.end->is_Named()) {
                        this->checkTypeResolvedPath(sp, e.end->as_Named().path);
                    }
                    break;
                }
                case HIRPatternData::TAG_PathValue: {
                    auto& e = pat.data.as_PathValue();
                    this->checkTypeResolvedPath(sp, e.path);
                    break;
                }
                case HIRPatternData::TAG_PathTuple: {
                    auto& e = pat.data.as_PathTuple();
                    this->checkTypeResolvedPath(sp, e.path);
                    break;
                }
                case HIRPatternData::TAG_PathNamed: {
                    auto& e = pat.data.as_PathNamed();
                    this->checkTypeResolvedPath(sp, e.path);
                    break;
                }
            }
            HIRExprVisitorDef::visitPattern(sp, pat);
        }

        void visit(HIRExprNodeBlock& node) override {
            HIRExprVisitorDef::visit(node);
            if (node.valueNode) {
                checkTypesEqual(node.span(), node.resType, node.valueNode->resType);
            }
            // If the last node diverges (yields `!`) then this block can yield `!` (or anything)
            else if (node.diverges) {
            } else {
                // Non-diverging (empty, or with a non-diverging last node) blocks must yield `()`
                checkTypesEqual(node.span(), node.resType, context.crate.types.unit());
            }
        }

        void visit(HIRExprNodeLet& node) override {
            this->checkTypeResolvedTop(node.span(), node.type);
            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeClosure& node) override {
            for (auto& arg : node.args) {
                this->checkTypeResolvedTop(node.span(), arg.second);
            }
            this->checkTypeResolvedTop(node.span(), node.returnType);
            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeGenerator& node) override {
            this->checkTypeResolvedTop(node.span(), node.returnType);
            this->checkTypeResolvedTop(node.span(), node.yieldTy);
            this->checkTypeResolvedTop(node.span(), node.resumeTy);
            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeAsyncBlock& node) override {
            this->checkTypeResolvedTop(node.span(), node.returnType);
            if (node.isAsyncGen) {
                this->checkTypeResolvedTop(node.span(), node.yieldTy);
            }
            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeGeneratorWrapper& node) override {
            BUG(node.span(), "");
        }

        void visitCallcache(const Span& sp, HIRExprCallCache& cache) {
            for (auto& ty : cache.argTypes) {
                this->checkTypeResolvedTop(sp, ty);
            }
        }

        void visit(HIRExprNodeCallPath& node) override {
            this->visitCallcache(node.span(), node.cache);

            this->checkTypeResolvedPath(node.span(), node.path);
            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeCallMethod& node) override {
            this->visitCallcache(node.span(), node.cache);

            this->checkTypeResolvedPath(node.span(), node.methodPath);
            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeCallValue& node) override {
            for (auto& ty : node.argTypes) {
                this->checkTypeResolvedTop(node.span(), ty);
            }

            {
                const auto& ty = context.getType(node.value->resType);
                switch (node.traitUsed) {
                    // An async callable target was chosen during inference, and
                    // its future is already the result type.
                    case HIRExprNodeCallValue::TraitUsed::AsyncFn:
                    case HIRExprNodeCallValue::TraitUsed::AsyncFnMut:
                    case HIRExprNodeCallValue::TraitUsed::AsyncFnOnce:
                        HIRExprVisitorDef::visit(node);
                        return;
                    default:
                        break;
                }
                if (ty->is_NodeType() && ty->as_NodeType().is_Closure()) {
                    node.traitUsed = HIRExprNodeCallValue::TraitUsed::Unknown;
                } else if (/*const auto* e =*/ty->opt_Function()) {
                    node.traitUsed = HIRExprNodeCallValue::TraitUsed::Fn;
                } else {
                    // 1. Create a param set with a single tuple (of all argument types)
                    HIRPathParams traitPp;
                    {
                        ::std::vector<HIRTypeRef> argTypes;
                        for (const auto& argTy : node.argIvars) {
                            argTypes.push_back(this->context.getType(argTy));
                        }
                        traitPp.types.push_back(context.crate.types.tuple(mv$(argTypes)));
                    }

                    // 3. Locate the most permissive implemented Fn* trait (Fn first, then FnMut, then assume just FnOnce)
                    // NOTE: Borrowing is added by the expansion to CallPath
                    if (!this->context.resolve.langFn().components().empty() && this->context.resolve.findTraitImpls(node.span(), this->context.resolve.langFn(), traitPp, ty, [&](auto impl, auto cmp) {
                        return true;
                    })) {
                        DEBUG("-- Using Fn");
                        node.traitUsed = HIRExprNodeCallValue::TraitUsed::Fn;
                    } else if (!this->context.resolve.langFnMut().components().empty() && this->context.resolve.findTraitImpls(node.span(), this->context.resolve.langFnMut(), traitPp, ty, [&](auto impl, auto cmp) {
                        return true;
                    })) {
                        DEBUG("-- Using FnMut");
                        node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnMut;
                    } else {
                        DEBUG("-- Using FnOnce (default)");
                        node.traitUsed = HIRExprNodeCallValue::TraitUsed::FnOnce;
                    }
                }
            }

            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodePathValue& node) override {
            this->checkTypeResolvedPath(node.span(), node.path);
        }

        void visit(HIRExprNodeUnitVariant& node) override {
            this->checkTypeResolvedGenericpath(node.span(), node.path);
        }

        void visit(HIRExprNodeStructLiteral& node) override {
            this->checkTypeResolvedGenericpath(node.span(), node.realPath);
            for (auto& ty : node.valueTypes) {
                if (ty != HIRTypeRef()) {
                    this->checkTypeResolvedTop(node.span(), ty);
                }
            }

            const auto& sp = node.span();
            const auto& tyPath = node.realPath;
            const auto& ty = node.resType;
            ASSERT_BUG(sp, ty->is_Path(), "Result type of _StructLiteral isn't Path");
            const tStructFields* fieldsPtr = nullptr;
            {
                auto& tuMatch = ty->as_Path().binding;
                switch (tuMatch.tag()) {
                    case HIRTypePathBinding::TAG_Unbound: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Opaque: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Enum: {
                        auto& e = tuMatch.as_Enum();
                        const auto& varName = tyPath.path.components().back();
                        const auto& enm = *e;
                        auto idx = enm.findVariant(varName);
                        ASSERT_BUG(sp, idx != SIZE_MAX, "");
                        ASSERT_BUG(sp, enm.data.is_Data(), "");
                        const auto& var = enm.data.as_Data()[idx];

                        const auto& str = *var.type->as_Path().binding.as_Struct();
                        ASSERT_BUG(sp, var.isStruct, "Struct literal for enum on non-struct variant");
                        fieldsPtr = &str.data.as_Named();
                        break;
                    }
                    case HIRTypePathBinding::TAG_Union: {
                        auto& e = tuMatch.as_Union();
                        fieldsPtr = &e->variants;
                        ASSERT_BUG(node.span(), node.values.size() > 0, "Union with no values");
                        ASSERT_BUG(node.span(), node.values.size() == 1, "Union with multiple values");
                        ASSERT_BUG(node.span(), !node.baseValue, "Union can't have a base value");
                        break;
                    }
                    case HIRTypePathBinding::TAG_ExternType: {
                        BUG(sp, "ExternType in StructLiteral");
                        break;
                    }
                    case HIRTypePathBinding::TAG_Struct: {
                        auto& e = tuMatch.as_Struct();
                        if (e->data.is_Unit()) {
                            ASSERT_BUG(node.span(), node.values.size() == 0, "Values provided for unit-like struct");
                            ASSERT_BUG(node.span(), !node.baseValue, "Values provided for unit-like struct");
                            return;
                        }
                        if (e->data.is_Tuple()) {
                            // `S { 0: a, ..base }` names the tuple fields by index.
                            ASSERT_BUG(node.span(), node.baseValue, "Tuple struct literal has no values or base");
                            HIRExprVisitorDef::visit(node);
                            return;
                        }

                        ASSERT_BUG(node.span(), e->data.is_Named(), "StructLiteral not pointing to a braced struct, instead " << e->data.tagStr() << " - " << ty);
                        fieldsPtr = &e->data.as_Named();
                        break;
                    }
                }
            }
            ASSERT_BUG(node.span(), fieldsPtr, "Didn't get field for path in _StructLiteral - " << ty);
            const tStructFields& fields = *fieldsPtr;
            for(const auto& fld : fields) {
                DEBUG(fld.name << ": " << fld.ty);
            }

            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeTupleVariant& node) override {
            this->checkTypeResolvedPp(node.span(), node.path.params, HIRTypeRef());
            for (auto& ty : node.argTypes) {
                if (ty != HIRTypeRef()) {
                    this->checkTypeResolvedTop(node.span(), ty);
                }
            }

            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeLiteral& node) override {
            const HIRTypeData* literalType = node.resType;
            if (const auto* pattern = literalType->opt_Pattern()) {
                literalType = pattern->inner;
            }
            switch (node.data.tag()) {
                case HIRExprNodeLiteral::Data::TAG_Integer: {
                    auto& e = node.data.as_Integer();
                    ASSERT_BUG(node.span(), literalType->is_Primitive(), "Integer _Literal didn't return primitive-backed type - " << node.resType); e.type = literalType->as_Primitive();
                    break;
                }
                case HIRExprNodeLiteral::Data::TAG_Float: {
                    auto& e = node.data.as_Float();
                    ASSERT_BUG(node.span(), literalType->is_Primitive(), "Float Literal didn't return primitive-backed type - " << node.resType); e.type = literalType->as_Primitive();
                    break;
                }
                case HIRExprNodeLiteral::Data::TAG_Boolean: {
                    break;
                }
                case HIRExprNodeLiteral::Data::TAG_ByteString: {
                    break;
                }
                case HIRExprNodeLiteral::Data::TAG_CString: {
                    break;
                }
                case HIRExprNodeLiteral::Data::TAG_String: {
                    break;
                }
            }
        }

        void visit(HIRExprNodeCast& node) override {
            this->checkTypeResolvedTop(node.span(), node.dstType);
            HIRExprVisitorDef::visit(node);
        }

        void visit(HIRExprNodeUnsize& node) override {
            this->checkTypeResolvedTop(node.span(), node.dstType);
            HIRExprVisitorDef::visit(node);
        }

    private:
        void checkTypeResolvedTop(const Span& sp, HIRTypeRef& ty) const {
            checkTypeResolved(sp, ty, ty);
            ty = this->context.resolve.expandAssociatedTypes(sp, mv$(ty));
            DEBUG(ty);
        }

        void checkTypeResolvedConstgeneric(const Span& sp, HIRConstGeneric& v, const HIRTypeData* topType) const {
            if (v.is_Infer()) {
                auto val = ivars.getValue(v).clone();
                ASSERT_BUG(sp, !val.is_Infer(), "Failure to infer " << v << " in " << topType);
                v = std::move(val);
            }
        }

        void checkTypeResolvedPp(const Span& sp, HIRPathParams& pp, const HIRTypeData* topType) const {
            for (auto& ty : pp.types) {
                checkTypeResolved(sp, ty, topType);
            }
            for (auto& val : pp.values) {
                checkTypeResolvedConstgeneric(sp, val, topType);
            }
        }

        void checkTypeResolvedPath(const Span& sp, HIRPath& path) const {
            auto tmp = context.crate.types.path(path.clone(), {});
            checkTypeResolvedPath(sp, path, tmp);
            switch (path.data.tag()) {
                case HIRPath::Data::TAG_Generic: {
                    auto& pe = path.data.as_Generic();
                    for (auto& ty : pe.params.types) ty = this->context.resolve.expandAssociatedTypes(sp, mv$(ty));
                    break;
                }
                case HIRPath::Data::TAG_UfcsInherent: {
                    auto& pe = path.data.as_UfcsInherent();
                    pe.type = this->context.resolve.expandAssociatedTypes(sp, mv$(pe.type)); for (auto& ty : pe.params.types) ty = this->context.resolve.expandAssociatedTypes(sp, mv$(ty)); for (auto& ty : pe.implParams.types) ty = this->context.resolve.expandAssociatedTypes(sp, mv$(ty));
                    break;
                }
                case HIRPath::Data::TAG_UfcsKnown: {
                    auto& pe = path.data.as_UfcsKnown();
                    pe.type = this->context.resolve.expandAssociatedTypes(sp, mv$(pe.type)); for (auto& ty : pe.params.types) ty = this->context.resolve.expandAssociatedTypes(sp, mv$(ty)); for (auto& ty : pe.trait.params.types) ty = this->context.resolve.expandAssociatedTypes(sp, mv$(ty));
                    break;
                }
                case HIRPath::Data::TAG_UfcsUnknown: {
                    throw "";
                }
            }
        }

        void checkTypeResolvedPath(const Span& sp, HIRPath& path, const HIRTypeData* topType) const {
            switch (path.data.tag()) {
                case HIRPath::Data::TAG_Generic: {
                    auto& pe = path.data.as_Generic();
                    checkTypeResolvedPp(sp, pe.params, topType);
                    break;
                }
                case HIRPath::Data::TAG_UfcsInherent: {
                    auto& pe = path.data.as_UfcsInherent();
                    checkTypeResolved(sp, pe.type, topType); checkTypeResolvedPp(sp, pe.params, topType); checkTypeResolvedPp(sp, pe.implParams, topType);
                    break;
                }
                case HIRPath::Data::TAG_UfcsKnown: {
                    auto& pe = path.data.as_UfcsKnown();
                    checkTypeResolved(sp, pe.type, topType); checkTypeResolvedPp(sp, pe.trait.params, topType); checkTypeResolvedPp(sp, pe.params, topType);
                    break;
                }
                case HIRPath::Data::TAG_UfcsUnknown: {
                    ERROR(sp, E0000, "UfcsUnknown " << path << " left in " << topType);
                    break;
                }
            }
        }

        void checkTypeResolvedGenericpath(const Span& sp, HIRGenericPath& path) const {
            auto tmp = context.crate.types.path(path.clone(), {});
            checkTypeResolvedPp(sp, path.params, tmp);
        }

        void checkTypeResolved(const Span& sp, HIRTypeRef& ty, const HIRTypeData* topType) const {
            class InnerVisitor: public HIRVisitor {
                const ExprVisitorApply& parent;
                const Span& sp;
                const HIRTypeData* topType;

            public:
                InnerVisitor(const ExprVisitorApply& parent, const Span& sp, const HIRTypeData* topType)
                    : HIRVisitor(nullptr, parent.context.crate.types)
                    , parent(parent)
                    , sp(sp)
                    , topType(topType)
                {
                }

                void visitPath(HIRPath& path, HIRVisitor::PathContext pc) override {
                    if (path.data.is_UfcsUnknown()) {
                        ERROR(sp, E0000, "UfcsUnknown " << path << " left in " << topType);
                    }
                    HIRVisitor::visitPath(path, pc);
                }

                void visitConstgeneric(HIRConstGeneric& v) override {
                    if (v.is_Infer()) {
                        auto val = parent.ivars.getValue(v).clone();
                        DEBUG(v << " -> " << val);
                        v = std::move(val);
                    }
                    HIRVisitor::visitConstgeneric(v);
                }

                [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
                    if (ty->is_Infer()) {
                        auto newTy = parent.ivars.getType(ty);
                        DEBUG(ty << " -> " << newTy);
                        // - Move over before checking, so that the source type mentions the correct ivar
                        ty = mv$(newTy);
                        if (ty->is_Infer()) {
                            ERROR(sp, E0000, "Failed to infer type " << ty << " in " << topType);
                        }
                    }

                    ty = visitTypeDefaultViaHooks(ty);

                    if (ty->is_Array()) {
                        auto data = ty->cloneData();
                        auto& size = data.as_Array().size;
                        if (size.is_Unevaluated()) {
                            if (size.as_Unevaluated().is_Evaluated()) {
                                DEBUG("Known size: " << size);
                                size = size.as_Unevaluated().as_Evaluated()->readUsize(0);
                                ty = parent.context.crate.types.intern(std::move(data));
                            }
                        }
                    }
                    return ty;
                }
            };

            InnerVisitor v(*this, sp, topType);
            ty = v.visitType(ty);
        }

        void checkTypesEqual(const Span& sp, const HIRTypeData* l, const HIRTypeData* r) const {
            DEBUG(sp << " - " << l << " == " << r);
            if (r->is_Diverge()) {
                // Diverge on the right is always valid
                // - NOT on the left, because `!` can become everything, but nothing can become `!`
            } else if (l != r && !l->equalsIgnoringRegions(r)) {
                ERROR(sp, E0000, "Type mismatch\n - " << l << "\n!= " << r);
            } else {
                // All good
            }
        }
        void visit(HIRExprNodeArraySized& node) override {
            HIRExprVisitorDef::visit(node);
            // An inferred `[val; _]` length only exists in the ivar table:
            // write it back so MIR lowering and codegen see a concrete size.
            if (node.size.is_Unevaluated() && node.size.as_Unevaluated().is_Infer()) {
                auto count = ivars.getValue(node.size.as_Unevaluated()).clone();
                ASSERT_BUG(node.span(), !count.is_Infer(), "Failure to infer the length of " << node.resType);
                if (count.is_Evaluated()) {
                    node.size = HIRArraySize::make_Known(count.as_Evaluated()->readUsize(0));
                } else {
                    // A generic length (`[0; _]` inside `fn f<const N: usize>()`)
                    // stays symbolic until monomorphisation.
                    node.size = HIRArraySize(std::move(count));
                }
            }
        }

    }; // class ExprVisitor_Apply

    class ExprVisitorPrint: public HIRExprVisitor {
        const Context& context;
        ::std::ostream& os;

    public:
        ExprVisitorPrint(const Context& context, ::std::ostream& os)
            : context(context)
            , os(os)
        {
        }

        void visit(HIRExprNodeBlock& node) override {
            os << "_Block {" << context.ivars.fmtType(node.nodes.back()->resType) << "}";
        }

        void visit(HIRExprNodeConstBlock& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeAsm& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeAsm2& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeReturn& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeYield& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeAWait& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeUse& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeLet& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeLoop& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeLoopControl& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeMatch& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeAssign& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeBinOp& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeUniOp& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeBorrow& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeRawBorrow& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeCast& node) override {
            os << "_Cast {" << context.ivars.fmtType(node.value->resType) << "}";
        }

        void visit(HIRExprNodeUnsize& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeIndex& node) override {
            os << "_Index {" << fmtResTy(*node.value) << "}[{" << fmtResTy(*node.index) << "}]";
        }

        void visit(HIRExprNodeDeref& node) override {
            os << "_Deref {" << fmtResTy(*node.value) << "}";
        }

        void visit(HIRExprNodeEmplace& node) override {
            os << "_Emplace(" << fmtResTy(*node.value) << " in " << fmtResTy(*node.place) << ")";
        }

        void visit(HIRExprNodeTupleVariant& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeCallPath& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeCallValue& node) override {
            os << "_CallValue {" << fmtResTy(*node.value) << "}(";
            for (const auto& arg : node.args) {
                os << "{" << fmtResTy(*arg) << "}, ";
            }
            os << ")";
        }

        void visit(HIRExprNodeCallMethod& node) override {
            os << "_CallMethod {" << fmtResTy(*node.value) << "}." << node.method << "(";
            for (const auto& arg : node.args) {
                os << "{" << fmtResTy(*arg) << "}, ";
            }
            os << ")";
        }

        void visit(HIRExprNodeField& node) override {
            os << "_Field {" << fmtResTy(*node.value) << "}." << node.field;
        }

        void visit(HIRExprNodeLiteral& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeUnitVariant& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodePathValue& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeVariable& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeConstParam& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeStructLiteral& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeTuple& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeArrayList& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeArraySized& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeClosure& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeGenerator& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeGeneratorWrapper& node) override {
            noRevisit(node);
        }

        void visit(HIRExprNodeAsyncBlock& node) override {
            noRevisit(node);
        }

    private:
        HMTypeInferrence::FmtType fmtResTy(const HIRExprNode& n) {
            return context.ivars.fmtType(n.resType);
        }

        void noRevisit(HIRExprNode& n) {
            throw "";
        }
    }; // class ExprVisitor_Print
}

void Context::dump() const {
    DEBUG("--- Variables");
    for (unsigned int i = 0; i < bindings.size(); i++) {
        DEBUG(i << " " << bindings[i].name << ": " << this->ivars.fmtType(bindings[i].ty));
    }
    DEBUG("--- Ivars");
    ivars.dump();
    DEBUG("--- CS Context - " << linkCoerce.size() << " Coercions, " << linkAssoc.size() << " associated, " << toVisit.size() << " nodes, " << advRevisits.size() << " callbacks");
    for (const auto& vp : linkCoerce) {
        const auto& v = *vp;
        DEBUG("R" << v.ruleIdx << " " << this->ivars.fmtType(v.leftTy) << " := " << v.rightNodePtr << " " << &**v.rightNodePtr << " (" << this->ivars.fmtType((*v.rightNodePtr)->resType) << ")");
    }
    for (const auto& v : linkAssoc) {
        DEBUG(v);
    }
    for (const auto& v : toVisit) {
        DEBUG(
            &*v << " "
                << FMT_CB(
                       os,
                       {
                           ExprVisitorPrint ev(*this, os);
                           v->visit(ev);
                       }
                   )
                << " -> " << this->ivars.fmtType(v->resType)
        );
    }
    for (const auto& v : advRevisits) {
        DEBUG(FMT_CB(ss, v->fmt(ss);));
    }
    DEBUG("---");
}

void Context::equateTypes(const Span& sp, const HIRTypeData* li, const HIRTypeData* ri) {
    const auto& liRes = this->ivars.getType(li);
    const auto& riRes = this->ivars.getType(ri);
    if (li == ri || liRes == riRes || liRes->equalsIgnoringRegions(riRes)) {
        DEBUG(li << " == " << ri);
        return;
    }

    // Instantly apply equality
    TRACE_FUNCTION_F(li << " == " << ri);

    ASSERT_BUG(sp, !typeContainsImplPlaceholder(crate.types, ri), "Type contained an impl placeholder parameter - " << ri);
    ASSERT_BUG(sp, !typeContainsImplPlaceholder(crate.types, li), "Type contained an impl placeholder parameter - " << li);

    HIRTypeRef lTmp;
    HIRTypeRef rTmp;
    const auto& lT = this->resolve.expandAssociatedTypes(sp, this->ivars.getType(li), lTmp);
    const auto& rT = this->resolve.expandAssociatedTypes(sp, this->ivars.getType(ri), rTmp);

    // Strip HRLs, just in case

    if (lT->is_Diverge() && !rT->is_Infer()) {
        return;
    }
    if (rT->is_Diverge() && !lT->is_Infer()) {
        return;
    }

    equateTypesInner(sp, lT, rT);
}

void Context::equateTypesInner(const Span& sp, const HIRTypeData* li, const HIRTypeData* ri) {
    const auto& liRes = this->ivars.getType(li);
    const auto& riRes = this->ivars.getType(ri);
    if (li == ri || liRes == riRes || liRes->equalsIgnoringRegions(riRes)) {
        return;
    }

    // Check if the type contains a replacable associated type
    HIRTypeRef lTmp;
    HIRTypeRef rTmp;
    const auto& lT = this->resolve.expandAssociatedTypes(sp, this->ivars.getType(li), lTmp);
    const auto& rT = this->resolve.expandAssociatedTypes(sp, this->ivars.getType(ri), rTmp);
    if (lT == rT || lT->equalsIgnoringRegions(rT)) {
        return;
    }

    // AliasRelate first tries to normalise projections.  If both sides stay
    // as the same rigid projection with a Self inference variable, defer the
    // structural Self relation until ordinary constraints have had a chance
    // to resolve it.  Eagerly assigning Self here treats associated types as
    // injective and can select an arbitrary type whose associated output
    // happens to match; at fallback the expected function signature can still
    // guide an otherwise free Self.
    const auto* lPath = lT->opt_Path();
    const auto* rPath = rT->opt_Path();
    const auto* lProjection = lPath ? lPath->path.data.opt_UfcsKnown() : nullptr;
    const auto* rProjection = rPath ? rPath->path.data.opt_UfcsKnown() : nullptr;
    const bool lRigidProjection = lPath && (lPath->binding.is_Unbound() || lPath->binding.is_Opaque());
    const bool rRigidProjection = rPath && (rPath->binding.is_Unbound() || rPath->binding.is_Opaque());
    const auto typesMayRelate = [&](const HIRTypeData* left, const HIRTypeData* right) {
        if (left == right || left->equalsIgnoringRegions(right)) {
            return true;
        }
        return this->ivars.getType(left)->is_Infer() || this->ivars.getType(right)->is_Infer();
    };
    const auto paramsMayRelate = [&](const HIRPathParams& left, const HIRPathParams& right) {
        if (left.types.size() != right.types.size() || left.values.size() != right.values.size()) {
            return false;
        }
        for (size_t i = 0; i < left.types.size(); i++) {
            if (left.types[i] != right.types[i] && !left.types[i]->equalsIgnoringRegions(right.types[i])) {
                return false;
            }
        }
        for (size_t i = 0; i < left.values.size(); i++) {
            if (left.values[i] != right.values[i]) {
                return false;
            }
        }
        return true;
    };
    if (lRigidProjection && rRigidProjection && lProjection && rProjection
        && lProjection->trait.path == rProjection->trait.path
        && lProjection->item == rProjection->item
        && (this->ivars.getType(lProjection->type)->is_Infer() || this->ivars.getType(rProjection->type)->is_Infer())
        && typesMayRelate(lProjection->type, rProjection->type)
        && paramsMayRelate(lProjection->trait.params, rProjection->trait.params)
        && paramsMayRelate(lProjection->params, rProjection->params)) {
        struct DeferredRigidProjectionSelf final: Revisitor {
            Span sp;
            HIRTypeRef leftAlias;
            HIRTypeRef rightAlias;
            HIRTypeRef leftSelf;
            HIRTypeRef rightSelf;

            DeferredRigidProjectionSelf(Span sp, HIRTypeRef leftAlias, HIRTypeRef rightAlias, HIRTypeRef leftSelf, HIRTypeRef rightSelf)
                : sp(mv$(sp))
                , leftAlias(leftAlias)
                , rightAlias(rightAlias)
                , leftSelf(leftSelf)
                , rightSelf(rightSelf)
            {
            }

            const Span& span() const override {
                return sp;
            }

            void fmt(::std::ostream& os) const override {
                os << "Deferred rigid projection self " << leftSelf << " = " << rightSelf;
            }

            bool revisit(Context& context, bool isFallback) override {
                const auto left = context.ivars.getType(leftSelf);
                const auto right = context.ivars.getType(rightSelf);
                if (!left->is_Infer() && !right->is_Infer()) {
                    context.equateTypes(sp, leftAlias, rightAlias);
                    return true;
                }
                if (!isFallback) {
                    return false;
                }
                context.equateTypes(sp, left, right);
                return true;
            }
        };
        this->addRevisitAdv(box$((DeferredRigidProjectionSelf(
            sp,
            lT,
            rT,
            lProjection->type,
            rProjection->type
        ))));
        return;
    }

    auto bindInferToAlias = [&](const HIRTypeData* infer, const HIRTypeData* alias) {
        const auto* inferData = infer->opt_Infer();
        // Keep an unresolved projection containing ivars as an AliasRelate
        // goal.  Eagerly assigning it to the output ivar can hide that the
        // projection's self is subsequently equated with that same output,
        // turning a solvable deferred cycle into an opaque ivar binding.
        if (!inferData || inferData->isLit() || this->resolve.typeContainsIvars(alias) || visitTyWith(alias, [&](const HIRTypeData* inner) {
            return inner == infer;
        })) {
            return false;
        }
        const auto ivarIdx = inferData->index;
        if (ivarIdx < ivarsSized.size() && ivarsSized.at(ivarIdx)) {
            this->requireSized(sp, alias);
        }
        this->ivars.setIvarTo(ivarIdx, alias);
        return true;
    };

    // If either side is still a UfcsKnown after `expand_associated_types`, then emit an assoc bound instead of damaging ivars
    if (const auto* rE = rT->opt_Path()) {
        if (const auto* rpe = rE->path.data.opt_UfcsKnown()) {
            if (rE->binding.is_Unbound()) {
                if (bindInferToAlias(lT, rT)) {
                    return;
                }
                this->equateTypesAssoc(sp, lT, rpe->trait.path, rpe->trait.params.clone(), rpe->type, rpe->item.c_str(), rpe->params, false);
                return;
            }
        }
    }
    if (const auto* lE = lT->opt_Path()) {
        if (const auto* lpe = lE->path.data.opt_UfcsKnown()) {
            if (lE->binding.is_Unbound()) {
                if (bindInferToAlias(rT, lT)) {
                    return;
                }
                this->equateTypesAssoc(sp, rT, lpe->trait.path, lpe->trait.params.clone(), lpe->type, lpe->item.c_str(), lpe->params, false);
                return;
            }
        }
    }

    // Relating two applications of the same opaque alias constrains the
    // alias arguments.  This has to precede the defining-scope handling
    // below: neither application is the hidden type of the other.
    if (const auto* lErased = lT->opt_ErasedType()) {
        if (const auto* rErased = rT->opt_ErasedType()) {
            const auto* lAlias = lErased->inner.opt_Alias();
            const auto* rAlias = rErased->inner.opt_Alias();
            if (lAlias && rAlias && lAlias->inner == rAlias->inner) {
                ASSERT_BUG(sp, lAlias->params.types.size() == rAlias->params.types.size(), "Opaque alias type argument count mismatch");
                ASSERT_BUG(sp, lAlias->params.values.size() == rAlias->params.values.size(), "Opaque alias const argument count mismatch");
                for (size_t i = 0; i < lAlias->params.types.size(); i++) {
                    equateTypesInner(sp, lAlias->params.types[i], rAlias->params.types[i]);
                }
                for (size_t i = 0; i < lAlias->params.values.size(); i++) {
                    equateValues(sp, lAlias->params.values[i], rAlias->params.values[i]);
                }
                return;
            }
        }
    }

    auto equateErasedAlias = [&](const HIRTypeDataErasedType& erased, const auto& alias, const HIRTypeData* hiddenType) {
        if (!resolve.isOpaqueAliasDefiningScope(*alias.inner)) {
            return false;
        }

        auto inserted = this->erasedTypeAliases.insert(std::make_pair(alias.inner.get(), Context::TaitEntry{alias.params, hiddenType}));
        if (!inserted.second) {
            equateTypesInner(sp, inserted.first->second.ourType, hiddenType);
            return true;
        }

        struct MonomorphErasedSelf: MonomorphiserNop {
            const HIRTypeData* hiddenType;

            MonomorphErasedSelf(HIRTypeInterner& types, const HIRTypeData* hiddenType)
                : MonomorphiserNop(types)
                , hiddenType(hiddenType)
            {
            }

            HIRTypeRef getType(const Span&, const HIRGenericRef& type) const override {
                if (type.binding == GENERICErasedSelf) {
                    return hiddenType;
                }
                return types.generic(type.name, type.binding);
            }
        } monomorph{crate.types, hiddenType};

        for (const auto& trait : erased.traits) {
            auto traitMono = monomorph.monomorphTraitpath(sp, trait, false);
            if (traitMono.typeBounds.empty()) {
                equateTypesAssoc(sp, crate.types.infer(), traitMono.path.path, traitMono.path.params.clone(), hiddenType, "", {}, false);
                continue;
            }
            for (const auto& aty : traitMono.typeBounds) {
                equateTypesAssoc(sp, aty.second.type, aty.second.sourceTrait.path, aty.second.sourceTrait.params.clone(), hiddenType, aty.first.c_str(), aty.second.atyParams, false);
            }
        }
        return true;
    };

    if (const auto* et = rT->opt_ErasedType()) {
        if (const auto* ee = et->inner.opt_Alias()) {
            if (!lT->is_Infer() && equateErasedAlias(*et, *ee, lT)) {
                return;
            }
        }
    }
    if (const auto* et = lT->opt_ErasedType()) {
        if (const auto* ee = et->inner.opt_Alias()) {
            if (equateErasedAlias(*et, *ee, rT)) {
                return;
            }
        }
    }

    const auto* lRevealed = revealOpaqueType(lT);
    const auto* rRevealed = revealOpaqueType(rT);
    if (lRevealed != lT || rRevealed != rT) {
        equateTypesInner(sp, lRevealed, rRevealed);
        return;
    }

    auto setIvar = [&](const HIRTypeData* dst, const HIRTypeData* src) {
        auto ivarIdx = dst->as_Infer().index;
        if (ivarIdx < ivarsSized.size() && ivarsSized.at(ivarIdx)) {
            this->requireSized(sp, src);
        }
        // Ensure no HRLs
        if (visitTyWith(src, [&](const HIRTypeData* ity) {
            return ity == dst;
        })) {
            DEBUG("Start of a loop detected: rewrite");
            // Ensure that there's an unexpanded ATY in here (containing the ivar)
            // Replace the ATY with a new ivar
            // Equate this ivar with the updated type
            // Add an ATY equality rule for the new ivar
            // - `_0 = Ty< <_0 as Foo>::Type >`
            // becomes
            // - `_0 = Ty<_1>`
            // - `<_0 as Foo>::Type = _1`
            auto newSrc = cloneTyWith(crate.types, sp, src, [&](const HIRTypeData* tpl, HIRTypeRef& outTy) -> bool {
                if (tpl->is_Path() && tpl->as_Path().binding.is_Unbound()) {
                    if (visitTyWith(src, [&](const HIRTypeData* ity) {
                        return ity == dst;
                    })) {
                        const auto& pe = tpl->as_Path().path.data.as_UfcsKnown();
                        outTy = this->ivars.newIvarTr();
                        this->equateTypesAssoc(sp, outTy, pe.trait.path, pe.trait.params.clone(), pe.type, pe.item.c_str(), pe.params, false);
                        return true;
                    } else {
                    }
                }
                return false;
            });
            ASSERT_BUG(
                sp,
                !visitTyWith(
                    newSrc,
                    [&](const HIRTypeData* ity) {
                return ity == dst;
            }
                ),
                ""
            );
            this->ivars.setIvarTo(ivarIdx, std::move(newSrc));
        } else {
            this->ivars.setIvarTo(ivarIdx, src);
        }
    };

    DEBUG("- l_t = " << lT << ", r_t = " << rT);
    if (const auto* rE = rT->opt_Infer()) {
        if (const auto* lE = lT->opt_Infer()) {
            // If both are infer, unify the two ivars (alias right to point to left)
            // TODO: Unify sized flags

            if ((rE->index < ivarsSized.size() && ivarsSized.at(rE->index)) || (lE->index < ivarsSized.size() && ivarsSized.at(lE->index))) {
                this->requireSized(sp, lT);
                this->requireSized(sp, rT);
            }

            this->ivars.ivarUnify(lE->index, rE->index);
        } else {
            // Righthand side is infer, alias it to the left
            setIvar(rT, lT);
        }
    } else {
        if (/*const auto* l_e =*/lT->opt_Infer()) {
            // Lefthand side is infer, alias it to the right
            setIvar(lT, rT);
        } else {
            // Helper function for Path and TraitObject
            auto equalityTypeparams = [&](const HIRPathParams& l, const HIRPathParams& r) {
                if (l.types.size() != r.types.size()) {
                    ERROR(sp, E0000, "Type mismatch in path params (type count) `" << l << "` and `" << r << "`");
                }
                for (unsigned int i = 0; i < l.types.size(); i++) {
                    this->equateTypesInner(sp, l.types[i], r.types[i]);
                }

                if (l.values.size() != r.values.size()) {
                    ERROR(sp, E0000, "Type mismatch in path params (value count) `" << l << "` and `" << r << "`");
                }
                for (unsigned int i = 0; i < l.values.size(); i++) {
                    this->equateValues(sp, l.values[i], r.values[i]);
                }
            };
            auto equalityPath = [&](const HIRPath& l, const HIRPath& r) -> bool {
                if (l.data.tag() != r.data.tag()) {
                    return false;
                }
                switch (l.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        auto& lpe = l.data.as_Generic();
                        auto& rpe = r.data.as_Generic();
                        if (lpe.path != rpe.path) {
                            return false;
                        }
                        equalityTypeparams(lpe.params, rpe.params);
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        auto& lpe = l.data.as_UfcsInherent();
                        auto& rpe = r.data.as_UfcsInherent();
                        equalityTypeparams(lpe.params, rpe.params);
                        if (lpe.item != rpe.item) {
                            return false;
                        }
                        this->equateTypesInner(sp, lpe.type, rpe.type);
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        auto& lpe = l.data.as_UfcsKnown();
                        auto& rpe = r.data.as_UfcsKnown();
                        if (lpe.trait.path != rpe.trait.path || lpe.item != rpe.item) {
                            return false;
                        }
                        equalityTypeparams(lpe.trait.params, rpe.trait.params);
                        equalityTypeparams(lpe.params, rpe.params);
                        this->equateTypesInner(sp, lpe.type, rpe.type);
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        auto& lpe = l.data.as_UfcsUnknown();
                        auto& rpe = r.data.as_UfcsUnknown();
                        // TODO: If the type is fully known, locate a suitable trait item
                        equalityTypeparams(lpe.params, rpe.params);
                        if (lpe.item != rpe.item) {
                            return false;
                        }
                        this->equateTypesInner(sp, lpe.type, rpe.type);
                        break;
                    }
                }
                return true;
            };

            // If either side is !, return early
            // TODO: Should ! end up in an ivar?
            if (lT->is_Diverge() && rT->is_Diverge()) {
                return;
            }

            else if (rT->is_Diverge()) {
                if (const auto* rE = ri->opt_Infer()) {
                    this->ivars.setIvarTo(rE->index, lT);
                }
                return;
            } else {
            }

            if (lT->tag() != rT->tag()) {
                ERROR(sp, E0000, "Type mismatch between " << this->ivars.fmtType(lT) << " and " << this->ivars.fmtType(rT));
            }
            switch ((*lT).tag()) {
                case HIRTypeData::TAG_Infer: {
                    throw "";
                }
                case HIRTypeData::TAG_Diverge: {
                    // ignore?
                    break;
                }
                case HIRTypeData::TAG_Primitive: {
                    auto& lE = (*lT).as_Primitive();
                    auto& rE = (*rT).as_Primitive();
                    if (lE != rE) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    break;
                }
                case HIRTypeData::TAG_Path: {
                    auto& lE = (*lT).as_Path();
                    auto& rE = (*rT).as_Path();
                    if (!equalityPath(lE.path, rE.path)) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    break;
                }
                case HIRTypeData::TAG_Generic: {
                    auto& lE = (*lT).as_Generic();
                    auto& rE = (*rT).as_Generic();
                    if (lE.binding != rE.binding) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    break;
                }
                case HIRTypeData::TAG_TraitObject: {
                    auto& lE = (*lT).as_TraitObject();
                    auto& rE = (*rT).as_TraitObject();
                    if (lE.trait.path.path != rE.trait.path.path) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    equalityTypeparams(lE.trait.path.params, rE.trait.path.params);
                    for (auto itL = lE.trait.typeBounds.begin(), itR = rE.trait.typeBounds.begin(); itL != lE.trait.typeBounds.end(); itL++, itR++) {
                        if (itL->first != itR->first) {
                            ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - associated bounds differ");
                        }
                        this->equateTypesInner(sp, itL->second.type, itR->second.type);
                    }
                    if (lE.markers.size() != rE.markers.size()) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - trait counts differ");
                    }
                    // TODO: Is this list sorted in any way? (if it's not sorted, this could fail when source does Send+Any instead of Any+Send)
                    for (unsigned int i = 0; i < lE.markers.size(); i++) {
                        auto& lP = lE.markers[i];
                        auto& rP = rE.markers[i];
                        if (lP.path != rP.path) {
                            ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                        }
                        equalityTypeparams(lP.params, rP.params);
                    }
                    // NOTE: Lifetime is ignored
                    break;
                }
                case HIRTypeData::TAG_ErasedType: {
                    auto& lE = (*lT).as_ErasedType();
                    auto& rE = (*rT).as_ErasedType();
                    if (lE.inner.tag() != rE.inner.tag()) {
                            ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - different erased class");
                        }
                    switch (lE.inner.tag()) {
                        case TypeDataErasedTypeInner::TAG_Fcn: {
                            auto& lee = lE.inner.as_Fcn();
                            auto& ree = rE.inner.as_Fcn();
                            ASSERT_BUG(sp, lee.origin != HIRSimplePath(), "ErasedType " << lT << " wasn't bound to its origin");
                            ASSERT_BUG(sp, ree.origin != HIRSimplePath(), "ErasedType " << rT << " wasn't bound to its origin");
                            if (!equalityPath(lee.origin, ree.origin)) {
                                ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - different source");
                            }
                            break;
                        }
                        case TypeDataErasedTypeInner::TAG_Alias: {
                            auto& lee = lE.inner.as_Alias();
                            auto& ree = rE.inner.as_Alias();
                            if (lee.inner != ree.inner) {
                                ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - different source");
                            }
                            equalityTypeparams(lee.params, ree.params);
                            break;
                        }
                        case TypeDataErasedTypeInner::TAG_Known: {
                            auto& lee = lE.inner.as_Known();
                            auto& ree = rE.inner.as_Known();
                            equateTypesInner(sp, lee, ree);
                            break;
                        }
                    }
                    break;
                }
                case HIRTypeData::TAG_Array: {
                    auto& lE = (*lT).as_Array();
                    auto& rE = (*rT).as_Array();
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    if (lE.size != rE.size) {
                        if (lE.size.is_Unevaluated() || rE.size.is_Unevaluated()) {
                            // Handle one side being fully-known
                            if (!lE.size.is_Unevaluated()) {
                                assert(lE.size.is_Known());
                                assert(rE.size.is_Unevaluated());
                                this->equateValues(sp, freezeEncodedLiteral(EncodedLiteral::makeUsize(lE.size.as_Known())), rE.size.as_Unevaluated());
                            } else if (!rE.size.is_Unevaluated()) {
                                assert(lE.size.is_Unevaluated());
                                assert(rE.size.is_Known());
                                this->equateValues(sp, lE.size.as_Unevaluated(), freezeEncodedLiteral(EncodedLiteral::makeUsize(rE.size.as_Known())));
                            } else {
                                this->equateValues(sp, lE.size.as_Unevaluated(), rE.size.as_Unevaluated());
                            }
                        } else {
                            ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - sizes differ");
                        }
                    }
                    break;
                }
                case HIRTypeData::TAG_Slice: {
                    auto& lE = (*lT).as_Slice();
                    auto& rE = (*rT).as_Slice();
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    break;
                }
                case HIRTypeData::TAG_Pattern: {
                    auto& lE = (*lT).as_Pattern();
                    auto& rE = (*rT).as_Pattern();
                    if (lE.pattern.alternatives.size() != rE.pattern.alternatives.size()) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - pattern alternative counts differ");
                    }
                    for (size_t i = 0; i < lE.pattern.alternatives.size(); i++) {
                        const auto& left = lE.pattern.alternatives[i];
                        const auto& right = rE.pattern.alternatives[i];
                        if (left.hasStart != right.hasStart || left.hasEnd != right.hasEnd || left.endInclusive != right.endInclusive) {
                            ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - pattern shapes differ");
                        }
                        if (left.hasStart) this->equateValues(sp, left.start, right.start);
                        if (left.hasEnd) this->equateValues(sp, left.end, right.end);
                    }
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    break;
                }
                case HIRTypeData::TAG_Tuple: {
                    auto& lE = (*lT).as_Tuple();
                    auto& rE = (*rT).as_Tuple();
                    if (lE.size() != rE.size()) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - Tuples are of different length");
                    }
                    for (unsigned int i = 0; i < lE.size(); i++) {
                        this->equateTypesInner(sp, lE[i], rE[i]);
                    }
                    break;
                }
                case HIRTypeData::TAG_Borrow: {
                    auto& lE = (*lT).as_Borrow();
                    auto& rE = (*rT).as_Borrow();
                    if (lE.type != rE.type) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - Borrow classes differ");
                    }
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    break;
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& lE = (*lT).as_Pointer();
                    auto& rE = (*rT).as_Pointer();
                    if (lE.type != rE.type) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT << " - Pointer mutability differs");
                    }
                    this->equateTypesInner(sp, lE.inner, rE.inner);
                    break;
                }
                case HIRTypeData::TAG_NamedFunction: {
                    auto& lE = (*lT).as_NamedFunction();
                    auto& rE = (*rT).as_NamedFunction();
                    if (!equalityPath(lE.path, rE.path)) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    break;
                }
                case HIRTypeData::TAG_Function: {
                    auto& lE = (*lT).as_Function();
                    auto& rE = (*rT).as_Function();
                    if (lE.isUnsafe != rE.isUnsafe || lE.isVariadic != rE.isVariadic || lE.trackCaller != rE.trackCaller || lE.abi != rE.abi || lE.argTypes.size() != rE.argTypes.size()) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    // TODO: HRLs
                    this->equateTypesInner(sp, lE.rettype, rE.rettype);
                    for (unsigned int i = 0; i < lE.argTypes.size(); i++) {
                        this->equateTypesInner(sp, lE.argTypes[i], rE.argTypes[i]);
                    }
                    break;
                }
                case HIRTypeData::TAG_NodeType: {
                    auto& lE = (*lT).as_NodeType();
                    auto& rE = (*rT).as_NodeType();
                    if (lE != rE) {
                        ERROR(sp, E0000, "Type mismatch between " << lT << " and " << rT);
                    }
                    break;
                }
            }
        }
    }
}

namespace {
    struct ConstExprEquate {
        Context& context;
        const Span& sp;

        const HIRConstGeneric* getParam(const HIRConstGenericUnevaluated& value, unsigned int binding) const {
            const HIRPathParams* params = nullptr;
            switch (binding >> 8) {
                case GENERICImpl:
                    params = &value.paramsImpl;
                    break;
                case GENERICItem:
                    params = &value.paramsItem;
                    break;
                default:
                    return nullptr;
            }
            const unsigned int index = binding & 0xFF;
            return index < params->values.size() ? &params->values[index] : nullptr;
        }

        const HIRConstGeneric* identity(const HIRConstGeneric& value) const {
            const auto* unevaluated = value.opt_Unevaluated();
            if (!unevaluated) {
                return nullptr;
            }

            const HIRExprNode* node = &**(*unevaluated)->expr;
            for (;;) {
                if (const auto* block = cast<const HIRExprNodeBlock>(node)) {
                    if (!block->nodes.empty() || !block->valueNode) {
                        return nullptr;
                    }
                    node = &*block->valueNode;
                    continue;
                }
                if (const auto* block = cast<const HIRExprNodeConstBlock>(node)) {
                    node = &*block->inner;
                    continue;
                }
                break;
            }

            const auto* param = cast<const HIRExprNodeConstParam>(node);
            return param ? getParam(**unevaluated, param->binding) : nullptr;
        }

        bool equateIdentity(const HIRConstGeneric& value, const HIRConstGeneric& other) const {
            const auto* replacement = identity(value);
            if (!replacement || *replacement == value) {
                return false;
            }
            context.equateValues(sp, *replacement, other);
            return true;
        }

        bool equateLiteral(const HIRExprNodeLiteral& left, const HIRExprNodeLiteral& right) const {
            if (left.data.tag() != right.data.tag()) {
                return false;
            }
            switch (left.data.tag()) {
                case HIRExprLiteral::TAG_Integer: {
                    auto& l = left.data.as_Integer();
                    auto& r = right.data.as_Integer();
                    return l.type == r.type && l.value == r.value;
                }
                case HIRExprLiteral::TAG_Float: {
                    auto& l = left.data.as_Float();
                    auto& r = right.data.as_Float();
                    return l.type == r.type && l.value == r.value;
                }
                case HIRExprLiteral::TAG_Boolean: {
                    auto& l = left.data.as_Boolean();
                    auto& r = right.data.as_Boolean();
                    return l == r;
                }
                case HIRExprLiteral::TAG_String: {
                    auto& l = left.data.as_String();
                    auto& r = right.data.as_String();
                    return l == r;
                }
                case HIRExprLiteral::TAG_CString: {
                    auto& l = left.data.as_CString();
                    auto& r = right.data.as_CString();
                    return l.v == r.v;
                }
                case HIRExprLiteral::TAG_ByteString: {
                    auto& l = left.data.as_ByteString();
                    auto& r = right.data.as_ByteString();
                    return l == r;
                }
            }
            throw "";
        }

        const EncodedLiteral* evaluatedPath(const HIRConstGenericUnevaluated& value, const HIRExprNodePathValue& node) const {
            MonomorphStatePtr monomorph(context.crate.types, value.selfType, &value.paramsImpl, &value.paramsItem);
            auto path = monomorph.monomorphPath(sp, node.path);
            StaticTraitResolve resolve(context.resolve.board());
            MonomorphState params(context.crate.types);
            auto resolved = resolve.getValue(sp, path, params);
            const auto* constant = resolved.opt_Constant();
            if (!constant) {
                return nullptr;
            }

            const auto& item = **constant;
            if (item.valueState == HIRConstant::ValueState::Known) {
                return &item.valueRes;
            }
            if (item.valueState == HIRConstant::ValueState::Generic) {
                auto cached = item.monomorphCache.find(path);
                return cached == item.monomorphCache.end() ? nullptr : &cached->second;
            }
            return nullptr;
        }

        bool equateLiteralPath(const HIRExprNodeLiteral& literal, const HIRConstGenericUnevaluated& pathValue, const HIRExprNodePathValue& path) const {
            const auto* evaluated = evaluatedPath(pathValue, path);
            if (!evaluated || !evaluated->relocations.empty() || evaluated->bytes.empty()) {
                return false;
            }
            if (const auto* integer = literal.data.opt_Integer()) {
                return EncodedLiteralSlice(*evaluated).readUint() == integer->value;
            }
            if (const auto* boolean = literal.data.opt_Boolean()) {
                return (EncodedLiteralSlice(*evaluated).readUint() != 0) == *boolean;
            }
            return false;
        }

        bool equatePath(const HIRConstGenericUnevaluated& leftValue, const HIRPath& left, const HIRConstGenericUnevaluated& rightValue, const HIRPath& right) const {
            MonomorphStatePtr leftMonomorph(context.crate.types, leftValue.selfType, &leftValue.paramsImpl, &leftValue.paramsItem);
            MonomorphStatePtr rightMonomorph(context.crate.types, rightValue.selfType, &rightValue.paramsImpl, &rightValue.paramsItem);
            const auto leftPath = leftMonomorph.monomorphPath(sp, left);
            const auto rightPath = rightMonomorph.monomorphPath(sp, right);
            if (leftPath == rightPath || leftPath.equalsIgnoringRegions(rightPath)) {
                return true;
            }

            auto equateParams = [&](const HIRPathParams& leftParams, const HIRPathParams& rightParams) {
                if (leftParams.types.size() != rightParams.types.size() || leftParams.values.size() != rightParams.values.size()) {
                    return false;
                }
                for (unsigned int i = 0; i < leftParams.types.size(); i++) {
                    context.equateTypesInner(sp, leftParams.types[i], rightParams.types[i]);
                }
                for (unsigned int i = 0; i < leftParams.values.size(); i++) {
                    context.equateValues(sp, leftParams.values[i], rightParams.values[i]);
                }
                return true;
            };

            if (leftPath.data.tag() != rightPath.data.tag()) {
                return false;
            }
            switch (leftPath.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    auto& l = leftPath.data.as_Generic();
                    auto& r = rightPath.data.as_Generic();
                    return l.path == r.path && equateParams(l.params, r.params);
                }
                case HIRPathData::TAG_UfcsInherent: {
                    auto& l = leftPath.data.as_UfcsInherent();
                    auto& r = rightPath.data.as_UfcsInherent();
                    if (l.item != r.item || !equateParams(l.params, r.params)) {
                        return false;
                    }
                    context.equateTypesInner(sp, l.type, r.type);
                    return true;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    auto& l = leftPath.data.as_UfcsKnown();
                    auto& r = rightPath.data.as_UfcsKnown();
                    if (l.trait.path != r.trait.path || l.item != r.item || !equateParams(l.trait.params, r.trait.params) || !equateParams(l.params, r.params)) {
                        return false;
                    }
                    context.equateTypesInner(sp, l.type, r.type);
                    return true;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    auto& l = leftPath.data.as_UfcsUnknown();
                    auto& r = rightPath.data.as_UfcsUnknown();
                    if (l.item != r.item || !equateParams(l.params, r.params)) {
                        return false;
                    }
                    context.equateTypesInner(sp, l.type, r.type);
                    return true;
                }
            }
            throw "";
        }

        bool equateNode(const HIRConstGenericUnevaluated& leftValue, const HIRExprNode& left, const HIRConstGenericUnevaluated& rightValue, const HIRExprNode& right) const {
            if (const auto* block = cast<const HIRExprNodeBlock>(&left)) {
                if (block->nodes.empty() && block->valueNode) {
                    return equateNode(leftValue, *block->valueNode, rightValue, right);
                }
            }
            if (const auto* block = cast<const HIRExprNodeBlock>(&right)) {
                if (block->nodes.empty() && block->valueNode) {
                    return equateNode(leftValue, left, rightValue, *block->valueNode);
                }
            }
            if (const auto* block = cast<const HIRExprNodeConstBlock>(&left)) {
                return equateNode(leftValue, *block->inner, rightValue, right);
            }
            if (const auto* block = cast<const HIRExprNodeConstBlock>(&right)) {
                return equateNode(leftValue, left, rightValue, *block->inner);
            }
            if (const auto* param = cast<const HIRExprNodeConstParam>(&left)) {
                if (const auto* value = getParam(leftValue, param->binding)) {
                    if (const auto* unevaluated = value->opt_Unevaluated()) {
                        return equateNode(**unevaluated, **(**unevaluated).expr, rightValue, right);
                    }
                }
            }
            if (const auto* param = cast<const HIRExprNodeConstParam>(&right)) {
                if (const auto* value = getParam(rightValue, param->binding)) {
                    if (const auto* unevaluated = value->opt_Unevaluated()) {
                        return equateNode(leftValue, left, **unevaluated, **(**unevaluated).expr);
                    }
                }
            }
            if (const auto* l = cast<const HIRExprNodeConstParam>(&left)) {
                const auto* r = cast<const HIRExprNodeConstParam>(&right);
                if (!r) {
                    return false;
                }
                const auto* lParam = getParam(leftValue, l->binding);
                const auto* rParam = getParam(rightValue, r->binding);
                if (!lParam || !rParam) {
                    return l->binding == r->binding;
                }
                context.equateValues(sp, *lParam, *rParam);
                return true;
            }
            if (const auto* l = cast<const HIRExprNodeLiteral>(&left)) {
                if (const auto* r = cast<const HIRExprNodeLiteral>(&right)) {
                    return equateLiteral(*l, *r);
                }
                if (const auto* r = cast<const HIRExprNodePathValue>(&right)) {
                    return equateLiteralPath(*l, rightValue, *r);
                }
                return false;
            }
            if (const auto* l = cast<const HIRExprNodePathValue>(&left)) {
                if (const auto* r = cast<const HIRExprNodePathValue>(&right)) {
                    return l->target == r->target && equatePath(leftValue, l->path, rightValue, r->path);
                }
                if (const auto* r = cast<const HIRExprNodeLiteral>(&right)) {
                    return equateLiteralPath(*r, leftValue, *l);
                }
                return false;
            }
            if (const auto* l = cast<const HIRExprNodeBinOp>(&left)) {
                const auto* r = cast<const HIRExprNodeBinOp>(&right);
                return r && l->op == r->op && equateNode(leftValue, *l->left, rightValue, *r->left) && equateNode(leftValue, *l->right, rightValue, *r->right);
            }
            if (const auto* l = cast<const HIRExprNodeUniOp>(&left)) {
                const auto* r = cast<const HIRExprNodeUniOp>(&right);
                return r && l->op == r->op && equateNode(leftValue, *l->value, rightValue, *r->value);
            }
            if (const auto* l = cast<const HIRExprNodeCast>(&left)) {
                const auto* r = cast<const HIRExprNodeCast>(&right);
                if (!r) {
                    return false;
                }
                context.equateTypesInner(sp, l->dstType, r->dstType);
                return equateNode(leftValue, *l->value, rightValue, *r->value);
            }
            if (const auto* l = cast<const HIRExprNodeConstBlock>(&left)) {
                const auto* r = cast<const HIRExprNodeConstBlock>(&right);
                return r && equateNode(leftValue, *l->inner, rightValue, *r->inner);
            }
            if (const auto* l = cast<const HIRExprNodeCallPath>(&left)) {
                const auto* r = cast<const HIRExprNodeCallPath>(&right);
                if (!r || !equatePath(leftValue, l->path, rightValue, r->path) || l->args.size() != r->args.size()) {
                    return false;
                }
                for (unsigned int i = 0; i < l->args.size(); i++) {
                    if (!equateNode(leftValue, *l->args[i], rightValue, *r->args[i])) {
                        return false;
                    }
                }
                return true;
            }
            if (const auto* l = cast<const HIRExprNodeBlock>(&left)) {
                const auto* r = cast<const HIRExprNodeBlock>(&right);
                if (!r || l->nodes.size() != r->nodes.size() || static_cast<bool>(l->valueNode) != static_cast<bool>(r->valueNode)) {
                    return false;
                }
                for (unsigned int i = 0; i < l->nodes.size(); i++) {
                    if (!equateNode(leftValue, *l->nodes[i], rightValue, *r->nodes[i])) {
                        return false;
                    }
                }
                return !l->valueNode || equateNode(leftValue, *l->valueNode, rightValue, *r->valueNode);
            }
            return false;
        }

        bool equate(const HIRConstGenericUnevaluated& left, const HIRConstGenericUnevaluated& right) const {
            return equateNode(left, **left.expr, right, **right.expr);
        }
    };
}

void Context::equateValues(const Span& sp, const HIRConstGeneric& rl, const HIRConstGeneric& rr) {
    const auto& l = this->ivars.getValue(rl);
    const auto& r = this->ivars.getValue(rr);
    if (l != r) {
        DEBUG(l << " != " << r);
        if (l.is_Infer()) {
            if (r.is_Infer()) {
                // Unify ivars
                this->ivars.ivarValUnify(l.as_Infer().index, r.as_Infer().index);
            } else {
                this->ivars.setIvarValTo(l.as_Infer().index, r.clone());
            }
        } else {
            if (r.is_Infer()) {
                this->ivars.setIvarValTo(r.as_Infer().index, l.clone());
            } else {
                auto normalizedL = l.clone();
                auto normalizedR = r.clone();

                struct ResolveIvars: HIRVisitor {
                    Context& context;

                    explicit ResolveIvars(Context& context)
                        : HIRVisitor(nullptr, context.crate.types)
                        , context(context)
                    {
                    }

                    void visitConstgeneric(HIRConstGeneric& value) override {
                        if (value.is_Infer()) {
                            const auto& resolved = context.ivars.getValue(value);
                            if (resolved != value) {
                                value = resolved.clone();
                            }
                        }
                        HIRVisitor::visitConstgeneric(value);
                    }

                    [[nodiscard]] HIRTypeRef visitType(HIRTypeRef type) override {
                        if (type->is_Infer()) {
                            const auto resolved = context.ivars.getType(type);
                            if (resolved != type) {
                                type = resolved;
                            }
                        }
                        return visitTypeDefaultViaHooks(type);
                    }
                } resolveIvars{*this};

                resolveIvars.visitConstgeneric(normalizedL);
                resolveIvars.visitConstgeneric(normalizedR);
                ConvertHIRConstantEvaluateConstGeneric(sp, resolve.board(), crate, normalizedL);
                ConvertHIRConstantEvaluateConstGeneric(sp, resolve.board(), crate, normalizedR);

                ConstExprEquate exprEquate{*this, sp};
                if (normalizedL == normalizedR) {
                } else if (exprEquate.equateIdentity(normalizedL, normalizedR)) {
                } else if (exprEquate.equateIdentity(normalizedR, normalizedL)) {
                } else if (normalizedL.is_Unevaluated() && normalizedR.is_Unevaluated() && exprEquate.equate(*normalizedL.as_Unevaluated(), *normalizedR.as_Unevaluated())) {
                } else {
                    // TODO: What about unevaluated values due to type inference?
                    ERROR(sp, E0000, "Value mismatch between " << normalizedL << " and " << normalizedR);
                }
            }
        }
    } else {
        DEBUG(l << " == " << r);
    }
}

void Context::addBindingInner(const Span& sp, const HIRPatternBinding& pb, HIRTypeRef type) {
    assert(pb.isValid());
    switch (pb.type) {
        case HIRPatternBinding::Type::Move:
            this->addVar(sp, pb.slot, pb.name, mv$(type));
            break;
        case HIRPatternBinding::Type::Ref:
            this->addVar(sp, pb.slot, pb.name, crate.types.borrow(HIRBorrowType::Shared, type));
            break;
        case HIRPatternBinding::Type::MutRef:
            this->addVar(sp, pb.slot, pb.name, crate.types.borrow(HIRBorrowType::Unique, type));
            break;
    }
}

// NOTE: Mutates the pattern to add ivars to contained paths
namespace {
    // Pattern constant paths never pass through the expression visitors, so any `_` holes in
    // them (e.g. `<S as Format<_>>::FORMAT`) have to be populated here, once, before the
    // pattern is (re)visited. Registers the implied trait bound at the same time.
    void fixupPatternValuePaths(Context& context, const Span& sp, HIRPattern::Value& val) {
        if (auto* ve = val.opt_Named()) {
            if (ve->binding && ve->path.data.is_UfcsKnown()) {
                auto& pe = ve->path.data.as_UfcsKnown();
                context.ivars.addIvars(pe.type);
                context.ivars.addIvarsParams(pe.trait.params);
                context.ivars.addIvarsParams(pe.params);
                context.addTraitBound(sp, pe.type, pe.trait.path, pe.trait.params.clone());
            } else if (ve->binding && ve->path.data.is_UfcsInherent()) {
                auto& pe = ve->path.data.as_UfcsInherent();
                context.ivars.addIvars(pe.type);
                context.ivars.addIvarsParams(pe.params);
                context.ivars.addIvarsParams(pe.implParams);
            }
        }
    }

    void fixupPatternValuePaths(Context& context, const Span& sp, HIRPattern& pat) {
        switch (pat.data.tag()) {
            case HIRPatternData::TAG_Any: {
                break;
            }
            case HIRPatternData::TAG_Box: {
                auto& e = pat.data.as_Box();
                fixupPatternValuePaths(context, sp, *e.sub);
                break;
            }
            case HIRPatternData::TAG_Deref: {
                auto& e = pat.data.as_Deref();
                fixupPatternValuePaths(context, sp, *e.sub);
                break;
            }
            case HIRPatternData::TAG_Ref: {
                auto& e = pat.data.as_Ref();
                fixupPatternValuePaths(context, sp, *e.sub);
                break;
            }
            case HIRPatternData::TAG_Tuple: {
                auto& e = pat.data.as_Tuple();
                for (auto& subpat : e.subPatterns) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                break;
            }
            case HIRPatternData::TAG_SplitTuple: {
                auto& e = pat.data.as_SplitTuple();
                for (auto& subpat : e.leading) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                for (auto& subpat : e.trailing) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                break;
            }
            case HIRPatternData::TAG_PathValue: {
                break;
            }
            case HIRPatternData::TAG_PathTuple: {
                auto& e = pat.data.as_PathTuple();
                for (auto& subpat : e.leading) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                for (auto& subpat : e.trailing) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                break;
            }
            case HIRPatternData::TAG_PathNamed: {
                auto& e = pat.data.as_PathNamed();
                for (auto& subpat : e.subPatterns) {
                    fixupPatternValuePaths(context, sp, subpat.second);
                }
                break;
            }
            case HIRPatternData::TAG_Or: {
                auto& e = pat.data.as_Or();
                for (auto& subpat : e) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                break;
            }
            case HIRPatternData::TAG_Value: {
                auto& e = pat.data.as_Value();
                fixupPatternValuePaths(context, sp, e.val);
                break;
            }
            case HIRPatternData::TAG_Range: {
                auto& e = pat.data.as_Range();
                if (e.start) {
                    fixupPatternValuePaths(context, sp, *e.start);
                }
                if (e.end) {
                    fixupPatternValuePaths(context, sp, *e.end);
                }
                break;
            }
            case HIRPatternData::TAG_Slice: {
                auto& e = pat.data.as_Slice();
                for (auto& subpat : e.subPatterns) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                break;
            }
            case HIRPatternData::TAG_SplitSlice: {
                auto& e = pat.data.as_SplitSlice();
                for (auto& subpat : e.leading) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                for (auto& subpat : e.trailing) {
                    fixupPatternValuePaths(context, sp, subpat);
                }
                break;
            }
        }
    }
}

void Context::handlePattern(const Span& sp, HIRPattern& pat, const HIRTypeData* type, bool isIrrefutable /*=false*/) {
    TRACE_FUNCTION_F("pat = " << pat << ", type = " << type);

    fixupPatternValuePaths(*this, sp, pat);

    // Match ergonomics allows automatic insertion of borrow/deref when matching.
    // - Handling this will make pattern matching slightly harder (all patterns needing revisist)
    // - BUT: New bindings will still be added as usualin this pass.
    // - Any use of `&` (or `ref`?) in the pattern disables match ergonomics for the entire pattern.
    //   - Does `box` also do this disable?
    // - Add a counter to each pattern indicting how many implicit borrows/derefs are applied.
    // - When this function is called, check if the pattern is eligable for pattern auto-ref/deref
    // - Detect if the pattern uses & or ref. If it does, then invoke the existing code
    // - Otherwise, register a revisit for the pattern

    // 1. Determine if this pattern can apply auto-ref/deref
    if (pat.data.is_Any()) {
        // `_` pattern, no destructure/match, so no auto-ref/deref
        // - TODO: Does this do auto-borrow too?
        for (const auto& pb : pat.bindings) {
            this->addBindingInner(sp, pb, type);
        }
        return;
    }

    // NOTE: Even if the top-level is a binding, and even if the top-level type is fully known, match ergonomics
    // still applies.
    {
        // There's not a `&` or `ref` in the pattern, so run the match ergonomics handler.
        struct MatchErgonomicsRevisit: public Revisitor {
            Span sp;
            bool isIrrefutable;
            HIRTypeRef outerTy;
            HIRPattern& pattern;
            HIRPatternBinding::Type outerMode;

            mutable ::std::vector<HIRTypeRef> tempIvars;
            mutable ::std::optional<HIRTypeRef> possibleType;
            mutable const HIRPattern* possibleTypePattern = nullptr;

            MatchErgonomicsRevisit(Span sp, bool isIrrefutable, HIRTypeRef outer, HIRPattern& pat, HIRPatternBinding::Type bindingMode = HIRPatternBinding::Type::Move)
                : sp(mv$(sp))
                , isIrrefutable(isIrrefutable)
                , outerTy(mv$(outer))
                , pattern(pat)
                , outerMode(bindingMode)
            {
            }

            const Span& span() const override {
                return sp;
            }

            void fmt(::std::ostream& os) const override {
                os << "MatchErgonomicsRevisit { " << pattern << " : " << outerTy << " }";
            }

            bool revisit(Context& context, bool isFallbackMode) override {
                TRACE_FUNCTION_F("Match ergonomics - " << pattern << " : " << outerTy << (isFallbackMode ? " (fallback)" : ""));
                outerTy = context.resolve.expandAssociatedTypes(sp, mv$(outerTy));
                return this->revisitInnerReal(context, pattern, outerTy, outerMode, isFallbackMode);
            }

            // TODO: Recurse into inner patterns, creating new revisitors?
            // - OR, could just recurse on it.
            // Recusring incurs costs on every iteration, but is less expensive the first time around
            // New revisitors are cheaper when inferrence takes multiple iterations, but takes longer first time.
            bool revisitInner(Context& context, HIRPattern& pattern, const HIRTypeData* type, HIRPatternBinding::Type bindingMode) const {
                if (!revisitInnerReal(context, pattern, type, bindingMode, false)) {
                    DEBUG("Add revisit for " << pattern << " : " << type << "(mode = " << (int)bindingMode << ")");
                    context.addRevisitAdv(box$((MatchErgonomicsRevisit{sp, isIrrefutable, type, pattern, bindingMode})));
                }
                return true;
            }

            ::std::optional<HIRTypeRef> getPossibleTypeVal(Context& context, HIRPattern::Value& pv) const {
                switch (pv.tag()) {
                    case HIRPatternValue::TAG_Integer: {
                        auto& ve = pv.as_Integer();
                        if (ve.type == HIRCoreType::Str) {
                            return context.ivars.newIvarTr(HIRInferClass::Integer);
                        }
                        return context.crate.types.primitive(ve.type);
                    }
                    case HIRPatternValue::TAG_Float: {
                        auto& ve = pv.as_Float();
                        if (ve.type == HIRCoreType::Str) {
                            return context.ivars.newIvarTr(HIRInferClass::Float);
                        }
                        return context.crate.types.primitive(ve.type);
                    }
                    case HIRPatternValue::TAG_String: {
                        return context.crate.types.borrow(HIRBorrowType::Shared, context.crate.types.primitive(HIRCoreType::Str));
                    }
                    case HIRPatternValue::TAG_ByteString: {
                        auto& ve = pv.as_ByteString();
                        // This is the default literal type. A known &[u8]
                        // scrutinee is still accepted by the pattern handler.
                        return context.crate.types.borrow(
                            HIRBorrowType::Shared,
                            context.crate.types.array(context.crate.types.primitive(HIRCoreType::U8), ve.v.size()));
                    }
                    case HIRPatternValue::TAG_Named: {
                        auto& ve = pv.as_Named();
                        DEBUG("TODO: Look up the path and get the type: " << ve.path);
                        if (ve.binding) {
                            if (ve.path.data.is_UfcsKnown()) {
                                // Trait-associated constant: its type can name trait params, so
                                // map them through the (pre-populated) params from the path.
                                const auto& pe = ve.path.data.as_UfcsKnown();
                                auto ms = MonomorphStatePtr(context.crate.types, pe.type, &pe.trait.params, nullptr);
                                return ms.monomorphType(sp, ve.binding->type);
                            }
                            return ve.binding->type;
                        } else if (ve.path.data.is_Generic()) {
                            TODO(sp, "Look up pattern value: " << ve.path);
                        } else {
                            return ::std::nullopt;
                        }
                        break;
                    }
                }
                throw "";
            }

            ::std::optional<HIRTypeRef> getPossibleTypeInner(Context& context, HIRPattern& pattern) const {
                ::std::optional<HIRTypeRef> possibleType;
                // Get a potential type from the pattern, and set as a possibility.
                // - Note, this is only if no derefs were applied
                switch (pattern.data.tag()) {
                    case HIRPatternData::TAG_Any: {
                        // No type information.
                        break;
                    }
                    case HIRPatternData::TAG_Value: {
                        auto& pe = pattern.data.as_Value();
                        possibleType = getPossibleTypeVal(context, pe.val);
                        break;
                    }
                    case HIRPatternData::TAG_Range: {
                        auto& pe = pattern.data.as_Range();
                        if (pe.start) {
                            possibleType = getPossibleTypeVal(context, *pe.start);
                        }
                        if (!possibleType) {
                            if (pe.end) {
                                possibleType = getPossibleTypeVal(context, *pe.end);
                            }
                        } else {
                            // TODO: Check that the type from .end matches .start
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Box: {
                        // TODO: Get type info (Box<_>) ?
                        // - Is this possible? Shouldn't a box pattern disable ergonomics?
                        break;
                    }
                    case HIRPatternData::TAG_Deref: {
                        // The inner pattern constrains `Deref::Target`, not the
                        // smart-pointer source itself.  It cannot be used as a
                        // fallback type for the outer scrutinee.
                        break;
                    }
                    case HIRPatternData::TAG_Ref: {
                        BUG(sp, "Match ergonomics - & pattern");
                        break;
                    }
                    case HIRPatternData::TAG_Tuple: {
                        auto& e = pattern.data.as_Tuple();
                        // Get type info `(T, U, ...)`
                        if (tempIvars.size() != e.subPatterns.size()) {
                            for (size_t i = 0; i < e.subPatterns.size(); i++) {
                                tempIvars.push_back(context.ivars.newIvarTr());
                            }
                        }
                        decltype(tempIvars) tuple;
                        for (const auto& ty : tempIvars) {
                            tuple.push_back(ty);
                        }
                        possibleType = context.crate.types.tuple(::std::move(tuple));
                        break;
                    }
                    case HIRPatternData::TAG_SplitTuple: {
                        // Can't get type information, tuple size is unkown
                        break;
                    }
                    case HIRPatternData::TAG_Slice: {
                        auto& e = pattern.data.as_Slice();
                        // Can be either a [T] or [T; n]. Can't provide a hint
                        // - Can provide the hint if not behind a borrow.
                        possibleType = context.crate.types.array(context.ivars.newIvarTr(), e.subPatterns.size());
                        break;
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        // Can be either a [T] or [T; n]. Can't provide a hint
                        break;
                    }
                    case HIRPatternData::TAG_PathValue: {
                        auto& e = pattern.data.as_PathValue();
                        switch (e.binding.tag()) {
                            case HIRPatternPathBinding::TAG_Unbound: {
                                auto& _ = e.binding.as_Unbound();
                                BUG(sp, "");
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Struct: {
                                auto& be = e.binding.as_Struct();
                                auto& p = e.path.data.as_Generic();
                                assert(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Union: {
                                auto& be = e.binding.as_Union();
                                auto& p = e.path.data.as_Generic();
                                assert(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Enum: {
                                auto& be = e.binding.as_Enum();
                                auto& p = e.path.data.as_Generic();
                                assert(be.ptr);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(getParentPath(p), HIRTypePathBinding(be.ptr));
                                break;
                            }
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathTuple: {
                        auto& e = pattern.data.as_PathTuple();
                        switch (e.binding.tag()) {
                            case HIRPatternPathBinding::TAG_Unbound: {
                                auto& _ = e.binding.as_Unbound();
                                BUG(sp, "");
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Struct: {
                                auto& be = e.binding.as_Struct();
                                auto& p = e.path.data.as_Generic();
                                assert(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Union: {
                                auto& be = e.binding.as_Union();
                                auto& p = e.path.data.as_Generic();
                                assert(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Enum: {
                                auto& be = e.binding.as_Enum();
                                auto& p = e.path.data.as_Generic();
                                assert(be.ptr);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(getParentPath(p), HIRTypePathBinding(be.ptr));
                                break;
                            }
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathNamed: {
                        auto& e = pattern.data.as_PathNamed();
                        switch (e.binding.tag()) {
                            case HIRPatternPathBinding::TAG_Unbound: {
                                auto& _ = e.binding.as_Unbound();
                                BUG(sp, "");
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Struct: {
                                auto& be = e.binding.as_Struct();
                                auto& p = e.path.data.as_Generic();
                                assert(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Union: {
                                auto& be = e.binding.as_Union();
                                auto& p = e.path.data.as_Generic();
                                assert(be);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Enum: {
                                auto& be = e.binding.as_Enum();
                                auto& p = e.path.data.as_Generic();
                                assert(be.ptr);
                                context.addIvarsParams(p.params);
                                possibleType = context.crate.types.path(getParentPath(p), HIRTypePathBinding(be.ptr));
                                break;
                            }
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Or: {
                        auto& e = pattern.data.as_Or();
                        for (auto& subpat : e) {
                            possibleType = getPossibleTypeInner(context, subpat);
                            if (possibleType) {
                                break;
                            }
                        }
                        break;
                    }
                }
                return possibleType;
            }

            const ::std::optional<HIRTypeRef>& getPossibleType(Context& context, HIRPattern& pattern) const {
                if (!possibleType || possibleTypePattern != &pattern) {
                    possibleType = getPossibleTypeInner(context, pattern);
                    possibleTypePattern = &pattern;
                }
                return possibleType;
            }

            static bool hasMutableBinding(const HIRPattern& pattern) {
                for (const auto& binding : pattern.bindings) {
                    if (binding.type == HIRPatternBinding::Type::MutRef) return true;
                }
                switch (pattern.data.tag()) {
                    case HIRPatternData::TAG_Any: {
                        return false;
                    }
                    case HIRPatternData::TAG_Value: {
                        return false;
                    }
                    case HIRPatternData::TAG_Range: {
                        return false;
                    }
                    case HIRPatternData::TAG_Box: {
                        auto& e = pattern.data.as_Box();
                        return hasMutableBinding(*e.sub);
                    }
                    case HIRPatternData::TAG_Deref: {
                        auto& e = pattern.data.as_Deref();
                        return hasMutableBinding(*e.sub);
                    }
                    case HIRPatternData::TAG_Ref: {
                        auto& e = pattern.data.as_Ref();
                        return hasMutableBinding(*e.sub);
                    }
                    case HIRPatternData::TAG_Tuple: {
                        auto& e = pattern.data.as_Tuple();
                        for (const auto& sub : e.subPatterns) if (hasMutableBinding(sub)) return true; return false;
                        break;
                    }
                    case HIRPatternData::TAG_SplitTuple: {
                        auto& e = pattern.data.as_SplitTuple();
                        for (const auto& sub : e.leading) if (hasMutableBinding(sub)) return true; for (const auto& sub : e.trailing) if (hasMutableBinding(sub)) return true; return false;
                        break;
                    }
                    case HIRPatternData::TAG_PathValue: {
                        return false;
                    }
                    case HIRPatternData::TAG_PathTuple: {
                        auto& e = pattern.data.as_PathTuple();
                        for (const auto& sub : e.leading) if (hasMutableBinding(sub)) return true; for (const auto& sub : e.trailing) if (hasMutableBinding(sub)) return true; return false;
                        break;
                    }
                    case HIRPatternData::TAG_PathNamed: {
                        auto& e = pattern.data.as_PathNamed();
                        for (const auto& sub : e.subPatterns) if (hasMutableBinding(sub.second)) return true; return false;
                        break;
                    }
                    case HIRPatternData::TAG_Slice: {
                        auto& e = pattern.data.as_Slice();
                        for (const auto& sub : e.subPatterns) if (hasMutableBinding(sub)) return true; return false;
                        break;
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        auto& e = pattern.data.as_SplitSlice();
                        if (e.extraBind.type == HIRPatternBinding::Type::MutRef) return true; for (const auto& sub : e.leading) if (hasMutableBinding(sub)) return true; for (const auto& sub : e.trailing) if (hasMutableBinding(sub)) return true; return false;
                        break;
                    }
                    case HIRPatternData::TAG_Or: {
                        auto& e = pattern.data.as_Or();
                        for (const auto& sub : e) if (hasMutableBinding(sub)) return true; return false;
                        break;
                    }
                }
                throw "";
            }

            static bool directlyMatches(const HIRPattern& pattern, const HIRTypeData* type) {
                auto matchesPath = [&](const HIRPath& patternPath, const HIRPattern::PathBinding& binding) {
                    const auto* actual = type->opt_Path();
                    if (!actual || !actual->path.data.is_Generic() || !patternPath.data.is_Generic()) return false;
                    const auto& actualPath = actual->path.data.as_Generic().path;
                    const auto& patternGeneric = patternPath.data.as_Generic();
                    return binding.is_Enum() ? actualPath == getParentPath(patternGeneric).path
                                             : actualPath == patternGeneric.path;
                };
                switch (pattern.data.tag()) {
                    case HIRPatternData::TAG_Any: {
                        return true;
                    }
                    case HIRPatternData::TAG_Deref: {
                        return true;
                    }
                    case HIRPatternData::TAG_Box: {
                        return type->is_Path();
                    }
                    case HIRPatternData::TAG_Ref: {
                        return type->is_Borrow();
                    }
                    case HIRPatternData::TAG_Tuple: {
                        return type->is_Tuple();
                    }
                    case HIRPatternData::TAG_SplitTuple: {
                        return type->is_Tuple();
                    }
                    case HIRPatternData::TAG_Slice: {
                        return type->is_Array() || type->is_Slice();
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        return type->is_Array() || type->is_Slice();
                    }
                    case HIRPatternData::TAG_PathValue: {
                        auto& e = pattern.data.as_PathValue();
                        return matchesPath(e.path, e.binding);
                    }
                    case HIRPatternData::TAG_PathTuple: {
                        auto& e = pattern.data.as_PathTuple();
                        return matchesPath(e.path, e.binding);
                    }
                    case HIRPatternData::TAG_PathNamed: {
                        auto& e = pattern.data.as_PathNamed();
                        return matchesPath(e.path, e.binding);
                    }
                    case HIRPatternData::TAG_Range: {
                        return type->is_Primitive();
                    }
                    case HIRPatternData::TAG_Value: {
                        auto& e = pattern.data.as_Value();
                        if (e.val.is_Named()) return true;
                        if (e.val.is_String()) return type->is_Primitive() && type->as_Primitive() == HIRCoreType::Str;
                        if (e.val.is_ByteString()) return type->is_Array() || type->is_Slice();
                        if (e.val.is_Integer() || e.val.is_Float()) return type->is_Primitive();
                        return false;
                    }
                    case HIRPatternData::TAG_Or: {
                        auto& e = pattern.data.as_Or();
                        return !e.empty() && directlyMatches(e.front(), type);
                    }
                }
                throw "";
            }

            bool revisitInnerReal(Context& context, HIRPattern& pattern, const HIRTypeData* type, HIRPatternBinding::Type bindingMode, bool isFallback) const {
                TRACE_FUNCTION_F(pattern << " : " << type);

                // Binding applies to the raw input type (not after dereferencing)
                for (auto& pb : pattern.bindings) {
                    // From 2024, a binding written `mut`, `ref` or `ref mut` may
                    // not sit under a default binding mode that match ergonomics
                    // changed: `let [mut x] = &[()]` binds by value inside a
                    // pattern that is already borrowing.
                    // `mut_ref` lifts that restriction again, so a file that
                    // asks for it may write either.
                    if (bindingMode != HIRPatternBinding::Type::Move
                        && context.crate.edition >= ASTEdition::Rust2024
                        && !context.crate.featureEnabled("mut_ref")
                        && (pb.isMutable || pb.type != HIRPatternBinding::Type::Move)) {
                        ERROR(sp, E0000, "cannot bind `" << pb.name << "` with `"
                            << (pb.type != HIRPatternBinding::Type::Move ? "ref" : "mut")
                            << "` within an implicitly-borrowing pattern");
                    }
                    // - Binding present, use the current binding mode
                    if (pb.type == HIRPatternBinding::Type::Move
                        && (!pb.isMutable || context.crate.edition >= ASTEdition::Rust2024)) {
                        pb.type = bindingMode;
                    }
                    HIRTypeRef tmp;
                    const HIRTypeData* bindingType = nullptr;
                    switch (pb.type) {
                        case HIRPatternBinding::Type::Move:
                            bindingType = type;
                            break;
                        case HIRPatternBinding::Type::MutRef:
                            // NOTE: Needs to deref and borrow to get just `&mut T` (where T isn't a &mut T)
                            bindingType = (tmp = context.crate.types.borrow(HIRBorrowType::Unique, type));
                            break;
                        case HIRPatternBinding::Type::Ref:
                            // NOTE: Needs to deref and borrow to get just `&mut T` (where T isn't a &mut T)
                            bindingType = (tmp = context.crate.types.borrow(HIRBorrowType::Shared, type));
                            break;
                        default:
                            TODO(sp, "Assign variable type using mode " << (int)bindingMode << " and " << type);
                    }
                    assert(bindingType);
                    context.equateTypes(sp, context.getVar(sp, pb.slot), bindingType);
                }

                // For `_` patterns, there's nothing to match, so they just succeed with no derefs
                if (pattern.data.is_Any()) {
                    return true;
                }

                // Nothing inhabits `!`, so an arm matching it is unreachable and
                // there is no type for a pattern to disagree with. `match return 1
                // { 2 => 3, _ => panic!() }` is accepted for that reason.
                if (type->is_Diverge()) {
                    return true;
                }

                if (auto* pe = pattern.data.opt_Ref()) {
                    // From 2024, a `&` pattern may not sit under a default
                    // binding mode that match ergonomics changed: the reference
                    // it would take apart is the one already being borrowed
                    // through. `ref_pat_eat_one_layer_2024` is the rule that
                    // replaces this one.
                    if (bindingMode != HIRPatternBinding::Type::Move
                        && context.crate.edition >= ASTEdition::Rust2024
                        && !context.crate.featureEnabled("ref_pat_eat_one_layer_2024")
                        && !context.crate.featureEnabled("mut_ref")) {
                        ERROR(sp, E0000, "cannot explicitly dereference within an implicitly-borrowing pattern - " << pattern);
                    }
                    // Require a &-ptr (hard requirement), then visit sub-pattern
                    auto innerTy = context.ivars.newIvarTr();
                    auto newTy = context.crate.types.borrow(pe->type, innerTy);
                    context.equateTypes(sp, type, newTy);

                    return this->revisitInner(context, *pe->sub, innerTy, HIRPatternBinding::Type::Move);
                }
                if (auto* pe = pattern.data.opt_Or()) {
                    bool rv = true;
                    for (auto& subpat : *pe) {
                        rv &= this->revisitInner(context, subpat, type, bindingMode);
                    }
                    return rv;
                }
                if (auto* pe = pattern.data.opt_Value(); pe && pe->val.is_Named()) {
                    // A named constant is itself the pattern and can have a reference type.
                    // Unlike literals and destructuring patterns, rustc does not peel the
                    // scrutinee before relating it to the constant's instantiated type.
                    const auto valueType = getPossibleTypeVal(context, pe->val);
                    ASSERT_BUG(sp, valueType, "No type for named value pattern " << pattern);
                    pattern.implicitDerefCount = 0;
                    context.equateTypes(sp, type, context.getType(*valueType));
                    return true;
                }

                // If the type is a borrow, then count derefs required for the borrow
                // - If the first non-borrow inner is an ivar, return false
                unsigned nDeref = 0;
                HIRBorrowType bt = HIRBorrowType::Owned;
                const auto* ty = context.revealOpaqueType(type);
                while (const auto* te = ty->opt_Borrow()) {
                    DEBUG("bt " << bt << ", " << te->type);
                    bt = ::std::min(bt, te->type);
                    ty = context.revealOpaqueType(te->inner);
                    nDeref++;
                }
                DEBUG("- " << nDeref << " derefs of class " << bt << " to get " << ty);
                if (ty->is_Infer() || ((*ty).is_Path() && ((*ty).as_Path().binding.is_Unbound()))) {
                    // Still pure infer, can't do anything
                    // - What if it's a literal?

                    // TODO: Don't do fallback if the ivar is marked as being hard blocked
                    if (const auto* te = ty->opt_Infer()) {
                        if (te->index < context.possibleIvarVals.size() && context.possibleIvarVals[te->index].forceDisable) {
                            MatchErgonomicsRevisit::disablePossibilitiesOnBindings(sp, context, pattern);
                            return false;
                        }
                    }

                    // If there's no dereferences done, then add a possible unsize type
                    const auto& possibleType = getPossibleType(context, pattern);
                    if (possibleType) {
                        DEBUG("n_deref = " << nDeref << ", possible_type = " << *possibleType);
                        const HIRTypeData* possibleTypeP = *possibleType;
                        // Unwrap borrows as many times as we've already dereferenced
                        for (size_t i = 0; i < nDeref && possibleTypeP; i++) {
                            if (const auto* te = possibleTypeP->opt_Borrow()) {
                                possibleTypeP = te->inner;
                            } else {
                                possibleTypeP = nullptr;
                            }
                        }
                        if (possibleTypeP) {
                            const auto* possibleType = possibleTypeP;
                            if (isFallback) {
                                DEBUG("Fallback equate " << possibleType);
                                context.equateTypes(sp, ty, possibleType);
                            } else if (const auto* te = ty->opt_Infer()) {
                                // If this is a slice pattern (i.e. the possible type is an array), then add deref-to-slice too
                                if (const auto* te2 = possibleType->opt_Array()) {
                                    if (isIrrefutable) {
                                        context.possibleEquateIvar(sp, te->index, possibleType, Context::PossibleTypeSource::UnsizeTo);
                                    } else {
                                        auto t = context.crate.types.slice(te2->inner);
                                        context.possibleEquateIvar(sp, te->index, t, Context::PossibleTypeSource::UnsizeTo);
                                    }
                                } else {
                                    context.possibleEquateIvar(sp, te->index, possibleType, Context::PossibleTypeSource::UnsizeTo);
                                }
                            } else {
                            }
                        }
                    }

                    // Visit all inner bindings and disable coercion fallbacks on them.
                    MatchErgonomicsRevisit::disablePossibilitiesOnBindings(sp, context, pattern, /*is_top_level=*/true);
                    return false;
                }
                if (ty->is_Primitive() && ty->as_Primitive() == HIRCoreType::Str) {
                    // Can't match on `str`, so unwrap it?
                    // - Unwrapping happens in Pattern::Value handling
                }
                // Here we have a known type and binding mode for this pattern
                // - Time to handle this pattern then recurse into sub-patterns

                // Store the deref count in the pattern.
                pattern.implicitDerefCount = nDeref;
                // Determine the new binding mode from the borrow type
                switch (bt) {
                    case HIRBorrowType::Owned:
                        // No change
                        break;
                    case HIRBorrowType::Unique:
                        switch (bindingMode) {
                            case HIRPatternBinding::Type::Move:
                            case HIRPatternBinding::Type::MutRef:
                                bindingMode = HIRPatternBinding::Type::MutRef;
                                break;
                            case HIRPatternBinding::Type::Ref:
                                // No change
                                break;
                        }
                        break;
                    case HIRBorrowType::Shared:
                        bindingMode = HIRPatternBinding::Type::Ref;
                        break;
                }

                if (!pattern.data.is_Deref() && !directlyMatches(pattern, ty)) {
                    HIRPattern::DerefKind derefKind;
                    HIRTypeRef target;
                    if (const auto* inner = context.resolve.typeIsOwnedBox(sp, ty)) {
                        derefKind = HIRPattern::DerefKind::Box;
                        target = inner;
                    } else {
                        ::std::optional<HIRTypeRef> implType;
                        const auto result = context.resolve.autoderefStep(sp, ty, target, &implType);
                        if (result == TraitResolution::AutoderefResult::Ambiguous) return false;
                        if (result == TraitResolution::AutoderefResult::NoMatch || !implType) {
                            ERROR(sp, E0000, "Pattern " << pattern << " cannot match " << ty);
                        }
                        context.equateTypes(sp, ty, *implType);
                        context.equateTypesAssoc(sp, target, context.crate.getLangItemPath(sp, "deref"), {}, ty, "Target", {}, true, TypeckPrimitiveOperator::Deref);
                        context.addTraitBound(sp, ty, context.crate.getLangItemPath(sp, "deref_pure"), {});
                        const bool unique = bindingMode == HIRPatternBinding::Type::MutRef || hasMutableBinding(pattern);
                        if (unique) context.addTraitBound(sp, ty, context.crate.getLangItemPath(sp, "deref_mut"), {});
                        derefKind = unique ? HIRPattern::DerefKind::Unique : HIRPattern::DerefKind::Shared;
                    }

                    HIRPattern inner(::std::vector<HIRPatternBinding>{}, mv$(pattern.data));
                    pattern.data = HIRPattern::Data::make_Deref({derefKind, target, box$(mv$(inner))});
                    auto& deref = pattern.data.as_Deref();
                    // An overloaded dereference borrows the smart pointer, but unlike
                    // peeling an `&` it does not change the default binding mode of
                    // bindings in the inner pattern.
                    return this->revisitInner(context, *deref.sub, target, bindingMode);
                }

                bool rv = false;
                switch (pattern.data.tag()) {
                    case HIRPatternData::TAG_Ref: {
                        BUG(sp, "Match ergonomics - `&` pattern already handled");
                        break;
                    }
                    case HIRPatternData::TAG_Or: {
                        BUG(sp, "Match ergonomics - `|` pattern already handled");
                        break;
                    }
                    case HIRPatternData::TAG_Any: {
                        // no-op
                        rv = true;
                        break;
                    }
                    case HIRPatternData::TAG_Value: {
                        auto& pe = pattern.data.as_Value();
                        if (pe.val.is_String()) {
                            if (!(ty->is_Primitive() && ty->as_Primitive() == HIRCoreType::Str)) {
                                ASSERT_BUG(sp, pattern.implicitDerefCount >= 1, "");
                                pattern.implicitDerefCount -= 1;
                            }
                        } else if (pe.val.is_ByteString()) {
                            const auto& bytes = pe.val.as_ByteString().v;
                            if (const auto* array = ty->opt_Array()) {
                                context.equateTypes(sp, array->inner, context.crate.types.primitive(HIRCoreType::U8));
                                if (array->size.is_Known() && array->size.as_Known() != bytes.size()) {
                                    ERROR(sp, E0000, "Byte string pattern has length " << bytes.size() << ", but is matching " << ty);
                                }
                            } else if (const auto* slice = ty->opt_Slice()) {
                                context.equateTypes(sp, slice->inner, context.crate.types.primitive(HIRCoreType::U8));
                            } else {
                                ASSERT_BUG(sp, pattern.implicitDerefCount >= 1, "");
                                pattern.implicitDerefCount -= 1;
                            }
                        }
                        rv = true;
                        break;
                    }
                    case HIRPatternData::TAG_Range: {
                        // no-op?
                        rv = true;
                        break;
                    }
                    case HIRPatternData::TAG_Box: {
                        auto& pe = pattern.data.as_Box();
                        // Box<T>
                        if (((*ty).is_Path() && (*ty).as_Path().path.data.is_Generic() && (*ty).as_Path().path.data.as_Generic().path == context.langBox)) {
                            const auto& path = ty->as_Path().path.data.as_Generic();
                            const auto& inner = path.params.types.at(0);
                            rv = this->revisitInner(context, *pe.sub, inner, bindingMode);
                        } else {
                            TODO(sp, "Match ergonomics - box pattern - Non Box<T> type: " << ty);
                            //::HIR::GenericPath  path { m_lang_Box, ::HIR::PathParams(mv$(inner)) };
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Deref: {
                        auto& pe = pattern.data.as_Deref();
                        if (const auto* inner = context.resolve.typeIsOwnedBox(sp, ty)) {
                            pe.kind = HIRPattern::DerefKind::Box;
                            pe.targetType = inner;
                            rv = this->revisitInner(context, *pe.sub, inner, bindingMode);
                            break;
                        }

                        HIRTypeRef target;
                        ::std::optional<HIRTypeRef> implType;
                        const auto result = context.resolve.autoderefStep(sp, ty, target, &implType);
                        if (result == TraitResolution::AutoderefResult::Ambiguous) return false;
                        if (result == TraitResolution::AutoderefResult::NoMatch || !implType) {
                            ERROR(sp, E0000, "Type " << ty << " cannot be used in a deref pattern");
                        }
                        context.equateTypes(sp, ty, *implType);
                        context.equateTypesAssoc(sp, target, context.crate.getLangItemPath(sp, "deref"), {}, ty, "Target", {}, true, TypeckPrimitiveOperator::Deref);
                        context.addTraitBound(sp, ty, context.crate.getLangItemPath(sp, "deref_pure"), {});

                        const bool unique = bindingMode == HIRPatternBinding::Type::MutRef || hasMutableBinding(*pe.sub);
                        if (unique) context.addTraitBound(sp, ty, context.crate.getLangItemPath(sp, "deref_mut"), {});
                        pe.kind = unique ? HIRPattern::DerefKind::Unique : HIRPattern::DerefKind::Shared;
                        pe.targetType = target;
                        rv = this->revisitInner(context, *pe.sub, target, bindingMode);
                        break;
                    }
                    case HIRPatternData::TAG_Tuple: {
                        auto& e = pattern.data.as_Tuple();
                        if (!ty->is_Tuple()) {
                            ERROR(sp, E0000, "Matching a non-tuple with a tuple pattern - " << ty);
                        }
                        const auto& te = ty->as_Tuple();
                        if (e.subPatterns.size() != te.size()) {
                            ERROR(sp, E0000, "Tuple pattern with an incorrect number of fields, expected " << e.subPatterns.size() << "-tuple, got " << ty);
                        }

                        rv = true;
                        for (unsigned int i = 0; i < e.subPatterns.size(); i++) {
                            rv &= this->revisitInner(context, e.subPatterns[i], te[i], bindingMode);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitTuple: {
                        auto& pe = pattern.data.as_SplitTuple();
                        if (!ty->is_Tuple()) {
                            ERROR(sp, E0000, "Matching a non-tuple with a tuple pattern - " << ty);
                        }
                        const auto& te = ty->as_Tuple();
                        if (pe.leading.size() + pe.trailing.size() > te.size()) {
                            ERROR(sp, E0000, "Split-tuple pattern with an incorrect number of fields, expected at most " << (pe.leading.size() + pe.trailing.size()) << "-tuple, got " << te.size());
                        }
                        pe.totalSize = te.size();
                        rv = true;
                        for (size_t i = 0; i < pe.leading.size(); i++) {
                            rv &= this->revisitInner(context, pe.leading[i], te[i], bindingMode);
                        }
                        for (size_t i = 0; i < pe.trailing.size(); i++) {
                            rv &= this->revisitInner(context, pe.trailing[i], te[te.size() - pe.trailing.size() + i], bindingMode);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Slice: {
                        auto& e = pattern.data.as_Slice();
                        const HIRTypeData* sliceInner;
                        if (const auto* te = ty->opt_Slice()) {
                            sliceInner = te->inner;
                        } else if (const auto* te = ty->opt_Array()) {
                            sliceInner = te->inner;
                            // Equate the array size
                            context.equateTypes(sp, ty, context.crate.types.array(sliceInner, e.subPatterns.size()));
                        } else {
                            ERROR(sp, E0000, "Matching a non-array/slice with a slice pattern - " << ty);
                        }
                        rv = true;
                        for (auto& sub : e.subPatterns) {
                            rv |= this->revisitInner(context, sub, sliceInner, bindingMode);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        auto& pe = pattern.data.as_SplitSlice();
                        const HIRTypeData* sliceInner;
                        if (const auto* te = ty->opt_Slice()) {
                            sliceInner = te->inner;
                        } else if (const auto* te = ty->opt_Array()) {
                            sliceInner = te->inner;
                        } else {
                            ERROR(sp, E0000, "Matching a non-array/slice with a slice pattern - " << ty);
                        }
                        rv = true;
                        for (auto& sub : pe.leading) {
                            rv |= this->revisitInner(context, sub, sliceInner, bindingMode);
                        }
                        if (pe.extraBind.isValid()) {
                            HIRTypeRef bindingTyInner = context.crate.types.slice(sliceInner);
                            // TODO: Do arrays get bound as arrays?
                            if (ty->is_Array()) {
                                size_t sizeSub = pe.leading.size() + pe.trailing.size();
                                bindingTyInner = context.crate.types.array(sliceInner, ty->as_Array().size.as_Known() - sizeSub);
                                //TODO(sp, "SplitSlice extra bind with array: " << pe.extra_bind << " on " << ty);
                            }
                            HIRTypeRef bindingTy;
                            if (pe.extraBind.type == HIRPatternBinding::Type::Move) {
                                pe.extraBind.type = bindingMode;
                            }
                            switch (pe.extraBind.type) {
                                case HIRPatternBinding::Type::Move:
                                    // Only valid for an array?
                                    ASSERT_BUG(sp, ty->is_Array(), "Non-array SplitSlize move bind");
                                    bindingTy = mv$(bindingTyInner);
                                    break;
                                case HIRPatternBinding::Type::Ref:
                                    bindingTy = context.crate.types.borrow(HIRBorrowType::Shared, bindingTyInner);
                                    break;
                                case HIRPatternBinding::Type::MutRef:
                                    bindingTy = context.crate.types.borrow(HIRBorrowType::Unique, bindingTyInner);
                                    break;
                            }
                            context.equateTypes(sp, context.getVar(sp, pe.extraBind.slot), bindingTy);
                        }
                        for (auto& sub : pe.trailing) {
                            rv |= this->revisitInner(context, sub, sliceInner, bindingMode);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathValue: {
                        auto& e = pattern.data.as_PathValue();
                        auto possibleType = getPossibleTypeInner(context, pattern);
                            ASSERT_BUG(sp, possibleType, "No type for path pattern " << pattern);
                            context.equateTypes(sp, ty, *possibleType);

                        switch (e.binding.tag()) {
                            case HIRPatternPathBinding::TAG_Unbound: {
                                throw "";
                            }
                            case HIRPatternPathBinding::TAG_Struct: {
                                auto& be = e.binding.as_Struct();
                                const auto& str = *be;
                                ASSERT_BUG(sp, str.data.is_Unit(), "PathValue used on non-unit struct variant");
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Union: {
                                BUG(sp, "PathValue used for union");
                                break;
                            }
                            case HIRPatternPathBinding::TAG_Enum: {
                                auto& be = e.binding.as_Enum();
                                const auto& enm = *be.ptr;
                                if (const auto* ee = enm.data.opt_Data()) {
                                    ASSERT_BUG(sp, be.varIdx < ee->size(), "");
                                    const auto& var = (*ee)[be.varIdx];
                                    ASSERT_BUG(sp, var.type == context.crate.types.unit(), "EnumValue used on non-value enum variant");
                                }
                                break;
                            }
                        }
                        rv = true;
                        break;
                    }
                    case HIRPatternData::TAG_PathTuple: {
                        auto& e = pattern.data.as_PathTuple();
                        auto possibleType = getPossibleTypeInner(context, pattern);
                        ASSERT_BUG(sp, possibleType, "No type for tuple path pattern " << pattern);
                        context.equateTypes(sp, ty, *possibleType);

                        const auto& sd = patternGetTuple(sp, e.path, e.binding);

                        auto ms = MonomorphStatePtr(context.crate.types, nullptr, &e.path.data.as_Generic().params, nullptr);
                        HIRTypeRef tmp;
                        auto maybeMonomorph = [&](const HIRTypeData* fieldType) -> const HIRTypeData* {
                            return (monomorphiseTypeNeeded(fieldType) ? (tmp = context.resolve.expandAssociatedTypes(sp, ms.monomorphType(sp, fieldType))) : fieldType);
                        };

                        e.totalSize = sd.size();

                        rv = true;
                        for (unsigned int i = 0; i < e.leading.size(); i++) {
                            /*const*/ auto& subPat = e.leading[i];
                            const auto& varTy = maybeMonomorph(sd[i].ent);
                            rv &= this->revisitInner(context, subPat, varTy, bindingMode);
                        }
                        for (unsigned int i = 0; i < e.trailing.size(); i++) {
                            /*const*/ auto& subPat = e.trailing[i];
                            const auto& varTy = maybeMonomorph(sd[sd.size() - e.trailing.size() + i].ent);
                            rv &= this->revisitInner(context, subPat, varTy, bindingMode);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathNamed: {
                        auto& e = pattern.data.as_PathNamed();
                        auto possibleType = getPossibleTypeInner(context, pattern);
                        ASSERT_BUG(sp, possibleType, "No type for named path pattern " << pattern);
                        context.equateTypes(sp, ty, *possibleType);

                        //if( ! e.is_wildcard() )
                        if (e.subPatterns.empty()) {
                            // TODO: Check the field count?
                            rv = true;
                        } else {
                            const auto& sd = patternGetNamed(sp, e.path, e.binding);

                            auto ms = MonomorphStatePtr(context.crate.types, nullptr, &e.path.data.as_Generic().params, nullptr);
                            HIRTypeRef tmp;
                            auto maybeMonomorph = [&](const HIRTypeData* fieldType) -> const HIRTypeData* {
                                return (monomorphiseTypeNeeded(fieldType) ? (tmp = context.resolve.expandAssociatedTypes(sp, ms.monomorphType(sp, fieldType))) : fieldType);
                            };

                            rv = true;
                            for (auto& fieldPat : e.subPatterns) {
                                unsigned int fIdx = ::std::find_if(sd.begin(), sd.end(), [&](const HIRStructField& x) {
                                    return x.name == fieldPat.first;
                                }) - sd.begin();
                                if (fIdx == sd.size()) {
                                    ERROR(sp, E0000, "Struct " << e.path << " doesn't have a field " << fieldPat.first);
                                }
                                const HIRTypeData* fieldType = maybeMonomorph(sd[fIdx].ty);
                                rv &= this->revisitInner(context, fieldPat.second, fieldType, bindingMode);
                            }
                        }
                        break;
                    }
                }
                return rv;
            }

            static void disablePossibilitiesOnBindings(const Span& sp, Context& context, const HIRPattern& pat, bool isTopLevel = false) {
                if (!isTopLevel) {
                    for (const auto& pb : pat.bindings) {
                        context.possibleEquateTypeUnknown(sp, context.getVar(sp, pb.slot), Context::IvarUnknownType::Bound);
                    }
                }
                switch (pat.data.tag()) {
                    case HIRPatternData::TAG_Any: {
                        break;
                    }
                    case HIRPatternData::TAG_Value: {
                        break;
                    }
                    case HIRPatternData::TAG_Range: {
                        break;
                    }
                    case HIRPatternData::TAG_Box: {
                        auto& e = pat.data.as_Box();
                        disablePossibilitiesOnBindings(sp, context, *e.sub);
                        break;
                    }
                    case HIRPatternData::TAG_Deref: {
                        auto& e = pat.data.as_Deref();
                        disablePossibilitiesOnBindings(sp, context, *e.sub);
                        break;
                    }
                    case HIRPatternData::TAG_Ref: {
                        auto& e = pat.data.as_Ref();
                        disablePossibilitiesOnBindings(sp, context, *e.sub);
                        break;
                    }
                    case HIRPatternData::TAG_Tuple: {
                        auto& e = pat.data.as_Tuple();
                        for (auto& subpat : e.subPatterns) {
                            disablePossibilitiesOnBindings(sp, context, subpat);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitTuple: {
                        auto& e = pat.data.as_SplitTuple();
                        for (auto& subpat : e.leading) {
                            disablePossibilitiesOnBindings(sp, context, subpat);
                        }
                        for (auto& subpat : e.trailing) {
                            disablePossibilitiesOnBindings(sp, context, subpat);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Slice: {
                        auto& e = pat.data.as_Slice();
                        for (auto& sub : e.subPatterns) {
                            disablePossibilitiesOnBindings(sp, context, sub);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        auto& e = pat.data.as_SplitSlice();
                        for (auto& sub : e.leading) {
                            disablePossibilitiesOnBindings(sp, context, sub);
                        }
                        if (e.extraBind.isValid()) {
                            context.possibleEquateTypeUnknown(sp, context.getVar(sp, e.extraBind.slot), Context::IvarUnknownType::Bound);
                        }
                        for (auto& sub : e.trailing) {
                            disablePossibilitiesOnBindings(sp, context, sub);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathValue: {
                        break;
                    }
                    case HIRPatternData::TAG_PathTuple: {
                        auto& e = pat.data.as_PathTuple();
                        for (auto& subpat : e.leading) {
                            disablePossibilitiesOnBindings(sp, context, subpat);
                        }
                        for (auto& subpat : e.trailing) {
                            disablePossibilitiesOnBindings(sp, context, subpat);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathNamed: {
                        auto& e = pat.data.as_PathNamed();
                        for (auto& fieldPat : e.subPatterns) {
                            disablePossibilitiesOnBindings(sp, context, fieldPat.second);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Or: {
                        auto& e = pat.data.as_Or();
                        for (auto& subpat : e) {
                            disablePossibilitiesOnBindings(sp, context, subpat);
                        }
                        break;
                    }
                }
            }

            static void createBindings(const Span& sp, Context& context, HIRPattern& pat) {
                for (const auto& pb : pat.bindings) {
                    context.addVar(sp, pb.slot, pb.name, context.ivars.newIvarTr());
                    // TODO: Ensure that there's no more bindings below this?
                    // - I'll leave the option open, MIR generation should handle cases where there's multiple borrows
                    //   or moves.
                }
                switch (pat.data.tag()) {
                    case HIRPatternData::TAG_Any: {
                        break;
                    }
                    case HIRPatternData::TAG_Value: {
                        break;
                    }
                    case HIRPatternData::TAG_Range: {
                        break;
                    }
                    case HIRPatternData::TAG_Box: {
                        auto& e = pat.data.as_Box();
                        createBindings(sp, context, *e.sub);
                        break;
                    }
                    case HIRPatternData::TAG_Deref: {
                        auto& e = pat.data.as_Deref();
                        createBindings(sp, context, *e.sub);
                        break;
                    }
                    case HIRPatternData::TAG_Ref: {
                        auto& e = pat.data.as_Ref();
                        createBindings(sp, context, *e.sub);
                        break;
                    }
                    case HIRPatternData::TAG_Tuple: {
                        auto& e = pat.data.as_Tuple();
                        for (auto& subpat : e.subPatterns) {
                            createBindings(sp, context, subpat);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitTuple: {
                        auto& e = pat.data.as_SplitTuple();
                        for (auto& subpat : e.leading) {
                            createBindings(sp, context, subpat);
                        }
                        for (auto& subpat : e.trailing) {
                            createBindings(sp, context, subpat);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Slice: {
                        auto& e = pat.data.as_Slice();
                        for (auto& sub : e.subPatterns) {
                            createBindings(sp, context, sub);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_SplitSlice: {
                        auto& e = pat.data.as_SplitSlice();
                        for (auto& sub : e.leading) {
                            createBindings(sp, context, sub);
                        }
                        if (e.extraBind.isValid()) {
                            const auto& pb = e.extraBind;
                            context.addVar(sp, pb.slot, pb.name, context.ivars.newIvarTr());
                        }
                        for (auto& sub : e.trailing) {
                            createBindings(sp, context, sub);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathValue: {
                        break;
                    }
                    case HIRPatternData::TAG_PathTuple: {
                        auto& e = pat.data.as_PathTuple();
                        for (auto& subpat : e.leading) {
                            createBindings(sp, context, subpat);
                        }
                        for (auto& subpat : e.trailing) {
                            createBindings(sp, context, subpat);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_PathNamed: {
                        auto& e = pat.data.as_PathNamed();
                        for (auto& fieldPat : e.subPatterns) {
                            createBindings(sp, context, fieldPat.second);
                        }
                        break;
                    }
                    case HIRPatternData::TAG_Or: {
                        auto& e = pat.data.as_Or();
                        assert(e.size() > 0);
                        createBindings(sp, context, e[0]);
                        // TODO: Ensure that the other arms have the same binding set
                        break;
                    }
                }
            }
        };

        // - Create variables, assigning new ivars for all of them.
        MatchErgonomicsRevisit::createBindings(sp, *this, pat);
        // - Try the pattern right away: if the matched type is already known, the
        //   bindings get their real types before the body is visited. A binding
        //   left as a bare ivar until the revisit runs would instead be decided
        //   by its first use, which is wrong for a `!` binding (every use of it
        //   coerces separately).
        DEBUG("Handle match ergonomics - " << pat << " with " << type);
        auto revisit = box$((MatchErgonomicsRevisit{sp, isIrrefutable, type, pat}));
        if (!revisit->revisit(*this, false)) {
            this->addRevisitAdv(mv$(revisit));
        }
        return;
    }

    // ---
    this->handlePatternDirectInner(sp, pat, type);
}

void Context::handlePatternDirectInner(const Span& sp, HIRPattern& pat, const HIRTypeData* type) {
    TRACE_FUNCTION_F("pat = " << pat << ", type = " << type);

    for (const auto& pb : pat.bindings) {
        this->addBindingInner(sp, pb, type);
    }

    struct H {
        static void handleValue(Context& context, const Span& sp, const HIRTypeData* type, HIRPattern::Value& val) {
            switch (val.tag()) {
                case HIRPattern::Value::TAG_Integer: {
                    auto& v = val.as_Integer();
                    DEBUG("Integer " << v.type);
                    // TODO: Apply an ivar bound? (Require that this ivar be an integer?)
                    if (v.type != HIRCoreType::Str) { context.equateTypes(sp, type, context.crate.types.primitive(v.type)); }
                    break;
                }
                case HIRPattern::Value::TAG_Float: {
                    auto& v = val.as_Float();
                    DEBUG("Float " << v.type);
                    // TODO: Apply an ivar bound? (Require that this ivar be a float?)
                    if (v.type != HIRCoreType::Str) { context.equateTypes(sp, type, context.crate.types.primitive(v.type)); }
                    break;
                }
                case HIRPattern::Value::TAG_String: {
                    context.equateTypes(sp, type, context.crate.types.borrow(HIRBorrowType::Shared, context.crate.types.primitive(HIRCoreType::Str)));
                    break;
                }
                case HIRPattern::Value::TAG_ByteString: {
                    // NOTE: Matches both &[u8] and &[u8; N], so doesn't provide type information
                    // TODO: Check the type.
                    break;
                }
                case HIRPattern::Value::TAG_Named: {
                    auto& v = val.as_Named();
                    // A trait-associated constant: equate against its type through the (already
                    // populated) trait params, so `<S as Format<_>>::FORMAT` pins the `_`.
                    if (v.binding && v.path.data.is_UfcsKnown()) {
                        const auto& pe = v.path.data.as_UfcsKnown();
                        auto ms = MonomorphStatePtr(context.crate.types, pe.type, &pe.trait.params, nullptr);
                        context.equateTypes(sp, type, ms.monomorphType(sp, v.binding->type));
                    }
                    break;
                }
            }
        }

        static HIRTypeRef getPathType(Context& context, const Span& sp, HIRPath& path, const HIRPattern::PathBinding& binding) {
            switch (binding.tag()) {
                case HIRPatternPathBinding::TAG_Unbound: {
                    auto& _ = binding.as_Unbound();
                    BUG(sp, "");
                    break;
                }
                case HIRPatternPathBinding::TAG_Struct: {
                    auto& be = binding.as_Struct();
                    auto& p = path.data.as_Generic();
                    assert(be);
                    context.addIvarsParams(p.params);
                    return context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                }
                case HIRPatternPathBinding::TAG_Union: {
                    auto& be = binding.as_Union();
                    auto& p = path.data.as_Generic();
                    assert(be);
                    context.addIvarsParams(p.params);
                    return context.crate.types.path(p.clone(), HIRTypePathBinding(be));
                }
                case HIRPatternPathBinding::TAG_Enum: {
                    auto& be = binding.as_Enum();
                    auto& p = path.data.as_Generic();
                    assert(be.ptr);
                    context.addIvarsParams(p.params);
                    return context.crate.types.path(getParentPath(p), HIRTypePathBinding(be.ptr));
                }
            }
            throw "";
        }
    };

    switch (pat.data.tag()) {
        case HIRPatternData::TAG_Any: {
            // Just leave it, the pattern says nothing
            break;
        }
        case HIRPatternData::TAG_Value: {
            auto& e = pat.data.as_Value();
            H::handleValue(*this, sp, type, e.val);
            break;
        }
        case HIRPatternData::TAG_Range: {
            auto& e = pat.data.as_Range();
            if (e.start) {
                H::handleValue(*this, sp, type, *e.start);
            }
            if (e.end) {
                H::handleValue(*this, sp, type, *e.end);
            }
            break;
        }
        case HIRPatternData::TAG_Box: {
            auto& e = pat.data.as_Box();
            if (langBox == HIRSimplePath()) {
                ERROR(sp, E0000, "Use of `box` pattern without the `owned_box` lang item");
            }
            const auto& ty = this->getType(type);
            // Two options:
            // 1. Enforce that the current type must be "owned_box"
            // 2. Make a new ivar for the inner and emit an associated type bound on Deref

            // Taking option 1 for now
            if (const auto* te = ty->opt_Path()) {
                if ((te->path.data.is_Generic() && (te->path.data.as_Generic().path == langBox))) {
                    // Box<T>
                    const auto& inner = te->path.data.as_Generic().params.types.at(0);
                    this->handlePatternDirectInner(sp, *e.sub, inner);
                    break;
                }
            }

            auto inner = this->ivars.newIvarTr();
            this->handlePatternDirectInner(sp, *e.sub, inner);
            HIRGenericPath path{langBox, HIRPathParams(mv$(inner))};
            this->equateTypes(sp, type, crate.types.path(mv$(path), HIRTypePathBinding(&crate.getStructByPath(sp, langBox))));
            break;
        }
        case HIRPatternData::TAG_Deref: {
            auto& e = pat.data.as_Deref();
            const auto* ty = this->getType(type);
            if (const auto* inner = resolve.typeIsOwnedBox(sp, ty)) {
                e.kind = HIRPattern::DerefKind::Box;
                e.targetType = inner;
                this->handlePatternDirectInner(sp, *e.sub, inner);
                break;
            }
            HIRTypeRef target;
            ::std::optional<HIRTypeRef> implType;
            const auto result = resolve.autoderefStep(sp, ty, target, &implType);
            if (result != TraitResolution::AutoderefResult::Match || !implType) {
                ERROR(sp, E0000, "Type " << ty << " cannot be used in a deref pattern");
            }
            equateTypes(sp, ty, *implType);
            equateTypesAssoc(sp, target, crate.getLangItemPath(sp, "deref"), {}, ty, "Target", {}, true, TypeckPrimitiveOperator::Deref);
            addTraitBound(sp, ty, crate.getLangItemPath(sp, "deref_pure"), {});
            e.kind = HIRPattern::DerefKind::Shared;
            e.targetType = target;
            this->handlePatternDirectInner(sp, *e.sub, target);
            break;
        }
        case HIRPatternData::TAG_Ref: {
            auto& e = pat.data.as_Ref();
            const auto& ty = this->getType(type);
            if (const auto* te = ty->opt_Borrow()) {
                if (te->type != e.type) {
                    ERROR(sp, E0000, "Pattern-type mismatch, &-ptr mutability mismatch");
                }
                this->handlePatternDirectInner(sp, *e.sub, te->inner);
            } else {
                auto inner = this->ivars.newIvarTr();
                this->handlePatternDirectInner(sp, *e.sub, inner);
                this->equateTypes(sp, type, crate.types.borrow(e.type, inner));
            }
            break;
        }
        case HIRPatternData::TAG_Tuple: {
            auto& e = pat.data.as_Tuple();
            const auto& ty = this->getType(type);
            if (const auto* tep = ty->opt_Tuple()) {
                const auto& te = *tep;
                if (e.subPatterns.size() != te.size()) {
                    ERROR(sp, E0000, "Tuple pattern with an incorrect number of fields, expected " << e.subPatterns.size() << "-tuple, got " << ty);
                }

                for (unsigned int i = 0; i < e.subPatterns.size(); i++) {
                    this->handlePatternDirectInner(sp, e.subPatterns[i], te[i]);
                }
            } else {
                ::std::vector<HIRTypeRef> subTypes;
                for (unsigned int i = 0; i < e.subPatterns.size(); i++) {
                    subTypes.push_back(this->ivars.newIvarTr());
                    this->handlePatternDirectInner(sp, e.subPatterns[i], subTypes[i]);
                }
                this->equateTypes(sp, ty, crate.types.tuple(mv$(subTypes)));
            }
            break;
        }
        case HIRPatternData::TAG_SplitTuple: {
            auto& e = pat.data.as_SplitTuple();
            const auto& ty = this->getType(type);
            if (const auto* tep = ty->opt_Tuple()) {
                const auto& te = *tep;
                // - Should have been checked in AST resolve
                ASSERT_BUG(sp, e.leading.size() + e.trailing.size() <= te.size(), "Invalid field count for split tuple pattern");

                unsigned int tupIdx = 0;
                for (auto& subpat : e.leading) {
                    this->handlePatternDirectInner(sp, subpat, te[tupIdx++]);
                }
                tupIdx = te.size() - e.trailing.size();
                for (auto& subpat : e.trailing) {
                    this->handlePatternDirectInner(sp, subpat, te[tupIdx++]);
                }

                // TODO: Should this replace the pattern with a non-split?
                // - Changing the address of the pattern means that the below revisit could fail.
                e.totalSize = te.size();
            } else {
                if (!ty->is_Infer()) {
                    ERROR(sp, E0000, "Tuple pattern on non-tuple");
                }

                ::std::vector<HIRTypeRef> leadingTys;
                leadingTys.reserve(e.leading.size());
                for (auto& subpat : e.leading) {
                    leadingTys.push_back(this->ivars.newIvarTr());
                    this->handlePatternDirectInner(sp, subpat, leadingTys.back());
                }
                ::std::vector<HIRTypeRef> trailingTys;
                for (auto& subpat : e.trailing) {
                    trailingTys.push_back(this->ivars.newIvarTr());
                    this->handlePatternDirectInner(sp, subpat, trailingTys.back());
                }

                struct SplitTuplePatRevisit: public Revisitor {
                    Span sp;
                    HIRTypeRef outerTy;
                    ::std::vector<HIRTypeRef> leadingTys;
                    ::std::vector<HIRTypeRef> trailingTys;
                    unsigned int& patTotalSize;

                    SplitTuplePatRevisit(Span sp, HIRTypeRef outer, ::std::vector<HIRTypeRef> leading, ::std::vector<HIRTypeRef> trailing, unsigned int& patTotalSize)
                        : sp(mv$(sp))
                        , outerTy(mv$(outer))
                        , leadingTys(mv$(leading))
                        , trailingTys(mv$(trailing))
                        , patTotalSize(patTotalSize)
                    {
                    }

                    const Span& span() const override {
                        return sp;
                    }
                    void fmt(::std::ostream& os) const override {
                        os << "SplitTuplePatRevisit { " << outerTy << " = (" << leadingTys << ", ..., " << trailingTys << ") }";
                    }
                    bool revisit(Context& context, bool isFallback) override {
                        const auto& ty = context.getType(outerTy);
                        if (ty->is_Infer()) {
                            return false;
                        } else if (const auto* tep = ty->opt_Tuple()) {
                            const auto& te = *tep;
                            if (te.size() < leadingTys.size() + trailingTys.size()) {
                                ERROR(sp, E0000, "Tuple pattern too large for tuple");
                            }
                            for (unsigned int i = 0; i < leadingTys.size(); i++) {
                                context.equateTypes(sp, te[i], leadingTys[i]);
                            }
                            unsigned int ofs = te.size() - trailingTys.size();
                            for (unsigned int i = 0; i < trailingTys.size(); i++) {
                                context.equateTypes(sp, te[ofs + i], trailingTys[i]);
                            }
                            patTotalSize = te.size();
                            return true;
                        } else {
                            ERROR(sp, E0000, "Tuple pattern on non-tuple - " << ty);
                        }
                    }
                };

                // Register a revisit and wait until the tuple is known - then bind through.
                this->addRevisitAdv(box$((SplitTuplePatRevisit{sp, ty, mv$(leadingTys), mv$(trailingTys), e.totalSize})));
            }
            break;
        }
        case HIRPatternData::TAG_Slice: {
            auto& e = pat.data.as_Slice();
            const auto& ty = this->getType(type);
            switch ((*ty).tag()) {
default:
                ERROR(sp, E0000, "Slice pattern on non-array/-slice - " << ty);
                case HIRTypeData::TAG_Slice: {
                    auto& te = (*ty).as_Slice();
                    for (auto& sub : e.subPatterns) {
                        this->handlePatternDirectInner(sp, sub, te.inner);
                    }
                    break;
                }
                case HIRTypeData::TAG_Array: {
                    auto& te = (*ty).as_Array();
                    for (auto& sub : e.subPatterns) {
                        this->handlePatternDirectInner(sp, sub, te.inner);
                    }
                    break;
                }
                case HIRTypeData::TAG_Infer: {
                    auto inner = this->ivars.newIvarTr();
                    for (auto& sub : e.subPatterns) {
                        this->handlePatternDirectInner(sp, sub, inner);
                    }

                    struct SlicePatRevisit: public Revisitor {
                        Span sp;
                        HIRTypeRef inner;
                        HIRTypeRef type;
                        unsigned int size;

                        SlicePatRevisit(Span sp, HIRTypeRef inner, HIRTypeRef type, unsigned int size)
                            : sp(mv$(sp))
                            , inner(mv$(inner))
                            , type(mv$(type))
                            , size(size)
                        {
                        }

                        const Span& span() const override {
                            return sp;
                        }
                        void fmt(::std::ostream& os) const override {
                            os << "SlicePatRevisit { " << inner << ", " << type << ", " << size;
                        }
                        bool revisit(Context& context, bool isFallback) override {
                            const auto& ty = context.getType(type);
                    switch ((*ty).tag()) {
default:
                        ERROR(sp, E0000, "Slice pattern on non-array/-slice - " << ty);
                        case HIRTypeData::TAG_Infer: {
                            return false;
                        }
                        case HIRTypeData::TAG_Slice: {
                            auto& te = (*ty).as_Slice();
                            context.equateTypes(sp, te.inner, inner);
                            return true;
                        }
                        case HIRTypeData::TAG_Array: {
                            auto& te = (*ty).as_Array();
                            if (te.size.as_Known() != size) {
                                ERROR(sp, E0000, "Slice pattern on an array if differing size");
                            }
                            context.equateTypes(sp, te.inner, inner);
                            return true;
                        }
                    }
                    throw "unreachable"; //UNREACHABLE();
                        }
                    };
                    this->addRevisitAdv(box$((SlicePatRevisit{sp, mv$(inner), ty, static_cast<unsigned int>(e.subPatterns.size())})));
                    break;
                }
            }
            break;
        }
        case HIRPatternData::TAG_SplitSlice: {
            auto& e = pat.data.as_SplitSlice();
            HIRTypeRef inner;
                unsigned int minLen = e.leading.size() + e.trailing.size();
                const auto& ty = this->getType(type);
            switch ((*ty).tag()) {
default:
                ERROR(sp, E0000, "SplitSlice pattern on non-array/-slice - " << ty);
                case HIRTypeData::TAG_Slice: {
                    auto& te = (*ty).as_Slice();
                    // Slice - Fetch inner and set new variable also be a slice
                    // - TODO: Better new variable handling.
                    inner = te.inner;
                    if (e.extraBind.isValid()) {
                        this->addBindingInner(sp, e.extraBind, ty);
                    }
                    break;
                }
                case HIRTypeData::TAG_Array: {
                    auto& te = (*ty).as_Array();
                    inner = te.inner;
                    if (te.size.as_Known() < minLen) {
                        ERROR(sp, E0000, "Slice pattern on an array smaller than the pattern");
                    }
                    unsigned extra_len = te.size.as_Known() - minLen;

                    if (e.extraBind.isValid()) {
                        this->addBindingInner(sp, e.extraBind, crate.types.array(inner, extra_len));
                    }
                    break;
                }
                case HIRTypeData::TAG_Infer: {
                    inner = this->ivars.newIvarTr();
                    HIRTypeRef varTy;
                    if (e.extraBind.isValid()) {
                        varTy = this->ivars.newIvarTr();
                        this->addBindingInner(sp, e.extraBind, varTy);
                    }

                    struct SplitSlicePatRevisit: public Revisitor {
                        Span sp;
                        // Inner type
                        HIRTypeRef inner;
                        // Outer ivar (should be either Slice or Array)
                        HIRTypeRef type;
                        // Binding type (if not default value)
                        HIRTypeRef varTy;
                        unsigned int minSize;

                        SplitSlicePatRevisit(Span sp, HIRTypeRef inner, HIRTypeRef type, HIRTypeRef varTy, unsigned int size)
                            : sp(mv$(sp))
                            , inner(mv$(inner))
                            , type(mv$(type))
                            , varTy(mv$(varTy))
                            , minSize(size)
                        {
                        }

                        const Span& span() const override {
                            return sp;
                        }
                        void fmt(::std::ostream& os) const override {
                            os << "SplitSlice inner=" << inner << ", outer=" << type << ", binding=" << varTy << ", " << minSize;
                        }
                        bool revisit(Context& context, bool isFallback) override {
                            const auto& ty = context.getType(this->type);
                    switch ((*ty).tag()) {
default:
                        ERROR(sp, E0000, "Slice pattern on non-array/-slice - " << ty);
                        case HIRTypeData::TAG_Infer: {
                            return false;
                        }
                        case HIRTypeData::TAG_Slice: {
                            auto& te = (*ty).as_Slice();
                            // Slice - Equate inners
                            context.equateTypes(this->sp, this->inner, te.inner);
                            if (this->varTy != HIRTypeRef()) {
                                context.equateTypes(this->sp, this->varTy, ty);
                            }
                            break;
                        }
                        case HIRTypeData::TAG_Array: {
                            auto& te = (*ty).as_Array();
                            // Array - Equate inners and check size
                            context.equateTypes(this->sp, this->inner, te.inner);
                            if (te.size.as_Known() < this->minSize) {
                                ERROR(sp, E0000, "Slice pattern on an array smaller than the pattern");
                            }
                            unsigned extra_len = te.size.as_Known() - this->minSize;

                            if (this->varTy != HIRTypeRef()) {
                                context.equateTypes(this->sp, this->varTy, context.crate.types.array(this->inner, extra_len));
                            }
                            break;
                        }
                    }
                    return true;
                        }
                    };
                    // Callback
                    this->addRevisitAdv(box$((SplitSlicePatRevisit{sp, inner, ty, mv$(varTy), minLen})));
                    break;
                }
            }

            for(auto& sub : e.leading)
                this->handlePatternDirectInner( sp, sub, inner );
            for(auto& sub : e.trailing)
                this->handlePatternDirectInner( sp, sub, inner );
            break;
        }
        case HIRPatternData::TAG_PathValue: {
            auto& e = pat.data.as_PathValue();
            this->equateTypes(sp, type, H::getPathType(*this, sp, e.path, e.binding));
            switch (e.binding.tag()) {
                case HIRPatternPathBinding::TAG_Unbound: {
                    auto& _ = e.binding.as_Unbound();
                    BUG(sp, "");
                    break;
                }
                case HIRPatternPathBinding::TAG_Struct: {
                    assert(e.binding.as_Struct()->data.is_Unit());
                    break;
                }
                case HIRPatternPathBinding::TAG_Union: {
                    BUG(sp, "PathValue used for union");
                    break;
                }
                case HIRPatternPathBinding::TAG_Enum: {
                    auto& be = e.binding.as_Enum();
                    if (const auto* ee = be.ptr->data.opt_Data()) {
                        ASSERT_BUG(sp, be.varIdx < ee->size(), "");
                        const auto& var = (*ee)[be.varIdx];
                        if (var.type->is_Tuple() && var.type->as_Tuple().size() == 0) {
                            // All good
                        } else {
                            // TODO: Error here due to invalid variant type
                        }
                    }
                    break;
                }
            }
            break;
        }
        case HIRPatternData::TAG_PathTuple: {
            auto& e = pat.data.as_PathTuple();
            this->equateTypes(sp, type, H::getPathType(*this, sp, e.path, e.binding));

            const auto& sd = patternGetTuple(sp, e.path, e.binding);

            auto ms = MonomorphStatePtr(crate.types, nullptr, &e.path.data.as_Generic().params, nullptr);
            HIRTypeRef tmp;
            auto maybeMonomorph = [&](const HIRTypeData* ty) -> const HIRTypeData* {
                if (monomorphiseTypeNeeded(ty)) {
                    return (tmp = ms.monomorphType(sp, ty));
                } else {
                    return ty;
                }
            };
            if (e.isSplit) {
                ASSERT_BUG(sp, e.leading.size() + e.trailing.size() <= sd.size(), "PathTuple size mismatch, expected at most " << sd.size() << " fields but got " << e.leading.size() + e.trailing.size());
            } else {
                ASSERT_BUG(sp, e.leading.size() == sd.size(), "PathTuple size mismatch, expected " << sd.size() << " fields but got " << e.leading.size());
                assert(e.trailing.size() == 0);
            }
            e.totalSize = sd.size();

            for (size_t i = 0; i < e.leading.size(); i++) {
                /*const*/ auto& subPat = e.leading[i];
                this->handlePatternDirectInner(sp, subPat, maybeMonomorph(sd[i].ent));
            }
            for (size_t i = 0; i < e.trailing.size(); i++) {
                /*const*/ auto& subPat = e.trailing[i];
                this->handlePatternDirectInner(sp, subPat, maybeMonomorph(sd[sd.size() - e.trailing.size() + i].ent));
            }
            break;
        }
        case HIRPatternData::TAG_PathNamed: {
            auto& e = pat.data.as_PathNamed();
            this->equateTypes(sp, type, H::getPathType(*this, sp, e.path, e.binding));

            if (e.isWildcard()) {
                return;
            }

            const auto& sd = patternGetNamed(sp, e.path, e.binding);

            auto ms = MonomorphStatePtr(crate.types, nullptr, &e.path.data.as_Generic().params, nullptr);

            for (auto& fieldPat : e.subPatterns) {
                unsigned int fIdx = ::std::find_if(sd.begin(), sd.end(), [&](const auto& x) {
                    return x.name == fieldPat.first;
                }) - sd.begin();
                if (fIdx == sd.size()) {
                    ERROR(sp, E0000, "Struct " << e.path << " doesn't have a field " << fieldPat.first);
                }
                const HIRTypeData* fieldType = sd[fIdx].ty;
                if (monomorphiseTypeNeeded(fieldType)) {
                    auto fieldTypeMono = ms.monomorphType(sp, fieldType);
                    this->handlePatternDirectInner(sp, fieldPat.second, fieldTypeMono);
                } else {
                    this->handlePatternDirectInner(sp, fieldPat.second, fieldType);
                }
            }
            break;
        }
        case HIRPatternData::TAG_Or: {
            auto& e = pat.data.as_Or();
            for (auto& subpat : e) {
                this->handlePatternDirectInner(sp, subpat, type);
            }
            break;
        }
    }
}

void Context::recordCoercionHint(const HIRTypeData* type, HIRExprNodeP& nodePtr) {
    auto* hintNode = nodePtr.get();
    // A block's tail expression is checked with the block's expectation.
    while (const auto* block = cast<const HIRExprNodeBlock>(hintNode)) {
        if (!block->valueNode) {
            break;
        }
        hintNode = block->valueNode.get();
    }
    if (hintNode) {
        this->coercionHints[hintNode] = type;
    }
}

void Context::equateTypesCoerce(const Span& sp, const HIRTypeData* l, HIRExprNodeP& nodePtr) {
    this->ivars.getType(l);
    this->recordCoercionHint(l, nodePtr);
    // - Just record the equality
    this->linkCoerce.push_back(std::make_unique<Coercion>(Coercion{this->nextRuleIdx++, l, &nodePtr}));
    DEBUG("++ " << *this->linkCoerce.back());
    this->ivars.markChange();
}

void Context::possibleEquateTypeUnknown(const Span& sp, const HIRTypeData* ty, Context::IvarUnknownType src) {
    {
        auto& tuMatch = (*this->getType(ty));
        switch (tuMatch.tag()) {
default:
        // TODO: Shadow sub-types too
            break;
            case HIRTypeData::TAG_Path: {
                auto& e = tuMatch.as_Path();
                switch (e.path.data.tag()) {
default:
                    // TODO: Ufcs?
                    break;
                    case HIRPathData::TAG_Generic: {
                        auto& pe = e.path.data.as_Generic();
                        for (const auto& sty : pe.params.types) {
                            this->possibleEquateTypeUnknown(sp, sty, src);
                        }
                        break;
                    }
                }
                break;
            }
            case HIRTypeData::TAG_Tuple: {
                auto& e = tuMatch.as_Tuple();
                for (const auto& sty : e) {
                    this->possibleEquateTypeUnknown(sp, sty, src);
                }
                break;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& e = tuMatch.as_Borrow();
                this->possibleEquateTypeUnknown(sp, e.inner, src);
                break;
            }
            case HIRTypeData::TAG_Array: {
                auto& e = tuMatch.as_Array();
                this->possibleEquateTypeUnknown(sp, e.inner, src);
                break;
            }
            case HIRTypeData::TAG_Slice: {
                auto& e = tuMatch.as_Slice();
                this->possibleEquateTypeUnknown(sp, e.inner, src);
                break;
            }
            case HIRTypeData::TAG_NodeType: {
                auto& e = tuMatch.as_NodeType();
                if (e.is_Closure()) {
                    auto* nodeP = e.as_Closure();
                    for (const auto& aty : nodeP->args) {
                        this->possibleEquateTypeUnknown(sp, aty.second, src);
                    }
                    this->possibleEquateTypeUnknown(sp, nodeP->returnType, src);
                }
                break;
            }
            case HIRTypeData::TAG_Infer: {
                auto& e = tuMatch.as_Infer();
                this->possibleEquateIvarUnknown(sp, e.index, src);
                break;
            }
        }
    }
}

void Context::equateTypesAssoc(const Span& sp, const HIRTypeData* l, const HIRSimplePath& trait, HIRPathParams pp, const HIRTypeData* implTy, const char* name, const HIRPathParams& atyPp, bool isOp, TypeckPrimitiveOperator operatorKind) {
    for (const auto& a : this->linkAssoc) {
        if (a.leftTy != l) {
            continue;
        }
        if (a.trait != trait) {
            continue;
        }
        if (a.params != pp) {
            continue;
        }
        if (a.implTy != implTy) {
            continue;
        }
        if (a.atyPp != atyPp) {
            continue;
        }
        if (a.name != name) {
            continue;
        }
        if (a.isOperator != isOp) {
            continue;
        }
        if (a.operatorKind != operatorKind) {
            continue;
        }

        DEBUG("(DUPLICATE " << a << ")");
        return;
    }
    visitTyWith(implTy, [&](const HIRTypeData* ty) {
        if (const auto* path = ty->opt_Path()) {
            if (const auto* projection = path->path.data.opt_UfcsKnown()) {
                this->addTraitBound(sp, projection->type, projection->trait.path, projection->trait.params.clone());
            }
        }
        return false;
    });
    this->linkAssoc.push_back(
        Associated{
            this->nextRuleIdx++,
            sp,
            MonomorphEraseHrls(crate.types).monomorphType(sp, l, true),

            trait.clone(),
            MonomorphEraseHrls(crate.types).monomorphPathParams(sp, pp, true),
            MonomorphEraseHrls(crate.types).monomorphType(sp, implTy, true),
            name,
            MonomorphEraseHrls(crate.types).monomorphPathParams(sp, atyPp, true),
            isOp,
            operatorKind
        }
    );
    DEBUG("++ " << this->linkAssoc.back());
    this->ivars.markChange();
}

void Context::addRevisit(HIRExprNode& node) {
    this->toVisit.push_back(&node);
}

void Context::addRevisitAdv(::std::unique_ptr<Revisitor> entPtr) {
    this->advRevisits.push_back(mv$(entPtr));
}

void Context::requireSized(const Span& sp, const HIRTypeData* ty_) {
    const auto& ty = ivars.getType(ty_);
    TRACE_FUNCTION_F(ty_ << " -> " << ty);
    if (resolve.typeIsSized(sp, ty) == HIRCompare::Unequal) {
        ERROR(sp, E0000, "Unsized type not valid here - " << ty);
    }
    if (const auto* e = ty->opt_Infer()) {
        switch (e->tyClass) {
            case HIRInferClass::Integer:
            case HIRInferClass::Float:
                // Has to be.
                break;
            default:
                // TODO: Flag for future checking
                ASSERT_BUG(sp, e->index != ~0u, "Unbound ivar " << ty);
                if (e->index >= ivarsSized.size()) {
                    ivarsSized.resize(e->index + 1);
                }
                ivarsSized.at(e->index) = true;
                break;
        }
    } else if (const auto* e = ty->opt_Path()) {
        const HIRGenericParams* paramsDef = nullptr;
        switch (e->binding.tag()) {
            case HIRTypePathBinding::TAG_Unbound: {
                // TODO: Add a trait check rule
                paramsDef = nullptr;
                break;
            }
            case HIRTypePathBinding::TAG_Opaque: {
                // Already checked by type_is_sized
                paramsDef = nullptr;
                break;
            }
            case HIRTypePathBinding::TAG_ExternType: {
                static HIRGenericParams emptyParams; paramsDef = &emptyParams;
                break;
            }
            case HIRTypePathBinding::TAG_Enum: {
                auto& pb = e->binding.as_Enum();
                paramsDef = &pb->params;
                break;
            }
            case HIRTypePathBinding::TAG_Union: {
                auto& pb = e->binding.as_Union();
                paramsDef = &pb->params;
                break;
            }
            case HIRTypePathBinding::TAG_Struct: {
                auto& pb = e->binding.as_Struct();
                paramsDef = &pb->params;

                if (pb->structMarkings.dstType == HIRStructMarkings::DstType::Possible) {
                    // Check sized-ness of the unsized param
                    this->requireSized(sp, e->path.data.as_Generic().params.types.at(pb->structMarkings.unsizedParam));
                }
                break;
            }
        }

        if (paramsDef) {
            const auto& gpTys = e->path.data.as_Generic().params.types;
            for (size_t i = 0; i < gpTys.size(); i++) {
                if (paramsDef->types.at(i).isSized) {
                    this->requireSized(sp, gpTys[i]);
                }
            }
        }
    } else if (const auto* e = ty->opt_Tuple()) {
        // All entries in a tuple must be Sized
        for (const auto& ity : *e) {
            this->requireSized(sp, ity);
        }
    } else if (const auto* e = ty->opt_Array()) {
        // Inner type of an array must be sized
        this->requireSized(sp, e->inner);
    }
}

std::ostream& operator<<(std::ostream& os, const Context::PossibleTypeSource& x) {
    switch (x) {
        case Context::PossibleTypeSource::UnsizeTo:
            os << "UnsizeTo";
            break;
        case Context::PossibleTypeSource::CoerceTo:
            os << "CoerceTo";
            break;
        case Context::PossibleTypeSource::UnsizeFrom:
            os << "UnsizeFrom";
            break;
        case Context::PossibleTypeSource::CoerceFrom:
            os << "CoerceFrom";
            break;
    }
    return os;
}

Context::IVarPossible* Context::getIvarPossibilities(const Span& sp, unsigned int ivarIndex) {
    {
        const auto& realTy = ivars.getType(ivarIndex);
        if (!((*realTy).is_Infer() && ((*realTy).as_Infer().index == ivarIndex))) {
            DEBUG("IVar " << ivarIndex << " is actually " << realTy);
            return nullptr;
        }
    }

    return getPossibleIvarSink(ivarIndex);
}

Context::IVarPossible* Context::getPossibleIvarSink(unsigned index) {
    if (possibleIvarSink) {
        for (auto& captured : *possibleIvarSink) {
            if (captured.index == index) {
                return &captured.possibilities;
            }
        }
        possibleIvarSink->push_back(Associated::CapturedIvarPossible{index, {}});
        return &possibleIvarSink->back().possibilities;
    }

    if (index >= possibleIvarVals.size()) {
        possibleIvarVals.resize(index + 1);
    }
    return &possibleIvarVals[index];
}

void Context::possibleEquateIvar(const Span& sp, unsigned int ivarIndex, const HIRTypeData* rawT, PossibleTypeSource src) {
    const auto& t = this->ivars.getType(rawT);
    DEBUG(ivarIndex << " " << src << " " << rawT << " " << t);
    auto* entp = getIvarPossibilities(sp, ivarIndex);
    if (!entp) {
        return;
    }
    auto& ent = *entp;

    switch (src) {
        case PossibleTypeSource::UnsizeTo:
            ent.typesCoerceTo.push_back(IVarPossible::CoerceTy(t, false));
            break;
        case PossibleTypeSource::CoerceTo:
            ent.typesCoerceTo.push_back(IVarPossible::CoerceTy(t, true));
            break;
        case PossibleTypeSource::UnsizeFrom:
            ent.typesCoerceFrom.push_back(IVarPossible::CoerceTy(t, false));
            break;
        case PossibleTypeSource::CoerceFrom:
            ent.typesCoerceFrom.push_back(IVarPossible::CoerceTy(t, true));
            break;
    }

    // Tag ivars embedded in `raw_t` to prevent them from being guessed unless no other option
    if (!t->is_Infer()) {
        switch (src) {
            case PossibleTypeSource::UnsizeTo:
            case PossibleTypeSource::CoerceTo:
                possibleEquateTypeUnknown(sp, t, IvarUnknownType::To);
                break;
            case PossibleTypeSource::UnsizeFrom:
            case PossibleTypeSource::CoerceFrom:
                possibleEquateTypeUnknown(sp, t, IvarUnknownType::From);
                break;
        }
    }
}

void Context::possibleEquateIvarBounds(const Span& sp, unsigned int ivarIndex, std::vector<HIRTypeRef> types) {
    // Obtain the entry (and returning early if already known)
    auto* entp = getIvarPossibilities(sp, ivarIndex);
    if (!entp) {
        return;
    }
    auto& ent = *entp;

    // Determine if this ivar is in the list of possibilities
    bool hasSelf = false;
    for (auto it = types.begin(); it != types.end();) {
        auto& e = *it;
        ASSERT_BUG(sp, !typeContainsImplPlaceholder(crate.types, e), "Type contained an impl placeholder parameter - " << e);
        e = ivars.getType(e);
        if (((*e).is_Infer() && ((*e).as_Infer().index == ivarIndex))) {
            hasSelf = true;
            it = types.erase(it);
        } else {
            ++it;
        }
    }

    if (ent.hasBounded) {
        // Get the union of the bound set and this

        // TODO: If `ent.bounds_include_self` was set, then accept check if it's still set?
        ent.boundsIncludeSelf |= hasSelf;
        if (ent.boundsIncludeSelf) {
            // Accept everything in `types`
            for (auto& ty : types) {
                if (std::find(ent.bounded.begin(), ent.bounded.end(), ty) == ent.bounded.end()) {
                    ent.bounded.push_back(::std::move(ty));
                }
            }
        } else {
            // For each existing bound
            for (auto itExisting = ent.bounded.begin(); itExisting != ent.bounded.end();) {
                // Remove if it can't be found in the incoming set
                if (std::find(types.begin(), types.end(), *itExisting) != types.end()) {
                    ++itExisting;
                } else {
                    itExisting = ent.bounded.erase(itExisting);
                }
            }
        }
        DEBUG(ivarIndex << " bounded as [" << ent.bounded << "], union from [" << types << "] has_self=" << ent.boundsIncludeSelf);
    } else {
        ent.hasBounded = true;
        ent.boundsIncludeSelf = hasSelf;
        ent.bounded = std::move(types);
        DEBUG(ivarIndex << " bounded as [" << ent.bounded << "] has_self=" << hasSelf);
    }
}

std::ostream& operator<<(std::ostream& os, const Context::IvarUnknownType& x) {
    switch (x) {
        case Context::IvarUnknownType::To:
            os << "To";
            break;
        case Context::IvarUnknownType::From:
            os << "From";
            break;
        case Context::IvarUnknownType::Bound:
            os << "Bound";
            break;
    }
    return os;
}

void Context::possibleEquateIvarUnknown(const Span& sp, unsigned int ivarIndex, IvarUnknownType src) {
    DEBUG(ivarIndex << " = ?? (" << src << ")");
    ASSERT_BUG(sp, ivars.getType(ivarIndex)->is_Infer(), "possible_equate_ivar_unknown on known ivar");

    auto& ent = *getPossibleIvarSink(ivarIndex);
    switch (src) {
        case IvarUnknownType::To:
            ent.forceNoTo = true;
            break;
        case IvarUnknownType::From:
            ent.forceNoFrom = true;
            break;
        case IvarUnknownType::Bound:
            ent.forceDisable = true;
            break;
    }
}

void Context::addVar(const Span& sp, unsigned int index, const RcString& name, HIRTypeRef type) {
    DEBUG("(" << index << " " << name << " : " << type << ")");
    assert(index != ~0u);
    ASSERT_BUG(sp, type != HIRTypeRef(), "Unset ivar in variable type");
    if (bindings.size() <= index) {
        const auto oldSize = bindings.size();
        bindings.resize(index + 1);
        for (auto i = oldSize; i < bindings.size(); i++) {
            // Constant expressions can consume lexical binding slots before
            // they are evaluated out of the surrounding function. Keep the
            // resulting unused slots valid so the dense MIR local table never
            // contains a null type.
            bindings[i].ty = crate.types.unit();
        }
    }
    if (bindings[index].name == "") {
        bindings[index] = Binding{name, mv$(type)};
        // NOTE: Disabled to support unsized locals (1.74)
    } else {
        ASSERT_BUG(sp, bindings[index].name == name, "");
        this->equateTypes(sp, bindings[index].ty, type);
    }
}

const HIRTypeData* Context::getVar(const Span& sp, unsigned int idx) const {
    if (idx < this->bindings.size()) {
        ASSERT_BUG(sp, this->bindings[idx].ty != HIRTypeRef(), "Local #" << idx << " `" << this->bindings[idx].name << "` with no populated type");
        return this->bindings[idx].ty;
    } else {
        BUG(sp, "get_var - Binding index out of range - " << idx << " >=" << this->bindings.size());
    }
}

HIRExprNodeP Context::createAutoderef(HIRExprNodeP valNode, HIRTypeRef tyDst) const {
    const auto& span = valNode->span();
    const auto& tySrc = valNode->resType;
    // Array-to-slice is an autoref followed by pointer unsizing. A slice value
    // itself is never produced: dereference the resulting fat borrow to obtain
    // the slice place used by method/index lookup.
    if (getType(tySrc)->is_Array()) {
        ASSERT_BUG(span, tyDst->is_Slice(), "Array should only ever autoderef to Slice");

        const auto borrowType = HIRBorrowType::Shared;
        auto tySrcBorrow = crate.types.borrow(borrowType, tySrc);
        auto tyDstBorrow = crate.types.borrow(borrowType, tyDst);
        auto tyDstBorrowCopy = tyDstBorrow;

        valNode = mkExprnodep(crate.pool->make<HIRExprNodeBorrow>(span, borrowType, mv$(valNode)), mv$(tySrcBorrow));
        auto* unsizeNode = crate.pool->make<HIRExprNodeUnsize>(span, mv$(valNode), mv$(tyDstBorrowCopy));
        unsizeNode->isArrayToSliceAdjustment = true;
        valNode = mkExprnodep(unsizeNode, mv$(tyDstBorrow));
        valNode = mkExprnodep(crate.pool->make<HIRExprNodeDeref>(span, mv$(valNode)), tyDst);
        DEBUG("- Array-to-slice adjustment " << &*valNode << " -> " << valNode->resType);
    } else {
        valNode = mkExprnodep(crate.pool->make<HIRExprNodeDeref>(span, mv$(valNode)), mv$(tyDst));
        DEBUG("- Deref " << &*valNode << " -> " << valNode->resType);
    }

    return valNode;
}

namespace {
    void addCoerceBorrow(Context& context, HIRExprNodeP& origNodePtr, const HIRTypeData* desBorrowInner, ::std::function<void(HIRExprNodeP& n)> cb) {
        auto borrowType = context.ivars.getType(origNodePtr->resType)->as_Borrow().type;

        // Since this function operates on destructured &-ptrs, the dereferences have to be added behind a borrow
        HIRExprNodeP* nodePtrPtr = &origNodePtr;

        // If the coercion is of a block, apply the mutation to the inner node
        ASSERT_BUG(Span(), origNodePtr, "Null node pointer passed to `add_coerce_borrow`");
        while (auto* p = cast<HIRExprNodeBlock>(&**nodePtrPtr)) {
            DEBUG("- Moving into block");
            assert(p->valueNode);
            // Block result and the inner node's result must be the same type
            ASSERT_BUG(p->span(), context.ivars.typesEqual(p->resType, p->valueNode->resType), "Block and result mismatch - " << context.ivars.fmtType(p->resType) << " != " << context.ivars.fmtType(p->valueNode->resType));
            // - Override the the result type to the desired result
            p->resType = context.crate.types.borrow(borrowType, desBorrowInner);
            nodePtrPtr = &p->valueNode;
        }
        auto& nodePtr = *nodePtrPtr;
        const auto& srcType = context.ivars.getType(nodePtr->resType);

        // - If the pointed node is a borrow operation, add the dereferences within its value
        if (auto* p = cast<HIRExprNodeBorrow>(&*nodePtr)) {
            // Set the result of the borrow operation to the output type
            nodePtr->resType = context.crate.types.borrow(borrowType, desBorrowInner);

            nodePtrPtr = &p->value;
        }
        // - Otherwise, create a new borrow operation behind which the dereferences happen
        else {
            DEBUG("- Coercion node isn't a borrow, adding one");
            auto span = nodePtr->span();
            const auto& srcInnerTy = srcType->as_Borrow().inner;

            // NOTE: The type here is for _after_ `cb` has been called
            auto innerTyRef = context.crate.types.borrow(borrowType, desBorrowInner);

            // 1. Dereference (resulting in the dereferenced input type)
            nodePtr = NEWNODE(srcInnerTy, span, Deref, mv$(nodePtr));
            DEBUG("- Deref " << &*nodePtr << " -> " << nodePtr->resType);
            // 2. Borrow (resulting in the referenced output type)
            auto* borrowNode = context.crate.pool->make<HIRExprNodeBorrow>(span, borrowType, mv$(nodePtr));
            nodePtr = mkExprnodep(borrowNode, mv$(innerTyRef));
            DEBUG("- Borrow " << &*nodePtr << " -> " << nodePtr->resType);

            // - Set node pointer reference to point into the new borrow op
            nodePtrPtr = &borrowNode->value;
        }

        cb(*nodePtrPtr);

        context.ivars.markChange();
    }

    enum CoerceResult {
        Unknown,  // Coercion still unknown.
        Equality, // Types should be equated
        Fail,     // Equality would fail
        Custom,   // An op was emitted, and rule is complete
        Unsize,   // Emits an _Unsize op
    };

    // TODO: Add a (two?) callback(s) that handle type equalities (and possible equalities) so this function doesn't have to mutate the context
    CoerceResult checkUnsizeTys(const Context& context, const Span& sp, const HIRTypeData* dstRaw, const HIRTypeData* srcRaw, Context* contextMut, HIRExprNodeP* nodePtrPtr = nullptr) {
        const auto& dst = context.ivars.getType(dstRaw);
        const auto& src = context.ivars.getType(srcRaw);
        TRACE_FUNCTION_F("dst=" << dst << ", src=" << src);

        // If the types are already equal, no operation is required
        if (context.ivars.typesEqual(dst, src)) {
            DEBUG("Equal");
            return CoerceResult::Equality;
        }

        // Impossibilities
        if (src->is_Slice()) {
            // [T] can't unsize to anything
            DEBUG("Slice can't unsize");
            if (dst->is_Slice() || dst->is_Infer()) {
                return CoerceResult::Equality;
            } else {
                return CoerceResult::Fail;
            }
        }

        // Handle ivars specially
        if (dst->is_Infer() && src->is_Infer()) {
            // If both are literals, equate
            if (dst->as_Infer().isLit() && src->as_Infer().isLit()) {
                DEBUG("Literal ivars");
                return CoerceResult::Equality;
            }
            if (contextMut) {
                contextMut->possibleEquateIvar(sp, src->as_Infer().index, dst, Context::PossibleTypeSource::UnsizeTo);
                contextMut->possibleEquateIvar(sp, dst->as_Infer().index, src, Context::PossibleTypeSource::UnsizeFrom);
            }
            DEBUG("Both ivars");
            return CoerceResult::Unknown;
        } else if (const auto* dep = dst->opt_Infer()) {
            // Literal from a primtive has to be equal
            if (dep->isLit() && src->is_Primitive()) {
                DEBUG("Literal with primitive");
                return CoerceResult::Equality;
            }
            if (contextMut) {
                contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::UnsizeFrom);
                // Disable inner parts of the source? (E.g. if it's a closure)
                if (src->is_NodeType() && src->as_NodeType().is_Closure()) {
                    contextMut->possibleEquateTypeUnknown(sp, src, Context::IvarUnknownType::To);
                }
            }
            DEBUG("Dst ivar");
            return CoerceResult::Unknown;
        } else if (const auto* sep = src->opt_Infer()) {
            if (sep->isLit()) {
                if (!dst->is_TraitObject()) {
                    // Literal to anything other than a trait object must be an equality
                    DEBUG("Literal with primitive");
                    return CoerceResult::Equality;
                } else {
                    // Fall through
                }
            } else {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, sep->index, dst, Context::PossibleTypeSource::UnsizeTo);
                }
                DEBUG("Src is ivar (" << src << "), return Unknown");
                return CoerceResult::Unknown;
            }
        } else {
            // Neither side is an ivar, keep going.
        }

        // If either side is an unbound path, then return Unknown
        if (((*src).is_Path() && ((*src).as_Path().binding.is_Unbound()))) {
            DEBUG("Source unbound path");
            return CoerceResult::Unknown;
        }
        if (((*dst).is_Path() && ((*dst).as_Path().binding.is_Unbound()))) {
            DEBUG("Destination unbound path");
            return CoerceResult::Unknown;
        }

        // Array unsize (quicker than going into deref search)
        if (dst->is_Slice() && src->is_Array()) {
            if (contextMut) {
                contextMut->equateTypes(sp, dst->as_Slice().inner, src->as_Array().inner);
            }
            if (nodePtrPtr) {
                // TODO: Insert deref (instead of leading to a _Unsize op)
            } else {
                // Just return Unsize
            }
            DEBUG("Array => Slice");
            return CoerceResult::Unsize;
        }

        // Shortcut: Types that can't deref coerce (and can't coerce here because the target isn't a TraitObject)
        if (!dst->is_TraitObject()) {
            if (src->is_Generic()) {
            } else if (src->is_Path()) {
            } else if (src->is_Borrow()) {
            } else {
                DEBUG("Target isn't a trait object, and sources can't Deref");
                return CoerceResult::Equality;
            }
        }

        // Deref coercions
        // - If right can be dereferenced to left
        if (nodePtrPtr) {
            DEBUG("-- Deref coercions");
            HIRTypeRef tmpTy;
            const HIRTypeData* outTyP = src;
            unsigned int count = 0;
            ::std::vector<HIRTypeRef> types;
            while ((outTyP = context.resolve.autoderef(sp, outTyP, tmpTy))) {
                const auto& outTy = context.ivars.getType(outTyP);
                DEBUG("From? " << outTy);
                count += 1;
                // `#![recursion_limit]` bounds how far a coercion may follow
                // `Deref`, the same way it bounds macro expansion.
                if (count > context.resolve.board().settings->recursionLimit) {
                    ERROR(sp, E0000, "Reached the recursion limit while auto-dereferencing " << src);
                }

                bool literalMatchesDestination = false;
                if (const auto* sep = outTy->opt_Infer()) {
                    if (!sep->isLit()) {
                        // Hit a _, so can't keep going
                        if (contextMut) {
                            // Could also be any deref chain of the destination type
                            HIRTypeRef tmpTy2;
                            const HIRTypeData* dTyP = dst;
                            for (unsigned int i = 0; (dTyP = context.resolve.autoderef(sp, dTyP, tmpTy2)) && i < count - 1; i++) {
                            }
                            if (dTyP) {
                                // TODO: This should be a `DerefTo` (can't do other unsizings?)
                                contextMut->possibleEquateIvar(sp, sep->index, dTyP, Context::PossibleTypeSource::UnsizeTo);
                            } else {
                                // No type available, why?
                            }
                        }
                        DEBUG("Src derefs to ivar (" << src << "), return Unknown");
                        return CoerceResult::Unknown;
                    }
                    const auto* primitive = dst->opt_Primitive();
                    literalMatchesDestination = primitive
                        && ((sep->tyClass == HIRInferClass::Integer && isInteger(*primitive))
                            || (sep->tyClass == HIRInferClass::Float && isFloat(*primitive)));
                    if (literalMatchesDestination && contextMut) {
                        contextMut->equateTypes(sp, dst, outTy);
                    }
                }

                if (((*outTy).is_Generic() && ((*outTy).as_Generic().isPlaceholder()))) {
                    DEBUG("Src derefed to a placeholder generic type (" << outTy << "), return Unknown");
                    return CoerceResult::Unknown;
                }

                if (((*outTy).is_Path() && ((*outTy).as_Path().binding.is_Unbound()))) {
                    DEBUG("Src derefed to unbound type (" << outTy << "), return Unknown");
                    return CoerceResult::Unknown;
                }

                types.push_back(literalMatchesDestination ? dst : outTy);

                // Types aren't equal
                if (!literalMatchesDestination && context.ivars.typesEqual(dst, outTy) == false) {
                    // Check if they can be considered equivalent.
                    // - E.g. a fuzzy match, or both are slices/arrays
                    if (dst->tag() != outTy->tag()) {
                        DEBUG("Different types");
                        continue;
                    }

                    if (dst->is_Slice()) {
                        if (contextMut) {
                            contextMut->equateTypes(sp, dst, outTy);
                        }
                    } else if (dst->is_Borrow()) {
                        DEBUG("Borrow, continue");
                        continue;
                    } else {
                        if (dst->compareWithPlaceholders(sp, outTy, context.ivars.callbackResolveInfer()) == HIRCompare::Unequal) {
                            DEBUG("Same tag, but not fuzzy match");
                            continue;
                        }
                        DEBUG("Same tag and fuzzy match - assuming " << dst << " == " << outTy);
                        if (contextMut) {
                            contextMut->equateTypes(sp, dst, outTy);
                        }
                    }
                }

                if (contextMut && nodePtrPtr) {
                    auto& nodePtr = *nodePtrPtr;
                    addCoerceBorrow(*contextMut, nodePtr, types.back(), [&](auto& nodePtr) -> void {
                        // node_ptr = node that yeilds ty_src
                        assert(count == types.size());
                        for (unsigned int i = 0; i < types.size(); i++) {
                            auto span = nodePtr->span();
                            // TODO: Replace with a call to context.create_autoderef to handle cases where the below assertion would fire.
                            ASSERT_BUG(span, !nodePtr->resType->is_Array(), "Array->Slice shouldn't be in deref coercions");
                            auto ty = mv$(types[i]);
                            nodePtr = HIRExprNodeP(context.crate.pool->make<HIRExprNodeDeref>(mv$(span), mv$(nodePtr)));
                            DEBUG("- Deref " << &*nodePtr << " -> " << ty);
                            nodePtr->resType = mv$(ty);
                            context.ivars.getType(nodePtr->resType);
                        }
                    });
                }

                return CoerceResult::Custom;
            }
            // Either ran out of deref, or hit a _
            DEBUG("No applicable deref coercions");
        }

        // Trait objects
        if (const auto* dep = dst->opt_TraitObject()) {
            if (const auto* sep = src->opt_TraitObject()) {
                DEBUG("TraitObject => TraitObject");
                // Ensure that the trait list in the destination is a strict subset of the source

                // TODO: Equate these two trait paths
                if (dep->trait.path.path != sep->trait.path.path) {
// Trait mismatch!
#if 1 // 1.74: `trait_upcasting` feature
                    return CoerceResult::Unsize;
#endif
                    return CoerceResult::Equality;
                }
                const auto& tysD = dep->trait.path.params.types;
                const auto& tysS = sep->trait.path.params.types;
                if (contextMut) {
                    for (size_t i = 0; i < tysD.size(); i++) {
                        contextMut->equateTypes(sp, tysD[i], tysS.at(i));
                    }
                }

                // 2. Destination markers must be a strict subset
                for (const auto& mt : dep->markers) {
                    // TODO: Fuzzy match
                    bool found = false;
                    for (const auto& omt : sep->markers) {
                        if (omt == mt) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        // Return early.
                        return CoerceResult::Equality;
                    }
                }

                return CoerceResult::Unsize;
            } else {
                const auto& trait = dep->trait.path;

                // Check for trait impl
                if (trait.path != HIRSimplePath()) {
                    // Just call equate_types_assoc to add the required bounds.
                    if (contextMut) {
                        for (const auto& tyb : dep->trait.typeBounds) {
                            contextMut->equateTypesAssoc(sp, tyb.second.type, trait.path, trait.params.clone(), src, tyb.first.c_str(), tyb.second.atyParams, false);
                        }
                        if (dep->trait.typeBounds.empty()) {
                            contextMut->addTraitBound(sp, src, trait.path, trait.params.clone());
                        }
                    } else {
                        // Check that the trait is implemented (so this only returns `Unsize` if the rule would be valid - use for check_ivar_poss)
                        if (!context.resolve.findTraitImpls(sp, trait.path, trait.params, src, [&](auto _impl_ref, auto _cmp) {
                            return true;
                        })) {
                            return CoerceResult::Equality;
                        }
                    }
                }

                if (contextMut) {
                    for (const auto& marker : dep->markers) {
                        contextMut->addTraitBound(sp, src, marker.path, marker.params.clone());
                    }
                } else {
                    // TODO: Should this check?
                }

                // Add _Unsize operator
                return CoerceResult::Unsize;
            }
        }

        // Find an Unsize impl?
        struct H {
            static bool typeIsBounded(const HIRTypeData* ty) {
                if (ty->is_Generic()) {
                    return true;
                } else if (((*ty).is_Path() && ((*ty).as_Path().binding.is_Opaque()))) {
                    return true;
                } else {
                    return false;
                }
            }
        };

        if (H::typeIsBounded(src)) {
            DEBUG("Search for `Unsize<" << dst << ">` impl for `" << src << "`");

            ImplRef bestImpl;
            unsigned int count = 0;

            HIRPathParams pp{dst};
            bool found = context.resolve.findTraitImpls(sp, context.resolve.langUnsize(), pp, src, [&bestImpl, &count, &context, &sp](auto impl, auto cmp) {
                DEBUG("[check_unsize_tys] Found impl " << impl << (cmp == HIRCompare::Fuzzy ? " (fuzzy)" : ""));
                if (!context.resolve.implsOverlap(sp, impl, bestImpl)) {
                    // No overlap, count it as a new possibility
                    if (count == 0) {
                        bestImpl = mv$(impl);
                    }
                    count++;
                } else if (impl.moreSpecificThan(context.crate.types, bestImpl)) {
                    bestImpl = mv$(impl);
                } else {
                    // Less specific
                }
                // TODO: Record the best impl (if fuzzy) and equate params
                return cmp != HIRCompare::Fuzzy;
            });
            if (found) {
                return CoerceResult::Unsize;
            } else if (count == 1) {
                auto pp = bestImpl.getTraitParams(context.crate.types);
                DEBUG("Fuzzy, best was Unsize" << pp);
                if (contextMut) {
                    contextMut->equateTypes(sp, dst, pp.types.at(0));
                }
                return CoerceResult::Unsize;
            } else {
                // TODO: Fuzzy?
                DEBUG("Multiple impls for bounded unsize");
            }
        }

        // Path types
        if (src->tag() == dst->tag()) {
            switch ((*src).tag()) {
default:
                return CoerceResult::Equality;
                case HIRTypeData::TAG_Path: {
                    auto& se = (*src).as_Path();
                    auto& de = (*dst).as_Path();
                    if (se.binding.tag() == de.binding.tag()) {
                        switch (se.binding.tag()) {
                            case HIRTypePathBinding::TAG_Unbound: {
                                // Don't care
                                break;
                            }
                            case HIRTypePathBinding::TAG_Opaque: {
                                // Handled above in bounded
                                break;
                            }
                            case HIRTypePathBinding::TAG_ExternType: {
                                auto& sbe = se.binding.as_ExternType();
                                auto& dbe = de.binding.as_ExternType();
                                // Must be equal
                                if (sbe == dbe) { return CoerceResult::Equality; }
                                break;
                            }
                            case HIRTypePathBinding::TAG_Enum: {
                                auto& sbe = se.binding.as_Enum();
                                auto& dbe = de.binding.as_Enum();
                                // Must be equal
                                if (sbe == dbe) { return CoerceResult::Equality; }
                                break;
                            }
                            case HIRTypePathBinding::TAG_Union: {
                                auto& sbe = se.binding.as_Union();
                                auto& dbe = de.binding.as_Union();
                                if (sbe == dbe) {
                                    // Must be equal
                                    return CoerceResult::Equality;
                                }
                                break;
                            }
                            case HIRTypePathBinding::TAG_Struct: {
                                auto& sbe = se.binding.as_Struct();
                                auto& dbe = de.binding.as_Struct();
                                if (sbe == dbe) {
                                    const auto& sm = sbe->structMarkings;
                                    if (sm.dstType == HIRStructMarkings::DstType::Possible) {
                                        DEBUG("Possible DST");
                                        const auto& pSrc = se.path.data.as_Generic().params;
                                        const auto& pDst = de.path.data.as_Generic().params;
                                        const auto& isrc = pSrc.types.at(sm.unsizedParam);
                                        const auto& idst = pDst.types.at(sm.unsizedParam);
                                        auto rv = checkUnsizeTys(context, sp, idst, isrc, contextMut, nullptr);
                                        switch (rv) {
                                            case CoerceResult::Fail:
                                            case CoerceResult::Unknown:
                                                break;
                                            default:
                                                if (contextMut) {
                                                    for (size_t i = 0; i < pSrc.types.size(); i++) {
                                                        if (i != sm.unsizedParam) {
                                                            contextMut->equateTypes(sp, pDst.types.at(i), pSrc.types[i]);
                                                        }
                                                    }
                                                    for (size_t i = 0; i < pSrc.values.size(); i++) {
                                                        if (i != sm.unsizedParam) {
                                                            contextMut->equateValues(sp, pDst.values.at(i), pSrc.values[i]);
                                                        }
                                                    }
                                                }
                                                break;
                                        }
                                        return rv;
                                    } else {
                                        // Must be equal
                                        return CoerceResult::Equality;
                                    }
                                }
                                break;
                            }
                        }
                    }
                    break;
                }
            }
        }

        // If the destination is an Unbound path, return Unknown
        if (((*dst).is_Path() && ((*dst).as_Path().binding.is_Unbound()))) {
            DEBUG("Unbound destination");
            return CoerceResult::Unknown;
        }

        DEBUG("Reached end of check_unsize_tys, return Equality");
        // TODO: Determine if this unsizing could ever happen.
        return CoerceResult::Equality;
    }

    /// Checks if two types can be a valid coercion
    // General rules:
    // - CoerceUnsized generics/associated types can only involve generics/associated types
    // - CoerceUnsized structs only go between themselves (and either recurse or unsize a parameter)
    // - No other path can implement CoerceUnsized
    // - Pointers do unsizing (and maybe casting)
    // - All other types equate
    CoerceResult checkCoerceTys(const Context& context, const Span& sp, const HIRTypeData* dst, const HIRTypeData* srcR, Context* contextMut = nullptr, HIRExprNodeP* nodePtrPtr = nullptr) {
        auto src = srcR;
        TRACE_FUNCTION_F(dst << " := " << src);
        // If the types are equal, then return equality
        if (context.ivars.typesEqual(dst, src)) {
            return CoerceResult::Equality;
        }
        // If either side is a literal, then can't Coerce
        if (((*dst).is_Infer() && ((*dst).as_Infer().isLit()))) {
            if (!src->is_Diverge()) {
                return CoerceResult::Equality;
            }
        }
        // Nothing but `!` can become `!` (reverse does not hold, `!` can become anything)
        if (dst->is_Diverge()) {
            return CoerceResult::Equality;
        }
        if (((*src).is_Infer() && ((*src).as_Infer().isLit()))) {
            return CoerceResult::Equality;
        }

        // TODO: If the destination is bounded to be Sized, equate and return.
        // If both sides are `_`, then can't know about coerce yet
        if (dst->is_Infer() && src->is_Infer()) {
            // Add possibilities both ways
            if (contextMut) {
                contextMut->possibleEquateIvar(sp, src->as_Infer().index, dst, Context::PossibleTypeSource::CoerceTo);
                contextMut->possibleEquateIvar(sp, dst->as_Infer().index, src, Context::PossibleTypeSource::CoerceFrom);
            }
            return CoerceResult::Unknown;
        }

        struct H {
            static bool typeIsBounded(const HIRTypeData* ty) {
                if (ty->is_Generic()) {
                    return true;
                } else if (((*ty).is_Path() && ((*ty).as_Path().binding.is_Opaque()))) {
                    return true;
                } else {
                    return false;
                }
            }

            static HIRTypeRef makePruned(Context& context, const HIRTypeData* ty) {
                const auto& binding = ty->as_Path().binding;
                const auto& sm = binding.as_Struct()->structMarkings;
                HIRGenericPath gp = ty->as_Path().path.data.as_Generic().clone();
                assert(sm.coerceParam != ~0u);
                gp.params.types.at(sm.coerceParam) = context.ivars.newIvarTr();
                return context.crate.types.path(mv$(gp), binding.as_Struct());
            }
        };

        // A CoerceUnsized generic/aty/erased on one side
        // - If other side is an ivar, do a possible equality and return Unknown
        // - If impl is found, emit _Unsize
        // - Else, equate and return
        // TODO: Should ErasedType be counted here? probably not.
        if (H::typeIsBounded(src) || H::typeIsBounded(dst)) {
            const auto langCoerceUnsized = context.crate.getLangItemPathOpt("coerce_unsized"); // TODO: Pre-load
            // `CoerceUnsized<U> for T` means `T -> U`

            if (!langCoerceUnsized.components().empty()) {
                HIRPathParams pp{dst};

                // PROBLEM: This can false-negative leading to the types being falsely equated.

                bool fuzzyMatch = false;
                ImplRef bestImpl;
                bool found = context.resolve.findTraitImpls(sp, langCoerceUnsized, pp, src, [&](auto impl, auto cmp) -> bool {
                    DEBUG("[check_coerce] cmp=" << cmp << ", impl=" << impl);
                    // TODO: Allow fuzzy match if it's the only matching possibility?
                    // - Recorded for now to know if there could be a matching impl later
                    if (cmp == HIRCompare::Fuzzy) {
                        fuzzyMatch = true;
                        if (impl.moreSpecificThan(context.crate.types, bestImpl)) {
                            bestImpl = mv$(impl);
                        } else {
                            TODO(sp, "Equal specificity impls");
                        }
                    }
                    return cmp == HIRCompare::Equal;
                });
                // - Concretely found - emit the _Unsize op and remove this rule
                if (found) {
                    return CoerceResult::Unsize;
                }
                if (fuzzyMatch) {
                    DEBUG("- best_impl = " << bestImpl);
                    return CoerceResult::Unknown;
                }
                DEBUG("- No CoerceUnsized impl found");
            }
        }

        // CoerceUnsized struct paths
        // - If one side is an ivar, create a type-pruned version of the other
        // - Recurse/unsize inner value
        if (src->is_Infer() && ((*dst).is_Path() && (*dst).as_Path().binding.is_Struct() && (*dst).as_Path().binding.as_Struct()->structMarkings.coerceUnsized != HIRStructMarkings::Coerce::None)) {
            if (contextMut) {
                contextMut->possibleEquateIvar(sp, src->as_Infer().index, dst, Context::PossibleTypeSource::CoerceTo);
            }
            // TODO: Avoid needless loop return
            return CoerceResult::Unknown;
        }
        if (dst->is_Infer() && ((*src).is_Path() && (*src).as_Path().binding.is_Struct() && (*src).as_Path().binding.as_Struct()->structMarkings.coerceUnsized != HIRStructMarkings::Coerce::None)) {
            if (contextMut) {
                contextMut->possibleEquateIvar(sp, dst->as_Infer().index, src, Context::PossibleTypeSource::CoerceFrom);
            }
            // TODO: Avoid needless loop return
            return CoerceResult::Unknown;
        }
        if (((*dst).is_Path() && ((*dst).as_Path().binding.is_Struct())) && ((*src).is_Path() && ((*src).as_Path().binding.is_Struct()))) {
            const auto& spbe = src->as_Path().binding.as_Struct();
            const auto& dpbe = dst->as_Path().binding.as_Struct();
            if (spbe != dpbe) {
                // TODO: Error here? (equality in caller will cause an error)
                return CoerceResult::Equality;
            }
            const auto& sm = spbe->structMarkings;
            // Has to be equal?
            if (sm.coerceUnsized == HIRStructMarkings::Coerce::None) {
                return CoerceResult::Equality;
            }

            // Equate all parameters that aren't the unsizing param
            const auto& ppDst = dst->as_Path().path.data.as_Generic().params;
            const auto& ppSrc = src->as_Path().path.data.as_Generic().params;
            DEBUG(ppDst << " = " << ppSrc);
            assert(ppDst.types.size() == ppSrc.types.size());
            for (size_t i = 0; i < ppSrc.types.size(); i++) {
                if (i == sm.coerceParam) {
                    continue;
                }
                if (contextMut) {
                    contextMut->equateTypes(sp, ppDst.types.at(i), ppSrc.types.at(i));
                }
            }
            assert(ppDst.values.size() == ppSrc.values.size());
            for (size_t i = 0; i < ppSrc.values.size(); i++) {
                TODO(sp, "Handle values in CoerceUnsized");
            }
            // Check coercion/unsizing of the target type
            const auto& idst = ppDst.types.at(sm.coerceParam);
            const auto& isrc = ppSrc.types.at(sm.coerceParam);
            switch (sm.coerceUnsized) {
                case HIRStructMarkings::Coerce::None:
                    throw "";
                case HIRStructMarkings::Coerce::Passthrough:
                    DEBUG("Passthough CoerceUnsized");
                    // TODO: Force emitting `_Unsize` instead of anything else
                    return checkCoerceTys(context, sp, idst, isrc, contextMut, nullptr);
                case HIRStructMarkings::Coerce::Pointer:
                    DEBUG("Pointer CoerceUnsized");
                    return checkUnsizeTys(context, sp, idst, isrc, contextMut, nullptr);
            }
        }

        // An unresolved projection is still a coercion source/destination.
        // Keep that dependency in the possibility set instead of leaving only
        // a `!` arm visible and prematurely selecting the bottom type.
        const bool dstIsUnboundPath = ((*dst).is_Path() && ((*dst).as_Path().binding.is_Unbound()));
        const bool srcIsUnboundPath = ((*src).is_Path() && ((*src).as_Path().binding.is_Unbound()));
        if (dstIsUnboundPath || srcIsUnboundPath) {
            if (contextMut) {
                if (const auto* dep = dst->opt_Infer(); dep && srcIsUnboundPath) {
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::CoerceFrom);
                }
                if (const auto* sep = src->opt_Infer(); sep && dstIsUnboundPath) {
                    contextMut->possibleEquateIvar(sp, sep->index, dst, Context::PossibleTypeSource::CoerceTo);
                }
            }
            return CoerceResult::Unknown;
        }

        // Any other type, check for pointer
        // - If not a pointer, return Equality
        if (const auto* sep = src->opt_Infer()) {
            const auto& se = *sep;
            ASSERT_BUG(sp, !dst->is_Infer(), "Already handled?");

            // Add a disable flag to all ivars within the `dst` type
            // - This should prevent early guessing
            if (contextMut) {
                contextMut->possibleEquateTypeUnknown(sp, dst, Context::IvarUnknownType::From);
            }

            // If the other side is a pointer
            if (dst->is_Pointer() || dst->is_Borrow()) {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, se.index, dst, Context::PossibleTypeSource::CoerceTo);
                }
                return CoerceResult::Unknown;
            }
            // Not a pointer (handled just above), and not a CoerceUnsized struct (handled farther above)
            // - Could be a generic with CoerceUnsized
            // HACK: Composite types may hit issues with `!` within them, primtives don't have this issue?
            // - Unsure why, but if the below is unconditional then there's a type error with `Result<!,...>` and `Try`
            else if (dst->is_Primitive()) {
                return CoerceResult::Equality;
            } else {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, se.index, dst, Context::PossibleTypeSource::UnsizeFrom);
                }
                return CoerceResult::Unknown;
            }
        } else if (src->is_Diverge()) {
            if (const auto* dep = dst->opt_Infer()) {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::UnsizeFrom);
                }
                return CoerceResult::Unknown;
            } else {
                // Downstream just handles this
                return CoerceResult::Custom;
            }
        } else if (const auto* sep = src->opt_Pointer()) {
            const auto& se = *sep;
            if (const auto* dep = dst->opt_Infer()) {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::CoerceFrom);
                }
                return CoerceResult::Unknown;
            } else if (const auto* dep = dst->opt_Pointer()) {
                // Check strength reduction
                if (dep->type < se.type) {
                    if (nodePtrPtr) {
                        // > Convert `src` to `src as *mut SI`
                        auto newType = context.crate.types.pointer(dep->type, se.inner);

                        // If the coercion is of a block, do the reborrow on the last node of the block
                        // - Cleans up the dumped MIR and prevents needing a reborrow elsewhere.
                        // - TODO: Alter the block's result types
                        HIRExprNodeP* npp = nodePtrPtr; // Note: Node pointer can be null (when checking)
                        while (auto* p = cast<HIRExprNodeBlock>(npp->get())) {
                            DEBUG("- Propagate to the last node of a _Block");
                            ASSERT_BUG(p->span(), context.ivars.typesEqual(p->resType, p->valueNode->resType), "Block and result mismatch - " << context.ivars.fmtType(p->resType) << " != " << context.ivars.fmtType(p->valueNode->resType));
                            if (!context.ivars.typesEqual(p->resType, src)) {
                                DEBUG("Block and result mismatch - " << context.ivars.fmtType(p->resType) << " != " << context.ivars.fmtType(src));
                                return CoerceResult::Unknown;
                            }
                            if (contextMut) {
                                p->resType = dst;
                            }
                            npp = &p->valueNode;
                            ASSERT_BUG(sp, *npp, "Null node pointer on block");
                        }
                        HIRExprNodeP& nodePtr = *npp;

                        if (contextMut) {
                            // Add cast down
                            auto span = nodePtr->span();
                            // *<inner>
                            DEBUG("- NEWNODE _Cast -> " << newType);
                            nodePtr = NEWNODE(newType, span, Cast, mv$(nodePtr), newType);
                            context.ivars.getType(nodePtr->resType);

                            contextMut->ivars.markChange();
                        }

                        // Continue on with coercion (now that node_ptr is updated)
                        switch (checkUnsizeTys(context, sp, dep->inner, se.inner, contextMut, &nodePtr)) {
                            case CoerceResult::Fail:
                                return CoerceResult::Fail;
                            case CoerceResult::Unknown:
                                // Add new coercion at the new inner point
                                if (&nodePtr != nodePtrPtr) {
                                    DEBUG("Unknown check_unsize_tys after autoderef - " << dst << " := " << nodePtr->resType);
                                    if (contextMut) {
                                        contextMut->equateTypesCoerce(sp, dst, nodePtr);
                                    }
                                    return CoerceResult::Custom;
                                } else {
                                    return CoerceResult::Unknown;
                                }
                            case CoerceResult::Custom:
                                return CoerceResult::Custom;
                            case CoerceResult::Equality:
                                if (contextMut) {
                                    contextMut->equateTypes(sp, dep->inner, se.inner);
                                }
                                return CoerceResult::Custom;
                            case CoerceResult::Unsize:
                                if (contextMut) {
                                    DEBUG("- NEWNODE _Unsize " << &nodePtr << " " << &*nodePtr << " -> " << dst);
                                    auto span = nodePtr->span();
                                    nodePtr = NEWNODE(dst, span, Unsize, mv$(nodePtr), dst);
                                }
                                return CoerceResult::Custom;
                        }
                        throw "";
                    } else {
                        //TODO(sp, "Borrow strength reduction with no node pointer - " << src << " -> " << dst);
                        DEBUG("Pointer strength reduction with no node pointer - " << src << " -> " << dst);
                        return CoerceResult::Unsize;
                    }
                } else if (dep->type == se.type) {
                    // Valid.
                } else {
                    // TODO: return CoerceResult::Failed? (indicating that it failed outright, don't even try)
                    return CoerceResult::Equality;
                }
                ASSERT_BUG(sp, dep->type == se.type, "Pointer strength mismatch");

                // Call unsizing code
                return checkUnsizeTys(context, sp, dep->inner, se.inner, contextMut, nodePtrPtr);
            } else {
                // TODO: Error here? (leave to caller)
                return CoerceResult::Equality;
            }
        } else if (const auto* sep = src->opt_Borrow()) {
            const auto& se = *sep;
            if (const auto* dep = dst->opt_Infer()) {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::CoerceFrom);
                }
                return CoerceResult::Unknown;
            } else if (const auto* dep = dst->opt_Pointer()) {
                // Add cast to the pointer (if valid strength reduction)
                // Call unsizing code on casted value

                // Borrows can coerce to pointers while reducing in strength
                // - Shared < Unique. If the destination is not weaker or equal to the source, it's an error
                if (!(dep->type <= se.type)) {
                    ERROR(sp, E0000, "Type mismatch between " << dst << " and " << src << " - Mutability not compatible");
                }

                // Add downcast
                switch (checkUnsizeTys(context, sp, dep->inner, se.inner, contextMut, nodePtrPtr)) {
                    case CoerceResult::Fail:
                        return CoerceResult::Fail;
                    case CoerceResult::Unknown:
                        return CoerceResult::Unknown;
                    case CoerceResult::Custom:
                        return CoerceResult::Custom;
                    case CoerceResult::Equality:
                        if (nodePtrPtr && contextMut) {
                            auto& nodePtr = *nodePtrPtr;
                            {
                                DEBUG("- NEWNODE _Cast " << &*nodePtr << " -> " << dst);
                                auto span = nodePtr->span();
                                nodePtr = HIRExprNodeP(context.crate.pool->make<HIRExprNodeCast>(mv$(span), mv$(nodePtr), dst));
                                nodePtr->resType = dst;
                            }
                        }

                        if (contextMut) {
                            contextMut->equateTypes(sp, dep->inner, se.inner);
                        }
                        return CoerceResult::Custom;
                    case CoerceResult::Unsize:
                        if (nodePtrPtr && contextMut) {
                            auto& nodePtr = *nodePtrPtr;
                            auto dstB = context.crate.types.borrow(se.type, dep->inner);
                            DEBUG("- NEWNODE _Unsize " << &*nodePtr << " -> " << dstB);
                            {
                                auto span = nodePtr->span();
                                nodePtr = NEWNODE(dstB, span, Unsize, mv$(nodePtr), dstB);
                            }

                            DEBUG("- NEWNODE _Cast " << &*nodePtr << " -> " << dst);
                            {
                                auto span = nodePtr->span();
                                nodePtr = HIRExprNodeP(context.crate.pool->make<HIRExprNodeCast>(mv$(span), mv$(nodePtr), dst));
                                nodePtr->resType = dst;
                            }
                        }
                        return CoerceResult::Custom;
                }
                throw "";
            } else if (const auto* dep = dst->opt_Borrow()) {
                // The expected pointee type reaches through a borrow
                // expression. This is observably different from coercing an
                // already-created `&!`: only `&panic!()` is itself
                // unreachable and may therefore have result type `&T`.
                if (dep->type == se.type && se.inner->is_Diverge() && contextMut && nodePtrPtr && *nodePtrPtr) {
                    HIRExprNodeP* borrowNodePtr = nodePtrPtr;
                    ::std::vector<HIRExprNodeBlock*> blocks;
                    while (auto* block = cast<HIRExprNodeBlock>(borrowNodePtr->get())) {
                        if (!block->valueNode) {
                            break;
                        }
                        blocks.push_back(block);
                        borrowNodePtr = &block->valueNode;
                    }
                    if (auto* borrow = cast<HIRExprNodeBorrow>(borrowNodePtr->get()); borrow && borrow->type == dep->type) {
                        contextMut->equateTypesCoerce(sp, dep->inner, borrow->value);
                        for (auto* block : blocks) {
                            block->resType = dst;
                        }
                        (*borrowNodePtr)->resType = dst;
                        return CoerceResult::Custom;
                    }
                }

                // Check strength reduction
                if (dep->type < se.type) {
                    if (nodePtrPtr) {
                        // > Goes from `src` -> `*src` -> `&`dep->type` `*src`
                        const auto innerTy = se.inner;
                        auto dstBt = dep->type;
                        auto newType = context.crate.types.borrow(dstBt, innerTy);

                        // If the coercion is of a block, do the reborrow on the last node of the block
                        // - Cleans up the dumped MIR and prevents needing a reborrow elsewhere.
                        // - TODO: Alter the block's result types
                        {
                            HIRExprNodeP* npp = nodePtrPtr;
                            while (auto* p = cast<HIRExprNodeBlock>(npp->get())) {
                                if (!context.ivars.typesEqual(p->resType, src)) {
                                    DEBUG("(borrow) Block and result mismatch - " << context.ivars.fmtType(p->resType) << " != " << context.ivars.fmtType(src));
                                    return CoerceResult::Unknown;
                                }
                                npp = &p->valueNode;
                                ASSERT_BUG(sp, *npp, "Null node pointer in block");
                            }
                        }
                        HIRExprNodeP* npp = nodePtrPtr;
                        while (auto* p = cast<HIRExprNodeBlock>(npp->get())) {
                            DEBUG("- Propagate borrow coercion to the last node of a _Block: " << context.ivars.fmtType(p->resType));
                            ASSERT_BUG(p->span(), context.ivars.typesEqual(p->resType, p->valueNode->resType), "(borrow) Block and result mismatch - " << context.ivars.fmtType(p->resType) << " != " << context.ivars.fmtType(p->valueNode->resType));
                            ASSERT_BUG(p->span(), context.ivars.typesEqual(p->resType, src), "(borrow) Block and result mismatch - " << context.ivars.fmtType(p->resType) << " != " << context.ivars.fmtType(src));
                            if (contextMut) {
                                p->resType = dst;
                            }
                            npp = &p->valueNode;
                        }
                        HIRExprNodeP& nodePtr = *npp;

                        if (contextMut) {
                            // Add cast down
                            auto span = nodePtr->span();
                            // *<inner>
                            DEBUG("- Deref -> " << innerTy);
                            nodePtr = NEWNODE(innerTy, span, Deref, mv$(nodePtr));
                            context.ivars.getType(nodePtr->resType);
                            // &*<inner>
                            DEBUG("- Borrow -> " << newType);
                            nodePtr = NEWNODE(mv$(newType), span, Borrow, dstBt, mv$(nodePtr));
                            context.ivars.getType(nodePtr->resType);

                            contextMut->ivars.markChange();
                        }

                        // Continue on with coercion (now that node_ptr is updated)
                        switch (checkUnsizeTys(context, sp, dep->inner, se.inner, contextMut, &nodePtr)) {
                            case CoerceResult::Fail:
                                return CoerceResult::Fail;
                            case CoerceResult::Unknown:
                                // Add new coercion at the new inner point
                                if (&nodePtr != nodePtrPtr) {
                                    DEBUG("Unknown check_unsize_tys after autoderef - " << dst << " := " << nodePtr->resType);
                                    if (contextMut) {
                                        contextMut->equateTypesCoerce(sp, dst, nodePtr);
                                    }
                                    return CoerceResult::Custom;
                                } else {
                                    return CoerceResult::Unknown;
                                }
                            case CoerceResult::Custom:
                                return CoerceResult::Custom;
                            case CoerceResult::Equality:
                                if (contextMut) {
                                    contextMut->equateTypes(sp, dep->inner, se.inner);
                                }
                                return CoerceResult::Custom;
                            case CoerceResult::Unsize:
                                if (contextMut) {
                                    DEBUG("- NEWNODE _Unsize " << &nodePtr << " " << &*nodePtr << " -> " << dst);
                                    auto span = nodePtr->span();
                                    nodePtr = NEWNODE(dst, span, Unsize, mv$(nodePtr), dst);
                                }
                                return CoerceResult::Custom;
                        }
                        throw "";
                    } else {
                        //TODO(sp, "Borrow strength reduction with no node pointer - " << src << " -> " << dst);
                        DEBUG("Borrow strength reduction with no node pointer - " << src << " -> " << dst);
                        return CoerceResult::Unsize;
                    }
                } else if (dep->type == se.type) {
                    // Valid.
                } else {
                    // TODO: return CoerceResult::Failed? (indicating that it failed outright, don't even try)
                    return CoerceResult::Equality;
                }
                ASSERT_BUG(sp, dep->type == se.type, "Borrow strength mismatch");

                // Call unsizing code
                return checkUnsizeTys(context, sp, dep->inner, se.inner, contextMut, nodePtrPtr);
            } else {
                // TODO: Error here?
                return CoerceResult::Equality;
            }
        } else if (src->is_NodeType() && src->as_NodeType().is_Closure()) {
            const auto* nodeP = src->as_NodeType().as_Closure();
            if (dst->is_ErasedType()) {
                // rustc derives an unannotated closure signature from the
                // predicates of its expected opaque type before checking the
                // closure body.  Querying FnOnce also elaborates Fn/FnMut and
                // user-defined supertraits, and exposes the associated Output
                // equality carried by the opaque bound.
                ::std::vector<HIRTypeRef> closureArgs;
                closureArgs.reserve(nodeP->args.size());
                for (const auto& arg : nodeP->args) {
                    closureArgs.push_back(arg.second);
                }
                HIRPathParams desiredParams{context.crate.types.tuple(mv$(closureArgs))};

                ::std::vector<HIRTypeRef> expectedArgs;
                HIRTypeRef expectedOutput;
                const bool foundExpectation = context.resolve.findTraitImpls(sp, context.resolve.langFnOnce(), desiredParams, dst, [&](ImplRef impl, HIRCompare) {
                    auto params = impl.getTraitParams(context.crate.types);
                    if (params.types.size() != 1 || !params.types.front()->is_Tuple()) {
                        return false;
                    }
                    const auto& args = params.types.front()->as_Tuple();
                    if (args.size() != nodeP->args.size()) {
                        return false;
                    }
                    auto output = impl.getType(context.crate.types, "Output", {});
                    if (output == HIRTypeRef()) {
                        return false;
                    }

                    // A fuzzy impl candidate can retain placeholders for its
                    // own generic parameters.  Those parameters belong to the
                    // candidate matcher, not to this inference context, so use
                    // only the signature components that the opaque bounds
                    // actually determined.
                    bool hasExpectation = false;
                    ::std::vector<HIRTypeRef> concreteArgs;
                    concreteArgs.reserve(args.size());
                    for (const auto& arg : args) {
                        if (typeContainsImplPlaceholder(context.crate.types, arg)) {
                            concreteArgs.push_back(HIRTypeRef());
                        } else {
                            concreteArgs.push_back(arg);
                            hasExpectation = true;
                        }
                    }
                    if (typeContainsImplPlaceholder(context.crate.types, output)) {
                        output = HIRTypeRef();
                    } else {
                        hasExpectation = true;
                    }
                    if (!hasExpectation) {
                        return false;
                    }

                    expectedArgs = mv$(concreteArgs);
                    expectedOutput = mv$(output);
                    return true;
                });
                if (foundExpectation && contextMut) {
                    for (size_t i = 0; i < expectedArgs.size(); i++) {
                        if (expectedArgs[i] != HIRTypeRef()) {
                            contextMut->equateTypes(sp, nodeP->args[i].second, expectedArgs[i]);
                        }
                    }
                    if (expectedOutput != HIRTypeRef()) {
                        contextMut->equateTypes(sp, nodeP->returnType, expectedOutput);
                    }
                }
                return CoerceResult::Equality;
            } else if (dst->is_Function()) {
                const auto& de = dst->as_Function();
                if (nodePtrPtr) {
                    auto* coercedNodePtr = nodePtrPtr;
                    while (auto* block = cast<HIRExprNodeBlock>(coercedNodePtr->get())) {
                        ASSERT_BUG(block->span(), block->valueNode, "Closure coercion reached a non-yielding block");
                        ASSERT_BUG(block->span(), context.ivars.typesEqual(block->resType, block->valueNode->resType),
                            "Block and result mismatch - " << context.ivars.fmtType(block->resType)
                            << " != " << context.ivars.fmtType(block->valueNode->resType));
                        if (contextMut) {
                            block->resType = dst;
                        }
                        coercedNodePtr = &block->valueNode;
                    }

                    auto& nodePtr = *coercedNodePtr;
                    auto span = nodePtr->span();
                    if (de.abi != ABI_RUST) {
                        ERROR(span, E0000, "Cannot use closure for extern function pointer");
                    }
                    if (de.argTypes.size() != nodeP->args.size()) {
                        ERROR(span, E0000, "Mismatched argument count coercing closure to fn(...)");
                    }
                    if (contextMut) {
                        for (size_t i = 0; i < de.argTypes.size(); i++) {
                            contextMut->equateTypes(sp, de.argTypes[i], nodeP->args[i].second);
                        }
                        contextMut->equateTypes(sp, de.rettype, nodeP->returnType);
                        nodePtr = NEWNODE(dst, span, Cast, mv$(nodePtr), dst);
                    }
                }
                return CoerceResult::Custom;
            } else if (const auto* dep = dst->opt_Infer()) {
                if (contextMut) {
                    // Prevent inferrence of argument/return types
                    for (const auto& at : nodeP->args) {
                        contextMut->possibleEquateTypeUnknown(sp, at.second, Context::IvarUnknownType::To);
                    }
                    contextMut->possibleEquateTypeUnknown(sp, nodeP->returnType, Context::IvarUnknownType::Bound);
                    // Add as a possiblity
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::CoerceFrom);
                }
                return CoerceResult::Unknown;
            } else {
                return CoerceResult::Equality;
            }
        } else if (const auto* se = src->opt_NamedFunction()) {
            if (const auto* de = dst->opt_Function()) {
                auto ft = context.resolve.expandAssociatedTypes(sp, context.crate.types.function(se->decay(context.crate.types, sp)));
                const auto* se = &ft->as_Function();

                // ABI must match
                if (se->abi != de->abi) {
                    return CoerceResult::Equality;
                }
                // const can be removed
                //if( se->is_const != de->is_const && de->is_const ) // Error going TO a const function pointer
                // unsafe can be added
                if (se->isUnsafe != de->isUnsafe && se->isUnsafe) { // Error going FROM an unsafe function pointer
                    return CoerceResult::Equality;
                }
                // argument/return types must match
                if (se->argTypes.size() != de->argTypes.size()) {
                    return CoerceResult::Equality;
                }

                if (contextMut) {
                    auto& nodePtr = *nodePtrPtr;
                    auto span = nodePtr->span();

                    for (size_t i = 0; i < de->argTypes.size(); i++) {
                        contextMut->equateTypes(sp, de->argTypes[i], se->argTypes[i]);
                    }
                    contextMut->equateTypes(sp, de->rettype, se->rettype);
                    nodePtr = NEWNODE(dst, span, Cast, mv$(nodePtr), dst);
                }
                return CoerceResult::Custom;
            }
            // Function pointers can coerce safety
            else if (const auto* dep = dst->opt_Infer()) {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::UnsizeFrom);
                }
                return CoerceResult::Unknown;
            } else {
                return CoerceResult::Equality;
            }
        } else if (const auto* se = src->opt_Function()) {
            if (const auto* de = dst->opt_Function()) {
                auto& nodePtr = *nodePtrPtr;
                auto span = nodePtr->span();
                DEBUG("Function pointer coercion");
                // ABI must match
                if (se->abi != de->abi) {
                    return CoerceResult::Equality;
                }
                // const can be removed
                //if( se->is_const != de->is_const && de->is_const ) // Error going TO a const function pointer
                // unsafe can be added
                if (se->isUnsafe != de->isUnsafe && se->isUnsafe) { // Error going FROM an unsafe function pointer
                    return CoerceResult::Equality;
                }
                // argument/return types must match
                if (de->argTypes.size() != se->argTypes.size()) {
                    return CoerceResult::Equality;
                }
                if (contextMut) {
                    for (size_t i = 0; i < de->argTypes.size(); i++) {
                        contextMut->equateTypes(sp, de->argTypes[i], se->argTypes[i]);
                    }
                    contextMut->equateTypes(sp, de->rettype, se->rettype);
                    nodePtr = NEWNODE(dst, span, Cast, mv$(nodePtr), dst);
                }
                return CoerceResult::Custom;
            }
            // Function pointers can coerce safety
            else if (const auto* dep = dst->opt_Infer()) {
                if (contextMut) {
                    contextMut->possibleEquateIvar(sp, dep->index, src, Context::PossibleTypeSource::UnsizeFrom);
                }
                return CoerceResult::Unknown;
            } else {
                return CoerceResult::Equality;
            }
        } else {
            // TODO: ! should be handled above or in caller?
            return CoerceResult::Equality;
        }
    }

    bool checkCoerce(Context& context, const Context::Coercion& v) {
        HIRExprNodeP& nodePtr = *v.rightNodePtr;
        const auto& sp = nodePtr->span();
        const auto& tyDst = context.ivars.getType(v.leftTy);
        const auto& tySrc = context.ivars.getType(nodePtr->resType);
        TRACE_FUNCTION_FR(v << " - " << context.ivars.fmtType(tyDst) << " := " << context.ivars.fmtType(tySrc), v << " - " << context.ivars.fmtType(v.leftTy) << " := " << context.ivars.fmtType(nodePtr->resType));

        // A dereference with a visible trait implementation has a pending
        // `Deref::Target` equation.  Its output must be selected before an
        // outer coercion fixes an otherwise unbound result ivar to the
        // expected type.  This lets a selected `!` target use the ordinary
        // `! -> T` coercion afterwards.
        const bool hasPendingDerefTarget = ::std::any_of(context.linkAssoc.begin(), context.linkAssoc.end(), [&](const Context::Associated& rule) {
            return rule.operatorKind == TypeckPrimitiveOperator::Deref && context.ivars.typesEqual(rule.leftTy, tySrc);
        });
        if (hasPendingDerefTarget) {
            DEBUG("Deref target is pending - keep coercion");
            return false;
        }

        // NOTE: Coercions can happen on comparisons, which means that checking for Sized isn't valid (because you can compare unsized types)

        switch (checkCoerceTys(context, sp, tyDst, tySrc, &context, &nodePtr)) {
            case CoerceResult::Fail:
                return false;
            case CoerceResult::Unknown:
                DEBUG("Unknown - keep");
                return false;
            case CoerceResult::Custom:
                DEBUG("Custom - Completed");
                return true;
            case CoerceResult::Equality:
                DEBUG("Trigger equality - Completed");
                context.equateTypes(sp, tyDst, tySrc);
                return true;
            case CoerceResult::Unsize:
                DEBUG("Add _Unsize " << &*nodePtr << " -> " << tyDst);
                auto span = nodePtr->span();
                nodePtr = NEWNODE(tyDst, span, Unsize, mv$(nodePtr), tyDst);
                return true;
        }
        throw "";
    }

    void addImplBounds(Context& context, const Span& sp, const ImplRef& implRef) {
        const auto* ep = implRef.data.opt_TraitImpl();
        if (!ep) {
            return;
        }

        const auto& e = *ep;
        assert(e.impl);
        for (const auto& bound : e.impl->params.bounds) {
        switch (bound.tag()) {
default:
            break;
            case HIRGenericBound::TAG_TraitBound: {
                auto& be = bound.as_TraitBound();
                DEBUG("New bound (pre-mono) " << bound);
                auto ms = implRef.getCbMonomorphTraitimpl(context.crate.types, sp, {});
                auto bTyMono = ms.monomorphType(sp, be.type);
                auto bTpMono = ms.monomorphTraitpath(sp, be.trait, true);
                DEBUG("- " << bTyMono << " : " << bTpMono);
                if (bTpMono.typeBounds.size() > 0) {
                    for (const auto& atyBound : bTpMono.typeBounds) {
                        context.equateTypesAssoc(sp, atyBound.second.type, bTpMono.path.path, bTpMono.path.params.clone(), bTyMono, atyBound.first.c_str(), atyBound.second.atyParams, false);
                    }
                } else {
                    context.addTraitBound(sp, bTyMono, bTpMono.path.path, bTpMono.path.params.clone());
                }
                break;
            }
        }
        }
    }

    enum class AssociatedCheckResult {
        Complete,
        Retry,
        Stalled,
        Ambiguous,
    };

    AssociatedCheckResult checkAssociated(Context& context, Context::Associated& v) {
        const auto& sp = v.span;
        TRACE_FUNCTION_F(v);

        // Trait matching compares evaluated const arguments structurally.  A
        // monomorphised generic expression still carries its original HIR
        // together with the now-concrete substitutions, so evaluate it before
        // probing candidates instead of treating every concrete impl as fuzzy.
        auto normalizeConstParams = [&](HIRPathParams& params) {
            context.ivars.expandIvarsParams(params);
            for (auto& value : params.values) {
                ConvertHIRConstantEvaluateConstGeneric(sp, context.resolve.board(), context.crate, value);
            }
        };
        normalizeConstParams(v.params);
        normalizeConstParams(v.atyPp);

        ::std::optional<HIRTypeRef> outputType;

        struct H {
            static bool typeIsNum(const HIRTypeData* t) {
                switch ((*t).tag()) {
default:
                    return false;
                    case HIRTypeData::TAG_Primitive: {
                        auto& e = (*t).as_Primitive();
                        switch (e) {
                            case HIRCoreType::Str:
                            case HIRCoreType::Char:
                                return false;
                            default:
                                return true;
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Infer: {
                        auto& e = (*t).as_Infer();
                        return e.isLit();
                    }
                }
                throw "unreachable";
            }

            static bool unaryCanUseExpected(TypeckPrimitiveOperator op, const HIRTypeData* type) {
                if (primitiveOperatorHasBuiltin(op, type)) {
                    return true;
                }
                const auto* infer = type->opt_Infer();
                if (!infer) {
                    return false;
                }
                return (op == TypeckPrimitiveOperator::Not && infer->tyClass == HIRInferClass::Integer)
                    || (op == TypeckPrimitiveOperator::Neg
                        && (infer->tyClass == HIRInferClass::Integer || infer->tyClass == HIRInferClass::Float));
            }
        };

        // A trait implementation only suppresses the language primitive
        // candidate when its signature changes the operation's semantics.
        // Standard-library implementations such as `Shl<i32> for u64` have
        // the same inputs and output as the primitive operation; they must
        // still let an expected output type constrain an untyped lhs literal.
        // A custom impl with a different rhs or Output remains an overload.
        auto implHasBuiltinOperatorSignature = [&](const ImplRef& impl) {
            auto implTy = impl.getImplType(context.crate.types);
            auto implParams = impl.getTraitParams(context.crate.types);
            // Impl probing can expose inference placeholders that belong to
            // the candidate, not this expression.  They have no Context
            // slot, so do not feed them to expansion/comparison below.
            // Conservatively retain such a candidate as a semantic overload.
            if (context.ivars.typeContainsIvars(implTy, /*only_unbound=*/true) || context.ivars.pathparamsContainIvars(implParams, /*only_unbound=*/true)) {
                return false;
            }
            implTy = context.resolve.expandAssociatedTypes(sp, mv$(implTy));
            for (auto& ty : implParams.types) {
                ty = context.resolve.expandAssociatedTypes(sp, mv$(ty));
            }

            const bool hasBuiltinInputs = implParams.types.size() == 0 ? primitiveOperatorHasBuiltin(v.operatorKind, implTy) : implParams.types.size() == 1 && primitiveOperatorHasBuiltin(v.operatorKind, implTy, implParams.types.front());
            if (!hasBuiltinInputs) {
                return false;
            }
            if (v.name == "") {
                return true;
            }

            auto output = impl.getType(context.crate.types, v.name.c_str(), v.atyPp);
            if (output == HIRTypeRef()) {
                return false;
            }
            if (context.ivars.typeContainsIvars(output, /*only_unbound=*/true)) {
                return false;
            }
            output = context.resolve.expandAssociatedTypes(sp, mv$(output));

            auto builtinOutput = implTy;
            if (v.operatorKind == TypeckPrimitiveOperator::Deref) {
                if (const auto* e = implTy->opt_Pointer()) {
                    builtinOutput = e->inner;
                } else if (const auto* e = implTy->opt_Borrow()) {
                    builtinOutput = e->inner;
                } else {
                    return false;
                }
            }
            return output->compareWithPlaceholders(sp, builtinOutput, context.ivars.callbackResolveInfer()) == HIRCompare::Equal;
        };

        // The operator obligation is created with a fresh RHS variable, but
        // the coercion pass may already know the expression's concrete type.
        // Use that type while probing for semantic overloads: an ambiguous
        // `Add<_> for i8` hides the forwarding `Add<&i8>` candidate, and then
        // a builtin hint would incorrectly force the variable to `i8`.
        const auto concreteCoercionSource = [&](const HIRTypeData* type) {
            const auto* infer = context.getType(type)->opt_Infer();
            if (!infer || infer->index == ~0u || infer->index >= context.possibleIvarVals.size()) {
                return static_cast<const HIRTypeData*>(nullptr);
            }
            const auto& possible = context.possibleIvarVals[infer->index];
            for (const auto& source : possible.typesCoerceFrom) {
                const auto* sourceType = context.getType(source.ty);
                if (!sourceType->is_Infer()) {
                    return sourceType;
                }
            }
            return static_cast<const HIRTypeData*>(nullptr);
        };

        bool hasSemanticOperatorImpl = false;
        bool sawCurrentOperatorImpl = false;
        bool currentOperatorImplHasBuiltinSignature = false;
        if (v.operatorKind != TypeckPrimitiveOperator::None) {
            auto probeParams = v.params.clone();
            if (probeParams.types.size() == 1) {
                if (const auto* source = concreteCoercionSource(probeParams.types.front())) {
                    probeParams.types.front() = source;
                }
            }
            context.resolve.findTraitImpls(sp, v.trait, probeParams, v.implTy, [&](ImplRef impl, HIRCompare) {
                if (impl.isAmbiguousIdentity()) {
                    // A merged identity response says that no concrete impl
                    // may guide inference.  It is not itself an overloaded
                    // operator implementation.
                    return false;
                }
                if (context.isCurrentOperatorImpl(impl)) {
                    sawCurrentOperatorImpl = true;
                    currentOperatorImplHasBuiltinSignature = implHasBuiltinOperatorSignature(impl);
                    return false;
                }
                if (!implHasBuiltinOperatorSignature(impl)) {
                    hasSemanticOperatorImpl = true;
                    return true;
                }
                return false;
            });
        }

        // An integer literal can only become a primitive integer, and shifting
        // one yields that same type whatever is on the right. Linking the two
        // without waiting for an impl is what lets an enclosing operator reach
        // the literal in `value & (1 << i)`.
        if (v.name != ""
            && (v.operatorKind == TypeckPrimitiveOperator::Shl || v.operatorKind == TypeckPrimitiveOperator::Shr
                || v.operatorKind == TypeckPrimitiveOperator::ShlAssign || v.operatorKind == TypeckPrimitiveOperator::ShrAssign)) {
            const auto* valueTy = context.getType(v.implTy);
            if (const auto* borrow = valueTy->opt_Borrow()) {
                // Only the standard library's forwarding impls apply to a
                // borrowed primitive, and they yield the referent's type.
                valueTy = context.getType(borrow->inner);
            }
            const auto* leftInfer = valueTy->opt_Infer();
            if (leftInfer && leftInfer->tyClass == HIRInferClass::Integer) {
                DEBUG("- Shift of an integer literal yields its own type");
                context.equateTypes(sp, v.leftTy, valueTy);
            }
        }

        // `b & 1` with `b: &u8` goes through the standard library's forwarding
        // impls, and only those: an impl for `&u8` cannot be written elsewhere.
        // They take the value type on the right and yield it, so a borrowed
        // primitive says as much as a bare one -- including for a shift, whose
        // right side stays free.
        if (v.isOperator && v.params.types.size() == 1 && !context.ivars.typeContainsIvars(v.implTy, /*only_unbound=*/true)) {
            if (const auto* borrow = context.getType(v.implTy)->opt_Borrow()) {
                const auto& valueTy = context.getType(borrow->inner);
                const auto& rightTy = context.getType(v.params.types.front());
                // An unresolved right side may still turn out to be a
                // reference, which the forwarding impls also accept, so only a
                // value -- a literal included -- is enough to go on.
                if (H::typeIsNum(valueTy) && !valueTy->is_Infer() && H::typeIsNum(rightTy)) {
                    DEBUG("- Magic inferrence link through a borrowed primitive");
                    if (v.name != "") {
                        context.equateTypes(sp, v.leftTy, valueTy);
                    }
                    // A comparison forwards as `PartialEq<&B> for &A`, so its
                    // right side stays a reference and this says nothing about it.
                    const bool isComparison = v.operatorKind == TypeckPrimitiveOperator::Equal || v.operatorKind == TypeckPrimitiveOperator::Order;
                    if (!isComparison && primitiveOperatorLhsDeterminesRhs(v.operatorKind, valueTy)) {
                        context.equateTypes(sp, v.params.types.front(), valueTy);
                    }
                }
            }
        }

        // A raw inference placeholder can be the result of a lazy expression
        // (for example, a generic call), rather than an impl candidate.  A
        // language primitive whose known lhs determines the rhs type gives
        // that expression its missing context.  Keep unknown and semantic
        // overloads untouched: those must select an implementation first.
        const bool canContextualisePrimitiveRhs = v.isOperator && !hasSemanticOperatorImpl && (!sawCurrentOperatorImpl || currentOperatorImplHasBuiltinSignature) && v.params.types.size() == 1 && !context.ivars.typeContainsIvars(v.implTy, /*only_unbound=*/true) && primitiveOperatorLhsDeterminesRhs(v.operatorKind, context.getType(v.implTy)) && v.params.types.front()->is_Infer() && v.params.types.front()->as_Infer().index == ~0u;
        if (canContextualisePrimitiveRhs) {
            context.addIvars(v.params.types.front());
        }

        // A lazily generated expression can still contain a raw inference
        // placeholder from an impl candidate. Such a placeholder has no slot
        // in this Context, so primitive equations must wait for it to be
        // contextualised rather than asking HMTypeInferrence to resolve it.
        const bool primitiveTypesAreContextual = !context.ivars.typeContainsIvars(v.implTy, /*only_unbound=*/true) && !context.ivars.pathparamsContainIvars(v.params, /*only_unbound=*/true) && (v.name == "" || !context.ivars.typeContainsIvars(v.leftTy, /*only_unbound=*/true));

        // MAGIC! Have special handling for operator overloads
        // A semantic overload is allowed to determine its result type
        // instead, so don't install primitive equations in that case.
        if (v.isOperator && !hasSemanticOperatorImpl && primitiveTypesAreContextual) {
            if (v.params.types.size() == 0) {
                // Uni ops = If the value is a primitive, the output is the same type
                const auto& ty = context.getType(v.implTy);
                const auto& res = context.getType(v.leftTy);
                if (H::typeIsNum(ty)) {
                    DEBUG("- Magic inferrence link for uniops on numerics");
                    context.equateTypes(sp, res, ty);
                }
            } else if (v.params.types.size() == 1) {
                // Binary primitive operations use the lhs as their result
                // (except comparisons), and a known builtin lhs also gives a
                // raw rhs inference variable a concrete type.
                const auto& left = v.implTy; // yes, impl = LHS of binop
                const auto& right = v.params.types.at(0);
                const auto& res = v.leftTy;
                const auto& leftTy = context.getType(left);
                const auto& rightTy = context.getType(right);
                const bool primitiveOrLiteralPair = H::typeIsNum(leftTy) && H::typeIsNum(rightTy);
                const bool languagePrimitiveCandidate = primitiveOperatorHasLanguageCandidate(v.operatorKind, leftTy, rightTy);
                const bool isShiftOperator = v.operatorKind == TypeckPrimitiveOperator::Shl || v.operatorKind == TypeckPrimitiveOperator::Shr
                    || v.operatorKind == TypeckPrimitiveOperator::ShlAssign || v.operatorKind == TypeckPrimitiveOperator::ShrAssign;
                if (primitiveOrLiteralPair || languagePrimitiveCandidate) {
                    DEBUG("- Magic inferrence link for primitive binops");
                    if (v.name == "") {
                        // Comparison op, output already known to be `bool`
                    } else {
                        context.equateTypes(sp, res, left);
                    }
                    if (isShiftOperator) {
                        // Shifts can have mismatched types on each side.
                    } else {
                        // NOTE: This only holds if not a shift
                        context.equateTypes(sp, left, right);
                    }
                    // Assignment and comparison trait constraints have no
                    // associated output (`name == ""`), so `res` is an
                    // absent placeholder rather than a Context ivar.
                    if (v.name != "" && context.getType(left)->is_Infer() && context.getType(right)->is_Infer() && context.getType(res)->is_Infer()) {
                        context.possibleEquateTypeUnknown(sp, right, Context::IvarUnknownType::To);
                        DEBUG("> All are infer, skip");
                        return AssociatedCheckResult::Stalled;
                    }
                }

                context.possibleEquateTypeUnknown(sp, right, Context::IvarUnknownType::To);
            } else {
                BUG(sp, "Associated type rule with `is_operator` set but an incorrect parameter count");
            }
        }

        // If the output type is present, prevent it from being guessed
        // - This generates an exact equation.
        if (v.name != "") {
            context.possibleEquateTypeUnknown(sp, v.leftTy, Context::IvarUnknownType::Bound);
        }

        if (v.isOperator && v.params.types.empty() && context.getType(v.implTy)->is_Diverge()
            && H::unaryCanUseExpected(v.operatorKind, context.getType(v.leftTy))) {
            return AssociatedCheckResult::Complete;
        }

        // If the impl type is an unbounded ivar, and there's no trait args - don't bother searching
        if (const auto* e = context.ivars.getType(v.implTy)->opt_Infer()) {
            // TODO: ?
            if (!e->isLit() && v.params.types.empty()) {
                return AssociatedCheckResult::Ambiguous;
            }

            // If the type is completely unbounded, then any lookup will fail.
            // - Disable inference on the type params (as a future impl will add bounds)
            if (!e->isLit()) {
                for (const auto& t : v.params.types) {
                    context.possibleEquateTypeUnknown(sp, t, Context::IvarUnknownType::To);
                }
                return AssociatedCheckResult::Ambiguous;
            }
        }

        // While typechecking an operator implementation, its own associated
        // type remains a valid assumption for a genuine overload.  Exclude
        // it only when the expression itself has the language primitive
        // semantics; otherwise a generic smart pointer cannot establish the
        // Target used by its own `deref` body.
        const auto currentOperatorUsesLanguagePrimitive = [&]() {
            if (!v.isOperator || v.operatorKind == TypeckPrimitiveOperator::None || (sawCurrentOperatorImpl && !currentOperatorImplHasBuiltinSignature)) {
                return false;
            }

            const auto& implTy = context.getType(v.implTy);
            if (v.params.types.size() == 0) {
                return primitiveOperatorHasBuiltin(v.operatorKind, implTy);
            }
            if (v.params.types.size() == 1) {
                return primitiveOperatorHasLanguageCandidate(v.operatorKind, implTy, context.getType(v.params.types.front()));
            }
            return false;
        };

        // Locate applicable trait impl
        unsigned int count = 0;
        DEBUG("Searching for impl " << v.trait << v.params << " for " << context.ivars.fmtType(v.implTy));

        struct Possibility {
            HIRTypeRef implTy;
            HIRPathParams params;
            ImplRef implRef;
        };

        ::std::vector<Possibility> possibleImpls;
        bool sawAmbiguousIdentity = false;
        try {
            auto candidateCallback = [&](ImplRef impl, HIRCompare cmp) {
                DEBUG("[check_associated] Found cmp=" << cmp << " " << impl);
                if (impl.isAmbiguousIdentity()) {
                    ASSERT_BUG(sp, cmp == HIRCompare::Fuzzy, "Definite solver response marked as ambiguous identity");
                    sawAmbiguousIdentity = true;
                    return false;
                }
                if (v.operatorKind != TypeckPrimitiveOperator::None && context.isCurrentOperatorImpl(impl)) {
                    if (currentOperatorUsesLanguagePrimitive()) {
                        DEBUG("[check_associated] - language primitive wins over current trait impl");
                        return false;
                    }
                    DEBUG("[check_associated] - use current trait impl as overload assumption");
                }
                if (v.name != "") {
                    // A generic associated type is substituted with the
                    // arguments the projection gave it (`Self::Bar<T>`).
                    auto outTyO = impl.getType(context.crate.types, v.name.c_str(), v.atyPp);
                    if (outTyO == HIRTypeRef()) {
                        outTyO = context.crate.types.path(HIRPath(v.implTy, HIRGenericPath(v.trait, v.params.clone()), v.name, v.atyPp.clone()), {});
                    }
                    outTyO = context.resolve.expandAssociatedTypes(sp, mv$(outTyO));

                    // TODO: if this is an unbound UfcsUnknown, treat as a fuzzy match.
                    // - Shouldn't compare_with_placeholders do that?

                    // - If we're looking for an associated type, allow it to eliminate impossible impls
                    //  > This makes `let v: usize = !0;` work without special cases
                    auto cmp2 = v.leftTy->compareWithPlaceholders(sp, outTyO, context.ivars.callbackResolveInfer());
                    bool definingOpaqueOutput = false;
                    if (const auto* erased = v.leftTy->opt_ErasedType()) {
                        if (const auto* alias = erased->inner.opt_Alias()) {
                            definingOpaqueOutput = context.resolve.isOpaqueAliasDefiningScope(*alias->inner);
                        }
                    }
                    if (cmp2 == HIRCompare::Unequal && !definingOpaqueOutput) {
                        DEBUG("[check_associated] - (fail) known result can't match (" << context.ivars.fmtType(v.leftTy) << " and " << context.ivars.fmtType(outTyO) << ")");
                        return false;
                    }
                    // if solid or fuzzy, leave as-is
                    outputType = mv$(outTyO);
                    DEBUG("[check_associated] cmp = " << cmp << " (2) out=" << *outputType);
                }
                if (cmp == HIRCompare::Equal) {
                    // NOTE: Sometimes equal can be returned when it's not 100% equal (TODO)
                    // - Equate the types
                    context.equateTypes(sp, v.implTy, impl.getImplType(context.crate.types));
                    auto itp = impl.getTraitParams(context.crate.types);
                    ASSERT_BUG(sp, v.params.types.size() == itp.types.size(), "Parameter count mismatch between impl and rule: r=" << v.params << " i=" << itp);
                    for (unsigned int i = 0; i < v.params.types.size(); i++) {
                        context.equateTypes(sp, v.params.types[i], itp.types[i]);
                    }
                    for (unsigned int i = 0; i < v.params.values.size(); i++) {
                        context.equateValues(sp, v.params.values[i], itp.values[i]);
                    }
                    // The next solver has already evaluated the selected
                    // impl's where-clauses while building this response.
                    // Re-exporting them into the legacy constraint loop
                    // evaluates the same proof a second time and turns a
                    // coinductive fixed point into an endless new rule.
                    if (!context.resolve.board().settings->solver.globally) {
                        addImplBounds(context, sp, impl);
                    }
                    return true;
                } else {
                    count += 1;
                    DEBUG("[check_associated] - (possible) " << impl);

                    auto implTy = impl.getImplType(context.crate.types);
                    auto implParams = impl.getTraitParams(context.crate.types);

                    implTy = context.resolve.expandAssociatedTypes(sp, std::move(implTy));
                    for (auto& t : implParams.types) {
                        t = context.resolve.expandAssociatedTypes(sp, mv$(t));
                    }

                    if (possibleImpls.empty()) {
                        DEBUG("[check_associated] First - " << impl);
                        possibleImpls.push_back({std::move(implTy), std::move(implParams), std::move(impl)});
                    }
                    // If there is an existing impl, determine if this is part of the same specialisation tree
                    // - If more specific, replace. If less, ignore.
                    // NOTE: `overlaps_with` (should be) reflective
                    else {
                        bool wasUsed = false;
                        for (auto& possibleImpl : possibleImpls) {
                            const auto& bestImpl = possibleImpl.implRef;
                            // TODO: Handle duplicates (from overlapping bounds)
                            if (context.resolve.implsOverlap(sp, impl, bestImpl)) {
                                DEBUG("[check_associated] - Overlaps with existing - " << bestImpl);
                                if (impl.moreSpecificThan(context.crate.types, bestImpl)) {
                                    // Both candidates reached this branch only because
                                    // applicability is still fuzzy.  Specialisation orders
                                    // applicable candidates; it must not turn an unproven
                                    // child impl's predicates into obligations.  Keep the
                                    // ambiguity until inference can prove one candidate.
                                    DEBUG("[check_associated] - More specific, but applicability is still fuzzy");
                                } else if (bestImpl.moreSpecificThan(context.crate.types, impl)) {
                                    DEBUG("[check_associated] - Less specific, but applicability is still fuzzy");
                                } else {
                                    DEBUG("[check_associated] > Overlapping impls have equal or incomparable specificity");
                                }
                                // Both matches are fuzzy, so overlap only orders the
                                // candidates after their predicates are known.  Keep
                                // this candidate in the possibility set: collapsing
                                // it here makes `count` and `possible_impls` disagree
                                // and can incorrectly close an ivar's bounded set over
                                // just the first, ultimately inapplicable impl.
                                break;
                            } else {
                                // Disjoint impls.
                                DEBUG("[check_associated] Disjoint with " << bestImpl);
                            }

                            // Edge case: Might be just outright identical
                            if (possibleImpl.implTy == implTy && possibleImpl.params == implParams) {
                                auto t1 = v.name == "" ? HIRTypeRef() : possibleImpl.implRef.getType(context.crate.types, v.name.c_str(), v.atyPp);
                                auto t2 = v.name == "" ? HIRTypeRef() : impl.getType(context.crate.types, v.name.c_str(), {});
                                if (v.name == "" || t1 == t2 || t2 == HIRTypeRef()) {
                                    DEBUG("[check_associated] HACK: Same type and params, and ATY matches or this impl doesn't have it");
                                    wasUsed = true;
                                    count -= 1;
                                    break;
                                } else if (t1 == HIRTypeRef()) {
                                    DEBUG("[check_associated] - Same type and params, and has an ATY (while original doesn't)");
                                    // NOTE: This picks the _least_ specific impl
                                    possibleImpl.implTy = ::std::move(implTy);
                                    possibleImpl.params = ::std::move(implParams);
                                    possibleImpl.implRef = ::std::move(impl);
                                    wasUsed = true;
                                    count -= 1;
                                    break;
                                } else {
                                    DEBUG("[check_associated] HACK: Same type and params, but ATY mismatch - " << possibleImpl.implRef.getType(context.crate.types, v.name.c_str(), {}) << " != " << impl.getType(context.crate.types, v.name.c_str(), {}));
                                }
                            }
                        }
                        if (!wasUsed) {
                            DEBUG("[check_associated] Add new possible: " << impl);
                            possibleImpls.push_back({::std::move(implTy), ::std::move(implParams), ::std::move(impl)});
                        }
                    }

                    return false;
                }
            };
            const bool found = context.resolve.board().settings->solver.globally ? context.resolve.findTraitImplsNext(sp, v.trait, v.params, v.implTy, candidateCallback, v.name.c_str(), v.name == "" ? nullptr : v.leftTy, v.name == "" ? nullptr : &v.atyPp) : context.resolve.findTraitImpls(sp, v.trait, v.params, v.implTy, candidateCallback);
            if (found) {
                // Fully-known impl
                DEBUG("Fully-known impl located");
                if (v.name != "") {
                    // Stop this from just pushing the same rule again.
                    if ((*outputType)->is_Path() && (*outputType)->as_Path().path.data.is_UfcsKnown()) {
                        const auto& te = (*outputType)->as_Path();
                        const auto& pe = te.path.data.as_UfcsKnown();
                        // If the target type is unbound, and is this rule exactly, don't return success
                        if (te.binding.is_Unbound() && pe.type == v.implTy && pe.item == v.name && pe.trait.path == v.trait && pe.trait.params == v.params) {
                            DEBUG("Would re-create the same rule, setting opaque");
                            auto data = (*outputType)->cloneData();
                            data.as_Path().binding = HIRTypePathBinding::make_Opaque({});
                            outputType = context.crate.types.intern(std::move(data));
                        }
                    }
                    context.equateTypes(sp, v.leftTy, *outputType);
                }
                // TODO: Any equating of type params?
                return AssociatedCheckResult::Complete;
            } else if (count == 0) {
                if (sawAmbiguousIdentity) {
                    return AssociatedCheckResult::Ambiguous;
                }
                // No applicable impl
                // - TODO: This should really only fire when there isn't an impl. But it currently fires when _
                if (v.name == "") {
                    DEBUG("No impl of " << v.trait << context.ivars.fmt(v.params) << " for " << context.ivars.fmtType(v.implTy));
                } else {
                    DEBUG("No impl of " << v.trait << context.ivars.fmt(v.params) << " for " << context.ivars.fmtType(v.implTy) << " with " << v.name << " = " << context.ivars.fmtType(v.leftTy));
                }

                const auto& ty = context.getType(v.implTy);
                bool isKnown = !ty->is_Infer() && !(ty->is_Path() && ty->as_Path().binding.is_Unbound());
                if (!isKnown) {
                    // There's still an ivar (or an unbound UFCS), keep trying
                    return AssociatedCheckResult::Stalled;
                } else if (v.trait == context.resolve.langUnsize()) {
                    // TODO: Detect if this was a compiler-generated bound, or was actually in the code.

                    ASSERT_BUG(sp, v.params.types.size() == 1, "Incorrect number of parameters for Unsize");
                    const auto& srcTy = context.getType(v.implTy);
                    const auto& dstTy = context.getType(v.params.types[0]);

                    context.equateTypes(sp, dstTy, srcTy);
                    return AssociatedCheckResult::Complete;
                } else if (v.operatorKind != TypeckPrimitiveOperator::None && (v.params.types.size() == 0 ? primitiveOperatorHasBuiltin(v.operatorKind, context.getType(v.implTy)) : v.params.types.size() == 1 && primitiveOperatorHasBuiltin(v.operatorKind, context.getType(v.implTy), context.getType(v.params.types.at(0))))) {
                    // No trait implementation matched this expression.  The
                    // language-defined primitive candidate therefore wins.
                    return AssociatedCheckResult::Complete;
                } else {
                    if (v.name == "") {
                        ERROR(sp, E0000, "Failed to find an impl of " << v.trait << context.ivars.fmt(v.params) << " for " << context.ivars.fmtType(v.implTy));
                    } else {
                        ERROR(sp, E0000, "Failed to find an impl of " << v.trait << context.ivars.fmt(v.params) << " for " << context.ivars.fmtType(v.implTy) << " with " << v.name << " = " << context.ivars.fmtType(v.leftTy));
                    }
                }
            } else if (count == 1) {
                auto& possibleImplTy = possibleImpls.at(0).implTy;
                auto& possibleParams = possibleImpls.at(0).params;
                auto& bestImpl = possibleImpls.at(0).implRef;
                DEBUG("Only one impl " << v.trait << context.ivars.fmt(possibleParams) << " for " << context.ivars.fmtType(possibleImplTy) << FMT_CB(os, if (outputType) os << " - out=" << *outputType;));
                // - If there are any magic params in the impl, don't use it yet.
                //  > Ideally, there should be a match_test_generics to resolve the magic impls.
                DEBUG("> best_impl=" << bestImpl);
                if (bestImpl.hasMagicParams()) {
                    // Pick this impl, and evaluate it (expanding the magic params out)
                    // - Equate `v.impl_ty` and `best_impl`'s type...
                    //   - We expect an ivar from `v.impl_ty` to be matched against some sort of known type (struct, tuple, array, ...)
                    //   - When that happens, allocate new ivars for the magic params in that type and assign.
                    struct Matcher: public HIRMatchGenerics, public Monomorphiser {
                        Context& context;
                        mutable ::std::map<HIRGenericRef, HIRTypeRef> types;
                        mutable ::std::map<HIRGenericRef, HIRConstGeneric> values;

                        Matcher(Context& context)
                            : Monomorphiser(context.crate.types)
                            , context(context)
                        {
                        }

                        HIRCompare cmpType(const Span& sp, const HIRTypeData* tyL, const HIRTypeData* tyR, tCbResolveType resolveCb) override {
                            const auto& l = (tyL->is_Infer() ? resolveCb.getType(sp, tyL) : tyL);
                            const auto& r = (tyR->is_Infer() ? resolveCb.getType(sp, tyR) : tyR);
                            if (tyR->is_Generic() && tyR->as_Generic().group() == GENERICPlaceholder) {
                                BUG(sp, "Assigning into a placeholder? should have been known");
                            }
                            if (l->is_Infer() && !r->is_Infer()) {
                                // Monomorph the RHS, assigning new ivars to each impl param
                                auto newTy = this->monomorphType(sp, r, true);
                                context.equateTypes(sp, l, newTy);
                                return HIRCompare::Equal;
                            }
                            return HIRMatchGenerics::cmpType(sp, tyL, tyR, resolveCb);
                        }

                        HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) override {
                            if (ty->is_Generic() && ty->as_Generic() == g) {
                                return HIRCompare::Equal;
                            }
                            TODO(Span(), "match_ty - " << g << " = " << ty);
                        }

                        HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& v) override {
                            if (v.is_Generic() && v.as_Generic() == g) {
                                return HIRCompare::Equal;
                            }
                            TODO(Span(), "match_val - " << g << " = " << v);
                        }

                        HIRTypeRef getType(const Span& sp, const HIRGenericRef& g) const override {
                            if (g.group() == GENERICPlaceholder) {
                                auto it = types.find(g);
                                if (it == types.end()) {
                                    it = types.insert(std::make_pair(g, context.ivars.newIvarTr())).first;
                                    DEBUG("New type ivar for placeholder " << g << " = " << it->second);
                                }
                                return it->second;
                            } else {
                                return context.crate.types.generic(g.name, g.binding);
                            }
                        }

                        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override {
                            if (g.group() == GENERICPlaceholder) {
                                auto it = values.find(g);
                                if (it == values.end()) {
                                    auto v = HIRConstGeneric::make_Infer(HIRConstGeneric::Data_Infer{context.ivars.newIvarVal()});
                                    it = values.insert(std::make_pair(g, std::move(v))).first;
                                    DEBUG("New value ivar for placeholder " << g << " = " << it->second);
                                }
                                return it->second.clone();
                            } else {
                                return g;
                            }
                        }

                    } m{context};

                    m.cmpType(sp, v.implTy, possibleImplTy, context.ivars.callbackResolveInfer());
                    for (size_t i = 0; i < possibleParams.types.size(); i++) {
                        m.cmpType(sp, v.params.types[i], possibleParams.types[i], context.ivars.callbackResolveInfer());
                    }
                    DEBUG("> Magic params present, wait");
                    return AssociatedCheckResult::Retry;
                }
                const auto& implTy = context.ivars.getType(v.implTy);
                if (((*implTy).is_Path() && ((*implTy).as_Path().binding.is_Unbound()))) {
                    DEBUG("Unbound UfcsKnown, waiting");
                    return AssociatedCheckResult::Stalled;
                }
                if (((*implTy).is_Infer() && ((*implTy).as_Infer().isLit() == false))) {
                    DEBUG("Unbounded ivar, waiting - TODO: Add possibility " << implTy << " == " << possibleImplTy);
                    return AssociatedCheckResult::Ambiguous;
                }
                assert(possibleImplTy != HIRTypeRef());
                context.equateTypes(sp, v.implTy, possibleImplTy);
                for (unsigned int i = 0; i < possibleParams.types.size(); i++) {
                    context.equateTypes(sp, v.params.types[i], possibleParams.types[i]);
                }
                for (unsigned int i = 0; i < possibleParams.values.size(); i++) {
                    context.equateValues(sp, v.params.values[i], possibleParams.values[i]);
                }
                // Only one possible impl. Resolve its inputs before the output:
                // an associated type can depend on an ivar fixed by the selected impl.
                if (v.name != "") {
                    outputType = context.resolve.expandAssociatedTypes(sp, mv$(*outputType));

                    // A magic trait response can be the identity projection
                    // while its input is still a numeric literal ivar.  That
                    // response has not resolved the associated type: keep the
                    // rule until literal fallback fixes the input and lets the
                    // magic impl return its concrete output.
                    if (const auto* outputPath = (*outputType)->opt_Path();
                        outputPath && outputPath->path.data.is_UfcsKnown()) {
                        const auto& pe = outputPath->path.data.as_UfcsKnown();
                        if (pe.type == v.implTy && pe.trait.path == v.trait && pe.trait.params == v.params && pe.item == v.name
                            && context.getType(v.implTy)->is_Infer()) {
                            DEBUG("Identity associated projection still depends on an ivar");
                            return AssociatedCheckResult::Stalled;
                        }
                    }

                    // If the output type is just < v.impl_ty as v.trait >::v.name, return false
                    if (((**outputType).is_Path() && ((**outputType).as_Path().path.data.is_UfcsKnown()))) {
                        auto& pe = (*outputType)->as_Path().path.data.as_UfcsKnown();

                        if (pe.type == v.implTy && pe.trait.path == v.trait && pe.trait.params == v.params && pe.item == v.name) {
                            if (((*v.leftTy).is_Path() && ((*v.leftTy).as_Path().path.data.is_UfcsKnown()))) {
                                auto data = (*outputType)->cloneData();
                                data.as_Path().binding = HIRTypePathBinding::make_Opaque({});
                                outputType = context.crate.types.intern(std::move(data));
                            }
                        }
                    }
                    context.equateTypes(sp, v.leftTy, *outputType);
                }
                // - Obtain the bounds required for this impl and add those as trait bounds to check/equate
                // A fuzzy next-solver response has not proved the selected
                // impl's predicates.  Keep them in the constraint loop just
                // like the legacy selector; only the Equal response above
                // may consume its nested goals completely.
                addImplBounds(context, sp, bestImpl);
                return AssociatedCheckResult::Complete;
            } else {
                // Multiple possible impls, don't know yet
                DEBUG("Multiple impls");
                // TODO: Make a solid list of possibilities in each of `v.params`
                std::map<unsigned, std::vector<HIRTypeRef>> ivarPossibilities;
                for (const auto& pi : possibleImpls) {
                    DEBUG("impl " << v.trait << pi.params << " for " << pi.implTy);
                    for (size_t i = 0; i < pi.params.types.size(); i++) {
                        const auto& t = context.getType(v.params.types[i]);
                        if (const auto* e = t->opt_Infer()) {
                            const auto& piT = pi.params.types[i];
                            HIRTypeRef possibleTy;
                            if (!typeContainsImplPlaceholder(context.crate.types, piT)) {
                                possibleTy = piT;
                            } else {
                                DEBUG("Not adding placeholder-containing type as a bound - " << piT);
                                // Push this ivar
                                possibleTy = t;
                            }

                            if (std::find(ivarPossibilities[e->index].begin(), ivarPossibilities[e->index].end(), possibleTy) == ivarPossibilities[e->index].end()) {
                                ivarPossibilities[e->index].push_back(std::move(possibleTy));
                            }
                        }
                    }
                }
                for (auto& e : ivarPossibilities) {
                    DEBUG("IVar _/*" << e.first << "*/ ?= [" << e.second << "]");
                    context.possibleEquateIvarBounds(sp, e.first, std::move(e.second));
                }
                return AssociatedCheckResult::Ambiguous;
            }
        } catch (const TraitResolution::RecursionDetected&) {
            DEBUG("Recursion detected, deferring");
            return AssociatedCheckResult::Retry;
        }
    }

} // namespace "" - check_associated and check_coerce

// check_ivar_poss (and helpers)
namespace {
    bool pathParamsHaveUntrackedConst(const HIRPathParams& params) {
        return ::std::any_of(params.values.begin(), params.values.end(), [](const auto& value) {
            return value.is_Infer() || value.is_Unevaluated();
        });
    }

    struct AssociatedStallCollector {
        Context& context;
        ::std::vector<Context::Associated::StallDependency>& dependencies;
        ::std::vector<HIRTypeRef> pending;
        ::std::vector<HIRTypeRef> visited;
        bool hasRawInfer = false;

        void addType(HIRTypeRef type) {
            if (type->hasTypeInfer()) {
                pending.push_back(type);
            }
        }

        void collect() {
            while (!pending.empty() && !hasRawInfer) {
                const auto type = pending.back();
                pending.pop_back();
                if (::std::find(visited.begin(), visited.end(), type) != visited.end()) {
                    continue;
                }
                visited.push_back(type);

                visitTyWith(type, [&](const HIRTypeData* inner) {
                    const auto* infer = inner->opt_Infer();
                    if (!infer) {
                        return false;
                    }
                    if (infer->index == ~0u) {
                        hasRawInfer = true;
                        return true;
                    }

                    const auto resolved = context.ivars.getType(infer->index);
                    if (const auto* resolvedInfer = resolved->opt_Infer()) {
                        if (resolvedInfer->index == ~0u) {
                            hasRawInfer = true;
                            return true;
                        }
                        const auto existing = ::std::find_if(dependencies.begin(), dependencies.end(), [&](const auto& dependency) {
                            return dependency.index == resolvedInfer->index;
                        });
                        if (existing == dependencies.end()) {
                            dependencies.push_back({resolvedInfer->index, resolved});
                        }
                    } else if (resolved->hasTypeInfer()) {
                        pending.push_back(resolved);
                    }
                    return false;
                });
            }
        }
    };

    bool setAssociatedStall(Context& context, Context::Associated& rule) {
        rule.stalledOn.clear();

        const auto typeCanStall = [](const HIRTypeRef type) {
            return (type->flags & (HIRTypeData::HAS_UNEVALUATED_CONST | HIRTypeData::HAS_DEFERRED_CONST)) == 0;
        };
        if (!typeCanStall(rule.implTy) || (rule.name != "" && !typeCanStall(rule.leftTy)) || pathParamsHaveUntrackedConst(rule.params) || pathParamsHaveUntrackedConst(rule.atyPp)) {
            return false;
        }
        for (const auto type : rule.params.types) {
            if (!typeCanStall(type)) {
                return false;
            }
        }
        for (const auto type : rule.atyPp.types) {
            if (!typeCanStall(type)) {
                return false;
            }
        }

        AssociatedStallCollector collector{context, rule.stalledOn};
        collector.addType(rule.implTy);
        if (rule.name != "") {
            collector.addType(rule.leftTy);
        }
        for (const auto type : rule.params.types) {
            collector.addType(type);
        }
        for (const auto type : rule.atyPp.types) {
            collector.addType(type);
        }
        collector.collect();

        if (collector.hasRawInfer || rule.stalledOn.empty()) {
            rule.stalledOn.clear();
            return false;
        }
        return true;
    }

    bool associatedStillStalled(const Context& context, const Context::Associated& rule) {
        if (rule.stalledOn.empty()) {
            return false;
        }
        return ::std::all_of(rule.stalledOn.begin(), rule.stalledOn.end(), [&](const auto& dependency) {
            return context.ivars.getType(dependency.index) == dependency.resolved;
        });
    }

    void mergeAssociatedPossibilities(Context& context, const ::std::vector<Context::Associated::CapturedIvarPossible>& captured) {
        for (const auto& value : captured) {
            const auto resolved = context.ivars.getType(value.index);
            if (!resolved->is_Infer() || resolved->as_Infer().index != value.index) {
                continue;
            }
            if (value.index >= context.possibleIvarVals.size()) {
                context.possibleIvarVals.resize(value.index + 1);
            }
            context.possibleIvarVals[value.index].mergeFrom(value.possibilities);
        }
    }

    struct AssociatedPossibilityCapture {
        Context& context;
        ::std::vector<Context::Associated::CapturedIvarPossible>* previous;

        AssociatedPossibilityCapture(Context& context, ::std::vector<Context::Associated::CapturedIvarPossible>& destination)
            : context(context)
            , previous(context.possibleIvarSink)
        {
            context.possibleIvarSink = &destination;
        }

        ~AssociatedPossibilityCapture() {
            context.possibleIvarSink = previous;
        }
    };

    struct IvarDependencyIndex {
        Context& context;
        ::std::vector<::std::vector<unsigned int>> associatedTargets;
        ::std::vector<::std::vector<unsigned int>> possibilityTargets;

        static void collectDirectIvars(const HIRTypeData* type, ::std::vector<unsigned int>& out) {
            visitTyWith(type, [&](const HIRTypeData* inner) {
                if (const auto* infer = inner->opt_Infer()) {
                    out.push_back(infer->index);
                }
                return false;
            });
        }

        static void deduplicate(::std::vector<unsigned int>& values) {
            ::std::sort(values.begin(), values.end());
            values.erase(::std::unique(values.begin(), values.end()), values.end());
        }

        IvarDependencyIndex(Context& context)
            : context(context)
            , associatedTargets(context.possibleIvarVals.size())
            , possibilityTargets(context.possibleIvarVals.size())
        {
            for (const auto& rule : context.linkAssoc) {
                ::std::vector<unsigned int> sources;
                ::std::vector<unsigned int> targets;
                collectDirectIvars(rule.implTy, sources);
                if (rule.name != "") {
                    collectDirectIvars(rule.leftTy, sources);
                }
                collectDirectIvars(rule.implTy, targets);
                for (const auto& type : rule.params.types) {
                    collectDirectIvars(type, sources);
                    collectDirectIvars(type, targets);
                }
                deduplicate(sources);
                deduplicate(targets);
                for (const auto source : sources) {
                    if (source < associatedTargets.size()) {
                        associatedTargets[source].insert(associatedTargets[source].end(), targets.begin(), targets.end());
                    }
                }
            }

            for (size_t target = 0; target < context.possibleIvarVals.size(); target++) {
                const auto& possible = context.possibleIvarVals[target];
                ::std::vector<unsigned int> sources;
                for (const auto& type : possible.typesCoerceFrom) {
                    collectDirectIvars(type.ty, sources);
                }
                for (const auto& type : possible.typesCoerceTo) {
                    collectDirectIvars(type.ty, sources);
                }
                for (const auto& type : possible.bounded) {
                    collectDirectIvars(type, sources);
                }
                deduplicate(sources);
                for (const auto source : sources) {
                    if (source < possibilityTargets.size()) {
                        possibilityTargets[source].push_back(target);
                    }
                }
            }

            for (auto& targets : associatedTargets) {
                deduplicate(targets);
            }
            for (auto& targets : possibilityTargets) {
                deduplicate(targets);
            }
        }

        void disableDependents(unsigned int source) {
            if (source >= associatedTargets.size()) {
                return;
            }
            for (const auto rawTarget : associatedTargets[source]) {
                const auto& target = context.ivars.getType(rawTarget);
                if (const auto* infer = target->opt_Infer()) {
                    DEBUG("Disable IVar " << infer->index);
                    if (infer->index < context.possibleIvarVals.size()) {
                        context.possibleIvarVals[infer->index].forceDisable = true;
                    }
                }
            }
            for (const auto target : possibilityTargets[source]) {
                DEBUG("Block IVar " << target);
                context.possibleIvarVals[target].forceDisable = true;
            }
        }
    };

    struct IvarBoundRefs {
        ::std::vector<const Context::Coercion*> coercions;
        ::std::vector<const Context::Associated*> associated;
        ::std::vector<const HIRExprNodeCallMethod*> methods;
    };

    struct IvarBoundIndex {
        const Context& context;
        ::std::vector<IvarBoundRefs> refs;

        void collectIvars(const HIRTypeData* root, ::std::vector<unsigned int>& out) const {
            ::std::vector<HIRTypeRef> pending{root};
            ::std::vector<HIRTypeRef> visited;
            while (!pending.empty()) {
                const auto type = pending.back();
                pending.pop_back();
                if (::std::find(visited.begin(), visited.end(), type) != visited.end()) {
                    continue;
                }
                visited.push_back(type);
                visitTyWith(type, [&](const HIRTypeData* inner) {
                    if (const auto* infer = inner->opt_Infer()) {
                        out.push_back(infer->index);
                        const auto& resolved = context.getType(inner);
                        if (resolved != inner) {
                            pending.push_back(resolved);
                        }
                    }
                    return false;
                });
            }
        }

        template <typename T>
        void addRefs(const ::std::vector<unsigned int>& dependencies, ::std::vector<T> IvarBoundRefs::* member, T value) {
            for (const auto index : dependencies) {
                if (index < refs.size()) {
                    (refs[index].*member).push_back(value);
                }
            }
        }

        explicit IvarBoundIndex(const Context& context)
            : context(context)
            , refs(context.possibleIvarVals.size())
        {
            ::std::vector<unsigned int> dependencies;
            for (const auto& bound : context.linkCoerce) {
                dependencies.clear();
                collectIvars(bound->leftTy, dependencies);
                collectIvars((*bound->rightNodePtr)->resType, dependencies);
                IvarDependencyIndex::deduplicate(dependencies);
                addRefs(dependencies, &IvarBoundRefs::coercions, static_cast<const Context::Coercion*>(bound.get()));
            }
            for (const auto& bound : context.linkAssoc) {
                dependencies.clear();
                collectIvars(bound.implTy, dependencies);
                for (const auto& type : bound.params.types) {
                    collectIvars(type, dependencies);
                }
                IvarDependencyIndex::deduplicate(dependencies);
                addRefs(dependencies, &IvarBoundRefs::associated, &bound);
            }
            for (const auto* nodePtrDyn : context.toVisit) {
                if (const auto* nodePtr = cast<const HIRExprNodeCallMethod>(nodePtrDyn)) {
                    dependencies.clear();
                    collectIvars(context.getType(nodePtr->value->resType), dependencies);
                    IvarDependencyIndex::deduplicate(dependencies);
                    addRefs(dependencies, &IvarBoundRefs::methods, nodePtr);
                }
            }
        }

        const IvarBoundRefs& operator[](unsigned int index) const {
            return refs.at(index);
        }
    };

    bool checkIvarPossFailsBounds(
        const Span& sp,
        Context& context,
        const IvarBoundRefs& boundRefs,
        const HIRTypeData* tyL,
        const HIRTypeData* newTy,
        unsigned int* exactBoundCount = nullptr
    ) {
        TRACE_FUNCTION_F(tyL << " <- " << newTy);
        const auto ivarIdx = tyL->as_Infer().index;
        bool usedTy = false;
        unsigned int exactBounds = 0;

        struct Cb {
            bool& usedTy;
            const Span& sp;
            const Context& context;
            unsigned int ivarIdx;
            const HIRTypeData* newTy;

            Cb(bool& usedTy, const Span& sp, const Context& context, unsigned int ivarIdx, const HIRTypeData* newTy)
                : usedTy(usedTy)
                , sp(sp)
                , context(context)
                , ivarIdx(ivarIdx)
                , newTy(newTy)
            {
            }

            bool operator()(const HIRTypeData* ty, HIRTypeRef& outTy) {
                const auto* e = ty->opt_Infer();
                if (!e) {
                    return false;
                }
                if (e->index == ivarIdx) {
                    outTy = newTy;
                    usedTy = true;
                    return true;
                }
                const auto& rty = context.getType(ty);
                if (const auto* resolved = rty->opt_Infer(); resolved && resolved->index == e->index) {
                    return false;
                }
                outTy = cloneTyWith(context.crate.types, sp, rty, *this);
                return true;
            }
        };

        Cb cb{usedTy, sp, context, ivarIdx, newTy};
        for (const auto* bound : boundRefs.coercions) {
            usedTy = false;
            auto tL = cloneTyWith(context.crate.types, sp, bound->leftTy, cb);
            auto tR = cloneTyWith(context.crate.types, sp, (*bound->rightNodePtr)->resType, cb);
            if (!usedTy) {
                continue;
            }
            tL = context.resolve.expandAssociatedTypes(sp, mv$(tL));
            tR = context.resolve.expandAssociatedTypes(sp, mv$(tR));
            DEBUG("Check Coerce R" << bound->ruleIdx << " - " << bound->leftTy << " := " << (*bound->rightNodePtr)->resType);
            DEBUG("Testing " << tL << " := " << tR);

            switch (checkCoerceTys(context, sp, tL, tR, nullptr, bound->rightNodePtr)) {
                case CoerceResult::Fail:
                    DEBUG("Fail - Invalid");
                    return true;
                case CoerceResult::Unsize:
                    DEBUG("Unsize - Valid");
                    break;
                case CoerceResult::Unknown:
                    DEBUG("Unknown?");
                    break;
                case CoerceResult::Custom:
                    DEBUG("Custom");
                    break;
                case CoerceResult::Equality:
                    // NOTE: looking for strict inequality (fuzzy is allowed)
                    DEBUG("Equality requested, checking " << tL << " == " << tR);
                    if (tL->compareWithPlaceholders(sp, tR, context.ivars.callbackResolveInfer()) == HIRCompare::Unequal) {
                        DEBUG("- Bound failed");
                        return true;
                    }
                    break;
            }
        }

        if (ivarIdx < context.ivarsSized.size() && context.ivarsSized[ivarIdx]) {
            if (context.resolve.typeIsSized(sp, newTy) == HIRCompare::Unequal) {
                DEBUG("Unsized type not valid here");
                return true;
            }
        }

        for (const auto& pty : context.possibleIvarVals.at(tyL->as_Infer().index).typesCoerceTo) {
            HIRExprNodeP stubNode; // Empty node to
            CoerceResult res = CoerceResult::Unknown;
            switch (pty.op) {
                case Context::IVarPossible::CoerceTy::Coercion:
                    res = checkCoerceTys(context, sp, pty.ty, newTy, nullptr, &stubNode);
                    break;
                case Context::IVarPossible::CoerceTy::Unsizing:
                    res = checkUnsizeTys(context, sp, pty.ty, newTy, nullptr, &stubNode);
                    break;
            }
            switch (res) {
                case CoerceResult::Unsize:
                case CoerceResult::Unknown:
                case CoerceResult::Custom:
                    break;
                case CoerceResult::Fail:
                    return true;
                case CoerceResult::Equality:
                    // NOTE: looking for strict inequality (fuzzy is allowed)
                    DEBUG("Check " << pty.ty << " == " << newTy);
                    if (pty.ty->compareWithPlaceholders(sp, newTy, context.ivars.callbackResolveInfer()) == HIRCompare::Unequal) {
                        DEBUG("- Bound failed");
                        return true;
                    }
                    break;
            }
        }

        for (const auto* bound : boundRefs.associated) {
            usedTy = false;
            auto t = cloneTyWith(context.crate.types, sp, bound->implTy, cb);
            auto p = clonePathParamsWith(context.crate.types, sp, bound->params, cb);
            if (!usedTy) {
                continue;
            }
            // - Run EAT on t and p
            t = context.resolve.expandAssociatedTypes(sp, mv$(t));
            // TODO: Run EAT on `p`?
            DEBUG("Check Assoc R" << bound->ruleIdx << " - " << bound->implTy << " : " << bound->trait << bound->params);
            DEBUG("-> " << t << " : " << bound->trait << p);

            // Search for any trait impl that could match this,
            bool boundFailed = true;
            bool boundExact = false;
            context.resolve.findTraitImpls(sp, bound->trait, p, t, [&](const auto impl, auto cmp) {
                // If this bound specifies an associated type, then check that that type could match
                if (bound->name != "") {
                    auto aty = impl.getType(context.crate.types, bound->name.c_str(), {});
                    // The associated type is not present, what does that mean?
                    if (aty == HIRTypeRef()) {
                        DEBUG("[check_ivar_poss__fails_bounds] No ATY for " << bound->name << " in impl");
                        // A possible match was found, so don't delete just yet
                        boundFailed = false;
                        // - Return false to keep searching
                        return false;
                    }
                    const auto atyComparison = aty->compareWithPlaceholders(sp, bound->leftTy, context.ivars.callbackResolveInfer());
                    if (atyComparison == HIRCompare::Unequal) {
                        DEBUG("[check_ivar_poss__fails_bounds] ATY " << context.ivars.fmtType(aty) << " != left " << context.ivars.fmtType(bound->leftTy));
                        return false;
                    }
                }
                boundFailed = false;
                // Rank by the trait's input types. An unresolved associated
                // output is a later inference result and must not make an
                // otherwise exact input match tie with a fuzzy one.
                if (cmp == HIRCompare::Equal) {
                    boundExact = true;
                    return true;
                }
                return false;
            });
            if (boundFailed && !t->is_Infer()) {
                // If none was found, remove from the possibility list
                DEBUG("Remove possibility " << newTy << " because it failed a bound");
                return true;
            }
            exactBounds += boundExact;

            // TODO: Check for the resultant associated type
            DEBUG("Acceptable (Assoc R" << bound->ruleIdx << ")");
        }

        // Handle methods
        for (const auto* nodePtr : boundRefs.methods) {
            const auto& node = *nodePtr;
            const auto& tyTpl = context.getType(node.value->resType);

            bool usedTy = false;
            auto t = cloneTyWith(context.crate.types, sp, tyTpl, [&](const auto& ty, auto& outTy) {
                if (const auto* e = ty->opt_Infer(); e && e->index == ivarIdx) {
                    outTy = newTy;
                    usedTy = true;
                    return true;
                } else {
                    return false;
                }
            });
            if (!usedTy) {
                continue;
            }

            DEBUG("Check <" << t << ">::" << node.method);
            ::std::vector<::std::pair<TraitResolution::AutoderefBorrow, HIRPath>> possibleMethods;
            unsigned int derefCount = context.resolve.autoderefFindMethod(node.span(), node.traits, node.traitParamIvars, node.traitParamTypeIvars, t, node.method, possibleMethods);
            DEBUG("> deref_count = " << derefCount << ", possible_methods={" << possibleMethods << "}");
            // TODO: Detect the above hitting an ivar, and use that instead of this hacky check of if it's `_` or `&_`
            if (!(t->is_Infer() || ((*t).is_Borrow() && ((*t).as_Borrow().inner->is_Infer()))) && possibleMethods.empty()) {
                // No method found, which would be an error
                DEBUG("Remove possibility " << newTy << " because it didn't have a method");
                return true;
            }
        }

        DEBUG("- Bound passed");
        if (exactBoundCount) {
            *exactBoundCount = exactBounds;
        }
        return false;
    }

    enum class IvarPossFallbackType {
        // No fallback, only make safe/definitive decisions
        None,
        // Can propagate backwards
        Backwards,
        // Picks an option, even if there's ivars present?
        Assume,
        // Ignores the weaker disable flags (`force_no_to` and `force_no_from`)
        IgnoreWeakDisable,
        // First bound, if nothing else works
        PickFirstBound,
        // Just picks an option (even if it might be wrong)
        FinalOption,
    };

    ::std::ostream& operator<<(::std::ostream& os, IvarPossFallbackType t) {
        switch (t) {
            case IvarPossFallbackType::None:
                os << "";
                break;
            case IvarPossFallbackType::Backwards:
                os << " backwards";
                break;
            case IvarPossFallbackType::Assume:
                os << " weak";
                break;
            case IvarPossFallbackType::IgnoreWeakDisable:
                os << " unblock";
                break;
            case IvarPossFallbackType::PickFirstBound:
                os << " pick-bound";
                break;
            case IvarPossFallbackType::FinalOption:
                os << " final";
                break;
        }
        return os;
    }

    struct PossibleType {
        enum {
            Equal,
            CoerceTo,
            CoerceFrom,
            UnsizeTo,
            UnsizeFrom,
        } cls;

        enum class State {
            Concrete,
            Barrier,
            Removed,
        } state;

        const HIRTypeData* ty;

        static PossibleType concrete(decltype(cls) cls, const HIRTypeData* ty) {
            return PossibleType{cls, State::Concrete, ty};
        }

        static PossibleType barrier(decltype(cls) cls) {
            return PossibleType{cls, State::Barrier, nullptr};
        }

        bool isActive() const {
            return state != State::Removed;
        }

        bool hasType() const {
            return state == State::Concrete;
        }

        void remove() {
            state = State::Removed;
            ty = nullptr;
        }

        Ordering ord(const PossibleType& o) const {
            if (state != o.state) {
                return ::ord(static_cast<int>(state), static_cast<int>(o.state));
            }
            if (hasType() && ty != o.ty) {
                return ::ord(ty, o.ty);
            }
            if (cls != o.cls) {
                return ::ord(static_cast<int>(cls), static_cast<int>(o.cls));
            }
            return OrdEqual;
        }

        bool operator<(const PossibleType& o) const {
            return ord(o) == OrdLess;
        }

        bool operator==(const PossibleType& o) const {
            return ord(o) == OrdEqual;
        }

        ::std::ostream& fmt(::std::ostream& os) const {
            switch (cls) {
                case Equal:
                    os << "==";
                    break;
                case CoerceTo:
                    os << "C-";
                    break;
                case CoerceFrom:
                    os << "CD";
                    break;
                case UnsizeTo:
                    os << "--";
                    break;
                case UnsizeFrom:
                    os << "-D";
                    break;
            }
            os << " ";
            if (hasType()) {
                os << ty;
            } else if (state == State::Barrier) {
                os << "<barrier>";
            } else {
                os << "<removed>";
            }
            return os;
        }

        bool isSource() const {
            return cls == CoerceFrom || cls == UnsizeFrom;
        }

        bool isDest() const {
            return cls == CoerceTo || cls == UnsizeTo;
        }

        static bool isSourceS(const PossibleType& self) {
            return self.isSource();
        }

        static bool isDestS(const PossibleType& self) {
            return self.isDest();
        }

        bool isCoerce() const {
            return cls == CoerceTo || cls == CoerceFrom;
        }

        bool isUnsize() const {
            return cls == UnsizeTo || cls == UnsizeFrom;
        }

        static bool isCoerceS(const PossibleType& self) {
            return self.isCoerce();
        }

        static bool isUnsizeS(const PossibleType& self) {
            return self.isUnsize();
        }
    };

    struct TypeRestrictiveOrdering {
        /// Get the inner type of a pointer (if it matches a template)
        static const HIRTypeData* matchAndExtractPtrTy(const HIRTypeData* ptrTpl, const HIRTypeData* ty) {
            if (ty->tag() != ptrTpl->tag()) {
                return nullptr;
            }
            switch ((*ty).tag()) {
                case HIRTypeData::TAG_Borrow: {
                    auto& te = (*ty).as_Borrow();
                    if (te.type == ptrTpl->as_Borrow().type) {
                        return te.inner;
                    }
                    break;
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& te = (*ty).as_Pointer();
                    if (te.type == ptrTpl->as_Pointer().type) {
                        return te.inner;
                    }
                    break;
                }
                case HIRTypeData::TAG_Path: {
                    auto& te = (*ty).as_Path();
                    if (te.binding == ptrTpl->as_Path().binding) {
                        // TODO: Get inner
                    }
                    break;
                }
break;
                default:
                    break;
            }
            return nullptr;
        }

        /// Helper for `get_ordering_ty` - ordering of the type vs an infer type
        static Ordering getOrderingInfer(const Span& sp, const HIRTypeData* r) {
            // For infer, only concrete types are more restrictive
            switch ((*r).tag()) {
default:
                return OrdLess;
                case HIRTypeData::TAG_Path: {
                    auto& te = (*r).as_Path();
                    if (te.binding.is_Opaque()) {
                        return OrdLess;
                    }
                    if (te.binding.is_Unbound()) {
                        return OrdEqual;
                    }
                    // TODO: Check if the type is concrete? (Check an unsizing param if present)
                    return OrdLess;
                }
                case HIRTypeData::TAG_Borrow: {
                    auto& _ = (*r).as_Borrow();
                    return OrdEqual;
                }
                case HIRTypeData::TAG_Infer: {
                    auto& _ = (*r).as_Infer();
                    return OrdEqual;
                }
                case HIRTypeData::TAG_Pointer: {
                    auto& _ = (*r).as_Pointer();
                    return OrdEqual;
                }
            }
            throw "";
        }

        /// Ordering of `l` relative to `r` for ?unsizing
        /// - OrdLess means that the LHS is less restrictive
        static Ordering getOrderingTy(const Span& sp, const Context& context, const HIRTypeData* l, const HIRTypeData* r, bool& outUnordered) {
            if (l == r) {
                return OrdEqual;
            }
            if (l->is_Infer()) {
                return getOrderingInfer(sp, r);
            }
            if (r->is_Infer()) {
                switch (getOrderingInfer(sp, l)) {
                    case OrdLess:
                        return OrdGreater;
                    case OrdEqual:
                        return OrdEqual;
                    case OrdGreater:
                        return OrdLess;
                }
            }
            if (l->is_Path()) {
                const auto& teL = l->as_Path();
                // Path types can be unsize targets, and can also act like infers
                // - If it's a Unbound treat as Infer
                // - If Opaque, then search for a CoerceUnsized/Unsize bound?
                // - If Struct, look for ^ tag
                // - Else, more/equal specific
                switch ((*r).tag()) {
default:
                    // An ivar is less restrictive?
                    if( teL.binding.is_Unbound() )
                        return OrdLess;
                    outUnordered = true;
                    return OrdEqual;
                    //TODO(sp, l << " with " << r << " - LHS is Path, RHS is " << r->tag_str());
                    case HIRTypeData::TAG_Slice: {
                        // Paths can deref to a slice (well, to any type) - so `slice < path` in restrictiveness
                        return OrdGreater;
                    }
                    case HIRTypeData::TAG_Path: {
                        auto& teR = (*r).as_Path();
                        // If both are unbound, assume equal (effectively an ivar)
                        if (teL.binding.is_Unbound() && teR.binding.is_Unbound()) {
                            return OrdEqual;
                        }
                        if (teL.binding.is_Unbound()) {
                            return OrdLess;
                        }
                        if (teR.binding.is_Unbound()) {
                            return OrdGreater;
                        } else if (teR.binding.is_Opaque()) {
                            TODO(sp, l << " with " << r << " - LHS is Path, RHS is opaque type");
                        } else if ((teR.binding.is_Struct() && (teR.binding.as_Struct()->structMarkings.canUnsize))) {
                            TODO(sp, l << " with " << r << " - LHS is Path, RHS is unsize-capable struct");
                        } else {
                            return OrdEqual;
                        }
                        break;
                    }
                }
            }
            if (r->is_Path()) {
                // Path types can be unsize targets, and can also act like infers
                switch (getOrderingTy(sp, context, r, l, outUnordered)) {
                    case OrdLess:
                        return OrdGreater;
                    case OrdEqual:
                        return OrdEqual;
                    case OrdGreater:
                        return OrdLess;
                }
            }

            // Slice < Array
            if (l->tag() == r->tag()) {
                return OrdEqual;
            } else {
                if (l->is_Slice() && r->is_Array()) {
                    return OrdGreater;
                }
                if (l->is_Array() && r->is_Slice()) {
                    return OrdLess;
                }

                if (l->is_Borrow() && !r->is_Borrow()) {
                    return OrdGreater;
                }
                if (r->is_Borrow() && !l->is_Borrow()) {
                    return OrdLess;
                }

                outUnordered = true;
                return OrdEqual;
                //TODO(sp, "Compare " << l << " and " << r);
            }
        }

        /// Returns the restrictiveness ordering of `l` relative to `r`
        /// - &T is more restrictive than *const T
        /// - &mut T is more restrictive than &T
        /// Restrictive means that left can't be coerced from right
        static Ordering getOrderingPtr(const Span& sp, const Context& context, const HIRTypeData* l, const HIRTypeData* r, bool& outUnordered, bool deep = true) {
            Ordering cmp;
            TRACE_FUNCTION_FR(l << " , " << r, cmp);
            // Get ordering of this type to the current destination
            // - If lesser/greater then ignore/update
            // - If equal then what? (Instant error? Leave as-is and let the asignment happen? Disable the asignment?)
            static const HIRTypeData::Tag tagOrdering[] = {
                HIRTypeData::TAG_Pointer,
                HIRTypeData::TAG_Borrow,
                HIRTypeData::TAG_Path, // Strictly speaking, Path == Generic
                HIRTypeData::TAG_Generic,
                // An erased type is one concrete type, just not a named one
                HIRTypeData::TAG_ErasedType,
                HIRTypeData::TAG_Function,
                // These two are kinda their own pair
                HIRTypeData::TAG_NamedFunction,
                HIRTypeData::TAG_NodeType,
            };
            static const HIRTypeData::Tag* tagOrderingEnd = &tagOrdering[sizeof(tagOrdering) / sizeof(tagOrdering[0])];
            if (l->tag() != r->tag()) {
                auto pL = ::std::find(tagOrdering, tagOrderingEnd, l->tag());
                auto pR = ::std::find(tagOrdering, tagOrderingEnd, r->tag());
                if (pL == tagOrderingEnd) {
                    TODO(sp, "Type " << l << " not in ordering list");
                }
                if (pR == tagOrderingEnd) {
                    TODO(sp, "Type " << r << " not in ordering list");
                }
                cmp = ord(static_cast<int>(pL - pR), 0);
            } else {
                if (l == r) {
                    return OrdEqual;
                }
                switch ((*l).tag()) {
default:
                    BUG(sp, "Unexpected type class " << l << " in get_ordering_ty (" << r << ")");
                    break;
                    case HIRTypeData::TAG_Generic: {
                        cmp = OrdEqual;
                        break;
                    }
                    case HIRTypeData::TAG_NamedFunction: {
                        cmp = OrdEqual;
                        break;
                    }
                    case HIRTypeData::TAG_Path: {
                        // TODO: Prevent this rule from applying?
                        return OrdEqual;
                    }
                    case HIRTypeData::TAG_NodeType: {
                        // Does this need to care about the different types?
                        outUnordered = true;
                        return OrdEqual;
                    }
                    case HIRTypeData::TAG_ErasedType: {
                        // Two erased types say nothing about each other: neither
                        // is the more restrictive coercion target.
                        outUnordered = true;
                        return OrdEqual;
                    }
                    case HIRTypeData::TAG_Borrow: {
                        auto& teL = (*l).as_Borrow();
                        const auto& teR = r->as_Borrow();
                        cmp = ord((int)teL.type, (int)teR.type); // Unique>Shared in the listing, and Unique is more restrictive than Shared
                        if (cmp == OrdEqual && deep) {
                            cmp = getOrderingTy(sp, context, context.ivars.getType(teL.inner), context.ivars.getType(teR.inner), outUnordered);
                        }
                        break;
                    }
                    case HIRTypeData::TAG_Pointer: {
                        auto& teL = (*l).as_Pointer();
                        const auto& teR = r->as_Pointer();
                        cmp = ord((int)teR.type, (int)teL.type); // Note, reversed ordering because we want Unique>Shared
                        if (cmp == OrdEqual && deep) {
                            cmp = getOrderingTy(sp, context, context.ivars.getType(teL.inner), context.ivars.getType(teR.inner), outUnordered);
                        }
                        break;
                    }
                }
            }
            return cmp;
        }
    };

    /// Ordering of types based on the amount of type information they provide
    /// - E.g. `(_, i32)` will sort higher than `(_,_)`
    /// If types don't match (e.g. `i32` with `(_,_)`) then `Incompatible` is returned
    struct InfoOrdering {
        enum eInfoOrdering {
            Incompatible, // The types are incompatible
            Less,         // The LHS type provides less information (e.g. has more ivars)
            Same,         // Same number of ivars
            More,         // The RHS provides more information (less ivars)
        };

        static bool isInfer(const HIRTypeData* ty) {
            if (ty->is_Infer()) {
                return true;
            }
            if (((*ty).is_Path() && ((*ty).as_Path().binding.is_Unbound()))) {
                return true;
            }
            return false;
        }

        static bool compareScore(int& score, const HIRTypeData* tyL, const HIRTypeData* tyR) {
            auto rv = compare(tyL, tyR);
            switch (rv) {
                case Incompatible:
                    return Incompatible;
                case Less:
                    score--;
                    break;
                case Same:
                    break;
                case More:
                    score++;
                    break;
            }
            return rv;
        }

        static eInfoOrdering compare(const HIRTypeData* tyL, const HIRTypeData* tyR) {
            if (isInfer(tyL)) {
                if (isInfer(tyR)) {
                    return Same;
                }
                return Less;
            } else {
                if (isInfer(tyR)) {
                    return More;
                }
            }
            if (tyL->tag() != tyR->tag()) {
                return Incompatible;
            }
            switch ((*tyL).tag()) {
default:
                return Incompatible;
                case HIRTypeData::TAG_NodeType: {
                    auto& le = (*tyL).as_NodeType();
                    auto& re = (*tyR).as_NodeType();
                    if (le != re) {
                        return Incompatible;
                    }
                    return Same;
                }
                case HIRTypeData::TAG_Tuple: {
                    auto& le = (*tyL).as_Tuple();
                    auto& re = (*tyR).as_Tuple();
                    if (le.size() != re.size()) {
                        return Incompatible;
                    }
                    int score = 0;
                    for (size_t i = 0; i < le.size(); i++) {
                        if (compareScore(score, le[i], re[i]) == Incompatible) {
                            return Incompatible;
                        }
                    }
                    return (score == 0 ? Same : (score < 0 ? Less : More));
                }
            }
            throw "unreachable";
        }

        static eInfoOrdering compareTop(const Context& context, const HIRTypeData* tyL, const HIRTypeData* tyR, bool shouldDeref) {
            if (context.ivars.typesEqual(tyL, tyR)) {
                return Same;
            }
            if (isInfer(tyL)) {
                return Incompatible;
            }
            if (isInfer(tyR)) {
                return Incompatible;
            }
            if (tyL->tag() != tyR->tag()) {
                return Incompatible;
            }
            if (shouldDeref) {
                if (const auto* le = tyL->opt_Borrow()) {
                    const auto& re = tyR->as_Borrow();
                    if (le->type != re.type) {
                        return Incompatible;
                    }
                    return compareTop(context, context.ivars.getType(le->inner), context.ivars.getType(re.inner), false);
                } else if (const auto* le = tyL->opt_Pointer()) {
                    const auto& re = tyR->as_Pointer();
                    if (le->type != re.type) {
                        return Incompatible;
                    }
                    return compareTop(context, context.ivars.getType(le->inner), context.ivars.getType(re.inner), false);
                } else if (((*tyL).is_Path() && (*tyL).as_Path().binding.is_Struct() && (*tyL).as_Path().binding.as_Struct()->structMarkings.coerceUnsized != HIRStructMarkings::Coerce::None)) {
                    const auto& le = tyL->as_Path();
                    const auto& re = tyR->as_Path();
                    if (le.binding != re.binding) {
                        return Incompatible;
                    }
                    auto paramIdx = le.binding.as_Struct()->structMarkings.coerceParam;
                    assert(paramIdx != ~0u);
                    return compareTop(context, context.ivars.getType(le.path.data.as_Generic().params.types.at(paramIdx)), context.ivars.getType(re.path.data.as_Generic().params.types.at(paramIdx)), false);
                } else if (const auto* le = tyL->opt_NodeType()) {
                    const auto& re = tyR->as_NodeType();
                    return *le != re ? Incompatible : Same;
                } else {
                    BUG(Span(), "Can't deref " << tyL << " / " << tyR);
                }
            }
            if (tyL->is_Slice()) {
                const auto& le = tyL->as_Slice();
                const auto& re = tyR->as_Slice();

                switch (compare(context.ivars.getType(le.inner), context.ivars.getType(re.inner))) {
                    case Less:
                        return Less;
                    case More:
                        return More;
                    case Same:
                    case Incompatible:
                        return Same;
                }
                throw "";
            }
            return Incompatible;
        }
    };

    // TODO: Split the below into a common portion, and a "run" portion (which uses the fallback)

    /// Check IVar possibilities, from both coercion/unsizing (which have well-encoded rules) and from trait impls
    bool checkIvarPoss(Context& context, const IvarBoundIndex& boundIndex, unsigned int i, Context::IVarPossible& ivarEnt, IvarPossFallbackType fallbackTy = IvarPossFallbackType::None) {
        static Span _span;
        const auto& sp = _span;
        const bool honourDisable = (fallbackTy != IvarPossFallbackType::IgnoreWeakDisable);

        const auto& tyL = context.ivars.getType(i);
        const auto& boundRefs = boundIndex[i];

        if (!((*tyL).is_Infer() && ((*tyL).as_Infer().index == i))) {
            if (ivarEnt.hasRules()) {
                DEBUG("- IVar " << i << " had possibilities, but was known to be " << tyL);
                // Completely clear by reinitialising
                ivarEnt = Context::IVarPossible();
            } else {
            }
            return false;
        }

        if (!ivarEnt.hasRules()) {
            // No rules, don't do anything (and don't print)
            DEBUG(i << ": No rules");
            return false;
        }

        TRACE_FUNCTION_F(i << fallbackTy);

        /// (semi) Formal rules
        ///
        /// - Always rules:
        ///   - If the same type is in both the from/to lists, use that
        /// - Skip if:
        ///   - `bounds_include_self`: Means that the bound set is incomplete (this can be disabled)
        ///   - `bounds_populated && bounds.empty()`: Bound set is incomplete
        ///
        /// - Look for a "bottom" type in the sources
        ///   - E.g. If a trait object or slice is seen as a souce, pick that (they can't coerce to anything)
        ///   - Note: Can't look for a "top" type in the destinations, as deref coercions exist
        ///
        /// - If bounds are present:
        ///   - Look for a unique entry in the bounds also in the source/destination lists
        /// - If there are no destination disables
        ///   - Look for a destination that all other destinations can coerce from
        /// - If there are no source disables
        ///   - Look for a source that all other soures can coerce to
        ///
        /// TODO: If in fallback mode, there's no infer options, and there are bounds - Pick a random bound

        // ---
        // Always rules:
        // ---
        {
            // - Search for a type that is both a source and a destination
            for (const auto& t : ivarEnt.typesCoerceTo) {
                for (const auto& t2 : ivarEnt.typesCoerceFrom) {
                    // TODO: Compare such that &[_; 1] == &[u8; 1]? and `&[_]` == `&[T]`
                    if (t.ty == t2.ty && t.ty != tyL) {
                        DEBUG("- Source/Destination type");
                        context.equateTypes(sp, tyL, t.ty);
                        return true;
                    }
                }
            }
        }
        // ---
        // Skip Conditions
        // ---
        if (ivarEnt.hasBounded && (!ivarEnt.boundsIncludeSelf && ivarEnt.bounded.empty())) {
            DEBUG(i << ": Bounded, but bound set empty");
            return false;
        }
        if (ivarEnt.forceDisable && fallbackTy != IvarPossFallbackType::FinalOption) {
            DEBUG(i << ": forced unknown");
            return false;
        }

        // Don't attempt to guess literals
        // - TODO: What about if there's a bound?
        if (tyL->as_Infer().isLit()) {
            DEBUG(i << ": Literal " << tyL);
            return false;
        }

        //if( ivar_ent.force_no_to || ivar_ent.force_no_from )
        //{
        //    switch(fallback_ty)
        //    {
        //    case IvarPossFallbackType::IgnoreWeakDisable:
        //    case IvarPossFallbackType::FinalOption:
        //    default:
        //    }
        //}

        bool hasNoCoercePosiblities;

        // Fill a single list with all possibilities, and pick the most suitable type.
        // - This list needs to include flags to say if the type can be dereferenced.
        {
            bool allowUnsized = !(i < context.ivarsSized.size() ? context.ivarsSized.at(i) : false);

            ::std::vector<PossibleType> possibleTys;
            bool addPlaceholders = (fallbackTy < IvarPossFallbackType::IgnoreWeakDisable);
            if (addPlaceholders && ivarEnt.forceNoFrom) {
                possibleTys.push_back(PossibleType::barrier(PossibleType::UnsizeFrom));
            }
            for (const auto& newTy : ivarEnt.typesCoerceFrom) {
                possibleTys.push_back(PossibleType::concrete(newTy.op == Context::IVarPossible::CoerceTy::Coercion ? PossibleType::CoerceFrom : PossibleType::UnsizeFrom, newTy.ty));
            }
            if (addPlaceholders && ivarEnt.forceNoTo) {
                possibleTys.push_back(PossibleType::barrier(PossibleType::UnsizeTo));
            }
            for (const auto& newTy : ivarEnt.typesCoerceTo) {
                possibleTys.push_back(PossibleType::concrete(newTy.op == Context::IVarPossible::CoerceTy::Coercion ? PossibleType::CoerceTo : PossibleType::UnsizeTo, newTy.ty));
            }
            DEBUG(i << ": possible_tys = " << possibleTys);
            DEBUG(i << ": bounds = " << (ivarEnt.hasBounded ? "" : "? ") << (ivarEnt.boundsIncludeSelf ? "+, " : "") << ivarEnt.bounded);

            // De-duplicate
            // TODO: This [ideally] shouldn't happen?
            {
                for (size_t i = 0; i < possibleTys.size(); i++) {
                    if (possibleTys[i].isActive()) {
                        auto it = std::find(possibleTys.begin() + i + 1, possibleTys.end(), possibleTys[i]);
                        if (it != possibleTys.end()) {
                            it->remove();
                        }
                    }
                }
                auto newEnd = std::remove_if(possibleTys.begin(), possibleTys.end(), [](const PossibleType& x) {
                    return !x.isActive();
                });
                DEBUG(i << ": " << (possibleTys.end() - newEnd) << " duplicates");
                possibleTys.resize(newEnd - possibleTys.begin());
            }

            // If the bound set is populated, and is fully restrictive
            if (ivarEnt.hasBounded && !ivarEnt.boundsIncludeSelf) {
                // Look for a bound that matches all other restrictions
                const HIRTypeData* bestTy = nullptr;
                const HIRTypeData* strongestTy = nullptr;
                unsigned int strongestExactBounds = 0;
                bool foundTwo = false;
                bool strongestTied = false;
                for (const auto& bTy : ivarEnt.bounded) {
                    // Check bound against bounds
                    unsigned int exactBounds = 0;
                    if (!checkIvarPossFailsBounds(sp, context, boundRefs, tyL, bTy, &exactBounds)) {
                        if (bestTy) {
                            DEBUG(bTy << " passed bounds (second)");
                            foundTwo = true;
                        } else {
                            DEBUG(bTy << " passed bounds (first)");
                            bestTy = bTy;
                        }
                        if (!strongestTy || exactBounds > strongestExactBounds) {
                            strongestTy = bTy;
                            strongestExactBounds = exactBounds;
                            strongestTied = false;
                        } else if (exactBounds == strongestExactBounds) {
                            strongestTied = true;
                        }
                    } else {
                        DEBUG(bTy << " failed bounds");
                    }
                }
                if (!bestTy) {
                    TODO(sp, "No none of the bounded types (" << ivarEnt.bounded << ") fit other bounds");
                } else if (!foundTwo) {
                    DEBUG("Only one bound fit other bounds");
                    context.equateTypes(sp, tyL, bestTy);
                    return true;
                } else if (strongestTy && !strongestTied) {
                    DEBUG("Only one bound has the strongest exact obligation match");
                    context.equateTypes(sp, tyL, strongestTy);
                    return true;
                } else {
                    if (fallbackTy == IvarPossFallbackType::PickFirstBound && possibleTys.empty()) {
                        DEBUG("Multiple equally fitting types in bounded and no other rules, picking first (bounded=[" << ivarEnt.bounded << "])");
                        context.equateTypes(sp, tyL, strongestTy ? strongestTy : bestTy);
                        return true;
                    }
                }
            }

            // Check if any of the bounded types match only one of the possible types
            {
                struct H {
                    static const HIRTypeData* getBorrowInner(const HIRTypeData* ty) {
                        if (ty->is_Borrow()) {
                            return getBorrowInner(ty->as_Borrow().inner);
                        } else {
                            return ty;
                        }
                    }
                };

                bool failed = false;
                const HIRTypeData* foundTy = nullptr;
                for (const auto& boundedTy : ivarEnt.bounded) {
                    // Skip ivars
                    if (H::getBorrowInner(boundedTy)->is_Infer()) {
                        continue;
                    }
                    for (const auto& t : possibleTys) {
                        if (!t.hasType()) {
                            continue;
                        }
                        // Skip ivars
                        if (H::getBorrowInner(t.ty)->is_Infer()) {
                            continue;
                        }

                        if (boundedTy->compareWithPlaceholders(sp, t.ty, context.ivars.callbackResolveInfer()) != HIRCompare::Unequal) {
                            if (!foundTy) {
                                foundTy = boundedTy;
                            } else if (foundTy == boundedTy) {
                                // Same type still, continue
                            } else if (boundedTy->compareWithPlaceholders(sp, foundTy, context.ivars.callbackResolveInfer()) == HIRCompare::Unequal) {
                                // Incompatible types
                                failed = true;
                            } else {
                                // Compatible, keep the first one?
                                // - Nope, could be ivars involved.
                                failed = true;
                            }
                        }
                    }
                }
                if (foundTy && !failed) {
                    DEBUG("- Bounded and possible type - " << foundTy);
                    // Replace ivars in this type with new ivars (TODO: only if it's a fuzzy match)
                    auto t = cloneTyWith(context.crate.types, sp, foundTy, [&](const HIRTypeData* t1, HIRTypeRef& out) -> bool {
                        if (t1->is_Infer()) {
                            const auto& t = context.getType(t1);
                            if (t->is_Infer()) {
                                out = context.ivars.newIvarTr();
                            } else {
                                out = t;
                            }
                            return true;
                        } else {
                            return false;
                        }
                    });
                    context.equateTypes(sp, tyL, t);
                    return true;
                }
            }

            // Either there are no bounds available, OR the bounds are not fully restrictive
            // - Add the bounded types to `possible_tys`
            for (const auto& newTy : ivarEnt.bounded) {
                possibleTys.push_back(PossibleType::concrete(PossibleType::Equal, newTy));
            }

            // TODO: Rewrite ALL of the below (extract the helpers to somewhere useful)
            // Need FULLY codified rules

            // If in fallback mode, pick the only source (if it's valid)
            if (fallbackTy != IvarPossFallbackType::None && ::std::count_if(possibleTys.begin(), possibleTys.end(), PossibleType::isSourceS) == 1 && !ivarEnt.forceNoFrom && !ivarEnt.hasBounded) {
                // Single source, pick it?
                const auto& ent = *::std::find_if(possibleTys.begin(), possibleTys.end(), PossibleType::isSourceS);
                // - Only if there's no ivars
                if (!context.ivars.typeContainsIvars(ent.ty) && !(ent.ty)->is_Diverge()) {
                    if (!checkIvarPossFailsBounds(sp, context, boundRefs, tyL, ent.ty)) {
                        DEBUG("Single concrete source, " << ent.ty);
                        context.equateTypes(sp, tyL, ent.ty);
                        return true;
                    }
                }
            }
            if (fallbackTy == IvarPossFallbackType::IgnoreWeakDisable && possibleTys.size() == 1) {
                auto ent = possibleTys[0];
                if (!checkIvarPossFailsBounds(sp, context, boundRefs, tyL, ent.ty)) {
                    DEBUG("Single option (and in final), " << ent.ty);
                    context.equateTypes(sp, tyL, ent.ty);
                    return true;
                }
            }

            // If there's only one source, and one destination, and no possibility of unknown options, then pick whichever has no ivars (or whichever is valid)
            if (::std::count_if(possibleTys.begin(), possibleTys.end(), PossibleType::isDestS) == 1 && ::std::count_if(possibleTys.begin(), possibleTys.end(), PossibleType::isSourceS) == 1 && !ivarEnt.forceNoFrom && !ivarEnt.forceNoTo && !ivarEnt.hasBounded) {
                const auto& entS = *::std::find_if(possibleTys.begin(), possibleTys.end(), PossibleType::isSourceS);
                const auto& entD = *::std::find_if(possibleTys.begin(), possibleTys.end(), PossibleType::isDestS);

                // Only if both options are coerce?
                // TODO: And this ivar isn't Sized bounded?
                if (entS.isCoerce() && entD.isCoerce()) {
                    bool srcNoivars = !context.ivars.typeContainsIvars(entS.ty);
                    bool dstNoivars = !context.ivars.typeContainsIvars(entD.ty);
                    bool srcValid = !checkIvarPossFailsBounds(sp, context, boundRefs, tyL, entS.ty);
                    bool dstValid = !checkIvarPossFailsBounds(sp, context, boundRefs, tyL, entD.ty);

                    if (srcValid) {
                        if (srcNoivars) {
                            DEBUG("Single each way, concrete source, " << entS.ty);
                            context.equateTypes(sp, tyL, entS.ty);
                            return true;
                        }
                    }
                    if (dstValid) {
                        if (dstNoivars) {
                            DEBUG("Single each way, concrete destination, " << entD.ty);
                            context.equateTypes(sp, tyL, entD.ty);
                            return true;
                        }
                    }

                    if (srcValid) {
                        DEBUG("Single each way, ivar source, " << entS.ty);
                        context.equateTypes(sp, tyL, entS.ty);
                        return true;
                    }
                    if (dstValid) {
                        DEBUG("Single each way, ivar destination, " << entD.ty);
                        context.equateTypes(sp, tyL, entD.ty);
                        return true;
                    }
                    // All of them failed bounds, what?
                }
            }

            // If there's no disable flags set, and there's only one source, pick it.
            // - Slight hack to speed up flow-down inference
            if (possibleTys.size() == 1 && possibleTys[0].isSource() && !ivarEnt.forceNoFrom) {
                const auto* tyP = possibleTys[0].ty;
                // Never-type fallback: `!` coerces to every type, so before
                // edition 2024 an ivar whose only source is a diverging
                // expression settles on `()`, not on `!`. From 2024 on `!` is
                // itself the fallback.
                if ((tyP)->is_Diverge() && context.crate.edition < ASTEdition::Rust2024) {
                    DEBUG("Only source is `!`, never-type fallback to `()`");
                    tyP = context.crate.types.unit();
                }
                if (possibleTys[0].isUnsize()) {
                    HIRTypeRef tmpTy;

                    do {
                        if (!checkIvarPossFailsBounds(sp, context, boundRefs, tyL, tyP)) {
                            DEBUG("Single possibility failed bounds, trying deref - " << tyP);
                            break;
                        }
                    } while ((tyP = context.resolve.autoderef(sp, tyP, tmpTy)));
                    if (!tyP) {
                        // All would fail, just set something sensible
                        tyP = possibleTys[0].ty;
                    }
                } else {
                    //}
                }
                DEBUG("One possibility (before ivar removal), setting to " << tyP);
                context.equateTypes(sp, tyL, tyP);
                return true;
            }

            // TODO: This shouldn't just return, instead the above null placeholders should be tested
            if (ivarEnt.forceNoTo || ivarEnt.forceNoFrom) {
                switch (fallbackTy) {
                    case IvarPossFallbackType::IgnoreWeakDisable:
                    case IvarPossFallbackType::PickFirstBound:
                    case IvarPossFallbackType::FinalOption:
                        break;
                    default:
                        DEBUG(i << ": coercion blocked");
                        return false;
                }
            }

            ASSERT_BUG(
                sp,
                ::std::all_of(
                    possibleTys.begin(),
                    possibleTys.end(),
                    [](const PossibleType& ty) {
                return ty.hasType();
            }
                ),
                "Coercion barrier escaped into concrete type selection"
            );

            // Filter out ivars
            // - TODO: Should this also remove &_ types? (maybe not, as they give information about borrow classes)
            size_t nIvars;
            size_t nSrcIvars;
            size_t nDstIvars;
            bool possiblyDiverge = false;
            {
                nSrcIvars = 0;
                nDstIvars = 0;
                auto newEnd = ::std::remove_if(possibleTys.begin(), possibleTys.end(), [&](const PossibleType& ent) {
                    // TODO: Should this remove Unbound associated types too?
                    if ((ent.ty)->is_Infer()) {
                        if (ent.isSource()) {
                            nSrcIvars += 1;
                        } else {
                            nDstIvars += 1;
                        }
                        return true;
                    } else if ((ent.ty)->is_Diverge()) {
                        possiblyDiverge = true;
                        return true;
                    } else {
                        return false;
                    }
                });
                nIvars = possibleTys.end() - newEnd;
                possibleTys.erase(newEnd, possibleTys.end());
            }
            DEBUG(nIvars << " ivars (" << nSrcIvars << " src, " << nDstIvars << " dst)");

            // Distinct function items and closures have distinct source types,
            // but can share a function-pointer coercion target. Select that
            // target before the generic fallback can choose one source item.
            if (possibleTys.size() >= 2
                && (fallbackTy == IvarPossFallbackType::FinalOption || nSrcIvars == 0)
                && std::all_of(possibleTys.begin(), possibleTys.end(), [](const auto& e) {
                return e.hasType() && (((*e.ty).is_NodeType() && ((*e.ty).as_NodeType().is_Closure())) || (e.ty)->is_NamedFunction());
            })) {
                ::std::optional<HIRTypeDataFunctionPointer> target;
                for (const auto& possible : possibleTys) {
                    HIRTypeDataFunctionPointer candidate;
                    if (const auto* function = possible.ty->opt_NamedFunction()) {
                        candidate = function->decay(context.crate.types, sp);
                    } else if (const auto* closure = ((*possible.ty).is_NodeType() ? ((*possible.ty).as_NodeType().opt_Closure()) : nullptr)) {
                        candidate = HIRTypeDataFunctionPointer{false, false, RcString::newInterned(ABI_RUST), (*closure)->returnType, {}};
                        for (const auto& argument : (*closure)->args) {
                            candidate.argTypes.push_back(argument.second);
                        }
                    } else {
                        BUG(sp, "");
                    }

                    if (target) {
                        target->isUnsafe |= candidate.isUnsafe;
                    } else {
                        target = std::move(candidate);
                    }
                }
                auto newTy = context.crate.types.function(std::move(*target));
                DEBUG("All options are closures/functions, adding a function pointer - " << newTy);
                context.equateTypes(sp, tyL, newTy);
                return true;
            }

            if (ivarEnt.hasBounded && ivarEnt.boundsIncludeSelf) {
                nIvars += 1;
            }

            // Rules:
            // - If bounds_include_self

            // === If there's no source ivars, find the least permissive source ===
            // - If this source can't be unsized (e.g. in `&_, &str`, `&str` is the least permissive, and can't be
            // coerced without a `*const _` in the list), then equate to that
            // 1. Find the most accepting pointer type (if there is at least one coercion source)
            // 2. Look for an option that uses that pointer type, and contains an unsized type (that isn't a trait
            //    object with markers)
            // 3. Assign to that known most-permissive option
            // TODO: Do the oposite for the destination types (least permissive pointer, pick any Sized type)
            if (nSrcIvars == 0 || fallbackTy == IvarPossFallbackType::Assume) {
                const HIRTypeData* ptrTy = nullptr;
                if (::std::any_of(possibleTys.begin(), possibleTys.end(), [&](const auto& ent) {
                    return ent.cls == PossibleType::CoerceFrom;
                })) {
                    for (const auto& ent : possibleTys) {
                        if (!ent.isSource()) {
                            continue;
                        }

                        bool unusedUnordered = false;
                        if (ptrTy == nullptr) {
                            ptrTy = ent.ty;
                        } else if (TypeRestrictiveOrdering::getOrderingPtr(sp, context, ent.ty, ptrTy, unusedUnordered, /*deep=*/false) == OrdLess) {
                            ptrTy = ent.ty;
                        } else {
                        }
                    }
                }

                for (const auto& ent : possibleTys) {
                    // Sources only
                    if (!ent.isSource()) {
                        continue;
                    }
                    // Must match `ptr_ty`'s outer pointer
                    const HIRTypeData* innerTy = (ptrTy ? TypeRestrictiveOrdering::matchAndExtractPtrTy(ptrTy, ent.ty) : ent.ty);
                    if (!innerTy) {
                        continue;
                    }

                    bool isMaxAccepting = false;
                    if ((innerTy)->is_Slice()) {
                        isMaxAccepting = true;
                    } else if (((*innerTy).is_Primitive() && ((*innerTy).as_Primitive() == HIRCoreType::Str))) {
                        isMaxAccepting = true;
                    } else {
                    }
                    if (isMaxAccepting) {
                        DEBUG("Most accepting pointer class, and most permissive inner type - " << ent.ty);
                        context.equateTypes(sp, tyL, ent.ty);
                        return true;
                    }
                }
            }

            // If there's multiple destination types (which means that this ivar has to be a coercion from one of them)
            // Look for the least permissive of the available destination types and assign to that
            // NOTE: This only works for coercions (not usizings), so is restricted to all options being pointers
            if (::std::all_of(possibleTys.begin(), possibleTys.end(), PossibleType::isCoerceS)) {
                // 1. Count distinct (and non-ivar) source types
                // - This also ignores &_ types
                size_t numDistinct = 0;
                for (const auto& ent : possibleTys) {
                    if (!ent.isDest()) {
                        continue;
                    }
                    // Ignore infer borrows
                    if (((*ent.ty).is_Borrow() && ((*ent.ty).as_Borrow().inner->is_Infer()))) {
                        continue;
                    }
                    bool isDuplicate = false;
                    for (const auto& ent2 : possibleTys) {
                        if (&ent2 == &ent) {
                            break;
                        }
                        if (!ent2.isSource()) {
                            continue;
                        }
                        if (ent.ty == ent2.ty) {
                            isDuplicate = true;
                            break;
                        }
                        // TODO: Compare such that &[_; 1] == &[u8; 1]?
                    }
                    if (!isDuplicate) {
                        numDistinct += 1;
                    }
                }
                DEBUG("- " << numDistinct << " distinct destinations");
                // 2. Find the most restrictive destination type
                // - Borrows are more restrictive than pointers
                // - Borrows of Sized types are more restrictive than any other
                // - Decreasing borrow type ordering: Owned, Unique, Shared
                bool isUnordered = false;
                const HIRTypeData* destType = nullptr;
                for (const auto& ent : possibleTys) {
                    if (ent.isDest()) {
                        continue;
                    }
                    // Ignore &_ types?
                    // - No, need to handle them below
                    if (!destType) {
                        destType = ent.ty;
                        continue;
                    }

                    auto cmp = TypeRestrictiveOrdering::getOrderingPtr(sp, context, ent.ty, destType, isUnordered);
                    switch (cmp) {
                        case OrdLess:
                            // This entry is less restrictive, so don't update `dest_type`
                            break;
                        case OrdEqual:
                            break;
                        case OrdGreater:
                            // This entry is more restrictive, so DO update `dest_type`
                            destType = ent.ty;
                            isUnordered = false;
                            break;
                    }
                }
                // TODO: Unsized types? Don't pick an unsized if coercions are present?
                // TODO: If in a fallback mode, then don't require >1 (just require dest_type)
                if ((numDistinct > 1 || fallbackTy == IvarPossFallbackType::Assume) && destType && !isUnordered) {
                    DEBUG("- Least-restrictive source " << destType);
                    context.equateTypes(sp, tyL, destType);
                    return true;
                }
            }

            // If there's multiple source types (which means that this ivar has to be a coercion from one of them)
            // Look for the least permissive of the available destination types and assign to that
            // NOTE: This only works for coercions (not usizings), so is restricted to all options being pointers
            if (
                ::std::all_of(possibleTys.begin(), possibleTys.end(), PossibleType::isCoerceS)
                //||  ::std::none_of(possible_tys.begin(), possible_tys.end(), PossibleType::is_coerce_s)
            ) {
                // 1. Count distinct (and non-ivar) source types
                // - This also ignores &_ types
                size_t numDistinct = 0;
                for (const auto& ent : possibleTys) {
                    if (!ent.isSource()) {
                        continue;
                    }
                    // Ignore infer borrows
                    if (((*ent.ty).is_Borrow() && ((*ent.ty).as_Borrow().inner->is_Infer()))) {
                        continue;
                    }
                    bool isDuplicate = false;
                    for (const auto& ent2 : possibleTys) {
                        if (&ent2 == &ent) {
                            break;
                        }
                        if (!ent2.isSource()) {
                            continue;
                        }
                        if (ent.ty == ent2.ty) {
                            isDuplicate = true;
                            break;
                        }
                        // TODO: Compare such that &[_; 1] == &[u8; 1]?
                    }
                    if (!isDuplicate) {
                        numDistinct += 1;
                    }
                }
                DEBUG("- " << numDistinct << " distinct sources");
                // 2. Find the most restrictive destination type
                // - Borrows are more restrictive than pointers
                // - Borrows of Sized types are more restrictive than any other
                // - Decreasing borrow type ordering: Owned, Unique, Shared
                bool isUnordered = false;
                const HIRTypeData* destType = nullptr;
                for (const auto& ent : possibleTys) {
                    if (ent.isSource()) {
                        continue;
                    }
                    // Ignore &_ types?
                    // - No, need to handle them below
                    if (!destType) {
                        destType = ent.ty;
                        continue;
                    }

                    auto cmp = TypeRestrictiveOrdering::getOrderingPtr(sp, context, ent.ty, destType, isUnordered);
                    switch (cmp) {
                        case OrdLess:
                            // This entry is less restrictive, so DO update `dest_type`
                            destType = ent.ty;
                            isUnordered = false;
                            break;
                        case OrdEqual:
                            break;
                        case OrdGreater:
                            // This entry is more restrictive, so don't update `dest_type`
                            break;
                    }
                }
                // TODO: Unsized types? Don't pick an unsized if coercions are present?
                // TODO: If in a fallback mode, then don't require >1 (just require dest_type)
                if ((numDistinct > 1 || fallbackTy == IvarPossFallbackType::Assume) && destType && !isUnordered) {
                    DEBUG("- Most-restrictive destination " << destType);
                    context.equateTypes(sp, tyL, destType);
                    return true;
                }
            }

            // TODO: Remove any types that are covered by another type
            // - E.g. &[T] and &[U] can be considered equal, because [T] can't unsize again
            // - Comparison function: Returns one of Incomparible,Less,Same,More - Representing the amount of type information present.
            {
                // De-duplicate destinations and sources separately
                for (auto it = possibleTys.begin(); it != possibleTys.end(); ++it) {
                    if (!it->isActive()) {
                        continue;
                    }
                    for (auto it2 = it + 1; it2 != possibleTys.end(); ++it2) {
                        if (!it2->isActive()) {
                            continue;
                        }
                        if (it->cls != it2->cls) {
                            continue;
                        }

                        switch (InfoOrdering::compareTop(context, it->ty, it2->ty, /*should_deref=*/it->isCoerce())) {
                            case InfoOrdering::Incompatible:
                                break;
                            case InfoOrdering::Less:
                                DEBUG("(less) Remove " << *it << ", keep " << *it2);
                                if (0) {
                                    case InfoOrdering::Same:
                                        DEBUG("(same) Remove " << *it << ", keep " << *it2);
                                }
                                it->ty = it2->ty;
                                // Classes are already the same
                                it2->remove();
                                break;
                            case InfoOrdering::More:
                                DEBUG("(more) Keep " << *it << ", remove " << *it2);
                                it2->remove();
                                break;
                        }
                    }
                }
                auto newEnd = ::std::remove_if(possibleTys.begin(), possibleTys.end(), [](const auto& e) {
                    return !e.isActive();
                });
                DEBUG("Removing " << (possibleTys.end() - newEnd) << " redundant possibilities");
                possibleTys.erase(newEnd, possibleTys.end());
            }

            // TODO: If in fallback mode, pick the most permissive option
            // - E.g. If the options are &mut T and *const T, use the *const T
            if (fallbackTy == IvarPossFallbackType::Assume) {
                // All are coercions (not unsizings)
                if (::std::all_of(possibleTys.begin(), possibleTys.end(), PossibleType::isCoerceS) && nIvars == 0) {
                    // Find the least restrictive destination, and most restrictive source
                    const HIRTypeData* destType = nullptr;
                    bool anyIvarPresent = false;
                    bool isUnordered = false;
                    for (const auto& ent : possibleTys) {
                        if (visitTyWith(ent.ty, [](const HIRTypeData* t) {
                            return t->is_Infer();
                        })) {
                            anyIvarPresent = true;
                        }
                        if (!destType) {
                            destType = ent.ty;
                            continue;
                        }

                        auto cmp = TypeRestrictiveOrdering::getOrderingPtr(sp, context, ent.ty, destType, isUnordered);
                        switch (cmp) {
                            case OrdLess:
                                // This entry is less restrictive, so DO update `dest_type`
                                destType = ent.ty;
                                isUnordered = false;
                                break;
                            case OrdEqual:
                                break;
                            case OrdGreater:
                                // This entry is more restrictive, so don't update `dest_type`
                                break;
                        }
                    }

                    if (destType && nIvars == 0 && anyIvarPresent == false && !((*destType).is_NodeType() && ((*destType).as_NodeType().is_Closure())) && !isUnordered) {
                        DEBUG("Suitable option " << destType << " from " << possibleTys);
                        context.equateTypes(sp, tyL, destType);
                        return true;
                    }
                }
            }

            DEBUG("possible_tys = " << possibleTys);
            DEBUG("- Bounded [" << ivarEnt.bounded << "]");
            DEBUG("possible_tys = " << possibleTys);
            // Filter out useless options and impossiblities
            for (auto it = possibleTys.begin(); it != possibleTys.end();) {
                bool removeOption = false;
                if (it->ty == tyL) {
                    removeOption = true;
                } else if (!allowUnsized && context.resolve.typeIsSized(sp, it->ty) == HIRCompare::Unequal) {
                    removeOption = true;
                } else {
                    // Keep
                }

                // TODO: Ivars have been removed, this sort of check should be moved elsewhere.
                if (!removeOption && tyL->as_Infer().tyClass == HIRInferClass::Integer) {
                    if (const auto* te = (it->ty)->opt_Primitive()) {
                    } else if (const auto* te = (it->ty)->opt_Path()) {
                        // If not Unbound, remove option
                    } else if (const auto* te = (it->ty)->opt_Infer()) {
                    } else {
                        removeOption = true;
                    }
                }

                it = (removeOption ? possibleTys.erase(it) : it + 1);
            }
            DEBUG("possible_tys = " << possibleTys);
            for (auto it = possibleTys.begin(); it != possibleTys.end();) {
                bool removeOption = false;
                for (const auto& otherOpt : possibleTys) {
                    if (&otherOpt == &*it) {
                        continue;
                    }
                    if (otherOpt.ty == it->ty) {
                        // Potential duplicate
                        // - If the flag set is the same, then it is a duplicate
                        if (otherOpt.cls == it->cls) {
                            removeOption = true;
                            break;
                        }
                        // If not an ivar, AND both are either unsize/pointer AND the deref flags are different
                        // TODO: Ivars have been removed?
                        if (!(it->ty)->is_Infer() && otherOpt.isCoerce() == it->isCoerce() && otherOpt.isSource() != it->isSource()) {
                            // TODO: Possible duplicate with a check above...
                            DEBUG("Source and destination possibility, picking " << it->ty);
                            context.equateTypes(sp, tyL, it->ty);
                            return true;
                        }
                        // - Otherwise, we want to keep the option which doesn't allow dereferencing (remove current
                        // if it's the deref option)
                        if (it->isSource() && otherOpt.isCoerce() == it->isCoerce()) {
                            removeOption = true;
                            break;
                        }
                    }
                }
                it = (removeOption ? possibleTys.erase(it) : it + 1);
            }
            DEBUG("possible_tys = " << possibleTys);

            // Remove any options that are filled by other options (e.g. `str` and a derferencable String)
            for (auto it = possibleTys.begin(); it != possibleTys.end();) {
                bool removeOption = false;
                if (it->isSource() && !(it->ty)->is_Infer()) {
                    DEBUG("> " << *it);
                    // Dereference once before starting the search
                    HIRTypeRef tmp, tmp2;
                    const auto* dty = it->ty;
                    auto srcBty = HIRBorrowType::Shared;
                    if (it->isCoerce()) {
                        if ((dty)->is_Borrow()) {
                            srcBty = (dty)->as_Borrow().type;
                        }
                        dty = context.resolve.autoderef(sp, dty, tmp);
                        // NOTE: Coercions can also do closure->fn, so deref isn't always possible
                    }
                    //while( dty && !remove_option && (dty = context.m_resolve.autoderef(sp, dty, tmp)) )
                    if (dty) {
                        for (const auto& otherOpt : possibleTys) {
                            if (&otherOpt == &*it) {
                                continue;
                            }
                            if ((otherOpt.ty)->is_Infer()) {
                                continue;
                            }
                            DEBUG(" > " << otherOpt);

                            const auto* oty = otherOpt.ty;
                            auto oBty = HIRBorrowType::Owned;
                            if (otherOpt.isCoerce()) {
                                if ((oty)->is_Borrow()) {
                                    oBty = (oty)->as_Borrow().type;
                                }
                                oty = context.resolve.autoderef(sp, oty, tmp2);
                            }
                            if (oBty > srcBty) // Smaller means less powerful. Converting & -> &mut is invalid
                            {
                                // Borrow types aren't compatible
                                DEBUG("BT " << oBty << " > " << srcBty);
                                break;
                            }
                            // TODO: Check if unsize is possible from `dty` to `oty`
                            if (oty) {
                                DEBUG(" > " << dty << " =? " << oty);
                                auto cmp = checkUnsizeTys(context, sp, oty, dty, nullptr);
                                DEBUG("check_unsize_tys(..) = " << cmp);
                                if (cmp == CoerceResult::Equality) {
                                    //TODO(sp, "Impossibility for " << oty << " := " << dty);
                                } else if (cmp == CoerceResult::Unknown) {
                                } else {
                                    removeOption = true;
                                    DEBUG("- Remove " << *it << ", can deref to " << otherOpt);
                                    break;
                                }
                            }
                        }
                    }
                }
                if (!removeOption && !(it->ty)->is_Infer() && checkIvarPossFailsBounds(sp, context, boundRefs, tyL, it->ty)) {
                    removeOption = true;
                    DEBUG("- Remove " << *it << " due to bounds");
                }
                it = (removeOption ? possibleTys.erase(it) : it + 1);
            }
            DEBUG("possible_tys = {" << possibleTys << "} (" << nSrcIvars << " src ivars, " << nDstIvars << " dst ivars, possibly_diverge=" << possiblyDiverge << ")");

            if (nSrcIvars == 0 && /*n_dst_ivars == 0 &&*/ possibleTys.empty() && possiblyDiverge && fallbackTy == IvarPossFallbackType::IgnoreWeakDisable) {
                // Never-type fallback: before edition 2024 a variable that only
                // ever saw diverging expressions becomes `()`, not `!`. Picking
                // `!` leaves its pending bounds unsatisfiable (`Default for !`).
                if (context.crate.edition < ASTEdition::Rust2024) {
                    auto unit = context.crate.types.unit();
                    if (!checkIvarPossFailsBounds(sp, context, boundRefs, tyL, unit)) {
                        DEBUG("Possibly `!` and no other options - never-type fallback to `()`");
                        context.equateTypes(sp, tyL, unit);
                        return true;
                    }
                }
                auto t = context.crate.types.diverge();
                if (!checkIvarPossFailsBounds(sp, context, boundRefs, tyL, t)) {
                    DEBUG("Possibly `!` and no other options - setting");
                    context.equateTypes(sp, tyL, context.crate.types.diverge());
                    return true;
                }
            }

            // Find a CD option that can deref to a `--` option
            for (const auto& e : possibleTys) {
                if (e.cls == PossibleType::CoerceFrom) {
                    HIRTypeRef tmp;
                    const auto* dty = context.resolve.autoderef(sp, e.ty, tmp);
                    if (dty && !(dty)->is_Infer()) {
                        for (const auto& e2 : possibleTys) {
                            if (e2.cls == PossibleType::UnsizeTo) {
                                if (context.ivars.typesEqual(dty, e2.ty)) {
                                    DEBUG("Coerce source can deref once to an unsize destination, picking source " << e.ty);
                                    context.equateTypes(sp, tyL, e.ty);
                                    return true;
                                }
                            }
                        }
                    }
                }
            }

            // If there's only one option (or one real option w/ ivars, if in fallback mode) - equate it
            if (possibleTys.size() == 1) {
                bool active = false;
                switch (fallbackTy) {
                    case IvarPossFallbackType::None:
                    case IvarPossFallbackType::Backwards:
                    case IvarPossFallbackType::IgnoreWeakDisable:
                        active = (nIvars == 0 && ivarEnt.bounded.size() == 0);
                        break;
                    case IvarPossFallbackType::Assume:
                    case IvarPossFallbackType::PickFirstBound:
                        active = (ivarEnt.bounded.size() == 0);
                        break;
                    case IvarPossFallbackType::FinalOption:
                        active = true;
                        break;
                }
                if (active) {
                    const auto* newTy = possibleTys[0].ty;
                    DEBUG("Only one option: " << newTy);
                    context.equateTypes(sp, tyL, newTy);
                    return true;
                }
            }
            // -- Single source/destination --
            // Try if in first level fallback, or the bounded list is empty
            if ((!honourDisable || !ivarEnt.hasBounded)) {
                // If there's only one non-deref in the list OR there's only one deref in the list
                if (nSrcIvars == 0 && ::std::count_if(possibleTys.begin(), possibleTys.end(), PossibleType::isSourceS) == 1) {
                    auto it = ::std::find_if(possibleTys.begin(), possibleTys.end(), PossibleType::isSourceS);
                    const auto* newTy = it->ty;
                    DEBUG("Picking " << newTy << " as the only source [" << possibleTys << "]");
                    context.equateTypes(sp, tyL, newTy);
                    return true;
                }
                if (fallbackTy != IvarPossFallbackType::None && nDstIvars == 0 && ::std::count_if(possibleTys.begin(), possibleTys.end(), PossibleType::isDestS) == 1) {
                    auto it = ::std::find_if(possibleTys.begin(), possibleTys.end(), PossibleType::isDestS);
                    const auto* newTy = it->ty;
                    if (it->isCoerce()) {
                        DEBUG("Picking " << newTy << " as the only target [" << possibleTys << "]");
                        context.equateTypes(sp, tyL, newTy);
                        return true;
                    } else {
                        // HACK: Work around failure in librustc
                        DEBUG("Would pick " << newTy << " as the only target, but it's an unsize");
                    }
                }
            }
            // If there's multiple possiblilties, we're in fallback mode, AND there's no ivars in the list
            // TODO: Exclude bounds? (not all of those are safe to include)
            if (ivarEnt.bounded.size() == 0) {
                if (possibleTys.size() > 0 && !honourDisable && nIvars == 0) {
                    //::std::sort(possible_tys.begin(), possible_tys.end());  // Sorts ivars to the front
                    const auto* newTy = possibleTys.back().ty;
                    DEBUG("Picking " << newTy << " as an arbitary an option from [" << possibleTys << "]");
                    context.equateTypes(sp, tyL, newTy);
                    return true;
                }
            }

            // If only one bound meets the possible set, use it
            if (!possibleTys.empty() && (!ivarEnt.boundsIncludeSelf || fallbackTy == IvarPossFallbackType::FinalOption)) {
                DEBUG("Checking bounded [" << ivarEnt.bounded << "]");
                ::std::vector<const HIRTypeData*> feasableBounds;
                for (const auto& newTy : ivarEnt.bounded) {
                    bool failedABound = false;
                    // TODO: Check if this bounded type _cannot_ work with any of the existing bounds
                    // - Don't add to the possiblity list if so
                    for (const auto& opt : possibleTys) {
                        if (opt.cls == PossibleType::Equal) {
                            continue;
                        }
                        // If a fuzzy compare succeeds, keep
                        switch (newTy->compareWithPlaceholders(sp, opt.ty, context.ivars.callbackResolveInfer())) {
                            case HIRCompare::Unequal:
                                // If not equal, then maybe an unsize could happen
                                break;
                            case HIRCompare::Fuzzy:
                            case HIRCompare::Equal:
                                continue;
                        }
                        CoerceResult cmp;
                        if (opt.isSource()) {
                            DEBUG("(checking bounded) > " << newTy << " =? " << opt.ty);
                            cmp = checkUnsizeTys(context, sp, newTy, opt.ty, nullptr);
                        } else {
                            // Destination type, this option must deref to it
                            DEBUG("(checking bounded) > " << opt.ty << " =? " << newTy);
                            cmp = checkUnsizeTys(context, sp, opt.ty, newTy, nullptr);
                        }
                        DEBUG("(checking bounded) cmp = " << cmp);
                        if (cmp == CoerceResult::Equality) {
                            failedABound = true;
                            break;
                        }
                    }
                    // TODO: Should this also check check_ivar_poss__fails_bounds
                    if (!failedABound) {
                        feasableBounds.push_back(newTy);
                    }
                }
                DEBUG("Checking bounded: " << feasableBounds.size() << " feasible bounds");
                if (feasableBounds.size() == 1) {
                    const auto* newTy = feasableBounds.front();
                    DEBUG("Picking " << newTy << " as it's the only bound that fits coercions");
                    context.equateTypes(sp, tyL, newTy);
                    return true;
                }
            } else {
                // Not checking bounded list, because there's nothing to check
            }

            hasNoCoercePosiblities = possibleTys.empty() && nIvars == 0;
        }

        if (hasNoCoercePosiblities && !ivarEnt.bounded.empty()) {
            // TODO: Search know possibilties and check if they satisfy the bounds for this ivar
            DEBUG("Options: " << ivarEnt.bounded);
            unsigned int nGoodInts = 0;
            ::std::vector<const HIRTypeData*> goodTypes;
            goodTypes.reserve(ivarEnt.bounded.size());
            for (const auto& newTy : ivarEnt.bounded) {
                DEBUG("- Test " << newTy << " against current rules");
                if (checkIvarPossFailsBounds(sp, context, boundRefs, tyL, newTy)) {
                } else {
                    goodTypes.push_back(newTy);

                    if (newTy->is_Primitive()) {
                        nGoodInts++;
                    }

                    DEBUG("> " << newTy << " feasible");
                }
            }
            DEBUG(goodTypes.size() << " valid options (" << nGoodInts << " primitives)");
            // Picks the first if in fallback mode (which is signalled by `honour_disable` being false)
            // - This handles the case where there's multiple valid options (needed for libcompiler_builtins)
            // TODO: Only pick any if all options are the same class (or just all are integers)
            if (goodTypes.empty()) {
            } else if (goodTypes.size() == 1) {
                // Since it's the only possibility, choose it?
                DEBUG("Only " << goodTypes.front() << " fits current bound sets");
                context.equateTypes(sp, tyL, goodTypes.front());
                return true;
            } else if (goodTypes.size() > 0 && fallbackTy == IvarPossFallbackType::FinalOption) {
                auto typIsBorrow = [&](const HIRTypeData* typ) {
                    return typ->is_Borrow();
                };
                // NOTE: We want to select from sets of primitives and generics (which can be interchangable)
                if (::std::all_of(goodTypes.begin(), goodTypes.end(), typIsBorrow) == ::std::any_of(goodTypes.begin(), goodTypes.end(), typIsBorrow)) {
                    DEBUG("Picking " << goodTypes.front() << " as first of " << goodTypes.size() << " options [" << FMT_CB(ss, for (auto e : goodTypes) ss << e << ",";) << "]");
                    context.equateTypes(sp, tyL, goodTypes.front());
                    return true;
                } else {
                    // Mix of borrows with non-borrows
                }
            }
        }

        return false;
    }
}

void TypecheckCodeCS(const TypeckModuleState& ms, tArgs& args, const HIRTypeData* resultType, HIRExprPtr& expr) {
    TRACE_FUNCTION;

    // HIR nodes live in the crate pool, so the temporary wrapper does not own
    // the root. Keep the root reachable through `expr` while its typechecking
    // context indexes bounds: those bounds can contain this same unevaluated
    // const expression and compare it recursively.
    HIRExprNodeP rootPtr(expr.get());
    assert(!ms.modPaths.empty());
    Context context{ms.wb, ms.implGenerics, ms.itemGenerics, ms.modPaths.back(), ms.currentTrait, ms.currentTraitImpl};
    for (const auto& path : expr.state->defineOpaque) {
        context.resolve.addDefiningOpaqueAlias(path);
    }

    // - Build up ruleset from node tree
    TypecheckCodeCSEnumerateRules(context, ms, args, resultType, expr, rootPtr);

    const unsigned int MAX_ITERATIONS = 5000;
    unsigned int count = 0;
    while (context.takeChanged() /*&& context.has_rules()*/ && count < MAX_ITERATIONS) {
        TRACE_FUNCTION_F("=== PASS " << count << " ===");
        context.dump();

        // 1. Check coercions for ones that cannot coerce due to RHS type (e.g. `str` which doesn't coerce to anything)
        // 2. (???) Locate coercions that cannot coerce (due to being the only way to know a type)
        // - Keep a list in the ivar of what types that ivar could be equated to.
        if (!context.ivars.peekChanged()) {
            DEBUG("--- Coercion checking");
            for (size_t i = 0; i < context.linkCoerce.size();) {
                auto ent = mv$(context.linkCoerce[i]);
                const auto& span = (*ent->rightNodePtr)->span();
                auto& srcTy = (*ent->rightNodePtr)->resType;
                srcTy = context.resolve.expandAssociatedTypes(span, mv$(srcTy)); // TODO: This was commented, why?
                ent->leftTy = context.resolve.expandAssociatedTypes(span, mv$(ent->leftTy));
                if (checkCoerce(context, *ent)) {
                    DEBUG("- Consumed coercion R" << ent->ruleIdx << " " << ent->leftTy << " := " << srcTy);

                    context.linkCoerce.erase(context.linkCoerce.begin() + i);
                } else {
                    context.linkCoerce[i] = mv$(ent);
                    ++i;
                }
            }
            // 3. Check associated type rules
            DEBUG("--- Associated types");
            unsigned int linkAssocIterLimit = context.linkAssoc.size() * 4;
            for (unsigned int i = 0; i < context.linkAssoc.size();) {
                // - Move out (and back in later) to avoid holding a bad pointer if the list is updated
                auto rule = mv$(context.linkAssoc[i]);

                DEBUG("- " << rule);
                if (associatedStillStalled(context, rule)) {
                    mergeAssociatedPossibilities(context, rule.stalledPossibilities);
                    context.linkAssoc[i] = mv$(rule);
                    i++;
                    if (linkAssocIterLimit-- == 0) {
                        DEBUG("link_assoc iteration limit exceeded");
                        break;
                    }
                    continue;
                }

                for (auto& ty : rule.params.types) {
                    ty = context.resolve.expandAssociatedTypes(rule.span, mv$(ty));
                }
                if (rule.name != "") {
                    rule.leftTy = context.resolve.expandAssociatedTypes(rule.span, mv$(rule.leftTy));
                    // HACK: If the left type is `!`, remove the type bound
                    //}
                }
                rule.implTy = context.resolve.expandAssociatedTypes(rule.span, mv$(rule.implTy));

                ::std::vector<Context::Associated::CapturedIvarPossible> capturedPossibilities;
                AssociatedCheckResult result;
                {
                    AssociatedPossibilityCapture capture(context, capturedPossibilities);
                    result = checkAssociated(context, rule);
                }
                mergeAssociatedPossibilities(context, capturedPossibilities);
                rule.isAmbiguous = result == AssociatedCheckResult::Ambiguous;

                if (result == AssociatedCheckResult::Complete) {
                    DEBUG("- Consumed associated type rule " << i << "/" << context.linkAssoc.size() << " - " << rule);
                    if (i != context.linkAssoc.size() - 1) {
                        context.linkAssoc[i] = mv$(context.linkAssoc.back());
                    }
                    context.linkAssoc.pop_back();
                } else {
                    if ((result == AssociatedCheckResult::Stalled || result == AssociatedCheckResult::Ambiguous) && setAssociatedStall(context, rule)) {
                        rule.stalledPossibilities = mv$(capturedPossibilities);
                    } else {
                        rule.stalledOn.clear();
                        rule.stalledPossibilities.clear();
                    }
                    context.linkAssoc[i] = mv$(rule);
                    i++;
                }

                if (linkAssocIterLimit-- == 0) {
                    DEBUG("link_assoc iteration limit exceeded");
                    break;
                }
            }
        }
        // 4. Revisit nodes that require revisiting
        if (!context.ivars.peekChanged()) {
            DEBUG("--- Node revisits");
            for (auto it = context.toVisit.begin(); it != context.toVisit.end();) {
                HIRExprNode& node = **it;
                ExprVisitorRevisit visitor{context};
                DEBUG("> " << &node << " " << typeid(node).name() << " -> " << context.ivars.fmtType(node.resType));
                node.visit(visitor);
                //  - If the node is completed, remove it
                if (visitor.nodeCompleted()) {
                    DEBUG("- Completed " << &node << " - " << typeid(node).name());
                    it = context.toVisit.erase(it);
                } else {
                    ++it;
                }
            }
            {
                ::std::vector<bool> advRevisitRemoveList;
                size_t len = context.advRevisits.size();
                for (size_t i = 0; i < len; i++) {
                    auto& ent = *context.advRevisits[i];
                    DEBUG("> " << FMT_CB(os, ent.fmt(os)));
                    advRevisitRemoveList.push_back(ent.revisit(context, /*is_fallback=*/false));
                }
                for (size_t i = len; i--;) {
                    if (advRevisitRemoveList[i]) {
                        context.advRevisits.erase(context.advRevisits.begin() + i);
                    }
                }
            }
        }

        ::std::unique_ptr<IvarBoundIndex> ivarBoundIndex;
        if (!context.ivars.peekChanged()) {
            ivarBoundIndex = ::std::make_unique<IvarBoundIndex>(context);
        }

        // If nothing changed this pass, apply ivar possibilities
        // - This essentially forces coercions not to happen.
        if (!context.ivars.peekChanged()) {
            // Check the possible equations
            DEBUG("--- IVar possibilities");
            // TODO: De-duplicate this with the block ~80 lines below
            ::std::unique_ptr<IvarDependencyIndex> dependencyIndex;
            for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                if (checkIvarPoss(context, *ivarBoundIndex, i, context.possibleIvarVals[i])) {
                    // Look at all other ivar possibility sets, and disable processing if they depend on this ivar (prevents races)
                    if (!dependencyIndex) {
                        dependencyIndex = ::std::make_unique<IvarDependencyIndex>(context);
                    }
                    dependencyIndex->disableDependents(i);
                } else {
                }
            }
        } // `if peek_changed` (ivar possibilities)

        // If nothing has changed,
        if (!context.ivars.peekChanged()) {
            // Check the possible equations
            DEBUG("--- IVar possibilities (fallback 0)");
            for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                if (checkIvarPoss(context, *ivarBoundIndex, i, context.possibleIvarVals[i], IvarPossFallbackType::Backwards)) {
                    break;
                }
            }
        }

        // If nothing has changed, run check_ivar_poss again but allow it to assume is has all the options
        if (!context.ivars.peekChanged()) {
            // Check the possible equations
            DEBUG("--- IVar possibilities (fallback 1)");
            for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                if (checkIvarPoss(context, *ivarBoundIndex, i, context.possibleIvarVals[i], IvarPossFallbackType::Assume)) {
                    break;
                }
            }
        }

        // If nothing has changed, run check_ivar_poss again but ignoring the 'disable' flag
        if (!context.ivars.peekChanged()) {
            // Check the possible equations
            DEBUG("--- IVar possibilities (fallback)");
            for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                if (checkIvarPoss(context, *ivarBoundIndex, i, context.possibleIvarVals[i], IvarPossFallbackType::IgnoreWeakDisable)) {
                    break;
                } else {
                }
            }
        } // `if peek_changed` (ivar possibilities #2)

        if (!context.ivars.peekChanged()) {
            DEBUG("--- Node revisits (fallback)");
            for (auto it = context.toVisit.begin(); it != context.toVisit.end();) {
                HIRExprNode& node = **it;
                ExprVisitorRevisit visitor{context, true};
                DEBUG("> " << &node << " " << typeid(node).name() << " -> " << context.ivars.fmtType(node.resType));
                node.visit(visitor);
                //  - If the node is completed, remove it
                if (visitor.nodeCompleted()) {
                    DEBUG("- Completed " << &node << " - " << typeid(node).name());
                    it = context.toVisit.erase(it);
                } else {
                    ++it;
                }
            }
            {
                ::std::vector<bool> advRevisitRemoveList;
                size_t len = context.advRevisits.size();
                for (size_t i = 0; i < len; i++) {
                    auto& ent = *context.advRevisits[i];
                    DEBUG("> " << FMT_CB(os, ent.fmt(os)));
                    advRevisitRemoveList.push_back(ent.revisit(context, /*is_fallback=*/true));
                }
                for (size_t i = len; i--;) {
                    if (advRevisitRemoveList[i]) {
                        context.advRevisits.erase(context.advRevisits.begin() + i);
                    }
                }
            }
        } // `if peek_changed` (node revisits)

        // Default numeric literals before making an arbitrary choice between
        // trait-bound candidates. The default can turn a fuzzy obligation
        // (for example `u128: CastInto<_>`) into a definite mismatch and leave
        // the candidate selected by the actual `i32` obligation.
        if (!context.ivars.peekChanged()) {
            DEBUG("- Applying defaults");
            if (context.ivars.applyDefaults()) {
                context.ivars.markChange();
            }
        }

        if (!context.ivars.peekChanged()) {
            // Check the possible equations
            DEBUG("--- IVar possibilities (just pick a bound)");
            for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                if (checkIvarPoss(context, *ivarBoundIndex, i, context.possibleIvarVals[i], IvarPossFallbackType::PickFirstBound)) {
                    break;
                }
            }
        }
        if (!context.ivars.peekChanged()) {
            // Check the possible equations
            DEBUG("--- IVar possibilities (final fallback)");
            for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                if (checkIvarPoss(context, *ivarBoundIndex, i, context.possibleIvarVals[i], IvarPossFallbackType::FinalOption)) {
                    break;
                }
            }
        }

        // And after all that, apply custom defaults
        if (!context.ivars.peekChanged()) {
            DEBUG("- Applying generic defaults");
            for (unsigned int i = 0; i < context.possibleIvarVals.size(); i++) {
                const auto& ent = context.possibleIvarVals[i];
                if (!ent.typesDefault.empty()) {
                    const auto& tyL = context.ivars.getType(i);

                    if (((*tyL).is_Infer() && ((*tyL).as_Infer().index == i))) {
                        if (ent.typesDefault.size() != 1) {
                            // TODO: Error?
                        } else {
                            context.ivars.setIvarTo(i, *ent.typesDefault.begin());
                        }
                    }
                }
            }
        }

        if (!context.ivars.peekChanged()) {
            DEBUG("--- Coercion consume");
            if (!context.linkCoerce.empty()) {
                auto ent = mv$(context.linkCoerce.front());
                context.linkCoerce.erase(context.linkCoerce.begin());

                const auto& sp = (*ent->rightNodePtr)->span();
                auto& srcTy = (*ent->rightNodePtr)->resType;
                ent->leftTy = context.resolve.expandAssociatedTypes(sp, mv$(ent->leftTy));
                DEBUG("- Equate coercion R" << ent->ruleIdx << " " << ent->leftTy << " := " << srcTy);

                context.equateTypes(sp, ent->leftTy, srcTy);
            }
        }

        // Clear ivar possibilities for next pass
        for (auto& ivarEnt : context.possibleIvarVals) {
            ivarEnt.reset();
        }

        count++;
        context.resolve.compactIvars(context.ivars);
    }
    if (count == MAX_ITERATIONS) {
        if (!context.hasRules()) {
            BUG(rootPtr->span(), "Typecheck ran for too many iterations, max - " << MAX_ITERATIONS);
        }
        WARNING(rootPtr->span(), W0000, "Typecheck ran for too many iterations, max - " << MAX_ITERATIONS);
    }

    if (context.hasRules()) {
        for (const auto& rule : context.linkAssoc) {
            if (!rule.isAmbiguous) {
                continue;
            }
            if (rule.name == "") {
                ERROR(rule.span, E0000, "type annotations needed: cannot infer a type satisfying `" << context.ivars.fmtType(rule.implTy) << ": " << rule.trait << context.ivars.fmt(rule.params) << "`");
            } else {
                ERROR(rule.span, E0000, "type annotations needed: cannot infer `" << context.ivars.fmtType(rule.leftTy) << " = <" << context.ivars.fmtType(rule.implTy) << " as " << rule.trait << context.ivars.fmt(rule.params) << ">::" << rule.name << "`");
            }
        }
        context.dump();
        for (const auto& coercionP : context.linkCoerce) {
            const auto& coercion = *coercionP;
            const auto& sp = (**coercion.rightNodePtr).span();
            const auto& srcTy = (**coercion.rightNodePtr).resType;
            WARNING(sp, W0000, "Spare Rule - " << context.ivars.fmtType(coercion.leftTy) << " := " << context.ivars.fmtType(srcTy));
        }
        for (const auto& rule : context.linkAssoc) {
            const auto& sp = rule.span;
            if (rule.name == "") {
                WARNING(sp, W0000, "Spare Rule - " << context.ivars.fmtType(rule.implTy) << " : " << rule.trait << rule.params);
            } else {
                WARNING(sp, W0000, "Spare Rule - " << context.ivars.fmtType(rule.leftTy) << " = < " << context.ivars.fmtType(rule.implTy) << " as " << rule.trait << rule.params << " >::" << rule.name);
            }
        }
        // TODO: Print revisit rules and advanced revisit rules.
        for (const auto& node : context.toVisit) {
            const auto& sp = node->span();
            WARNING(sp, W0000, "Spare rule - " << FMT_CB(os, {
                                   ExprVisitorPrint ev(context, os);
                                   node->visit(ev);
                               }) << " -> " << context.ivars.fmtType(node->resType));
        }
        for (const auto& adv : context.advRevisits) {
            WARNING(adv->span(), W0000, "Spare Rule - " << FMT_CB(os, adv->fmt(os)));
        }
        BUG(rootPtr->span(), "Spare rules left after typecheck stabilised");
    }
    DEBUG("root_ptr = " << rootPtr->typeName() << " " << rootPtr->resType);

    // - Synchronize a replacement of the root back into the expression.
    expr.reset(rootPtr.release());
    //  > Steal the binding types
    expr.bindings.reserve(context.bindings.size());
    for (auto& binding : context.bindings) {
        expr.bindings.push_back(binding.ty);
    }

    // - Validate typeck
    {
        DEBUG("==== VALIDATE ==== (" << count << " rounds)");
        context.dump();

        ExprVisitorApply visitor{context};
        visitor.visitNodePtr(expr);
    }

    {
        StaticTraitResolve staticResolve(ms.wb);
        staticResolve.setBothGenericsRaw(ms.implGenerics, ms.itemGenerics);

        DEBUG("=== Method const params ===");

        struct VisitMethodConst: public HIRExprVisitorDef {
            const TypeckModuleState& ms;
            const StaticTraitResolve& staticResolve;

            VisitMethodConst(const TypeckModuleState& ms, const StaticTraitResolve& staticResolve)
                : HIRExprVisitorDef(ms.crate.types)
                , ms(ms)
                , staticResolve(staticResolve)
            {
            }

            void evaluateConstantParams(const Span& sp, HIRPath& path) {
                HIRPathParams* params = nullptr;
                switch (path.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        auto& e = path.data.as_Generic();
                        params = &e.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        auto& e = path.data.as_UfcsKnown();
                        params = &e.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        auto& e = path.data.as_UfcsInherent();
                        params = &e.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        BUG(sp, "Unresolved constant path " << path);
                        break;
                    }
                }

                const bool hasUnevaluated = ::std::any_of(params->values.begin(), params->values.end(), [](const HIRConstGeneric& value) {
                    return value.is_Unevaluated();
                });
                if (!hasUnevaluated) {
                    return;
                }

                MonomorphState outParams(ms.crate.types);
                auto value = staticResolve.getValue(sp, path, outParams, /*signatureOnly=*/true, nullptr);
                const auto* constant = value.opt_Constant();
                ASSERT_BUG(sp, constant, "Constant path resolved to " << value.tagStr() << ": " << path);
                ConvertHIRConstantEvaluateMethodParams(sp, ms.wb, ms.crate, &(*constant)->params, *params);
            }

            void visit(HIRExprNodePathValue& node) override {
                HIRExprVisitorDef::visit(node);
                if (node.target == HIRExprNodePathValue::CONSTANT) {
                    evaluateConstantParams(node.span(), node.path);
                }
            }

            void visitPatternValue(const Span& sp, HIRPattern::Value& value) {
                if (auto* named = value.opt_Named()) {
                    evaluateConstantParams(sp, named->path);
                }
            }

            void visitPattern(const Span& sp, HIRPattern& pattern) override {
                HIRExprVisitorDef::visitPattern(sp, pattern);
                switch (pattern.data.tag()) {
default:
                    break;
                    case HIRPatternData::TAG_Value: {
                        auto& e = pattern.data.as_Value();
                        visitPatternValue(sp, e.val);
                        break;
                    }
                    case HIRPatternData::TAG_Range: {
                        auto& e = pattern.data.as_Range();
                        if (e.start) {
                            visitPatternValue(sp, *e.start);
                        }
                        if (e.end) {
                            visitPatternValue(sp, *e.end);
                        }
                        break;
                    }
                }
            }

            void visit(HIRExprNodeCallPath& node) override {
                HIRExprVisitorDef::visit(node);

                HIRPathParams* paramsPtr = nullptr;
                switch (node.path.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        auto& pe = node.path.data.as_Generic();
                        paramsPtr = &pe.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        auto& pe = node.path.data.as_UfcsKnown();
                        paramsPtr = &pe.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        auto& pe = node.path.data.as_UfcsInherent();
                        paramsPtr = &pe.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        BUG(node.span(), "Unresolved call path " << node.path);
                        break;
                    }
                }

                const bool hasUnevaluated = ::std::any_of(paramsPtr->values.begin(), paramsPtr->values.end(), [](const HIRConstGeneric& value) {
                    return value.is_Unevaluated();
                });
                if (!hasUnevaluated) {
                    return;
                }

                TRACE_FUNCTION_FR("Call const params: " << node.path, "Call const params");
                MonomorphState outParams(ms.crate.types);
                auto valRef = staticResolve.getValue(node.span(), node.path, outParams, /*signatureOnly=*/true, nullptr);
                const auto* fcn = valRef.opt_Function();
                ASSERT_BUG(node.span(), fcn, "Call path resolved to " << valRef.tagStr() << ": " << node.path);
                ConvertHIRConstantEvaluateMethodParams(node.span(), ms.wb, ms.crate, &(*fcn)->params, *paramsPtr);
            }

            void visit(HIRExprNodeCallMethod& node) override {
                HIRExprVisitorDef::visit(node);

                HIRPathParams* paramsPtr = nullptr;
                switch (node.methodPath.data.tag()) {
                    case HIRPathData::TAG_Generic: {
                        BUG(node.span(), "");
                        break;
                    }
                    case HIRPathData::TAG_UfcsUnknown: {
                        BUG(node.span(), "");
                        break;
                    }
                    case HIRPathData::TAG_UfcsKnown: {
                        auto& pe = node.methodPath.data.as_UfcsKnown();
                        paramsPtr = &pe.params;
                        break;
                    }
                    case HIRPathData::TAG_UfcsInherent: {
                        auto& pe = node.methodPath.data.as_UfcsInherent();
                        paramsPtr = &pe.params;
                        break;
                    }
                }
                assert(paramsPtr);

                bool found = false;
                for(auto& v : paramsPtr->values)
                {
                    if (v.is_Unevaluated()) {
                        found = true;
                    }
                }
                if(found)
                {
                    TRACE_FUNCTION_FR("Method const params: " << node.methodPath, "Method const params");
                    MonomorphState outParams(ms.crate.types);
                    auto valRef = staticResolve.getValue(node.span(), node.methodPath, outParams, /*signature_only=*/true, nullptr);
                    const HIRFunction& fcn = *valRef.as_Function();
                    const HIRGenericParams& gpDef = fcn.params;
                    ConvertHIRConstantEvaluateMethodParams(node.span(), ms.wb, ms.crate, &gpDef, *paramsPtr);
                }
            }
        } v(ms, staticResolve);

        expr->visit(v);
    }
}

namespace {
    inline HIRSimplePath getRuleParentPath(const HIRSimplePath& sp) {
        return sp.parent();
    }

    inline HIRGenericPath getRuleParentPath(const HIRGenericPath& gp) {
        return HIRGenericPath(gp.path.parent(), gp.params.clone());
    }
}

bool visitCallPopulateCache(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache) __attribute__((warnUnusedResult));
bool visitCallPopulateCacheUfcsInherent(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache, const HIRFunction*& fcnPtr);

class OwnedImplMatcher: public HIRMatchGenerics {
    HIRPathParams& implParams;

public:
    OwnedImplMatcher(HIRPathParams& implParams)
        : implParams(implParams)
    {
    }

    HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType _resolve_cb) override {
        assert(g.binding < implParams.types.size());
        auto& slot = implParams.types[g.binding];
        if (!(slot->is_Infer() && slot->as_Infer().index == ~0u)) {
            return slot->compareWithPlaceholders(Span(), ty, _resolve_cb);
        }
        slot = ty;
        return HIRCompare::Equal;
    }

    HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
        assert(g.binding < implParams.values.size());
        ASSERT_BUG(Span(), implParams.values[g.binding] == HIRConstGeneric(), "TODO: Multiple values? " << implParams.values[g.binding] << " and " << sz);
        implParams.values[g.binding] = sz.clone();
        return HIRCompare::Equal;
    }
};

void populateDefaults(const Span& sp, Context& context, const MonomorphStatePtr& ms, const HIRGenericParams& paramDefs, HIRPathParams& params) {
    for (size_t i = 0; i < paramDefs.types.size(); i++) {
        const auto& ty = params.types[i];
        const auto& typ = paramDefs.types[i];
        if (const auto* te = ty->opt_Infer()) {
            if (!typ.defaultValue->is_Infer()) {
                if (auto* ent = context.getIvarPossibilities(sp, te->index)) {
                    auto defTy = ms.monomorphType(sp, typ.defaultValue);
                    DEBUG("Added default for " << ty << ": " << defTy);
                    ent->typesDefault.insert(std::move(defTy));
                }
            }
        }
    }
}

template <typename T>
void fix_param_count_(const Span& sp, Context& context, const HIRTypeData* selfTy, bool useDefaults, const T& path, const HIRGenericParams& paramDefs, HIRPathParams& params) {
    if (params.types.size() == paramDefs.types.size()) {
        // Nothing to do, all good
    } else if (params.types.size() > paramDefs.types.size()) {
        // The parser cannot tell a type argument from a const argument, so a
        // `_` const argument (`make_buf::<_>()`) lands in the type list. Move
        // the surplus placeholders across before complaining.
        while (params.types.size() > paramDefs.types.size()
            && params.values.size() < paramDefs.values.size()
            && params.types.back()->is_Infer()) {
            params.types.pop_back();
            params.values.push_back({});
            context.ivars.addIvars(params.values.back());
        }
        if (params.types.size() > paramDefs.types.size()) {
            ERROR(sp, E0000, "Too many type parameters passed to " << path);
        }
    } else {
        while (params.types.size() < paramDefs.types.size()) {
            const auto& typ = paramDefs.types[params.types.size()];
            if (useDefaults) {
                if (typ.defaultValue->is_Infer()) {
                    ERROR(sp, E0000, "Omitted type parameter with no default in " << path);
                } else if (monomorphiseTypeNeeded(typ.defaultValue)) {
                    auto cb = MonomorphStatePtr(context.crate.types, selfTy, nullptr, nullptr);
                    params.types.push_back(cb.monomorphType(sp, typ.defaultValue));
                } else {
                    params.types.push_back(typ.defaultValue);
                }
            } else {
                params.types.push_back(context.ivars.newIvarTr());
                // TODO: It's possible that the default could be added using `context.possible_equate_type_def` to give inferrence a fallback
            }
        }
    }

    if (params.values.size() == paramDefs.values.size()) {
        // Nothing to do, all good
    } else if (params.values.size() > paramDefs.values.size()) {
        ERROR(sp, E0000, "Too many const parameters passed to " << path);
    } else {
        while (params.values.size() < paramDefs.values.size()) {
            params.values.push_back({});
            context.ivars.addIvars(params.values.back());
        }
    }
}

void fixParamCount(const Span& sp, Context& context, const HIRTypeData* selfTy, bool useDefaults, const HIRPath& path, const HIRGenericParams& paramDefs, HIRPathParams& params) {
    fix_param_count_(sp, context, selfTy, useDefaults, path, paramDefs, params);
}

void fixParamCount(const Span& sp, Context& context, const HIRTypeData* selfTy, bool useDefaults, const HIRGenericPath& path, const HIRGenericParams& paramDefs, HIRPathParams& params) {
    fix_param_count_(sp, context, selfTy, useDefaults, path, paramDefs, params);
}

void applyBoundsAsRulesTrait(Context& context, const Span& sp, const HIRTypeData* realType, const HIRTraitPath& traitPath) {
    // If there's no type bounds, emit a trait bound
    // - Otherwise, the assocated type bounds will serve the same purpose
    if (traitPath.typeBounds.size() == 0) {
        context.addTraitBound(sp, realType, traitPath.path.path, traitPath.path.params.clone());
    }

    // Associated type equalities
    for (const auto& assoc : traitPath.typeBounds) {
        context.equateTypesAssoc(sp, assoc.second.type, assoc.second.sourceTrait.path, assoc.second.sourceTrait.params.clone(), realType, assoc.first.c_str(), assoc.second.atyParams.clone(), false);
    }
    // Associated type trait bounds:
    for (const auto& assoc : traitPath.traitBounds) {
        auto atyTy = context.crate.types.path(HIRPath(realType, assoc.second.sourceTrait.clone(), assoc.first, assoc.second.atyParams.clone()), {});
        for (const auto& tr : assoc.second.traits) {
            applyBoundsAsRulesTrait(context, sp, atyTy, tr);
        }
    }
}

void applyBoundsAsRules(Context& context, const Span& sp, const HIRGenericParams& paramsDef, const Monomorphiser& ms, bool isImplLevel) {
    TRACE_FUNCTION;
    for (const auto& bound : paramsDef.bounds) {
            switch (bound.tag()) {
                case HIRGenericBound::TAG_TraitBound: {
                    auto& be = bound.as_TraitBound();
                    DEBUG("Bound " << be.type << ":  " << be.trait);
                    auto realType = ms.monomorphType(sp, be.type);
                    auto realTrait = ms.monomorphTraitpath(sp, be.trait, false);
                    DEBUG("= (" << realType << ": " << realTrait << ")");
                    applyBoundsAsRulesTrait(context, sp, realType, realTrait);
                    break;
                }
                case HIRGenericBound::TAG_TypeEquality: {
                    auto& be = bound.as_TypeEquality();
                    auto realTypeLeft = context.resolve.expandAssociatedTypes(sp, ms.monomorphType(sp, be.type));
                    auto realTypeRight = context.resolve.expandAssociatedTypes(sp, ms.monomorphType(sp, be.otherType));
                    context.equateTypes(sp, realTypeLeft, realTypeRight);
                    break;
                }
            }
    }

    for (size_t i = 0; i < paramsDef.types.size(); i++) {
        if (paramsDef.types[i].isSized) {
            HIRTypeRef ty = context.crate.types.generic("", (isImplLevel ? 0 : 256) + i);
            context.requireSized(sp, ms.getType(Span(), ty->as_Generic()));
        }
    }
}

/// (HELPER) Populate the cache for nodes that use visit_call
/// TODO: If the function has multiple mismatched options, tell the caller to try again later?
bool visitCallPopulateCache(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache) {
    TRACE_FUNCTION_FR(path, path);
    assert(cache.argTypes.size() == 0);

    const HIRFunction* fcnPtr = nullptr;

    struct Monomorph: public Monomorphiser {
        Context& context;
        const HIRTypeData* selfTy;
        HIRPathParams implParams;
        bool hasImplParams;
        const HIRPathParams& fcnParams;
        const HIRPathParams hrlParams;

        Monomorph(Context& context, const HIRTypeData* selfTy, const HIRPathParams* implParams, const HIRPathParams& fcnParams, HIRPathParams hrlParams)
            : Monomorphiser(context.crate.types)
            , context(context)
            , selfTy(selfTy)
            , implParams(implParams ? implParams->clone() : HIRPathParams())
            , hasImplParams(implParams != nullptr)
            , fcnParams(fcnParams)
            , hrlParams(std::move(hrlParams))
        {
        }

        HIRTypeRef getType(const Span& sp, const HIRGenericRef& e) const override {
            if (e.name == "Self" || e.isSelf()) {
                if (selfTy) {
                    return selfTy;
                } else {
                    TODO(sp, "Handle 'Self' when monomorphising");
                }
            } else if (e.binding < 256) {
                if (hasImplParams) {
                    auto idx = e.idx();
                    ASSERT_BUG(sp, idx < implParams.types.size(), "Generic param (impl) out of input range - " << e << " >= " << implParams.types.size());
                    return context.getType(implParams.types[idx]);
                } else {
                    BUG(sp, "Impl-level parameter on free function (" << e << ")");
                }
            } else if (e.binding < 512) {
                auto idx = e.idx();
                ASSERT_BUG(sp, idx < fcnParams.types.size(), "Generic param out of input range - " << e << " >= " << fcnParams.types.size());
                return context.getType(fcnParams.types[idx]);
            } else if (e.group() == GENERICHrtb) {
                return context.crate.types.generic(e.name, e.binding);
            } else {
                BUG(sp, "Generic binding out of total range (" << e << ")");
            }
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& e) const override {
            if (e.binding < 256) {
                ASSERT_BUG(sp, hasImplParams, "Impl-level value parameter on free function (" << e << ")");
                auto idx = e.idx();
                ASSERT_BUG(sp, idx < implParams.values.size(), "Generic value (impl) out of input range - " << e << " >= " << implParams.values.size());
                return context.ivars.getValue(implParams.values[idx]).clone();
            } else if (e.binding < 512) {
                auto idx = e.idx();
                ASSERT_BUG(sp, idx < fcnParams.values.size(), "Generic value out of input range - " << e << " >= " << fcnParams.values.size());
                return context.ivars.getValue(fcnParams.values[idx]).clone();
            } else if (e.group() == GENERICHrtb) {
                return e;
            } else {
                BUG(sp, "Generic value bounding out of total range (" << e << ")");
            }
        }
    };

    cache.topParams = nullptr;
        switch (path.data.tag()) {
            case HIRPathData::TAG_Generic: {
                auto& e = path.data.as_Generic();
                const auto& fcn = context.crate.getFunctionByPath(sp, e.path);
                fixParamCount(sp, context, HIRTypeRef(), false, path, fcn.params, e.params);
                fcnPtr = &fcn;
                cache.fcnParams = &fcn.params;

                const auto& pathParams = e.params;
                cache.monomorph.reset(new Monomorph(context, nullptr, nullptr, pathParams, {}));
                break;
            }
            case HIRPathData::TAG_UfcsKnown: {
                auto& e = path.data.as_UfcsKnown();
                const auto& trait = context.crate.getTraitByPath(sp, e.trait.path);
                fixParamCount(sp, context, e.type, true, path, trait.params, e.trait.params);
                if (trait.values.count(e.item) == 0) {
                    BUG(sp, "Method '" << e.item << "' of trait " << e.trait.path << " doesn't exist");
                }
                const auto& fcn = trait.values.at(e.item).as_Function();
                fixParamCount(sp, context, e.type, false, path, fcn.params, e.params);
                cache.fcnParams = &fcn.params;
                cache.topParams = &trait.params;

                // Add a bound requiring the Self type impl the trait
                context.addTraitBound(sp, e.type, e.trait.path, e.trait.params.clone());

                fcnPtr = &fcn;
                const HIRPathParams* implParams = &e.trait.params;
                HIRPathParams selectedImplParams;

                if (!monomorphiseTypeNeeded(e.type) && !monomorphisePathparamsNeeded(e.trait.params) && !context.resolve.typeContainsIvars(e.type) && !context.resolve.paramsContainIvars(e.trait.params)) {
                    std::vector<ImplRef> impls;
                    context.resolve.findTraitImpls(sp, e.trait.path, e.trait.params, e.type, [&](ImplRef impl, HIRCompare cmp) {
                        if (cmp == HIRCompare::Equal && !impl.isAmbiguousIdentity() && impl.data.is_TraitImpl()) {
                            const auto* traitImpl = impl.data.as_TraitImpl().impl;
                            if (traitImpl->methods.find(e.item) == traitImpl->methods.end()) {
                                return false;
                            }
                            const bool seen = std::any_of(impls.begin(), impls.end(), [&](const ImplRef& other) {
                                return other.data.as_TraitImpl().impl == traitImpl;
                            });
                            if (!seen) {
                                impls.push_back(std::move(impl));
                            }
                        }
                        return false;
                    });

                    ImplRef* selected = nullptr;
                    for (auto& candidate : impls) {
                        const bool dominated = std::any_of(impls.begin(), impls.end(), [&](const ImplRef& other) {
                            return &candidate != &other && other.moreSpecificThan(context.crate.types, candidate);
                        });
                        if (!dominated) {
                            if (selected) {
                                selected = nullptr;
                                break;
                            }
                            selected = &candidate;
                        }
                    }

                    if (selected) {
                        auto& implData = selected->data.as_TraitImpl();
                        auto method = implData.impl->methods.find(e.item);
                        if (method != implData.impl->methods.end() && method->second.data.traitReturnType) {
                            fcnPtr = &method->second.data;
                            cache.fcnParams = &fcnPtr->params;
                            cache.topParams = &implData.impl->params;
                            selectedImplParams = implData.implParams.clone();
                            implParams = &selectedImplParams;
                        }
                    }
                }

                cache.monomorph.reset(new Monomorph(context, e.type, implParams, e.params, {}));
                break;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                // TODO: Eventually, the HIR `Resolve UFCS` pass will be removed, leaving this code responsible for locating the item.
                TODO(sp, "Hit a UfcsUnknown (" << path << ") - Is this an error?");
                break;
            }
            case HIRPathData::TAG_UfcsInherent: {
                // NOTE: This case is kinda long, so it's refactored out into a helper
                if (!visitCallPopulateCacheUfcsInherent(context, sp, path, cache, fcnPtr)) {
                    return false;
                }
                break;
            }
        }

        assert( fcnPtr );
        cache.fcn = fcnPtr;
        const auto& fcn = *fcnPtr;
        cache.monomorph->setConstevalState(context.resolve.board(), HIRItemPath(path));
        const auto& monomorph = *cache.monomorph;

        // --- Monomorphise the argument/return types (into current context)
        for (size_t i = 0; i < fcn.fixedArgCount(); i++) {
        const auto& arg = fcn.args[i];
        TRACE_FUNCTION_FR("ARG " << path << " - " << arg.first << ": " << arg.second, "Arg " << arg.first << " : " << cache.argTypes.back());
        cache.argTypes.push_back(monomorph.monomorphType(sp, arg.second, false));
        }
        {
        TRACE_FUNCTION_FR("RET " << path << " - " << fcn.returnType, "Ret " << cache.argTypes.back());
        auto returnType = monomorph.monomorphType(sp, fcn.returnType, false);
        if (const auto* traitCall = path.data.opt_UfcsKnown()) {
            if (const auto* erased = returnType->opt_ErasedType()) {
                if (const auto* origin = erased->inner.opt_Fcn()) {
                    auto name = RcString::newInterned(FMT(ATY_PREFIX_ERASED << traitCall->item << "_" << origin->index));
                    const auto& trait = context.crate.getTraitByPath(sp, traitCall->trait.path);
                    if (trait.types.find(name) != trait.types.end()) {
                        returnType = context.crate.types.path(HIRPath(traitCall->type, traitCall->trait.clone(), name, traitCall->params.clone()), {});
                    }
                }
            }
        }
        cache.argTypes.push_back(std::move(returnType));
        }

        // --- Apply bounds by adding them to the associated type ruleset
        if( cache.topParams ) {
        applyBoundsAsRules(context, sp, *cache.topParams, monomorph, /*is_impl_level=*/true);
        }
        applyBoundsAsRules(context, sp, *cache.fcnParams, monomorph, /*is_impl_level=*/false);

        return true;
}

bool visitCallPopulateCacheUfcsInherent(Context& context, const Span& sp, HIRPath& path, HIRExprCallCache& cache, const HIRFunction*& fcnPtr) {
    auto& e = path.data.as_UfcsInherent();

    const HIRTypeImpl* implPtr = nullptr;
    // Detect multiple applicable methods and get the caller to try again later if there are multiple
    unsigned int count = 0;
    context.crate.findTypeImpls(e.type, context.ivars.callbackResolveInfer(), [&](const auto& impl) {
        DEBUG("- impl" << impl.params.fmtArgs() << " " << impl.type);
        auto it = impl.methods.find(e.item);
        if (it == impl.methods.end()) {
            return false;
        }
        fcnPtr = &it->second.data;
        implPtr = &impl;
        count++;
        return false;
    });
    if (!fcnPtr) {
        ERROR(sp, E0000, "Failed to locate function " << path);
    }
    if (count > 1) {
        // Return a status to the caller so it can try again when there may be more information
        return false;
    }
    assert(implPtr);
    DEBUG("Found impl" << implPtr->params.fmtArgs() << " " << implPtr->type);
    fixParamCount(sp, context, e.type, false, path, fcnPtr->params, e.params);
    cache.fcnParams = &fcnPtr->params;

    // If the impl block has parameters, figure out what types they map to
    // - The function params are already mapped (from fix_param_count)
    auto& implParams = e.implParams;
    if (implPtr->params.isGeneric()) {
        while (implParams.types.size() < implPtr->params.types.size()) {
            implParams.types.push_back(context.crate.types.infer());
        }
        implParams.values.resize(implPtr->params.values.size());
        OwnedImplMatcher matcher(implParams);

        auto cmp = implPtr->type->matchTestGenericsFuzz(sp, e.type, context.ivars.callbackResolveInfer(), matcher);
        if (cmp == HIRCompare::Fuzzy) {
            // If the match was fuzzy, it could be due to a compound being matched against an ivar
            DEBUG("- Fuzzy match, adding ivars and equating");
            for (auto& ty : implParams.types) {
                if (ty->is_Infer() && ty->as_Infer().index == ~0u) {
                    // Allocate a new ivar for the param
                    ty = context.ivars.newIvarTr();
                }
            }

            context.ivars.addIvarsParams(implParams);

            // Monomorphise the impl type with the new ivars, and equate to e.type
            // TODO: Use a copy of `MonomorphStatePtr` that calls `context.get_type`
            auto implMonomorphCb = MonomorphStatePtr(context.crate.types, e.type, &implParams, nullptr);
            auto implTyMono = implMonomorphCb.monomorphType(sp, implPtr->type, false);
            DEBUG("- impl_ty_mono = " << implTyMono);

            context.equateTypes(sp, implTyMono, e.type);
        } else if (cmp == HIRCompare::Unequal) {
            BUG(sp, "Failed to match inherent impl?!");
        } else {
            context.ivars.addIvarsParams(implParams);
        }

        // Fill unknown parametrs with ivars
        for (auto& ty : implParams.types) {
            if (ty->is_Infer() && ty->as_Infer().index == ~0u) {
                // Allocate a new ivar for the param
                ty = context.ivars.newIvarTr();
            }
        }
    }

    // Create monomorphise callback
    const auto& fcnParams = e.params;
    // TODO: Use a copy of `MonomorphStatePtr` that calls `context.get_type`
    cache.monomorph.reset(new MonomorphStatePtr(context.crate.types, e.type, &implParams, &fcnParams));

    // Add trait bounds for all impl and function bounds
    applyBoundsAsRules(context, sp, implPtr->params, *cache.monomorph, /*is_impl_level=*/true);

    // Equate `Self` and `impl_ptr->m_type` (after monomorph)
    {
        HIRTypeRef tmp;
        const auto& implTyM = cache.monomorph->maybeMonomorphType(sp, tmp, implPtr->type);

        context.equateTypes(sp, e.type, implTyM);
    }

    return true;
}

// -----------------------------------------------------------------------
// IVar generation visitor
// Iterates the HIR expression tree and adds ivars to all types
// -----------------------------------------------------------------------
class ExprVisitorAddIvars: public HIRExprVisitorDef {
    Context& context;

    struct LocalImplTraitLowering: Monomorphiser {
        Context& context;
        mutable const HIRTypeData* curSelf = nullptr;

        explicit LocalImplTraitLowering(Context& context)
            : Monomorphiser(context.crate.types)
            , context(context)
        {
        }

        HIRTypeRef getType(const Span& sp, const HIRGenericRef& generic) const override {
            if (generic.binding == GENERICErasedSelf && curSelf) {
                return curSelf;
            }
            return types.generic(generic.name, generic.binding);
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& generic) const override {
            return generic;
        }

        HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* type, bool allowInfer = true) const override {
            if (const auto* erased = type->opt_ErasedType()) {
                if (const auto* fcn = erased->inner.opt_Fcn(); fcn && fcn->origin == HIRSimplePath()) {
                    auto result = context.ivars.newIvarTr();
                    const auto* savedSelf = curSelf;
                    curSelf = result;

                    for (const auto& trait : erased->traits) {
                        auto bound = this->monomorphTraitpath(sp, trait, allowInfer);
                        if (bound.typeBounds.empty()) {
                            context.addTraitBound(sp, result, bound.path.path, mv$(bound.path.params));
                        } else {
                            for (auto& associated : bound.typeBounds) {
                                context.equateTypesAssoc(sp, associated.second.type, bound.path.path, bound.path.params.clone(), result, associated.first.c_str(), associated.second.atyParams, false);
                            }
                        }
                    }
                    if (erased->isSized) {
                        context.requireSized(sp, result);
                    }

                    curSelf = savedSelf;
                    return result;
                }
            }
            return Monomorphiser::monomorphType(sp, type, allowInfer);
        }
    };

public:
    ExprVisitorAddIvars(Context& context)
        : HIRExprVisitorDef(context.crate.types)
        , context(context)
    {
    }

    void innerVisitType(HIRTypeRef& ty) {
        rewriteTyWith(context.crate.types, ty, [this](HIRTypeRef&, HIRTypeData& data) -> bool {
            if (auto* te = data.opt_Path()) {
                if (te->path.data.is_Generic()) {
                    auto& params = te->path.data.as_Generic().params;
                    const HIRGenericParams* paramDefs = nullptr;
                        switch (te->binding.tag()) {
                            case HIRTypePathBinding::TAG_Struct: {
                                auto& pbe = te->binding.as_Struct();
                                paramDefs = &pbe->params;
                                break;
                            }
                            case HIRTypePathBinding::TAG_Enum: {
                                auto& pbe = te->binding.as_Enum();
                                paramDefs = &pbe->params;
                                break;
                            }
                            case HIRTypePathBinding::TAG_Union: {
                                auto& pbe = te->binding.as_Union();
                                paramDefs = &pbe->params;
                                break;
                            }
                            case HIRTypePathBinding::TAG_ExternType: {
                                break;
                            }
                            case HIRTypePathBinding::TAG_Opaque: {
                                break;
                            }
                            case HIRTypePathBinding::TAG_Unbound: {
                                break;
                            }
                        }
                        if(paramDefs)
                        {
                        populateDefaults(Span(), context, MonomorphStatePtr(context.crate.types, nullptr, &params, nullptr), *paramDefs, params);
                        }
                }
            }
            return false;
        });
    }

    void visitPathParams(HIRPathParams& pp) override {
        this->context.ivars.addIvarsParams(pp);
        for (auto& ty : pp.types) {
            innerVisitType(ty);
        }
    }

    [[nodiscard]] HIRTypeRef visitType(HIRTypeRef ty) override {
        this->context.addIvars(ty);
        innerVisitType(ty);
        visitTyWith(ty, [&](const HIRTypeData* inner) {
            if (const auto* path = inner->opt_Path()) {
                if (const auto* projection = path->path.data.opt_UfcsKnown()) {
                    context.addTraitBound(Span(), projection->type, projection->trait.path, projection->trait.params.clone());
                    const auto& trait = context.crate.getTraitByPath(Span(), projection->trait.path);
                    auto monomorph = MonomorphStatePtr(context.crate.types, projection->type, &projection->trait.params, nullptr);
                    applyBoundsAsRules(context, Span(), trait.params, monomorph, true);
                }
            }
            return false;
        });
        return ty;
    }

    void visit(HIRExprNodeLet& node) override {
        node.type = LocalImplTraitLowering(context).monomorphType(node.span(), node.type);
        HIRExprVisitorDef::visit(node);
    }
};

// -----------------------------------------------------------------------
// Enumeration visitor
// Iterates the HIR expression tree and extracts type "equations"
// -----------------------------------------------------------------------
class ExprVisitorEnum: public HIRExprVisitor {
    Context& context;
    const HIRTypeData* retType;

    struct RetTarget {
        const HIRTypeData* retType;
        const HIRTypeData* resumeType;
        const HIRTypeData* yieldType;

        RetTarget(const HIRTypeData* retType)
            : retType(retType)
            , resumeType(nullptr)
            , yieldType(nullptr)
        {
        }

        RetTarget(const HIRTypeData* retType, const HIRTypeData* resumeType, const HIRTypeData* yieldType)
            : retType(retType)
            , resumeType(resumeType)
            , yieldType(yieldType)
        {
        }
    };

    ::std::vector<RetTarget> closureRetTypes;

    ::std::vector<bool> innerCoerceEnabledStack;

    ::std::vector<HIRExprNodeLoop*> loopBlocks; // Used for `break` type markings

    // TEMP: List of in-scope traits for buildup
    tTraitList traits;

    /// A statement whose type nothing constrained defaults to `()` at fallback.
    struct RevisitDefaultUnit: public Context::Revisitor {
        HIRExprNode* node;

        RevisitDefaultUnit(HIRExprNode* node)
            : node(node)
        {
        }

        const Span& span(void) const {
            return node->span();
        }

        void fmt(std::ostream& os) const {
            os << "RevisitDefaultUnit(" << node << ": " << node->resType << ")";
        }

        bool revisit(Context& context, bool isFallback) {
            DEBUG("is_fallback=" << isFallback);
            const auto& ty = context.getType(node->resType);
            if (const auto* i = ty->opt_Infer()) {
                if (i->tyClass != HIRInferClass::None) {
                    // Bounded ivar, remove this rule.
                    return true;
                }
                if (isFallback) {
                    context.equateTypes(node->span(), ty, context.crate.types.unit());
                    return true;
                }
                return false;
            } else {
                return true;
            }
        }
    };

public:
    ExprVisitorEnum(Context& context, tTraitList baseTraits, const HIRTypeData* retType)
        : context(context)
        , retType(retType)
        , traits(mv$(baseTraits))
    {
    }

    void visit(HIRExprNodeBlock& node) override {
        TRACE_FUNCTION_FR(&node << " { ... }", &node << " " << this->context.getType(node.resType));

        this->context.resolve.addOpaqueAliasScope(node.localMod);

        bool diverges = false;
        node.diverges = false;
        this->pushTraits(node.traits);
        if (node.nodes.size() > 0) {
            this->pushInnerCoerce(false);
            for (unsigned int i = 0; i < node.nodes.size(); i++) {
                auto& snp = node.nodes[i];
                this->context.addIvars(snp->resType);
                snp->visit(*this);

                // If this statement yields !, then mark the block as diverging
                if (this->nodeDiverges(*snp)) {
                    diverges = true;
                } else {
                    this->context.addRevisitAdv(std::make_unique<RevisitDefaultUnit>(&*snp));
                }
            }
            this->popInnerCoerce();
        }

        if (node.valueNode) {
            auto& snp = node.valueNode;
            DEBUG("Block yields final value");
            this->context.addIvars(snp->resType);
            this->context.equateTypes(snp->span(), node.resType, snp->resType);
            this->context.requireSized(snp->span(), snp->resType);
            snp->visit(*this);
            node.diverges = diverges || this->nodeDiverges(*snp);
        } else if (node.nodes.size() > 0) {
            // NOTE: If the final statement in the block diverges, mark this as diverging
            const auto& snp = node.nodes.back();
            bool defer = false;
            if (!diverges) {
                if (const auto* e = this->context.getType(snp->resType)->opt_Infer()) {
                    switch (e->tyClass) {
                        case HIRInferClass::Integer:
                        case HIRInferClass::Float:
                            diverges = false;
                            break;
                        default:
                            defer = true;
                            break;
                    }
                } else if (this->nodeDiverges(*snp)) {
                    diverges = true;
                } else {
                    diverges = false;
                }
            }

            // If a statement in this block diverges
            if (defer) {
                DEBUG("Block final node returns _, derfer diverge check");
                this->context.addRevisit(node);
            } else if (diverges) {
                DEBUG("Block diverges, yield !");
                // `!` coerces to any type, so pinning the block's result to it
                // would leak divergence into whatever the block feeds — a match
                // arm shares its ivar with the match. Before edition 2024,
                // where the fallback is `()`, record it as a coercion source
                // instead and let that fallback settle the ivar. From 2024 on
                // the fallback is `!` itself, so pin it directly.
                const auto* blockInfer = this->context.crate.edition < ASTEdition::Rust2024
                    ? this->context.getType(node.resType)->opt_Infer()
                    : nullptr;
                if (const auto* i = blockInfer) {
                    this->context.possibleEquateIvar(node.span(), i->index, this->context.crate.types.diverge(),
                        Context::PossibleTypeSource::CoerceFrom);
                    this->context.addRevisitAdv(std::make_unique<RevisitDefaultUnit>(&node));
                } else {
                    this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
                }
            } else {
                DEBUG("Block doesn't diverge but doesn't yield a value, yield ()");
                this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
            }
            node.diverges = diverges;
        } else {
            // Result should be `()`
            DEBUG("Block is empty, yield ()");
            this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
        }
        this->popTraits(node.traits);
    }

    void visit(HIRExprNodeConstBlock& node) override {
        TRACE_FUNCTION_F(&node << " const { ... }");
        this->context.addIvars(node.inner->resType);

        node.inner->visit(*this);
        node.diverges = this->nodeDiverges(*node.inner);
        this->context.equateTypes(node.span(), node.resType, node.inner->resType);
    }

    void visit(HIRExprNodeAsm& node) override {
        TRACE_FUNCTION_F(&node << " llvm_asm! ...");

        this->pushInnerCoerce(false);
        for (auto& v : node.outputs) {
            this->context.addIvars(v.value->resType);
            v.value->visit(*this);
            this->inheritDivergence(node, *v.value);
        }
        for (auto& v : node.inputs) {
            this->context.addIvars(v.value->resType);
            v.value->visit(*this);
            this->inheritDivergence(node, *v.value);
        }
        this->popInnerCoerce();
        // TODO: Revisit to check that the input are integers, and the outputs are integer lvalues
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
    }

    void visit(HIRExprNodeAsm2& node) override {
        TRACE_FUNCTION_F(&node << " asm! ...");

        bool hasLabel = false;
        this->pushInnerCoerce(false);
        for (auto& v : node.params) {
                switch (v.tag()) {
                    case HIRAsmParam::TAG_Const: {
                        auto& e = v.as_Const();
                        this->context.addIvars(e->resType);
                        visitNodePtr(e);
                        this->inheritDivergence(node, *e);
                        break;
                    }
                    case HIRAsmParam::TAG_Sym: {
                        break;
                    }
                    case HIRAsmParam::TAG_Label: {
                        auto& e = v.as_Label();
                        hasLabel = true;
                        this->context.addIvars(e.code->resType);
                        visitNodePtr(e.code);
                        this->context.equateTypes(e.code->span(), e.code->resType, this->context.crate.types.unit());
                        break;
                    }
                    case HIRAsmParam::TAG_RegSingle: {
                        auto& e = v.as_RegSingle();
                        this->context.addIvars(e.val->resType);
                        visitNodePtr(e.val);
                        this->inheritDivergence(node, *e.val);
                        break;
                    }
                    case HIRAsmParam::TAG_Reg: {
                        auto& e = v.as_Reg();
                        if (e.valIn) {
                            this->context.addIvars(e.valIn->resType);
                            visitNodePtr(e.valIn);
                            this->inheritDivergence(node, *e.valIn);
                        }
                        if (e.valOut) {
                            this->context.addIvars(e.valOut->resType);
                            visitNodePtr(e.valOut);
                            this->inheritDivergence(node, *e.valOut);
                        }
                        break;
                    }
                }
        }
        this->popInnerCoerce();
        // TODO: Revisit to check that the input are integers, and the outputs are integer lvalues
        if (node.options.noreturn && !hasLabel) {
            node.diverges = true;
            this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
        } else {
            this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
        }
    }

    void visit(HIRExprNodeReturn& node) override {
        TRACE_FUNCTION_F(&node << " return ...");
        if (node.isTailCall
            && !cast<HIRExprNodeCallPath>(node.value.get())
            && !cast<HIRExprNodeCallValue>(node.value.get())) {
            ERROR(node.span(), E0000, "`become` requires a direct function call");
        }
        this->context.addIvars(node.value->resType);

        const auto* retTy = (this->closureRetTypes.size() > 0 ? this->closureRetTypes.back().retType : this->retType);
        this->context.equateTypesCoerce(node.span(), retTy, node.value);

        this->pushInnerCoerce(true);
        node.value->visit(*this);
        this->popInnerCoerce();
        node.diverges = true;
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
    }

    void visit(HIRExprNodeYield& node) override {
        TRACE_FUNCTION_F(&node << " yield ...");
        this->context.addIvars(node.value->resType);

        if (this->closureRetTypes.empty() || this->closureRetTypes.back().yieldType == nullptr) {
            ERROR(node.span(), E0000, "`yield` outside a generator closure");
        }
        const auto* retTy = this->closureRetTypes.back().yieldType;
        const auto* resumeTy = this->closureRetTypes.back().resumeType;
        this->context.equateTypesCoerce(node.span(), retTy, node.value);

        this->pushInnerCoerce(true);
        node.value->visit(*this);
        this->popInnerCoerce();
        this->inheritDivergence(node, *node.value);
        this->context.equateTypes(node.span(), node.resType, resumeTy);
    }

    void visit(HIRExprNodeAWait& node) override {
        TRACE_FUNCTION_F(&node << "(...).await");
        this->context.addIvars(node.value->resType);
        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);
        if (node.isNext) {
            // Require that `return = Option<<[node.value] as `async_iterator`>::Item>`
            auto itemTy = this->context.ivars.newIvarTr();
            this->context.equateTypesAssoc(node.span(), itemTy, context.resolve.langAsyncIterator(), {}, node.value->resType, "Item", {});
            const auto& langOption = context.crate.getLangItemPath(node.span(), "Option");
            this->context.equateTypes(node.span(), node.resType, context.crate.types.path(HIRGenericPath(langOption, HIRPathParams(itemTy)), &context.crate.getEnumByPath(node.span(), langOption)));
            return;
        }
        // Require that `return = <[node.value] as `future_trait`>::Output`
        this->context.equateTypesAssoc(node.span(), node.resType, context.resolve.langFuture(), {}, node.value->resType, "Output", {});
    }

    void visit(HIRExprNodeUse& node) override {
        this->context.addIvars(node.value->resType);
        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);
        this->context.equateTypes(node.span(), node.resType, node.value->resType);
    }

    void visit(HIRExprNodeLoop& node) override {
        auto _ = this->pushInnerCoerceScoped(false);
        TRACE_FUNCTION_F(&node << " loop ('" << node.label << ") { ... }");
        // Push this node to a stack so `break` statements can update the yeilded value
        this->loopBlocks.push_back(&node);
        node.diverges = true; // Set to `false` if a break is hit

        this->context.addIvars(node.code->resType);
        this->context.equateTypes(node.span(), node.code->resType, this->context.crate.types.unit());
        node.code->visit(*this);

        this->loopBlocks.pop_back();

        if (node.diverges) {
            // NOTE: This doesn't set the ivar to !, but marks it as a ! ivar (similar to the int/float markers)
            this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
            DEBUG("Loop diverged");
        }
    }

    void visit(HIRExprNodeLoopControl& node) override {
        TRACE_FUNCTION_F(&node << " " << (node.isContinue ? "continue" : "break") << " '" << node.label);
        // Break types
        if (!node.isContinue) {
            HIRExprNodeLoop* loopNodePtr;
            if (node.label != "") {
                auto it = ::std::find_if(this->loopBlocks.rbegin(), this->loopBlocks.rend(), [&](const auto& np) {
                    return np->label == node.label;
                });
                if (it == this->loopBlocks.rend()) {
                    ERROR(node.span(), E0000, "Could not find loop '" << node.label << " for break");
                }
                loopNodePtr = &**it;
            } else {
                loopNodePtr = nullptr;
                for (auto it = this->loopBlocks.rbegin(); it != this->loopBlocks.rend(); ++it) {
                    if (!(*it)->requireLabel) {
                        loopNodePtr = *it;
                        break;
                    }
                }
                if (!loopNodePtr) {
                    ERROR(node.span(), E0000, "Break statement with no active loop");
                }
            }
            node.targetNode = loopNodePtr;

            DEBUG("Break out of loop " << loopNodePtr);
            auto& loopNode = *loopNodePtr;
            loopNode.diverges = false;

            if (node.value) {
                this->context.addIvars(node.value->resType);
                this->pushInnerCoerce(true);
                node.value->visit(*this);
                this->popInnerCoerce();
                this->context.equateTypesCoerce(node.span(), loopNode.resType, node.value);
                this->context.requireSized(node.span(), node.value->resType);
            } else {
                this->context.equateTypes(node.span(), loopNode.resType, this->context.crate.types.unit());
            }
        }
        node.diverges = true;
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
    }

    void visit(HIRExprNodeLet& node) override {
        TRACE_FUNCTION_F(&node << " let " << node.pattern << ": " << node.type);

        this->context.addIvars(node.type);
        this->context.handlePattern(node.span(), node.pattern, node.type, true);

        bool deferResultType = false;
        bool diverges = false;
        if (node.value) {
            this->context.addIvars(node.value->resType);
            // If the type was omitted or was just `_`, equate
            if (node.type->is_Infer()) {
                this->context.equateTypes(node.span(), node.type, node.value->resType);
                this->pushInnerCoerce(true);
            }
            // otherwise coercions apply
            else {
                this->context.equateTypesCoerce(node.span(), node.type, node.value);
                this->pushInnerCoerce(true);
            }

            node.value->visit(*this);
            // No need for `Sized` bound, as it could end up being a `ref` binding
            this->popInnerCoerce();

            const auto* valueType = this->context.getType(node.value->resType);
            const auto* infer = valueType->opt_Infer();
            if (!node.value->diverges && infer && infer->tyClass == HIRInferClass::None) {
                deferResultType = true;
            } else {
                diverges = this->nodeDiverges(*node.value);
            }
        }
        node.diverges = diverges;
        if (deferResultType) {
            this->context.addRevisit(node);
        } else {
            this->context.equateTypes(node.span(), node.resType,
                diverges ? this->context.crate.types.diverge() : this->context.crate.types.unit());
        }
    }

    void visit(HIRExprNodeMatch& node) override {
        TRACE_FUNCTION_F(&node << " match ...");

        auto valType = this->context.ivars.newIvarTr();

        {
            auto _ = this->pushInnerCoerceScoped(true);
            this->context.addIvars(node.value->resType);

            node.value->visit(*this);
            this->inheritDivergence(node, *node.value);
            // TODO: If a coercion point (and ivar for the value) is placed here, it will allow `match &string { "..." ... }`
            // - But, this can break some parts of inferrence
            this->context.equateTypes(node.span(), valType, node.value->resType);
        }

        for (auto& arm : node.arms) {
            TRACE_FUNCTION_F("ARM " << arm.patterns);
            const bool unconditionallySelected = &arm == &node.arms.front()
                && ::std::any_of(arm.patterns.begin(), arm.patterns.end(), [](const HIRPattern& pattern) {
                    return pattern.data.is_Any();
                });
            for (auto& pat : arm.patterns) {
                this->context.handlePattern(node.span(), pat, valType);
            }

            for (auto& c : arm.guards) {
                auto _ = this->pushInnerCoerceScoped(false);
                this->context.addIvars(c.val->resType);

                // Shortcut `if` to avoid the pattern matching complexity
                if (c.isIf) {
                    this->context.equateTypesCoerce(c.val->span(), this->context.crate.types.primitive(HIRCoreType::Bool), c.val);
                    c.val->visit(*this);
                } else {
                    c.val->visit(*this);
                    this->context.handlePattern(node.span(), c.pat, c.val->resType);
                }
                // `if`/`if let` is lowered to `match ()` with a wildcard first
                // arm.  A diverging first guard on that arm is always evaluated;
                // later guards may be skipped when an earlier guard is false.
                if (unconditionallySelected && &c == &arm.guards.front()) {
                    this->inheritDivergence(node, *c.val);
                }
            }

            this->context.addIvars(arm.code->resType);
            this->context.equateTypesCoerce(node.span(), node.resType, arm.code);
            arm.code->visit(*this);
        }

        if (node.arms.empty()) {
            DEBUG("Empty match");
            this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
        }
        // A match always selects an arm, so if every arm diverges so does the
        // match. Record that on the node: an arm's type may still be an
        // inference variable, which never-type fallback later settles on `()`,
        // and the enclosing block would then no longer look diverging.
        else if (::std::all_of(node.arms.begin(), node.arms.end(), [&](const HIRExprNodeMatch::Arm& arm) {
                     return this->nodeDiverges(*arm.code);
                 })) {
            DEBUG("Every arm diverges");
            node.diverges = true;
        }
    }

    void visit(HIRExprNodeAssign& node) override {
        auto _ = this->pushInnerCoerceScoped(false);

        TRACE_FUNCTION_F(&node << "... = ...");
        this->context.addIvars(node.slot->resType);
        this->context.addIvars(node.value->resType);

        // Plain assignment can't be overloaded, requires equal types
        if (node.op == HIRExprNodeAssign::Op::None) {
            this->context.equateTypesCoerce(node.span(), node.slot->resType, node.value);
        } else {
            // Type inferrence using the +=
            // - "" as type name to indicate that it's just using the trait magic?
            const char* langItem = nullptr;
            auto operatorKind = TypeckPrimitiveOperator::None;
            switch (node.op) {
                case HIRExprNodeAssign::Op::None:
                    throw "";
                case HIRExprNodeAssign::Op::Add:
                    langItem = "add_assign";
                    operatorKind = TypeckPrimitiveOperator::AddAssign;
                    break;
                case HIRExprNodeAssign::Op::Sub:
                    langItem = "sub_assign";
                    operatorKind = TypeckPrimitiveOperator::SubAssign;
                    break;
                case HIRExprNodeAssign::Op::Mul:
                    langItem = "mul_assign";
                    operatorKind = TypeckPrimitiveOperator::MulAssign;
                    break;
                case HIRExprNodeAssign::Op::Div:
                    langItem = "div_assign";
                    operatorKind = TypeckPrimitiveOperator::DivAssign;
                    break;
                case HIRExprNodeAssign::Op::Mod:
                    langItem = "rem_assign";
                    operatorKind = TypeckPrimitiveOperator::RemAssign;
                    break;
                case HIRExprNodeAssign::Op::And:
                    langItem = "bitand_assign";
                    operatorKind = TypeckPrimitiveOperator::BitAndAssign;
                    break;
                case HIRExprNodeAssign::Op::Or:
                    langItem = "bitor_assign";
                    operatorKind = TypeckPrimitiveOperator::BitOrAssign;
                    break;
                case HIRExprNodeAssign::Op::Xor:
                    langItem = "bitxor_assign";
                    operatorKind = TypeckPrimitiveOperator::BitXorAssign;
                    break;
                case HIRExprNodeAssign::Op::Shr:
                    langItem = "shr_assign";
                    operatorKind = TypeckPrimitiveOperator::ShrAssign;
                    break;
                case HIRExprNodeAssign::Op::Shl:
                    langItem = "shl_assign";
                    operatorKind = TypeckPrimitiveOperator::ShlAssign;
                    break;
            }
            assert(langItem);
            const auto& traitPath = this->context.crate.getLangItemPathOpt(langItem);

            auto ty = this->context.ivars.newIvarTr();
            this->context.equateTypesCoerce(node.span(), ty, node.value);
            if (!traitPath.components().empty()) {
                this->context.equateTypesAssoc(node.span(), this->context.crate.types.infer(), traitPath, HIRPathParams(mv$(ty)), node.slot->resType, "", {}, true, operatorKind);
            } else if (operatorKind != TypeckPrimitiveOperator::ShlAssign && operatorKind != TypeckPrimitiveOperator::ShrAssign) {
                this->context.equateTypes(node.span(), node.slot->resType, ty);
            }
        }

        node.slot->visit(*this);
        this->inheritDivergence(node, *node.slot);

        auto _2 = this->pushInnerCoerceScoped(node.op == HIRExprNodeAssign::Op::None);
        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);
        this->context.requireSized(node.span(), node.value->resType);

        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.unit());
    }

    void visit(HIRExprNodeBinOp& node) override {
        auto _ = this->pushInnerCoerceScoped(false);

        TRACE_FUNCTION_F(&node << "... " << HIRExprNodeBinOp::opname(node.op) << " ...");

        this->context.addIvars(node.left->resType);
        this->context.addIvars(node.right->resType);

        const auto& leftTy = node.left->resType;
        HIRTypeRef rightTyInner = this->context.ivars.newIvarTr();
        const auto& rightTy = rightTyInner; //node.m_right->m_res_type;
        this->context.equateTypesCoerce(node.span(), rightTyInner, node.right);

        node.left->visit(*this);
        {
            auto _2 = this->pushInnerCoerceScoped(true);
            node.right->visit(*this);
        }

        const bool leftDiverges = this->nodeDiverges(*node.left);
        const bool rightDiverges = this->nodeDiverges(*node.right);
        const bool diverges = leftDiverges
            || (rightDiverges && node.op != HIRExprNodeBinOp::Op::BoolAnd && node.op != HIRExprNodeBinOp::Op::BoolOr);
        node.diverges = diverges;
        const HIRTypeData* operatorResultType = node.resType;
        if (diverges) {
            operatorResultType = this->context.ivars.newIvarTr();
            this->context.equateTypes(node.span(), node.resType, this->context.crate.types.diverge());
        }

        switch (node.op) {
            case HIRExprNodeBinOp::Op::CmpEqu:
            case HIRExprNodeBinOp::Op::CmpNEqu:
            case HIRExprNodeBinOp::Op::CmpLt:
            case HIRExprNodeBinOp::Op::CmpLtE:
            case HIRExprNodeBinOp::Op::CmpGt:
            case HIRExprNodeBinOp::Op::CmpGtE: {
                this->context.equateTypes(node.span(), operatorResultType, this->context.crate.types.primitive(HIRCoreType::Bool));

                const char* itemName = nullptr;
                switch (node.op) {
                    case HIRExprNodeBinOp::Op::CmpEqu:
                        itemName = "eq";
                        break;
                    case HIRExprNodeBinOp::Op::CmpNEqu:
                        itemName = "eq";
                        break;
                    case HIRExprNodeBinOp::Op::CmpLt:
                        itemName = "partial_ord";
                        break;
                    case HIRExprNodeBinOp::Op::CmpLtE:
                        itemName = "partial_ord";
                        break;
                    case HIRExprNodeBinOp::Op::CmpGt:
                        itemName = "partial_ord";
                        break;
                    case HIRExprNodeBinOp::Op::CmpGtE:
                        itemName = "partial_ord";
                        break;
                    default:
                        break;
                }
                assert(itemName);
                const auto& opTrait = this->context.crate.getLangItemPathOpt(itemName);

                auto operatorKind = node.op == HIRExprNodeBinOp::Op::CmpEqu || node.op == HIRExprNodeBinOp::Op::CmpNEqu ? TypeckPrimitiveOperator::Equal : TypeckPrimitiveOperator::Order;
                if (!opTrait.components().empty()) {
                    this->context.equateTypesAssoc(node.span(), this->context.crate.types.infer(), opTrait, HIRPathParams(rightTy), leftTy, "", {}, true, operatorKind);
                } else {
                    this->context.equateTypes(node.span(), leftTy, rightTy);
                }
                break;
            }

            case HIRExprNodeBinOp::Op::BoolAnd:
            case HIRExprNodeBinOp::Op::BoolOr:
                this->context.equateTypes(node.span(), operatorResultType, this->context.crate.types.primitive(HIRCoreType::Bool));
                this->context.equateTypes(node.span(), leftTy, this->context.crate.types.primitive(HIRCoreType::Bool));
                this->context.equateTypes(node.span(), rightTy, this->context.crate.types.primitive(HIRCoreType::Bool));
                break;
            default: {
                const char* itemName = nullptr;
                auto operatorKind = TypeckPrimitiveOperator::None;
                switch (node.op) {
                    case HIRExprNodeBinOp::Op::CmpEqu:
                        throw "";
                    case HIRExprNodeBinOp::Op::CmpNEqu:
                        throw "";
                    case HIRExprNodeBinOp::Op::CmpLt:
                        throw "";
                    case HIRExprNodeBinOp::Op::CmpLtE:
                        throw "";
                    case HIRExprNodeBinOp::Op::CmpGt:
                        throw "";
                    case HIRExprNodeBinOp::Op::CmpGtE:
                        throw "";
                    case HIRExprNodeBinOp::Op::BoolAnd:
                        throw "";
                    case HIRExprNodeBinOp::Op::BoolOr:
                        throw "";

                    case HIRExprNodeBinOp::Op::Add:
                        itemName = "add";
                        operatorKind = TypeckPrimitiveOperator::Add;
                        break;
                    case HIRExprNodeBinOp::Op::Sub:
                        itemName = "sub";
                        operatorKind = TypeckPrimitiveOperator::Sub;
                        break;
                    case HIRExprNodeBinOp::Op::Mul:
                        itemName = "mul";
                        operatorKind = TypeckPrimitiveOperator::Mul;
                        break;
                    case HIRExprNodeBinOp::Op::Div:
                        itemName = "div";
                        operatorKind = TypeckPrimitiveOperator::Div;
                        break;
                    case HIRExprNodeBinOp::Op::Mod:
                        itemName = "rem";
                        operatorKind = TypeckPrimitiveOperator::Rem;
                        break;

                    case HIRExprNodeBinOp::Op::And:
                        itemName = "bitand";
                        operatorKind = TypeckPrimitiveOperator::BitAnd;
                        break;
                    case HIRExprNodeBinOp::Op::Or:
                        itemName = "bitor";
                        operatorKind = TypeckPrimitiveOperator::BitOr;
                        break;
                    case HIRExprNodeBinOp::Op::Xor:
                        itemName = "bitxor";
                        operatorKind = TypeckPrimitiveOperator::BitXor;
                        break;

                    case HIRExprNodeBinOp::Op::Shr:
                        itemName = "shr";
                        operatorKind = TypeckPrimitiveOperator::Shr;
                        break;
                    case HIRExprNodeBinOp::Op::Shl:
                        itemName = "shl";
                        operatorKind = TypeckPrimitiveOperator::Shl;
                        break;
                }
                assert(itemName);
                const auto& opTrait = this->context.crate.getLangItemPathOpt(itemName);

                // NOTE: `true` marks the association as coming from a binary operation, which changes integer handling
                if (!opTrait.components().empty()) {
                    this->context.equateTypesAssoc(node.span(), operatorResultType, opTrait, HIRPathParams(rightTy), leftTy, "Output", {}, true, operatorKind);
                } else {
                    this->context.equateTypes(node.span(), operatorResultType, leftTy);
                    if (operatorKind != TypeckPrimitiveOperator::Shl && operatorKind != TypeckPrimitiveOperator::Shr) {
                        this->context.equateTypes(node.span(), leftTy, rightTy);
                    }
                }
                break;
            }
        }
    }

    void visit(HIRExprNodeUniOp& node) override {
        auto _ = this->pushInnerCoerceScoped(false);

        TRACE_FUNCTION_F(&node << " " << HIRExprNodeUniOp::opname(node.op) << "...");
        this->context.addIvars(node.value->resType);
        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);

        const char* itemName = nullptr;
        auto operatorKind = TypeckPrimitiveOperator::None;
        switch (node.op) {
            case HIRExprNodeUniOp::Op::Invert:
                itemName = "not";
                operatorKind = TypeckPrimitiveOperator::Not;
                break;
            case HIRExprNodeUniOp::Op::Negate:
                itemName = "neg";
                operatorKind = TypeckPrimitiveOperator::Neg;
                break;
        }
        assert(itemName);
        const HIRTypeData* inputType = node.value->resType;
        if (this->context.getType(inputType)->is_Diverge()) {
            const HIRTypeData* expectedType = this->context.coercionHint(node);
            if (!expectedType) {
                expectedType = node.resType;
            }
            expectedType = this->context.getType(expectedType);
            if ((!expectedType->is_Infer() || expectedType->as_Infer().isLit()) && !expectedType->is_Diverge()) {
                inputType = expectedType;
            }
        }
        const auto& opTrait = this->context.crate.getLangItemPathOpt(itemName);
        if (!opTrait.components().empty()) {
            this->context.equateTypesAssoc(node.span(), node.resType, opTrait, HIRPathParams{}, inputType, "Output", {}, true, operatorKind);
        } else {
            this->context.equateTypes(node.span(), node.resType, inputType);
        }
    }

    void visit(HIRExprNodeBorrow& node) override {
        TRACE_FUNCTION_F(&node << " &_ ...");
        this->context.addIvars(node.value->resType);

        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.borrow(node.type, node.value->resType));

        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);
    }

    void visit(HIRExprNodeRawBorrow& node) override {
        TRACE_FUNCTION_F(&node << " &raw _ ...");
        this->context.addIvars(node.value->resType);

        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.pointer(node.type, node.value->resType));

        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);
    }

    void visit(HIRExprNodeCast& node) override {
        auto _ = this->pushInnerCoerceScoped(false);
        this->context.addIvars(node.dstType);

        TRACE_FUNCTION_F(&node << " ... as " << node.dstType);

        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);

        this->context.equateTypes(node.span(), node.resType, node.dstType);
        // TODO: Only revisit if the cast type requires inferring.
        this->context.addRevisit(node);
    }

    void visit(HIRExprNodeUnsize& node) override {
        // _Unsize is emitted for type annotations, and adds a coercion point to its inner
        this->context.addIvars(node.dstType);
        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);

        this->context.equateTypesCoerce(node.value->span(), node.dstType, node.value);
        this->context.equateTypes(node.span(), node.resType, node.dstType);
    }

    void visit(HIRExprNodeIndex& node) override {
        auto _ = this->pushInnerCoerceScoped(false);

        TRACE_FUNCTION_F(&node << " ... [ ... ]");
        this->context.addIvars(node.value->resType);
        node.cache.indexTy = this->context.ivars.newIvarTr();
        this->context.addIvars(node.index->resType);

        node.value->visit(*this);
        node.index->visit(*this);
        this->inheritDivergence(node, *node.value);
        this->inheritDivergence(node, *node.index);
        this->context.equateTypesCoerce(node.index->span(), node.cache.indexTy, node.index);

        this->context.addRevisit(node);
    }

    void visit(HIRExprNodeDeref& node) override {
        auto _ = this->pushInnerCoerceScoped(false);

        TRACE_FUNCTION_F(&node << " *...");
        this->context.addIvars(node.value->resType);

        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);

        // Resolve native dereference versus an overloaded Deref before
        // the enclosing expression's coercion is considered.  A trait
        // target is an output constraint, not an expected result type.
        const auto& ty = this->context.getType(node.value->resType);
        const HIRTypeData* inner = nullptr;
        if (const auto* e = ty->opt_Borrow()) {
            inner = e->inner;
        } else if (const auto* e = ty->opt_Pointer()) {
            inner = e->inner;
        } else if (const auto* e = this->context.resolve.typeIsOwnedBox(node.span(), ty)) {
            inner = e;
        }
        if (!inner) {
            this->context.addRevisit(node);
            return;
        }

        node.traitUsed = HIRExprNodeDeref::TraitUsed::Builtin;
        this->context.equateTypes(node.span(), node.resType, inner);
    }

    void visit(HIRExprNodeEmplace& node) override {
        auto _ = this->pushInnerCoerceScoped(false);
        TRACE_FUNCTION_F(&node << " ... <- ... ");
        this->context.addIvars(node.place->resType);
        this->context.addIvars(node.value->resType);

        node.place->visit(*this);
        this->inheritDivergence(node, *node.place);
        auto _2 = this->pushInnerCoerceScoped(true);
        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);

        this->context.addRevisit(node);
    }

    void addIvarsGenericPath(const Span& sp, HIRGenericPath& gp) {
        for (auto& ty : gp.params.types) {
            this->context.addIvars(ty);
        }
    }

    void addIvarsPath(const Span& sp, HIRPath& path) {
        switch (path.data.tag()) {
            case HIRPath::Data::TAG_Generic: {
                auto& e = path.data.as_Generic();
                this->addIvarsGenericPath(sp, e);
                break;
            }
            case HIRPath::Data::TAG_UfcsKnown: {
                auto& e = path.data.as_UfcsKnown();
                this->context.addIvars(e.type); this->addIvarsGenericPath(sp, e.trait); for (auto& ty : e.params.types) this->context.addIvars(ty);
                break;
            }
            case HIRPath::Data::TAG_UfcsUnknown: {
                TODO(sp, "Hit a UfcsUnknown (" << path << ") - Is this an error?");
                break;
            }
            case HIRPath::Data::TAG_UfcsInherent: {
                auto& e = path.data.as_UfcsInherent();
                this->context.addIvars(e.type); for (auto& ty : e.params.types) this->context.addIvars(ty);
                break;
            }
        }
    }

    HIRTypeRef getStructenumTy(const Span& sp, bool isStruct, HIRGenericPath& gp) {
        if (isStruct) {
            const auto& e = this->context.crate.getTypeitemByPath(sp, gp.path);
            if (e.is_Struct()) {
                const auto& str = e.as_Struct();
                fixParamCount(sp, this->context, HIRTypeRef(), false, gp, str.params, gp.params);

                return this->context.crate.types.path(gp.clone(), HIRTypePathBinding::make_Struct(&str));
            } else if (e.is_Union()) {
                const auto& u = e.as_Union();
                fixParamCount(sp, this->context, HIRTypeRef(), false, gp, u.params, gp.params);

                return this->context.crate.types.path(gp.clone(), HIRTypePathBinding::make_Union(&u));
            } else {
                BUG(sp, "Path " << gp << " doesn't refer to a struct/union");
            }
        } else {
            auto sPath = getRuleParentPath(gp.path);

            const auto& enm = this->context.crate.getEnumByPath(sp, sPath);
            fixParamCount(sp, this->context, HIRTypeRef(), false, gp, enm.params, gp.params);

            return this->context.crate.types.path(HIRGenericPath(mv$(sPath), gp.params.clone()), HIRTypePathBinding::make_Enum(&enm));
        }
    }

    void visit(HIRExprNodeTupleVariant& node) override {
        const auto& sp = node.span();
        TRACE_FUNCTION_F(&node << " " << node.path << "(...) [" << (node.isStruct ? "struct" : "enum") << "]");
        node.diverges = false;
        for (auto& val : node.args) {
            this->context.addIvars(val->resType);
        }
        this->context.ivars.addIvarsParams(node.path.params);

        // - Create ivars in path, and set result type
        const auto ty = this->getStructenumTy(node.span(), node.isStruct, node.path);
        this->context.equateTypes(node.span(), node.resType, ty);

        const tTupleFields* fieldsPtr = nullptr;
        const HIRGenericParams* generics = nullptr;
            {
                auto& tuMatch = ty->as_Path().binding;
                switch (tuMatch.tag()) {
                    case HIRTypePathBinding::TAG_Unbound: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Opaque: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Enum: {
                        auto& e = tuMatch.as_Enum();
                        const auto& varName = node.path.path.components().back();
                        const auto& enm = *e;
                        generics = &enm.params;
                        size_t idx = enm.findVariant(varName);
                        ASSERT_BUG(sp, idx < enm.data.as_Data().size(), "Unknown variant - " << node.path);
                        const auto& varTy = enm.data.as_Data()[idx].type;
                        ASSERT_BUG(sp, varTy->as_Path().binding.is_Struct(), "Pointed variant of TupleVariant (" << node.path << ") isn't a Tuple");
                        ASSERT_BUG(sp, varTy->as_Path().binding.as_Struct() != nullptr, "Pointed variant of TupleVariant (" << node.path << ") isn't a Tuple");
                        const auto& str = *varTy->as_Path().binding.as_Struct();
                        ASSERT_BUG(sp, str.data.is_Tuple(), "Pointed variant of TupleVariant (" << node.path << ") isn't a Tuple");
                        fieldsPtr = &str.data.as_Tuple();
                        break;
                    }
                    case HIRTypePathBinding::TAG_Struct: {
                        auto& e = tuMatch.as_Struct();
                        ASSERT_BUG(sp, e->data.is_Tuple(), "Pointed struct in TupleVariant (" << node.path << ") isn't a Tuple");
                        fieldsPtr = &e->data.as_Tuple();
                        generics = &e->params;
                        break;
                    }
                    case HIRTypePathBinding::TAG_Union: {
                        BUG(sp, "TupleVariant pointing to a union");
                        break;
                    }
                    case HIRTypePathBinding::TAG_ExternType: {
                        BUG(sp, "TupleVariant pointing to a extern type");
                        break;
                    }
                }
            }
            assert(fieldsPtr);
            assert(generics);
            const tTupleFields& fields = *fieldsPtr;
            if( fields.size() != node.args.size() ) {
            ERROR(node.span(), E0000, "Tuple variant constructor argument count doesn't match type - " << node.path);
            }

            auto monomorphCb = MonomorphStatePtr(this->context.crate.types, ty, &node.path.params, nullptr);

            applyBoundsAsRules(this->context, sp, *generics, monomorphCb, /*is_impl_level=*/true);

            // Bind fields with type params (coercable)
            node.argTypes.resize( node.args.size() );
            for( unsigned int i = 0; i < node.args.size(); i ++ )
            {
            const auto& desTyR = fields[i].ent;
            const auto* desTy = &desTyR;
            if (monomorphiseTypeNeeded(desTyR)) {
                node.argTypes[i] = monomorphCb.monomorphType(sp, desTyR);
                desTy = &node.argTypes[i];
            }

            this->context.equateTypesCoerce(node.span(), *desTy, node.args[i]);
            }

            auto _ = this->pushInnerCoerceScoped(true);
            for( auto& val : node.args ) {
            val->visit(*this);
            this->context.requireSized(node.span(), val->resType);
            node.diverges = node.diverges || this->nodeDiverges(*val);
            }
    }

    void visit(HIRExprNodeStructLiteral& node) override {
        const auto& sp = node.span();
        TRACE_FUNCTION_F(&node << " " << node.type << " (" << node.realPath << ") {...} [" << (node.isStruct ? "struct" : "enum") << "]");
        node.diverges = false;
        auto _ = this->pushInnerCoerceScoped(true);

        // Note: This can happen if doing a second pass on a const function (run first time for const eval)
        if (node.realPath == HIRGenericPath()) {
            this->context.addIvars(node.type);
        }

        for (auto& val : node.values) {
            this->context.addIvars(val.second->resType);
        }
        if (node.baseValue) {
            this->context.addIvars(node.baseValue->resType);
        }

        if (node.realPath == HIRGenericPath()) {
            auto t = this->context.resolve.expandAssociatedTypes(sp, mv$(node.type));
            node.type = HIRTypeRef();
            if (node.isStruct) {
                ASSERT_BUG(sp, ((*t).is_Path() && ((*t).as_Path().path.data.is_Generic())), "Struct literal with non-Generic path - " << t);
                node.realPath = t->as_Path().path.data.as_Generic().clone();
            } else {
                ASSERT_BUG(sp, ((*t).is_Path() && ((*t).as_Path().path.data.is_UfcsInherent())), "Enum struct literal with non-UfcsInherent path - " << t);
                auto& it = t->as_Path().path.data.as_UfcsInherent().type;
                auto& name = t->as_Path().path.data.as_UfcsInherent().item;
                ASSERT_BUG(sp, ((*it).is_Path() && ((*it).as_Path().path.data.is_Generic())), "Struct literal with non-Generic path - " << t);
                node.realPath = it->as_Path().path.data.as_Generic().clone();
                node.realPath.path += name;
            }
        }
        auto& tyPath = node.realPath;

        // - Create ivars in path, and set result type
        const auto ty = this->getStructenumTy(node.span(), node.isStruct, tyPath);
        this->context.equateTypes(node.span(), node.resType, ty);
        if (const auto* expectedHint = this->context.coercionHint(node)) {
            const auto* expected = this->context.getType(expectedHint);
            const auto* actualPath = ty->opt_Path();
            const auto* expectedPath = expected->opt_Path();
            if (actualPath && expectedPath && actualPath->path.data.is_Generic() && expectedPath->path.data.is_Generic() && actualPath->path.data.as_Generic().path == expectedPath->path.data.as_Generic().path && ty->compareWithPlaceholders(sp, expected, this->context.ivars.callbackResolveInfer()) != HIRCompare::Unequal) {
                this->context.equateTypes(sp, ty, expected);
            }
        }
        if (node.baseValue) {
            this->context.equateTypes(node.span(), node.baseValue->resType, ty);
        }

        const tStructFields* fieldsPtr = nullptr;
        const HIRGenericParams* generics = nullptr;
        // A tuple struct written as `S { 0: a, ..base }` names its fields by
        // index, so give the tuple fields those names and treat it as any other
        // braced literal. Without a base it is the tuple constructor and never
        // reaches here.
        tStructFields tupleFields;
            {
                auto& tuMatch = ty->as_Path().binding;
                switch (tuMatch.tag()) {
                    case HIRTypePathBinding::TAG_Unbound: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Opaque: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_ExternType: {
                        break;
                    }
                    case HIRTypePathBinding::TAG_Enum: {
                        auto& e = tuMatch.as_Enum();
                        const auto& varName = tyPath.path.components().back();
                        const auto& enm = *e;
                        auto idx = enm.findVariant(varName);
                        ASSERT_BUG(sp, idx != SIZE_MAX, "");
                        ASSERT_BUG(sp, enm.data.is_Data(), "");
                        const auto& var = enm.data.as_Data()[idx];
                        if (var.type == this->context.crate.types.unit()) {
                            ASSERT_BUG(node.span(), node.values.size() == 0, "Values provided for unit-like variant");
                            ASSERT_BUG(node.span(), !node.baseValue, "Values provided for unit-like variant");
                            return;
                        }
                        const auto& str = *var.type->as_Path().binding.as_Struct();

                        ASSERT_BUG(sp, var.isStruct, "Struct literal for enum on non-struct variant");
                        fieldsPtr = &str.data.as_Named();
                        generics = &enm.params;
                        break;
                    }
                    case HIRTypePathBinding::TAG_Union: {
                        auto& e = tuMatch.as_Union();
                        fieldsPtr = &e->variants;
                        generics = &e->params;
                        // Errors are done here, as from_ast may not know yet
                        if (node.baseValue) {
                            ERROR(node.span(), E0000, "Union can't have a base value");
                        }
                        ASSERT_BUG(node.span(), node.values.size() > 0, "Union literal with no values");
                        ASSERT_BUG(node.span(), node.values.size() == 1, "Union literal with multiple values");
                        break;
                    }
                    case HIRTypePathBinding::TAG_Struct: {
                        auto& e = tuMatch.as_Struct();
                        if (e->data.is_Tuple() && !node.values.empty()) {
                            const auto& tuple = e->data.as_Tuple();
                            for (size_t i = 0; i < tuple.size(); i++) {
                                tupleFields.push_back(HIRStructField{RcString::newInterned(FMT(i)), tuple[i].publicity, tuple[i].ent, nullptr});
                            }
                            fieldsPtr = &tupleFields;
                            generics = &e->params;
                            break;
                        }
                        if (e->data.is_Unit() || e->data.is_Tuple()) {
                            ASSERT_BUG(node.span(), node.values.size() == 0, "Values provided for " << e->data.tagStr() << "-like struct");

                            if (node.baseValue) {
                                auto _ = this->pushInnerCoerceScoped(false);
                                node.baseValue->visit(*this);
                                this->inheritDivergence(node, *node.baseValue);
                            }
                            return;
                        }

                        ASSERT_BUG(node.span(), e->data.is_Named(), "StructLiteral not pointing to a braced struct, instead " << e->data.tagStr() << " - " << ty);
                        fieldsPtr = &e->data.as_Named();
                        generics = &e->params;
                        break;
                    }
                }
            }
            ASSERT_BUG(node.span(), fieldsPtr, "");
            assert(generics);
            const tStructFields& fields = *fieldsPtr;

            auto monomorphCb = MonomorphStatePtr(this->context.crate.types, ty, &tyPath.params, nullptr);

            node.valueTypes.resize( fields.size() );

            // Bind fields with type params (coercable)
            for( auto& val : node.values)
            {
            const auto& name = val.first;
            auto it = ::std::find_if(fields.begin(), fields.end(), [&](const HIRStructField& v) -> bool {
                return v.name == name;
            });
            ASSERT_BUG(node.span(), it != fields.end(), "Field '" << name << "' not found in struct " << tyPath);
            const auto& desTyR = it->ty;
            auto& desTyCache = node.valueTypes[it - fields.begin()];
            const auto* desTy = &desTyR;

            DEBUG(name << " : " << desTyR);
            if (monomorphiseTypeNeeded(desTyR)) {
                if (desTyCache == HIRTypeRef()) {
                    desTyCache = monomorphCb.monomorphType(node.span(), desTyR);
                } else {
                    // TODO: Is it an error when it's already populated?
                }
                desTy = &desTyCache;
            }
            this->context.equateTypesCoerce(node.span(), *desTy, val.second);
            }

            // Convert bounds on the type into rules
            applyBoundsAsRules(context, node.span(), *generics, monomorphCb, /*is_impl_level=*/true);

            for( auto& val : node.values ) {
            val.second->visit(*this);
            this->context.requireSized(node.span(), val.second->resType);
            node.diverges = node.diverges || this->nodeDiverges(*val.second);
            }
            if( node.baseValue ) {
            auto _ = this->pushInnerCoerceScoped(false);
            node.baseValue->visit(*this);
            node.diverges = node.diverges || this->nodeDiverges(*node.baseValue);
            }
    }

    void visit(HIRExprNodeUnitVariant& node) override {
        TRACE_FUNCTION_F(&node << " " << node.path << " [" << (node.isStruct ? "struct" : "enum") << "]");

        // - Create ivars in path, and set result type
        const auto ty = this->getStructenumTy(node.span(), node.isStruct, node.path);
        this->context.equateTypes(node.span(), node.resType, ty);

        const HIRGenericParams* generics = nullptr;
        {
            auto& tuMatch = ty->as_Path().binding;
            switch (tuMatch.tag()) {
                case HIRTypePathBinding::TAG_Unbound: {
                    break;
                }
                case HIRTypePathBinding::TAG_Opaque: {
                    break;
                }
                case HIRTypePathBinding::TAG_Enum: {
                    auto& e = tuMatch.as_Enum();
                    generics = &e->params;
                    break;
                }
                case HIRTypePathBinding::TAG_Union: {
                    break;
                }
                case HIRTypePathBinding::TAG_ExternType: {
                    break;
                }
                case HIRTypePathBinding::TAG_Struct: {
                    auto& e = tuMatch.as_Struct();
                    generics = &e->params;
                    break;
                }
            }
        }
        ASSERT_BUG(node.span(), generics, "Unit variant has invalid type " << ty);
        auto monomorph = MonomorphStatePtr(this->context.crate.types, ty, &node.path.params, nullptr);
        applyBoundsAsRules(this->context, node.span(), *generics, monomorph, /*is_impl_level=*/true);
    }

    void visit(HIRExprNodeCallPath& node) override {
        this->visitPath(node.span(), node.path);
        TRACE_FUNCTION_F(&node << " " << node.path << "(...)");
        for (auto& val : node.args) {
            this->context.addIvars(val->resType);
        }

        // Populate cache
        // - If the path is still ambiguous, defer the equates to a revisit rather than aborting.
        const bool cacheOk = visitCallPopulateCache(this->context, node.span(), node.path, node.cache);
        if (cacheOk) {
            assert(node.cache.argTypes.size() >= 1);
            unsigned int expArgc = node.cache.argTypes.size() - 1;

            if (node.args.size() != expArgc) {
                if (node.cache.fcn->variadic && node.args.size() > expArgc) {
                } else {
                    ERROR(node.span(), E0000, "Incorrect number of arguments to " << node.path << " - exp " << expArgc << " got " << node.args.size());
                }
            }

            // TODO: Figure out a way to disable coercions in desugared for loops (will speed up typecheck)

            // Link arguments
            // - NOTE: Uses the cache for the count because vaargs aren't checked (they're checked for suitability in expr_check.cpp)
            for (unsigned int i = 0; i < node.cache.argTypes.size() - 1; i++) {
                this->context.equateTypesCoerce(node.span(), node.cache.argTypes[i], node.args[i]);
            }
            this->context.equateTypes(node.span(), node.resType, node.cache.argTypes.back());
        } else {
            // Ambiguous callee - revisit once inference has progressed.
            this->context.addRevisit(node);
        }

        // Type-check the argument subtrees (independent of the cache).
        {
            auto _ = this->pushInnerCoerceScoped(true);
            for (auto& val : node.args) {
                val->visit(*this);
                this->inheritDivergence(node, *val);
            }
        }
        if (cacheOk) {
            this->context.requireSized(node.span(), node.resType);
        }
    }

    void visit(HIRExprNodeCallValue& node) override {
        TRACE_FUNCTION_F(&node << " ...(...)");
        this->context.addIvars(node.value->resType);
        // Add ivars to node result types and create fresh ivars for coercion targets
        for (auto& val : node.args) {
            this->context.addIvars(val->resType);
            node.argIvars.push_back(this->context.ivars.newIvarTr());
        }

        {
            auto _ = this->pushInnerCoerceScoped(false);
            node.value->visit(*this);
        }
        this->inheritDivergence(node, *node.value);
        auto _ = this->pushInnerCoerceScoped(true);
        for (unsigned int i = 0; i < node.args.size(); i++) {
            auto& val = node.args[i];
            this->context.equateTypesCoerce(val->span(), node.argIvars[i], val);
            val->visit(*this);
            this->inheritDivergence(node, *val);
        }
        this->context.requireSized(node.span(), node.resType);

        // Nothing can be done until type is known
        this->context.addRevisit(node);
    }

    void visit(HIRExprNodeCallMethod& node) override {
        TRACE_FUNCTION_F(&node << " (...)." << node.method << "(...)");
        this->context.addIvars(node.value->resType);
        for (auto& val : node.args) {
            this->context.addIvars(val->resType);
        }
        for (auto& ty : node.params.types) {
            this->context.addIvars(ty);
        }

        // - Search in-scope trait list for traits that provide a method of this name
        const RcString& methodName = node.method;
        tTraitList possibleTraits;
        unsigned int maxNumParams = 0;
        unsigned int maxNumValueParams = 0;
        auto visitTraitInner = [&methodName, &maxNumParams, &maxNumValueParams, &possibleTraits](const HIRSimplePath& p, const HIRTrait& tr, bool push) {
            auto it = tr.values.find(methodName);
            if (it == tr.values.end()) {
                return;
            }
            if (!it->second.is_Function()) {
                return;
            }
            if (tr.params.types.size() > maxNumParams) {
                maxNumParams = tr.params.types.size();
            }
            if (tr.params.values.size() > maxNumValueParams) {
                maxNumValueParams = tr.params.values.size();
            }

            if (push) {
                DEBUG("Found method in " << p << " (push)");
                if (::std::none_of(possibleTraits.begin(), possibleTraits.end(), [&](const auto& x) {
                    return x.second == &tr;
                })) {
                    possibleTraits.push_back(std::make_pair(&p, &tr));
                }
            } else {
                DEBUG("Found method in " << p << " (no push)");
            }
        };
        auto visitTrait = [&visitTraitInner](const HIRSimplePath& p, const HIRTrait& trait) {
            DEBUG("[visit_trait] ? " << p);
            visitTraitInner(p, trait, true);
            for (const auto& pt : trait.allParentTraits) {
                visitTraitInner(pt.path.path, *pt.traitPtr, false);
            }
        };
        for (const auto& traitRef : ::reverse(traits)) {
            if (traitRef.first == nullptr) {
                break;
            }
            visitTrait(*traitRef.first, *traitRef.second);
        }
        if (context.resolve.currentTraitPath()) {
            const HIRSimplePath& tp = context.resolve.currentTraitPath()->path;
            const HIRTrait& tr = context.resolve.hirCrate().getTraitByPath(node.span(), tp);
            visitTrait(tp, tr);
        }
        //  > Store the possible set of traits for later
        node.traits = mv$(possibleTraits);
        node.traitParamTypeIvars = maxNumParams;
        node.traitParamIvars.reserve(maxNumParams + maxNumValueParams);
        for (unsigned int i = 0; i < maxNumParams; i++) {
            node.traitParamIvars.push_back(this->context.ivars.newIvar());
        }
        for (unsigned int i = 0; i < maxNumValueParams; i++) {
            node.traitParamIvars.push_back(this->context.ivars.newIvarVal());
        }

        {
            auto _ = this->pushInnerCoerceScoped(false);
            node.value->visit(*this);
        }
        this->inheritDivergence(node, *node.value);
        auto _ = this->pushInnerCoerceScoped(true);
        for (auto& val : node.args) {
            val->visit(*this);
            this->inheritDivergence(node, *val);
        }
        this->context.requireSized(node.span(), node.resType);

        // Resolution can't be done until lefthand type is known.
        // > Has to be done during iteraton
        this->context.addRevisit(node);
    }

    void visit(HIRExprNodeField& node) override {
        auto _ = this->pushInnerCoerceScoped(false);
        TRACE_FUNCTION_F(&node << " (...)." << node.field);
        this->context.addIvars(node.value->resType);

        node.value->visit(*this);
        this->inheritDivergence(node, *node.value);

        this->context.addRevisit(node);
    }

    void visit(HIRExprNodeTuple& node) override {
        TRACE_FUNCTION_F(&node << " (...,)");
        node.diverges = false;
        for (auto& val : node.vals) {
            this->context.addIvars(val->resType);
        }

        if (canCoerceInnerResult()) {
            DEBUG("Tuple inner coerce");
            const auto& ty = this->context.getType(node.resType);
            if (const auto* e = ty->opt_Tuple()) {
                if (e->size() != node.vals.size()) {
                    ERROR(node.span(), E0000, "Tuple literal node count mismatches with return type");
                }
            } else if (ty->is_Infer()) {
                ::std::vector<HIRTypeRef> tupleTys;
                for (const auto& val : node.vals) {
                    tupleTys.push_back(this->context.ivars.newIvarTr());
                }
                this->context.equateTypes(node.span(), node.resType, this->context.crate.types.tuple(mv$(tupleTys)));
            } else {
                // mismatch
                ERROR(node.span(), E0000, "Tuple literal used where a non-tuple expected - " << ty);
            }
            const auto& innerTys = this->context.getType(node.resType)->as_Tuple();
            assert(innerTys.size() == node.vals.size());

            for (unsigned int i = 0; i < innerTys.size(); i++) {
                this->context.equateTypesCoerce(node.span(), innerTys[i], node.vals[i]);
            }
        } else {
            // No inner coerce, just equate the return type.
            ::std::vector<HIRTypeRef> tupleTys;
            for (const auto& val : node.vals) {
                tupleTys.push_back(val->resType);
            }
            this->context.equateTypes(node.span(), node.resType, this->context.crate.types.tuple(mv$(tupleTys)));
        }

        for (auto& val : node.vals) {
            val->visit(*this);
            this->context.requireSized(node.span(), val->resType);
            node.diverges = node.diverges || this->nodeDiverges(*val);
        }
    }

    void visit(HIRExprNodeArrayList& node) override {
        TRACE_FUNCTION_F(&node << " [...,]");
        node.diverges = false;
        auto _ = this->pushInnerCoerceScoped(true);
        for (auto& val : node.vals) {
            this->context.addIvars(val->resType);
        }

        auto arrayTy = this->context.crate.types.array(context.ivars.newIvarTr(), node.vals.size());
        this->context.equateTypes(node.span(), node.resType, arrayTy);
        // Cleanly equate into array (with coercions)
        const auto& innerTy = arrayTy->as_Array().inner;
        for (auto& val : node.vals) {
            this->equateTypesInnerCoerce(node.span(), innerTy, val);
        }

        for (auto& val : node.vals) {
            val->visit(*this);
            node.diverges = node.diverges || this->nodeDiverges(*val);
        }
    }

    void visit(HIRExprNodeArraySized& node) override {
        TRACE_FUNCTION_F(&node << " [...; " << node.size << "]");
        node.diverges = false;
        this->context.addIvars(node.val->resType);

        // `[val; _]` arrives with an unassigned placeholder: give it an ivar
        // before it is cloned into the (interned, immutable) array type.
        if (node.size.is_Unevaluated()) {
            this->context.ivars.addIvars(node.size.as_Unevaluated());
        }

        // Create result type (can't be known until after const expansion)
        // - Should it be created in const expansion?
        auto ty = this->context.crate.types.array(context.ivars.newIvarTr(), node.size.clone());
        this->context.equateTypes(node.span(), node.resType, ty);
        // Equate with coercions
        const auto& innerTy = ty->as_Array().inner;
        this->equateTypesInnerCoerce(node.span(), innerTy, node.val);

        node.val->visit(*this);
        node.diverges = this->nodeDiverges(*node.val);
        this->context.addRevisit(node);
    }

    void visit(HIRExprNodeLiteral& node) override {
        HIRTypeRef ty;
            switch (node.data.tag()) {
                case HIRExprLiteral::TAG_Integer: {
                    auto& e = node.data.as_Integer();
                    DEBUG("_Literal (: " << e.type << " = " << e.value << ")");
                    if (e.type != HIRCoreType::Str) {
                        ty = this->context.crate.types.primitive(e.type);
                    } else {
                        ty = this->context.crate.types.infer(~0, HIRInferClass::Integer);
                    }
                    break;
                }
                case HIRExprLiteral::TAG_Float: {
                    auto& e = node.data.as_Float();
                    DEBUG("_Literal (: " << node.resType << " = " << e.value << ")");
                    if (e.type != HIRCoreType::Str) {
                        ty = this->context.crate.types.primitive(e.type);
                    } else {
                        ty = this->context.crate.types.infer(~0, HIRInferClass::Float);
                    }
                    break;
                }
                case HIRExprLiteral::TAG_Boolean: {
                    auto& e = node.data.as_Boolean();
                    DEBUG("_Literal ( " << (e ? "true" : "false") << ")");
                    ty = this->context.crate.types.primitive(HIRCoreType::Bool);
                    break;
                }
                case HIRExprLiteral::TAG_String: {
                    // TODO: &'static
                    DEBUG("_Literal (&str)");
                    ty = this->context.crate.types.borrow(HIRBorrowType::Shared, this->context.crate.types.primitive(HIRCoreType::Str));
                    break;
                }
                case HIRExprLiteral::TAG_ByteString: {
                    auto& e = node.data.as_ByteString();
                    // TODO: &'static
                    DEBUG("_Literal (&[u8])");
                    ty = this->context.crate.types.borrow(HIRBorrowType::Shared, this->context.crate.types.array(this->context.crate.types.primitive(HIRCoreType::U8), e.size()));
                    break;
                }
                case HIRExprLiteral::TAG_CString: {
                    DEBUG("_Literal (&CStr)");
                    auto p = context.crate.getLangItemPath(node.span(), "CStr");
                    ty = this->context.crate.types.path(p, &context.crate.getStructByPath(node.span(), p));
                    ty = this->context.crate.types.borrow(HIRBorrowType::Shared, ty);
                    break;
                }
            }
            this->context.addIvars(ty);
            this->context.equateTypes(node.span(), node.resType, ty);
    }

    void visit(HIRExprNodePathValue& node) override {
        const auto& sp = node.span();
        this->visitPath(sp, node.path);
        TRACE_FUNCTION_F(&node << " " << node.path);

        this->addIvarsPath(node.span(), node.path);

            switch (node.path.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    auto& e = node.path.data.as_Generic();
                    switch (node.target) {
                        case HIRExprNodePathValue::UNKNOWN:
                            BUG(sp, "_PathValue with target=UNKNOWN and a Generic path - " << e.path);
                        case HIRExprNodePathValue::FUNCTION: {
                            const auto& f = this->context.crate.getFunctionByPath(sp, e.path);
                            fixParamCount(sp, this->context, HIRTypeRef(), false, e, f.params, e.params);

                            auto ms = MonomorphStatePtr(this->context.crate.types, nullptr, nullptr, &e.params);
                            auto ty = this->context.crate.types.intern(HIRTypeData::make_NamedFunction({node.path.clone(), &f}));

                            // Apply bounds
                            applyBoundsAsRules(this->context, sp, f.params, ms, /*is_impl_level=*/false);

                            DEBUG("> " << node.path << " = " << ty);
                            this->context.equateTypes(sp, node.resType, ty);
                        } break;
                        case HIRExprNodePathValue::STRUCT_CONSTR: {
                            const auto& s = this->context.crate.getStructByPath(sp, e.path);
                            fixParamCount(sp, this->context, HIRTypeRef(), false, e, s.params, e.params);

                            auto ms = MonomorphStatePtr(this->context.crate.types, nullptr, &e.params, nullptr);
                            auto ty = this->context.crate.types.intern(HIRTypeData::make_NamedFunction({node.path.clone(), &s}));

                            applyBoundsAsRules(this->context, sp, s.params, ms, /*is_impl_level=*/true);
                            this->context.equateTypes(sp, node.resType, ty);
                        } break;
                        case HIRExprNodePathValue::ENUM_VAR_CONSTR: {
                            const auto& varName = e.path.components().back();
                            auto enumPath = getRuleParentPath(e.path);
                            const auto& enm = this->context.crate.getEnumByPath(sp, enumPath);
                            fixParamCount(sp, this->context, HIRTypeRef(), false, e, enm.params, e.params);
                            size_t idx = enm.findVariant(varName);
                            ASSERT_BUG(sp, idx != SIZE_MAX, "Missing variant - " << e.path);
                            ASSERT_BUG(sp, enm.data.is_Data(), "Enum " << enumPath << " isn't a data-holding enum");

                            auto ms = MonomorphStatePtr(this->context.crate.types, nullptr, &e.params, nullptr);
                            auto ty = this->context.crate.types.intern(HIRTypeData::make_NamedFunction({node.path.clone(), HIRTypeDataNamedFunctionTy::make_EnumConstructor({&enm, idx})}));
                            applyBoundsAsRules(this->context, sp, enm.params, ms, /*is_impl_level=*/true);
                            this->context.equateTypes(sp, node.resType, ty);
                        } break;
                        case HIRExprNodePathValue::STATIC: {
                            const auto& v = this->context.crate.getStaticByPath(sp, e.path);
                            DEBUG("static v.m_type = " << v.type);
                            this->context.equateTypes(sp, node.resType, v.type);
                        } break;
                        case HIRExprNodePathValue::CONSTANT: {
                            const auto& v = this->context.crate.getConstantByPath(sp, e.path);
                            DEBUG("const" << v.params.fmtArgs() << " v.m_type = " << v.type);
                            fixParamCount(sp, this->context, HIRTypeRef(), false, e, v.params, e.params);

                            auto ms = MonomorphStatePtr(this->context.crate.types, nullptr, nullptr, &e.params);
                            applyBoundsAsRules(this->context, sp, v.params, ms, /*is_impl_level=*/false);

                            HIRTypeRef tmp;
                            const auto& ty = ms.maybeMonomorphType(sp, tmp, v.type);
                            this->context.equateTypes(sp, node.resType, ty);
                        } break;
                    }
                    break;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    BUG(sp, "Encountered UfcsUnknown");
                    break;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    auto& e = node.path.data.as_UfcsKnown();
                    const auto& trait = this->context.crate.getTraitByPath(sp, e.trait.path);
                    fixParamCount(sp, this->context, e.type, true, e.trait, trait.params, e.trait.params);

                    // 1. Add trait bound to be checked.
                    this->context.addTraitBound(sp, e.type, e.trait.path, e.trait.params.clone());

                    // 2. Locate this item in the trait
                    // - If it's an associated `const`, will have to revisit
                    auto it = trait.values.find(e.item);
                    if (it == trait.values.end()) {
                        ERROR(sp, E0000, "`" << e.item << "` is not a value member of trait " << e.trait.path);
                    }
                    switch (it->second.tag()) {
                        case HIRTraitValueItem::TAG_Constant: {
                            auto& ie = it->second.as_Constant();
                            fixParamCount(sp, this->context, e.type, false, node.path, ie.params, e.params);

                            auto ms = MonomorphStatePtr(this->context.crate.types, e.type, &e.trait.params, &e.params);
                            applyBoundsAsRules(this->context, sp, ie.params, ms, /*is_impl_level=*/false);

                            HIRTypeRef tmp;
                            const auto& ty = ms.maybeMonomorphType(sp, tmp, ie.type);
                            this->context.equateTypes(sp, node.resType, ty);
                            break;
                        }
                        case HIRTraitValueItem::TAG_Static: {
                            auto& ie = it->second.as_Static();
                            TODO(sp, "Monomorpise associated static type - " << ie.type);
                            break;
                        }
                        case HIRTraitValueItem::TAG_Function: {
                            auto& ie = it->second.as_Function();
                            fixParamCount(sp, this->context, e.type, false, node.path, ie.params, e.params);

                            auto ms = MonomorphStatePtr(this->context.crate.types, e.type, &e.trait.params, &e.params);
                            applyBoundsAsRules(this->context, sp, ie.params, ms, /*is_impl_level=*/false);

                            auto ty = this->context.crate.types.intern(HIRTypeData::make_NamedFunction({node.path.clone(), &ie}));
                            this->context.equateTypes(node.span(), node.resType, ty);
                            break;
                        }
                    }
                    break;
                }
                case HIRPathData::TAG_UfcsInherent: {
                    auto& e = node.path.data.as_UfcsInherent();
                    // TODO: Share code with visit_call_populate_cache

                    // - Locate function (and impl block)
                    const HIRFunction* fcnPtr = nullptr;
                    const HIRConstant* constPtr = nullptr;
                    const HIRTypeImpl* implPtr = nullptr;
                    // TODO: Support mutiple matches here (if there's a fuzzy match) and retry if so
                    unsigned int count = 0;
                    this->context.crate.findTypeImpls(e.type, context.ivars.callbackResolveInfer(), [&](const auto& impl) {
                        DEBUG("- impl" << impl.params.fmtArgs() << " " << impl.type);
                        {
                            auto it = impl.methods.find(e.item);
                            if (it != impl.methods.end()) {
                                fcnPtr = &it->second.data;
                                implPtr = &impl;
                                count += 1;
                                return false;
                            }
                        }
                        {
                            auto it = impl.constants.find(e.item);
                            if (it != impl.constants.end()) {
                                constPtr = &it->second.data;
                                implPtr = &impl;
                                count += 1;
                                return false;
                            }
                        }
                        return false;
                    });
                    if (count == 0) {
                        ERROR(sp, E0000, "Failed to locate associated value " << node.path);
                    }
                    if (count > 1) {
                        TODO(sp, "Revisit _PathValue when UfcsInherent has multiple options - " << node.path);
                    }

                    assert(fcnPtr || constPtr);
                    assert(implPtr);

                    if (fcnPtr) {
                        fixParamCount(sp, this->context, e.type, false, node.path, fcnPtr->params, e.params);
                    } else {
                        fixParamCount(sp, this->context, e.type, false, node.path, constPtr->params, e.params);
                    }

                    // If the impl block has parameters, figure out what types they map to
                    // - The function params are already mapped (from fix_param_count)
                    auto& implParams = e.implParams;
                    if (implPtr->params.isGeneric()) {
                        while (implParams.types.size() < implPtr->params.types.size()) {
                            implParams.types.push_back(this->context.crate.types.infer());
                        }
                        implParams.values.resize(implPtr->params.values.size());
                        OwnedImplMatcher matcher(implParams);
                        // NOTE: Could be fuzzy.
                        bool r = implPtr->type->matchTestGenerics(sp, e.type, this->context.ivars.callbackResolveInfer(), matcher);
                        for (auto& ty : implParams.types) {
                            // Create new ivars if there's holes
                            if (ty->is_Infer() && ty->as_Infer().index == ~0u) {
                                this->context.addIvars(ty);
                            }
                        }
                        if (!r) {
                            auto t = MonomorphStatePtr(this->context.crate.types, nullptr, &implParams, nullptr).monomorphType(sp, implPtr->type);
                            this->context.equateTypes(node.span(), t, e.type);
                        }
                    }

                    if (fcnPtr) {
                        // Create monomorphise callback
                        const auto& fcnParams = e.params;
                        // TODO: call `context.get_type` in this?
                        auto ms = MonomorphStatePtr(this->context.crate.types, e.type, &implParams, &fcnParams);

                        // Bounds (both impl and fn)
                        applyBoundsAsRules(this->context, sp, implPtr->params, ms, /*is_impl_level=*/true);
                        applyBoundsAsRules(this->context, sp, fcnPtr->params, ms, /*is_impl_level=*/false);

                        auto ty = this->context.crate.types.intern(HIRTypeData::make_NamedFunction({node.path.clone(), fcnPtr}));
                        this->context.equateTypes(node.span(), node.resType, ty);
                    } else // !fcn_ptr, ergo const_ptr
                    {
                        assert(constPtr);
                        auto monomorphCb = MonomorphStatePtr(this->context.crate.types, e.type, &implParams, &e.params);

                        HIRTypeRef tmp;
                        const auto& ty = monomorphCb.maybeMonomorphType(sp, tmp, constPtr->type);

                        applyBoundsAsRules(this->context, sp, implPtr->params, monomorphCb, /*is_impl_level=*/true);
                        applyBoundsAsRules(this->context, sp, constPtr->params, monomorphCb, /*is_impl_level=*/false);
                        this->context.equateTypes(node.span(), node.resType, ty);
                    }
                    break;
                }
            }
    }

    void visit(HIRExprNodeVariable& node) override {
        TRACE_FUNCTION_F(&node << " " << node.name << "{" << node.slot << "}");

        this->context.equateTypes(node.span(), node.resType, this->context.getVar(node.span(), node.slot));
    }

    void visit(HIRExprNodeConstParam& node) override {
        TRACE_FUNCTION_F(&node << " " << node.name << "{" << node.binding << "}");

        this->context.equateTypes(node.span(), node.resType, this->context.resolve.getConstParamType(node.span(), node.binding));
    }

    void visit(HIRExprNodeClosure& node) override {
        TRACE_FUNCTION_F(&node << " |...| ...");
        for (auto& arg : node.args) {
            this->context.addIvars(arg.second);
            this->context.handlePattern(node.span(), arg.first, arg.second);
        }
        this->context.addIvars(node.returnType);
        this->context.addIvars(node.code->resType);

        // Closure result type
        ::std::vector<HIRTypeRef> argTypes;
        for (auto& arg : node.args) {
            argTypes.push_back(arg.second);
        }
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.closure(&node));

        this->context.equateTypesCoerce(node.span(), node.returnType, node.code);

        // Save/clear/restore loop labels
        auto savedLoops = ::std::move(this->loopBlocks);

        auto _ = this->pushInnerCoerceScoped(true);
        this->closureRetTypes.push_back(RetTarget(node.returnType));
        node.code->visit(*this);
        this->closureRetTypes.pop_back();

        this->loopBlocks = ::std::move(savedLoops);
    }

    void visit(HIRExprNodeGenerator& node) override {
        TRACE_FUNCTION_F(&node << " /*gen*/ || ...");
        this->context.addIvars(node.returnType);
        this->context.addIvars(node.yieldTy);
        this->context.addIvars(node.resumeTy);
        this->context.addIvars(node.code->resType);
        if (node.hasResumePattern) {
            this->context.handlePattern(node.span(), node.resumePattern, node.resumeTy);
        }

        // Generator result type
        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.generator(&node));

        this->context.equateTypesCoerce(node.span(), node.returnType, node.code);
        // TODO: Save/clear/restore loop labels
        auto _ = this->pushInnerCoerceScoped(true);
        this->closureRetTypes.push_back(RetTarget(node.returnType, node.resumeTy, node.yieldTy));
        node.code->visit(*this);
        this->closureRetTypes.pop_back();
    }

    void visit(HIRExprNodeGeneratorWrapper& node) override {
        BUG(node.span(), "ExprNode_GeneratorWrapper unexpected at this time");
    }

    void visit(HIRExprNodeAsyncBlock& node) override {
        TRACE_FUNCTION_F(&node << " async { ... }");
        ASSERT_BUG(node.span(), node.code, "empty async?");
        node.returnType = this->context.revealOpaqueType(node.returnType);
        this->context.addIvars(node.returnType);
        this->context.addIvars(node.code->resType);

        this->context.equateTypes(node.span(), node.resType, this->context.crate.types.asyncBlock(&node));
        this->context.equateTypesCoerce(node.span(), node.returnType, node.code);

        // TODO: Save/clear/restore loop labels
        auto _ = this->pushInnerCoerceScoped(true);
        if (node.isAsyncGen) {
            // A `yield` hands out an item and evaluates to `()`.
            this->context.addIvars(node.yieldTy);
            this->closureRetTypes.push_back(RetTarget(node.returnType, this->context.crate.types.unit(), node.yieldTy));
        } else {
            this->closureRetTypes.push_back(RetTarget(node.returnType));
        }
        node.code->visit(*this);
        this->closureRetTypes.pop_back();
    }

private:
    bool nodeDiverges(const HIRExprNode& node) const {
        return node.diverges || this->context.getType(node.resType)->is_Diverge();
    }

    void inheritDivergence(HIRExprNode& node, const HIRExprNode& child) const {
        node.diverges = node.diverges || this->nodeDiverges(child);
    }

    void pushTraits(const tTraitList& list) {
        this->traits.insert(this->traits.end(), list.begin(), list.end());
    }

    void popTraits(const tTraitList& list) {
        this->traits.erase(this->traits.end() - list.size(), this->traits.end());
    }

    void visitGenericPath(const Span& sp, HIRGenericPath& gp) {
        for (auto& ty : gp.params.types) {
            this->context.addIvars(ty);
        }
    }

    void visitPath(const Span& sp, HIRPath& path) {
        switch (path.data.tag()) {
            case HIRPath::Data::TAG_Generic: {
                auto& e = path.data.as_Generic();
                this->visitGenericPath(sp, e);
                break;
            }
            case HIRPath::Data::TAG_UfcsKnown: {
                auto& e = path.data.as_UfcsKnown();
                this->context.addIvars(e.type); this->visitGenericPath(sp, e.trait); for (auto& ty : e.params.types) this->context.addIvars(ty);
                break;
            }
            case HIRPath::Data::TAG_UfcsUnknown: {
                TODO(sp, "Hit a UfcsUnknown (" << path << ") - Is this an error?");
                break;
            }
            case HIRPath::Data::TAG_UfcsInherent: {
                auto& e = path.data.as_UfcsInherent();
                this->context.addIvars(e.type); for (auto& ty : e.params.types) this->context.addIvars(ty);
                break;
            }
        }
    }

    class InnerCoerceGuard {
        ExprVisitorEnum& t;

    public:
        InnerCoerceGuard(ExprVisitorEnum& t)
            : t(t)
        {
        }

        ~InnerCoerceGuard() {
            t.innerCoerceEnabledStack.pop_back();
            DEBUG("inner_coerce POP (S) " << t.canCoerceInnerResult());
        }
    };

    InnerCoerceGuard pushInnerCoerceScoped(bool val) {
        DEBUG("inner_coerce PUSH (S) " << val);
        this->innerCoerceEnabledStack.push_back(val);
        return InnerCoerceGuard(*this);
    }

    void pushInnerCoerce(bool val) {
        DEBUG("inner_coerce PUSH " << val);
        this->innerCoerceEnabledStack.push_back(val);
    }

    void popInnerCoerce() {
        assert(this->innerCoerceEnabledStack.size());
        this->innerCoerceEnabledStack.pop_back();
        DEBUG("inner_coerce POP " << canCoerceInnerResult());
    }

    bool canCoerceInnerResult() const {
        if (this->innerCoerceEnabledStack.size() == 0) {
            return true;
        } else {
            return this->innerCoerceEnabledStack.back();
        }
    }

    void equateTypesInnerCoerce(const Span& sp, const HIRTypeData* target, HIRExprNodeP& node) {
        DEBUG("can_coerce_inner_result() = " << canCoerceInnerResult());
        if (canCoerceInnerResult()) {
            this->context.equateTypesCoerce(sp, target, node);
        } else {
            this->context.equateTypes(sp, target, node->resType);
        }
    }
};

void TypecheckCodeCSEnumerateRules(Context& context, const TypeckModuleState& ms, tArgs& args, const HIRTypeData* resultType, HIRExprPtr& expr, HIRExprNodeP& rootPtr) {
    TRACE_FUNCTION;

    const Span& sp = rootPtr->span();

    DEBUG("args = " << args);
    DEBUG("result_type = " << resultType);
    for (auto& arg : args) {
        context.handlePattern(Span(), arg.first, arg.second);
    }

    struct M: public Monomorphiser {
        Context& context;
        HIRExprPtr& expr;
        mutable const HIRTypeData* curSelf;
        mutable const HIRPathParams* hrls;

        M(Context& context, HIRExprPtr& expr)
            : Monomorphiser(context.crate.types)
            , context(context)
            , expr(expr)
            , curSelf(nullptr)
            , hrls(nullptr)
        {
        }

        HIRTypeRef getType(const Span& sp, const HIRGenericRef& g) const override {
            if (g.binding == GENERICErasedSelf && curSelf) {
                return curSelf;
            }
            return context.crate.types.generic(g.name, g.binding);
        }

        HIRConstGeneric getValue(const Span& sp, const HIRGenericRef& g) const override {
            return g;
        }

        HIRTypeRef monomorphType(const Span& sp, const HIRTypeData* tpl, bool allowInfer = true) const override {
            if (const auto* e = tpl->opt_ErasedType()) {
                if (const auto* ee = e->inner.opt_Fcn()) {
                    // NOTE: `Typecheck Outer` visits erased types subtly differently (it recurses then handles)
                    // - This code handles then recurses (as the return needs to be allocated earlier)
                    // SO: We have to expand the list as it comes.
                    if (expr.erasedTypes.size() <= ee->index) {
                        expr.erasedTypes.resize(ee->index + 1);
                    }
                    ASSERT_BUG(sp, expr.erasedTypes[ee->index] == HIRTypeRef(), "Multiple-visits to erased type #" << ee->index);
                    expr.erasedTypes[ee->index] = context.ivars.newIvarTr();
                    auto rv = expr.erasedTypes[ee->index];
                    context.addRpitType(ee->origin, ee->index, rv);

                    auto prevCurSelf = this->curSelf;
                    this->curSelf = rv;
                    DEBUG(tpl << " -> " << rv);

                    auto prevHrls = this->hrls;
                    for (const auto& trait : e->traits) {
                        auto ppHrl = HIRPathParams();
                        this->hrls = &ppHrl;
                        if (trait.typeBounds.size() == 0) {
                            context.equateTypesAssoc(sp, context.crate.types.infer(), trait.path.path, this->monomorphPathParams(sp, trait.path.params, allowInfer), rv, "", {}, false);
                        } else {
                            for (const auto& aty : trait.typeBounds) {
                                auto atyCloned = this->monomorphType(sp, aty.second.type);
                                auto params = this->monomorphPathParams(sp, trait.path.params, allowInfer);
                                context.equateTypesAssoc(sp, std::move(atyCloned), trait.path.path, std::move(params), rv, aty.first.c_str(), aty.second.atyParams, false);
                            }
                        }
                        this->hrls = nullptr;
                    }

                    this->hrls = prevHrls;
                    this->curSelf = prevCurSelf;

                    return rv;
                }
            }

            return Monomorphiser::monomorphType(sp, tpl, allowInfer);
        }
    };

    // If the result type contans an erased type, replace that with a new ivar and emit trait bounds for it.
    HIRTypeRef newResTy = resultType
        ? M(context, expr).monomorphType(sp, resultType)
        : context.ivars.newIvarTr();
    // - Final check to ensure that all erased type indexes are visited
    for (size_t i = 0; i < expr.erasedTypes.size(); i++) {
        ASSERT_BUG(sp, expr.erasedTypes[i] != HIRTypeRef(), "Non-visited erased type #" << i);
    }

    if (true) {
        DEBUG("--- Pre-adding ivars");
        ExprVisitorAddIvars visitor(context);
        context.addIvars(rootPtr->resType);
        rootPtr->visit(visitor);
    }

    DEBUG("--- Enumerating");
    context.recordCoercionHint(newResTy, rootPtr);
    ExprVisitorEnum visitor(context, ms.traits, newResTy);
    context.addIvars(rootPtr->resType);
    rootPtr->visit(visitor);

    DEBUG("Return type = " << newResTy << ", root_ptr = " << rootPtr->typeName() << " " << rootPtr->resType);
    context.equateTypesCoerce(sp, newResTy, rootPtr);
}

Context::IVarPossible::CoerceTy::CoerceTy(HIRTypeRef ty, bool isCoerce)
    : op(isCoerce ? Coercion : Unsizing)
    , ty(ty)
{
}

void Context::IVarPossible::reset() {
    // Manually clear, to avoid needing to reallocate the lists all the time.
    this->forceDisable = false;
    this->forceNoTo = false;
    this->forceNoFrom = false;
    this->typesCoerceTo.clear();
    this->typesCoerceFrom.clear();
    this->hasBounded = false;
    this->boundsIncludeSelf = false;
    this->bounded.clear();
}

bool Context::IVarPossible::hasRules() const {
    if (forceDisable) {
        return true;
    }
    if (forceNoTo || !typesCoerceTo.empty()) {
        return true;
    }
    if (forceNoFrom || !typesCoerceFrom.empty()) {
        return true;
    }
    //if( !types_default.empty() )
    if (hasBounded) {
        return true;
    }
    return false;
}

void Context::IVarPossible::mergeFrom(const IVarPossible& source) {
    forceDisable |= source.forceDisable;
    forceNoTo |= source.forceNoTo;
    forceNoFrom |= source.forceNoFrom;

    auto mergeCoercions = [](auto& destination, const auto& values) {
        for (const auto& value : values) {
            const auto found = ::std::find_if(destination.begin(), destination.end(), [&](const auto& existing) {
                return existing.op == value.op && existing.ty == value.ty;
            });
            if (found == destination.end()) {
                destination.push_back(value);
            }
        }
    };
    mergeCoercions(typesCoerceTo, source.typesCoerceTo);
    mergeCoercions(typesCoerceFrom, source.typesCoerceFrom);
    typesDefault.insert(source.typesDefault.begin(), source.typesDefault.end());

    if (!source.hasBounded) {
        return;
    }
    if (!hasBounded) {
        hasBounded = true;
        boundsIncludeSelf = source.boundsIncludeSelf;
        bounded = source.bounded;
        return;
    }

    boundsIncludeSelf |= source.boundsIncludeSelf;
    if (boundsIncludeSelf) {
        for (const auto type : source.bounded) {
            if (::std::find(bounded.begin(), bounded.end(), type) == bounded.end()) {
                bounded.push_back(type);
            }
        }
    } else {
        bounded.erase(
            ::std::remove_if(
                bounded.begin(),
                bounded.end(),
                [&](const auto type) {
            return ::std::find(source.bounded.begin(), source.bounded.end(), type) == source.bounded.end();
        }
            ),
            bounded.end()
        );
    }
}

Context::TaitEntry::TaitEntry(const HIRPathParams& p, HIRTypeRef t)
    : params(p.clone())
    , ourType(std::move(t))
{
}

Context::Context(const WireBoard& wb, const HIRGenericParams* implParams, const HIRGenericParams* itemParams, const HIRSimplePath& modPath, const HIRGenericPath* currentTrait, const HIRTraitImpl* currentTraitImpl)
    : crate(*wb.crate)
    , currentTraitImpl(currentTraitImpl)
    , ivars(wb.crate->types)
    , resolve(ivars, wb, implParams, itemParams, modPath, currentTrait)
    , nextRuleIdx(0)
    , langBox(crate.getLangItemPathOpt("owned_box"))
{
    if (currentTraitImpl) {
        for (const auto& entry : currentTraitImpl->types) {
            visitTyWith(entry.second.data, [&](const HIRTypeData* type) {
                const auto* erased = type->opt_ErasedType();
                const auto* alias = erased ? erased->inner.opt_Alias() : nullptr;
                if (alias && alias->inner->path.components().back().c_str()[0] == '#') {
                    resolve.addDefiningOpaqueAlias(alias->inner->path);
                }
                return false;
            });
        }
    }
    resolve.setInherentTypeConstraint([this](const Span& sp, const HIRTypeData* receiver, const HIRTypeData* implType) {
        this->equateTypesInner(sp, receiver, implType);
    });
}

namespace {
    class RpitOriginMonomorph: public HIRMatchGenerics, public Monomorphiser {
        ::std::map<u32, HIRTypeRef> typeBindings;
        ::std::map<u32, HIRConstGeneric> valueBindings;

    public:
        explicit RpitOriginMonomorph(HIRTypeInterner& types)
            : Monomorphiser(types)
        {
        }

        HIRCompare matchTy(const HIRGenericRef& generic, const HIRTypeData* type, tCbResolveType resolve) override {
            type = resolve.getType(Span(), type);
            auto inserted = typeBindings.emplace(generic.binding, type);
            if (inserted.second) {
                return HIRCompare::Equal;
            }
            return inserted.first->second->compareWithPlaceholders(Span(), type, resolve);
        }

        HIRCompare matchVal(const HIRGenericRef& generic, const HIRConstGeneric& value) override {
            auto inserted = valueBindings.emplace(generic.binding, value.clone());
            return inserted.second || inserted.first->second == value ? HIRCompare::Equal : HIRCompare::Unequal;
        }

        HIRTypeRef getType(const Span&, const HIRGenericRef& generic) const override {
            const auto it = typeBindings.find(generic.binding);
            return it == typeBindings.end() ? types.generic(generic.name, generic.binding) : it->second;
        }

        HIRConstGeneric getValue(const Span&, const HIRGenericRef& generic) const override {
            const auto it = valueBindings.find(generic.binding);
            return it == valueBindings.end() ? HIRConstGeneric::make_Generic(generic) : it->second.clone();
        }
    };
}

const HIRTypeData* Context::revealOpaqueType(const HIRTypeData* type) const {
    type = ivars.getType(type);
    const size_t maxDepth = erasedTypeAliases.size() + rpitTypes.size();
    for (size_t depth = 0; depth < maxDepth; depth++) {
        const HIRTypeData* hiddenType = nullptr;
        auto revealRpit = [&](const HIRPath& origin, unsigned int index) {
            for (const auto& entry : rpitTypes) {
                if (entry.index != index) {
                    continue;
                }
                RpitOriginMonomorph monomorph(crate.types);
                if (monomorph.cmpPath(Span(), *entry.origin, origin, ivars.callbackResolveInfer()) != HIRCompare::Unequal) {
                    hiddenType = monomorph.monomorphType(Span(), ivars.getType(entry.ourType));
                    break;
                }
            }
        };

        if (const auto* erased = type->opt_ErasedType()) {
            if (const auto* alias = erased->inner.opt_Alias()) {
                if (!resolve.isOpaqueAliasDefiningScope(*alias->inner)) {
                    return type;
                }
                const auto it = erasedTypeAliases.find(alias->inner.get());
                if (it != erasedTypeAliases.end()) {
                    hiddenType = it->second.ourType;
                }
            } else if (const auto* fcn = erased->inner.opt_Fcn()) {
                revealRpit(fcn->origin, fcn->index);
            }
        } else if (const auto* path = type->opt_Path()) {
            if (const auto* projection = path->path.data.opt_UfcsKnown()) {
                for (const auto& entry : rpitTypes) {
                    const auto* origin = entry.origin->data.opt_UfcsKnown();
                    if (!origin) {
                        continue;
                    }
                    const auto expectedName = RcString::newInterned(FMT(ATY_PREFIX_ERASED << origin->item << "_" << entry.index));
                    if (projection->item != expectedName) {
                        continue;
                    }
                    HIRPath projectedOrigin(projection->type, projection->trait.clone(), origin->item, projection->params.clone());
                    revealRpit(projectedOrigin, entry.index);
                    if (hiddenType) {
                        break;
                    }
                }
            }
        } else {
            return type;
        }
        if (!hiddenType) {
            return type;
        }
        const auto* revealed = ivars.getType(hiddenType);
        if (revealed == type) {
            return type;
        }
        type = revealed;
    }
    return type;
}

void Context::addRpitType(const HIRPath& origin, unsigned int index, HIRTypeRef type) {
    for (const auto& entry : rpitTypes) {
        if (entry.index == index && *entry.origin == origin) {
            ASSERT_BUG(Span(), entry.ourType == type, "RPIT hidden type registered twice for " << origin << "#" << index);
            return;
        }
    }
    rpitTypes.push_back(RpitEntry{&origin, index, type});
}

const HIRTypeData* Context::coercionHint(const HIRExprNode& node) const {
    const auto it = coercionHints.find(&node);
    return it == coercionHints.end() ? nullptr : it->second;
}

bool Context::isCurrentOperatorImpl(const ImplRef& impl) const {
    const auto* traitImpl = impl.data.opt_TraitImpl();
    return currentTraitImpl && traitImpl && traitImpl->impl == currentTraitImpl;
}

::std::ostream& operator<<(::std::ostream& os, const Context::Coercion& v) {
    os << "R" << v.ruleIdx << " " << v.leftTy << " := " << v.rightNodePtr << " " << &**v.rightNodePtr << " (" << (*v.rightNodePtr)->resType << ")";
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const Context::Associated& v) {
    os << "R" << v.ruleIdx << " ";
    if (v.name == "") {
        os << "req ty " << v.implTy << " impl " << v.trait << v.params;
    } else {
        os << v.leftTy << " = " << "< `" << v.implTy << "` as `" << v.trait << v.params << "` >::" << v.name << v.atyPp;
    }
    if (v.isOperator) {
        os << " - op";
    }
    return os;
}
