#include "mir_operations.h"
#include "mir_operations.h"

#include "mir_mir.h"
#include "hir_expr.h"
#include "wire_board.h"
#include "hir_visitor.h"
#include "mir_helpers.h"
#include "trans_target.h"
#include "trans_trans_list.h"
#include "hir_typeck_static.h"
#include "mir_main_bindings.h"
#include "mir_visit_crate_mir.h"
#include "hir_conv_main_bindings.h"
#include "hir_conv_constant_evaluation.h"

#include <std/alg/defer.h>

#include <cmath>
#include <limits>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace stl;

struct WireBoard::MirOperationsContext {
    const RcString vtableName = RcString::newInterned("vtable#");
    std::vector<bool> visitedBlocks;
    std::vector<MIRBasicBlockId> pendingBlocks;
    std::vector<std::vector<unsigned>> blockPredecessors;
    bool visitingBlocks = false;
};

namespace {
    using MirOperationsContext = WireBoard::MirOperationsContext;

    struct MirMutator {
        MIRFunction& fcn;
        unsigned int curBlock;
        unsigned int curStmt;
        mutable std::vector<MIRStatement> newStatements;

        MirMutator(MIRFunction& fcn, unsigned int bb, unsigned int stmt);

        void updateState(MIRTypeResolve& state);

        MIRLValue newTemporary(HIRTypeRef ty);

        void pushStatement(MIRStatement stmt);

        MIRLValue inTemporary(HIRTypeRef ty, MIRRValue val);

        decltype(newStatements.begin()) flushStmt();

        void flushBlock();

        decltype(newStatements.begin()) flush();
    };

    struct LvalueVisitor {
        virtual bool visitLvalue(const MIRLValue& lv, MIRValUsage u) = 0;
    };

    struct LvalueVisitorMut {
        virtual bool visitLvalue(MIRLValue& lv, MIRValUsage u) = 0;
    };

    struct LvalueRefVisitorMut {
        virtual bool visitLvalue(MIRLValue::MRef& lv, MIRValUsage u) = 0;
    };

    struct LvalueConstAdapter final: public LvalueVisitorMut {
        LvalueVisitor& inner;

        explicit LvalueConstAdapter(LvalueVisitor& inner);

        bool visitLvalue(MIRLValue& lv, MIRValUsage u) override;
    };

    struct ParamsSet: public MonomorphiserPP {
        HIRPathParams implParams;
        const HIRPathParams* fcnParams;
        const HIRTypeData* selfTy;
        const HIRGenericParams* implParamsDef;
        const HIRGenericParams* fcnParamsDef;

        HIRPathParams fcnParamsTmp;

        explicit ParamsSet(HIRTypeInterner& types);

        const HIRTypeData* getSelfType() const override;

        const HIRPathParams* getImplParams() const override;

        const HIRPathParams* getMethodParams() const override;

        const HIRPathParams* getHrbParams() const override;

        bool hasUnevaluatedValues() const;
    };

    struct MIRBlockCallback {
        virtual void run(MIRBasicBlockId bb, MIRBasicBlock& block) const = 0;
    };

    template <typename F>
    struct MIRBlockCb final: MIRBlockCallback {
        F f;

        explicit MIRBlockCb(F f);

        void run(MIRBasicBlockId bb, MIRBasicBlock& block) const override;
    };

    struct MIRBlockConstCallback {
        virtual void run(MIRBasicBlockId bb, const MIRBasicBlock& block) const = 0;
    };

    template <typename F>
    struct MIRBlockConstCb final: MIRBlockConstCallback {
        F f;

        explicit MIRBlockConstCb(F f);

        void run(MIRBasicBlockId bb, const MIRBasicBlock& block) const override;
    };

    struct OptimiseStmtRef {
        unsigned bbIdx;
        unsigned stmtIdx;

        OptimiseStmtRef();

        OptimiseStmtRef(unsigned b, unsigned s);

        bool operator==(const OptimiseStmtRef& x) const;
    };

    struct IterPathCallback {
        virtual bool visitStatement(OptimiseStmtRef location, const MIRStatement& statement) = 0;
        virtual bool visitTerminator(OptimiseStmtRef location, const MIRTerminator& terminator) = 0;
    };

    template <typename S, typename T>
    struct IterPathCb final: IterPathCallback {
        S statement;
        T terminator;

        IterPathCb(S statement, T terminator);

        bool visitStatement(OptimiseStmtRef location, const MIRStatement& value) override;

        bool visitTerminator(OptimiseStmtRef location, const MIRTerminator& value) override;
    };

    struct CheckInvalidatesLvalue final: public LvalueVisitor {
        const MIRLValue& val;
        bool hasIndex;
        bool isCopy;
        bool alsoRead;

        CheckInvalidatesLvalue(const MIRLValue& val, bool isCopy, bool alsoRead = false);

        bool visitLvalue(const MIRLValue& lv, MIRValUsage vu) override;
    };

    MirOperationsContext& operationsContext(const MIRTypeResolve& state) {
        return *state.resolve.board().mirOperations;
    }

    HIRTypeRef getMetadataType(const MIRTypeResolve& state, const HIRTypeData* unsizedTy) {
        Span sp;
        auto& types = state.crate.types;
        if (const auto* tep = unsizedTy->opt_TraitObject()) {
            const auto& traitPath = tep->trait;

            if (traitPath.path.path == HIRSimplePath()) {
                return types.unit();
            } else {
                const auto& trait = *tep->trait.traitPtr;

                auto vtableTy = trait.getVtableType(state.sp, state.resolve.hirCrate(), *tep);

                return types.borrow(HIRBorrowType::Shared, vtableTy);
            }
        } else if (unsizedTy->is_Slice() || (unsizedTy->is_Primitive() && unsizedTy->as_Primitive() == HIRCoreType::Str)) {
            return types.primitive(HIRCoreType::Usize);
        } else if (const auto* tep = unsizedTy->opt_Path()) {
            if (tep->binding.is_Struct()) {
                switch (tep->binding.as_Struct()->structMarkings.dstType) {
                    case HIRStructMarkings::DstType::None:
                        return HIRTypeRef();
                    case HIRStructMarkings::DstType::Possible:
                    case HIRStructMarkings::DstType::Projection: {
                        const auto& path = tep->path.data.as_Generic();
                        const auto& str = *tep->binding.as_Struct();
                        auto monomorph = [&](const auto& tpl) {
                            auto rv = MonomorphStatePtr(types, unsizedTy, &path.params, nullptr).monomorphType(sp, tpl);
                            state.resolve.expandAssociatedTypes(sp, rv);
                            return rv;
                        };
                        switch (str.data.tag()) {
                            case HIRStructData::TAG_Unit: {
                                MIR_BUG(state, "Unit-like struct with DstType::Possible - " << unsizedTy);
                                break;
                            }
                            case HIRStructData::TAG_Tuple: {
                                auto& se = str.data.as_Tuple();
                                return getMetadataType(state, monomorph(se.back().ent));
                            }
                            case HIRStructData::TAG_Named: {
                                auto& se = str.data.as_Named();
                                return getMetadataType(state, monomorph(se.back().ty));
                            }
                        }
                        UNREACHABLE();
                    }
                    case HIRStructMarkings::DstType::Slice:
                        return types.primitive(HIRCoreType::Usize);
                    case HIRStructMarkings::DstType::TraitObject:
                        return types.unit(); // TODO: Get the actual inner metadata type?
                }
            }
            return HIRTypeRef();
        } else if (unsizedTy->is_Generic()) {
            HIRPath p{unsizedTy, state.resolve.langPointee(), "Metadata"};
            auto rv = types.path(std::move(p), {});
            state.resolve.expandAssociatedTypes(sp, rv);
            return rv;
        } else {
            return HIRTypeRef();
        }
    }

    void MIRCleanupLValue(const MIRTypeResolve& state, MirMutator& mutator, MIRLValue& lval);

    HIRTypeRef getVtableType(const Span& sp, const ::StaticTraitResolve& resolve, const HIRTypeData::Data_TraitObject& te) {
        return te.trait.traitPtr->getVtableType(sp, resolve.hirCrate(), te);
    }

    const EncodedLiteral* MIRCleanupGetConstant(const MIRTypeResolve& state, const HIRPath& path, HIRTypeRef& outTy, MonomorphState& params) {
        const HIRGenericParams* implParams = nullptr;
        auto v = state.resolve.getValue(state.sp, path, params, false, &implParams);
        if (const auto* e = v.opt_Constant()) {
            auto& hirConst = const_cast<HIRConstant&>(**e);
            outTy = params.monomorphType(state.sp, hirConst.type);
            state.resolve.expandAssociatedTypes(state.sp, outTy);
            switch (hirConst.valueState) {
                case HIRConstant::ValueState::Known:
                    return &hirConst.valueRes;
                case HIRConstant::ValueState::Generic: {
                    auto it = hirConst.monomorphCache.find(path);
                    if (it == hirConst.monomorphCache.end() && !monomorphisePathNeeded(path)) {
                        ConvertHIRConstantEvaluateConstant(state.resolve, implParams, HIRItemPath(path), hirConst);
                        it = hirConst.monomorphCache.find(path);
                    }
                    if (it == hirConst.monomorphCache.end()) {
                        return nullptr;
                    }
                    return &it->second;
                }
                case HIRConstant::ValueState::InProgress:
                    ERROR(state.sp, E0000, "cycle detected when evaluating constant `" << path << "`");
                case HIRConstant::ValueState::Unknown:
                    MIR_ASSERT(state, monomorphisePathNeeded(path), "Unevaluated constant - " << path);
                    return nullptr;
            }
            MIR_BUG(state, "Unreachable ValueState for " << path);
        } else if (v.is_NotYetKnown()) {
            auto v = state.resolve.getValue(state.sp, path, params, /*signature_only=*/true);
            if (const auto* e = v.opt_Constant()) {
                const auto& hirConst = **e;
                outTy = params.monomorphType(state.sp, hirConst.type);
            } else {
                MIR_BUG(state, "get_literal_for_const - Not a constant - " << path);
            }
            return nullptr;
        } else {
            MIR_BUG(state, "get_literal_for_const - Not a constant - " << path);
            return nullptr;
        }
    }

    bool typeAcceptsAllBitPatterns(const Span& sp, const StaticTraitResolve& resolve, const HIRTypeData* ty) {
        if (const auto* primitive = ty->opt_Primitive()) {
            return *primitive != HIRCoreType::Bool && *primitive != HIRCoreType::Char && *primitive != HIRCoreType::Str;
        }
        if (const auto* array = ty->opt_Array()) {
            return array->size.as_Known() == 0 || typeAcceptsAllBitPatterns(sp, resolve, array->inner);
        }
        if (ty->is_Tuple() || (ty->is_Path() && ty->as_Path().binding.is_Struct())) {
            const auto* repr = TargetGetTypeRepr(sp, resolve, ty);
            if (!repr) {
                return false;
            }
            for (const auto& field : repr->fields) {
                if (!typeAcceptsAllBitPatterns(sp, resolve, field.ty)) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    MIRConstant createVtable(HIRTypeRef ty, const HIRTraitPath& trait, const RcString& vtableName) {
        auto vtablePath = HIRPath(mv$(ty), trait.path.clone(), vtableName);
        return MIRConstant::make_ItemAddr(box$(vtablePath));
    }

    MIRRValue MIRCleanupLiteralToRValue(const MIRTypeResolve& state, MirMutator& mutator, EncodedLiteralSlice lit, HIRTypeRef ty, const MonomorphState& params, HIRPath path) {
        switch ((*ty).tag()) {
            default:
                if (path == HIRGenericPath()) {
                    MIR_TODO(state, "Literal of type " << ty << " - " << lit);
                }
                return MIRConstant::make_ItemAddr(box$(path));
            case HIRTypeData::TAG_Tuple: {
                auto* repr = TargetGetTypeRepr(state.sp, state.resolve, ty);
                MIR_ASSERT(state, repr, "No type repr, but encoded value available? " << ty);

                std::vector<MIRParam> lvals;
                lvals.reserve(repr->fields.size());

                for (const auto& fld : repr->fields) {
                    auto rval = MIRCleanupLiteralToRValue(state, mutator, lit.slice(fld.offset), fld.ty, params, HIRGenericPath());
                    lvals.push_back(mutator.inTemporary(fld.ty, mv$(rval)));
                }

                return MIRRValue::make_Tuple({mv$(lvals)});
            }
            case HIRTypeData::TAG_Array: {
                auto& te = (*ty).as_Array();
                size_t size = 0;
                MIR_ASSERT(state, TargetGetSizeOf(state.sp, state.resolve, te.inner, size), "No size, but encoded value available? " << ty);
                auto count = te.size.as_Known();

                bool isAllSame;
                if (count > 1) {
                    isAllSame = true;
                    size_t ofs = size;
                    auto element0 = lit.slice(0, size);
                    for (unsigned int i = 1; i < count; i++) {
                        auto cur = lit.slice(ofs, size);
                        if (element0 != cur) {
                            isAllSame = false;
                            break;
                        }
                        ofs += size;
                    }
                } else {
                    isAllSame = false;
                }

                if (isAllSame) {
                    auto rval = MIRCleanupLiteralToRValue(state, mutator, lit.slice(0, size), te.inner, params, HIRGenericPath());
                    auto dataLval = mutator.inTemporary(te.inner, mv$(rval));
                    return MIRRValue::make_SizedArray({mv$(dataLval), static_cast<unsigned int>(count)});
                } else {
                    std::vector<MIRParam> lvals;
                    lvals.reserve(te.size.as_Known());

                    size_t ofs = 0;
                    for (unsigned int i = 0; i < count; i++) {
                        auto rval = MIRCleanupLiteralToRValue(state, mutator, lit.slice(ofs, size), te.inner, params, HIRGenericPath());
                        lvals.push_back(mutator.inTemporary(te.inner, mv$(rval)));
                        ofs += size;
                    }

                    return MIRRValue::make_Array({mv$(lvals)});
                }
                break;
            }
            case HIRTypeData::TAG_Path: {
                auto& te = (*ty).as_Path();
                auto* repr = TargetGetTypeRepr(state.sp, state.resolve, ty);
                MIR_ASSERT(state, repr, "No type repr, but encoded value available? " << ty);

                if (te.binding.is_Struct()) {
                    std::vector<MIRParam> lvals;
                    lvals.reserve(repr->fields.size());

                    for (const auto& fld : repr->fields) {
                        auto rval = MIRCleanupLiteralToRValue(state, mutator, lit.slice(fld.offset), fld.ty, params, HIRGenericPath());
                        lvals.push_back(mutator.inTemporary(fld.ty, mv$(rval)));
                    }

                    return MIRRValue::make_Struct({te.path.data.as_Generic().clone(), mv$(lvals)});
                } else if (te.binding.is_Enum()) {
                    auto varInfo = repr->getEnumVariant(state.sp, state.resolve, lit);
                    unsigned varIdx = varInfo.first;
                    bool hasTagField = varInfo.second;

                    const auto& enm = *te.binding.as_Enum();

                    std::vector<MIRParam> vals;
                    if (enm.data.is_Data()) {
                        const auto& fld = repr->fields.at(varIdx);

                        size_t baseOfs = fld.offset;
                        const auto* repr = TargetGetTypeRepr(state.sp, state.resolve, fld.ty);
                        vals.reserve(repr->fields.size());

                        for (const auto& fld : repr->fields) {
                            if (hasTagField && &fld == &repr->fields.back()) {
                                continue;
                            }
                            auto rval = MIRCleanupLiteralToRValue(state, mutator, lit.slice(baseOfs + fld.offset), fld.ty, params, HIRGenericPath());
                            vals.push_back(mutator.inTemporary(fld.ty, mv$(rval)));
                        }
                    } else {
                    }
                    return MIRRValue::make_EnumVariant({te.path.data.as_Generic().clone(), varIdx, mv$(vals)});
                } else if (te.binding.is_Union()) {
                    unsigned varIdx = ~0u;
                    const auto* repr = TargetGetTypeRepr(state.sp, state.resolve, ty);
                    MIR_ASSERT(state, repr, "");
                    // TODO: Find a way of storing backing information that specifies the variant (maybe as a relocation?)

                    if (varIdx == ~0u) {
                        for (const auto& e : repr->fields) {
                            if (e.ty->is_Array() && e.ty->as_Array().inner == HIRCoreType::U8 && e.ty->as_Array().size.as_Known() == repr->size) {
                                varIdx = &e - &repr->fields.front();
                                break;
                            }
                        }
                    }
                    if (varIdx == ~0u) {
                        if (repr->fields.size() == 2 && repr->fields[0].ty == state.crate.types.unit()) {
                            bool isNonzero = false;
                            for (size_t i = 0; i < repr->size; i++) {
                                if (lit.slice(i, 1).readUint(1) != 0) {
                                    isNonzero = true;
                                    break;
                                }
                            }

                            varIdx = (isNonzero ? 1 : 0);
                        }
                    }

                    if (varIdx == ~0u) {
                        for (const auto& e : repr->fields) {
                            if (e.ty->is_Pointer() || e.ty->is_Primitive()) {
                                if (lit.getReloc() && !e.ty->is_Pointer()) {
                                    continue;
                                }

                                size_t fldSize = 0;
                                TargetGetSizeOf(state.sp, state.resolve, e.ty, fldSize);
                                if (fldSize == repr->size) {
                                    varIdx = &e - &repr->fields.front();
                                    break;
                                }
                            }
                        }
                    }

                    if (varIdx == ~0u) {
                        const auto literalEnd = lit.ofs + lit.size;
                        const bool hasRelocation = std::any_of(lit.base.relocations.begin(), lit.base.relocations.end(), [&](const auto& relocation) {
                            return relocation.ofs < literalEnd && lit.ofs < relocation.ofs + relocation.len;
                        });
                        if (!hasRelocation) {
                            for (const auto& e : repr->fields) {
                                size_t fieldSize = 0;
                                if (TargetGetSizeOf(state.sp, state.resolve, e.ty, fieldSize) && fieldSize == repr->size && typeAcceptsAllBitPatterns(state.sp, state.resolve, e.ty)) {
                                    varIdx = &e - &repr->fields.front();
                                    break;
                                }
                            }
                        }
                    }

                    if (varIdx == ~0u) {
                        const auto literalEnd = lit.ofs + lit.size;
                        const bool hasRelocation = std::any_of(lit.base.relocations.begin(), lit.base.relocations.end(), [&](const auto& relocation) {
                            return relocation.ofs < literalEnd && lit.ofs < relocation.ofs + relocation.len;
                        });
                        size_t widest = 0;
                        if (!hasRelocation) {
                            for (const auto& e : repr->fields) {
                                size_t fieldSize = 0;
                                if (TargetGetSizeOf(state.sp, state.resolve, e.ty, fieldSize) && fieldSize <= repr->size && fieldSize > widest && typeAcceptsAllBitPatterns(state.sp, state.resolve, e.ty)) {
                                    widest = fieldSize;
                                    varIdx = &e - &repr->fields.front();
                                }
                            }
                        }
                    }

                    if (varIdx == ~0u) {
                        MIR_TODO(state, "MIR_Cleanup_LiteralToRValue - " << path << ": " << ty << " = " << lit << " - Decode union into MIR");
                    }
                    auto innerRval = MIRCleanupLiteralToRValue(state, mutator, lit, repr->fields[varIdx].ty, params, mv$(path));
                    auto innerLval = mutator.inTemporary(repr->fields[varIdx].ty, mv$(innerRval));
                    return MIRRValue::make_UnionVariant({te.path.data.as_Generic().clone(), varIdx, mv$(innerLval)});
                } else {
                    MIR_BUG(state, "Unexpected type for literal from " << path << " - " << ty << " (lit = " << lit << ")");
                }
                break;
            }
            case HIRTypeData::TAG_Primitive: {
                auto& te = (*ty).as_Primitive();
                switch (te) {
                    case HIRCoreType::Char:
                        return MIRConstant::make_Uint({lit.readUint(4), te});
                    case HIRCoreType::Usize:
                        return MIRConstant::make_Uint({lit.readUint(TargetGetPointerBits() / 8), te});
                    case HIRCoreType::U128:
                        return MIRConstant::make_Uint({lit.readUint(16), te});
                    case HIRCoreType::U64:
                        return MIRConstant::make_Uint({lit.readUint(8), te});
                    case HIRCoreType::U32:
                        return MIRConstant::make_Uint({lit.readUint(4), te});
                    case HIRCoreType::U16:
                        return MIRConstant::make_Uint({lit.readUint(2), te});
                    case HIRCoreType::U8:
                        return MIRConstant::make_Uint({lit.readUint(1), te});

                    case HIRCoreType::Isize:
                        return MIRConstant::make_Int({lit.readSint(TargetGetPointerBits() / 8), te});
                    case HIRCoreType::I128:
                        return MIRConstant::make_Int({lit.readSint(16), te});
                    case HIRCoreType::I64:
                        return MIRConstant::make_Int({lit.readSint(8), te});
                    case HIRCoreType::I32:
                        return MIRConstant::make_Int({lit.readSint(4), te});
                    case HIRCoreType::I16:
                        return MIRConstant::make_Int({lit.readSint(2), te});
                    case HIRCoreType::I8:
                        return MIRConstant::make_Int({lit.readSint(1), te});

                    case HIRCoreType::F128:
                        return MIRConstant::make_Float({lit.readFloat(16), te});
                    case HIRCoreType::F64:
                        return MIRConstant::make_Float({lit.readFloat(8), te});
                    case HIRCoreType::F32:
                        return MIRConstant::make_Float({lit.readFloat(4), te});
                    case HIRCoreType::F16:
                        return MIRConstant::make_Float({lit.readFloat(2), te});
                    case HIRCoreType::Bool:
                        return MIRConstant::make_Bool({lit.readUint(1) != 0});

                    case HIRCoreType::Str:
                        MIR_BUG(state, "Const of type `str` - " << path);
                }
                UNREACHABLE();
            }
            case HIRTypeData::TAG_Pattern: {
                auto& te = (*ty).as_Pattern();
                return MIRCleanupLiteralToRValue(state, mutator, lit, te.inner, params, mv$(path));
            }
            case HIRTypeData::TAG_Pointer: {
                auto& te = (*ty).as_Pointer();
                if (lit.getReloc()) {
                    const auto* reloc = lit.getReloc();
                    if (reloc->p) {
                        auto itemPath = params.monomorphPath(state.sp, *reloc->p);
                        MonomorphState itemParams(state.crate.types);
                        const auto item = state.resolve.getValue(state.sp, itemPath, itemParams, /*signature_only=*/true);
                        if (item.is_Function()) {
                            const auto encodedAddress = lit.readUint(TargetGetPointerBits() / 8);
                            MIR_ASSERT(state, encodedAddress >= EncodedLiteral::PTR_BASE, "Invalid function address");
                            const auto offset = encodedAddress - EncodedLiteral::PTR_BASE;
                            MIR_ASSERT(state, offset == U128(0), "Function address has a non-zero offset: " << offset);
                            auto address = MIRConstant::make_ItemAddr({box$(mv$(itemPath)), offset});
                            auto addressType = state.getConstType(address);
                            auto value = mutator.inTemporary(mv$(addressType), mv$(address));
                            return MIRRValue::make_Cast({mv$(value), mv$(ty)});
                        }
                    }
                    auto tyBorrow = state.crate.types.borrow(te.type, te.inner);
                    auto rval = MIRCleanupLiteralToRValue(state, mutator, lit, tyBorrow, params, mv$(path));
                    auto lval = mutator.inTemporary(mv$(tyBorrow), mv$(rval));
                    return MIRRValue::make_Cast({mv$(lval), mv$(ty)});
                } else {
                    const auto ptrSize = TargetGetPointerBits() / 8;
                    auto v = lit.readUint(ptrSize);
                    if (state.resolve.metadataType(state.sp, te.inner) == MetadataType::Slice) {
                        const auto* slice = te.inner->opt_Slice();
                        MIR_ASSERT(state, slice || te.inner == HIRCoreType::Str, "Slice metadata on non-slice type " << te.inner);
                        auto thinTy = state.crate.types.pointer(te.type, slice ? slice->inner : state.crate.types.primitive(HIRCoreType::U8));
                        auto addr = mutator.inTemporary(state.crate.types.primitive(HIRCoreType::Usize), MIRConstant::make_Uint({v, HIRCoreType::Usize}));
                        auto ptr = mutator.inTemporary(thinTy, MIRRValue::make_Cast({mv$(addr), thinTy}));
                        auto size = MIRConstant::make_Uint({lit.slice(ptrSize).readUint(ptrSize), HIRCoreType::Usize});
                        return MIRRValue::make_MakeDst({mv$(ptr), mv$(size)});
                    }
                    auto lval = mutator.inTemporary(state.crate.types.primitive(HIRCoreType::Usize), MIRRValue(MIRConstant::make_Uint({v, HIRCoreType::Usize})));
                    return MIRRValue::make_Cast({mv$(lval), mv$(ty)});
                }
                break;
            }
            case HIRTypeData::TAG_Borrow: {
                auto& te = (*ty).as_Borrow();
                const auto* dataReloc = lit.getReloc();
                const auto data_ptr = lit.readUint(TargetGetPointerBits() / 8);
                MIR_ASSERT(state, dataReloc ? data_ptr >= EncodedLiteral::PTR_BASE : data_ptr != 0, "Bad pointer value - 0x" << std::hex << data_ptr);

                if (!dataReloc) {
                    HIRTypeRef ptrInner;
                    const auto metadataType = state.resolve.metadataType(state.sp, te.inner);
                    if (metadataType == MetadataType::Slice) {
                        if (const auto* slice = te.inner->opt_Slice()) {
                            ptrInner = slice->inner;
                        } else {
                            MIR_ASSERT(state, te.inner == HIRCoreType::Str, "Slice metadata on non-slice type " << te.inner);
                            ptrInner = state.crate.types.primitive(HIRCoreType::U8);
                        }
                    } else {
                        ptrInner = te.inner;
                    }

                    auto addr = mutator.inTemporary(state.crate.types.primitive(HIRCoreType::Usize), MIRConstant::make_Uint({data_ptr, HIRCoreType::Usize}));
                    auto ptrTy = state.crate.types.pointer(te.type, ptrInner);
                    auto ptr = mutator.inTemporary(ptrTy, MIRRValue::make_Cast({mv$(addr), ptrTy}));

                    switch (metadataType) {
                        case MetadataType::Zero:
                        case MetadataType::None:
                            return MIRRValue::make_Borrow({te.type, false, MIRLValue::newDeref(mv$(ptr))});
                        case MetadataType::Slice: {
                            const auto ptrSize = TargetGetPointerBits() / 8;
                            auto size = MIRConstant::make_Uint({lit.slice(ptrSize).readUint(ptrSize), HIRCoreType::Usize});
                            return MIRRValue::make_MakeDst({mv$(ptr), mv$(size)});
                        }
                        case MetadataType::TraitObject:
                        case MetadataType::Unknown:
                            MIR_TODO(state, "Integer-address borrow with metadata " << metadataType);
                    }
                }

                const auto ofs = data_ptr - EncodedLiteral::PTR_BASE;
                if (dataReloc->p) {
                    const auto& path = *dataReloc->p;
                    auto ptrVal = MIRConstant::make_ItemAddr({box$(params.monomorphPath(state.sp, path)), ofs});
                    HIRTypeRef tmp;
                    const auto& srcTy = state.getStaticType(tmp, path);

                    auto metaTy = state.resolve.metadataType(state.sp, te.inner);
                    switch (metaTy) {
                        case MetadataType::Zero:
                        case MetadataType::None:
                            // TODO: What if the type doesn't match? Emit a `_Cast foo as &Bar`?
                            if (srcTy != te.inner) {
                                auto srcRefTy = state.crate.types.borrow(te.type, srcTy);
                                auto srcPtrTy = state.crate.types.pointer(te.type, srcTy);
                                auto innerPtrTy = state.crate.types.pointer(te.type, te.inner);
                                auto srcTyRef = mutator.inTemporary(srcRefTy, mv$(ptrVal));
                                auto srcTyPtr = mutator.inTemporary(srcPtrTy, MIRRValue::make_Cast({mv$(srcTyRef), srcPtrTy}));
                                auto innerLval = mutator.inTemporary(innerPtrTy, MIRRValue::make_Cast({mv$(srcTyPtr), innerPtrTy}));
                                return MIRRValue::make_Borrow({te.type, false, MIRLValue::newDeref(mv$(innerLval))});
                            }
                            return mv$(ptrVal);
                        case MetadataType::Slice: {
                            const auto ptrSize = TargetGetPointerBits() / 8;
                            auto size = lit.slice(ptrSize).readUint(ptrSize);
                            auto sizeVal = MIRParam(MIRConstant::make_Uint({size, HIRCoreType::Usize}));
                            return MIRRValue::make_MakeDst({MIRParam(mv$(ptrVal)), mv$(sizeVal)});
                            break;
                        }
                        case MetadataType::TraitObject: {
                            const auto* tep = te.inner->opt_TraitObject();
                            if (!tep) {
                                MIR_TODO(state, "Hidden vtable");
                            }

                            auto vtableVal = MIRParam(createVtable(srcTy, tep->trait, operationsContext(state).vtableName));

                            return MIRRValue::make_MakeDst({MIRParam(mv$(ptrVal)), mv$(vtableVal)});
                            break;
                        }
                        case MetadataType::Unknown:
                            MIR_BUG(state, te.inner << " unknown metadata type");
                    }
                } else {
                    MIR_ASSERT(state, ofs <= dataReloc->bytes.size(), "Offset out of range");
                    auto s = dataReloc->bytes.begin() + ofs.truncateU64();
                    auto e = dataReloc->bytes.end();

                    if (te.inner->is_Slice() && te.inner->as_Slice().inner == HIRCoreType::U8) {
                        std::vector<u8> bytestr;
                        for (auto it = s; it != e; ++it) {
                            bytestr.push_back(static_cast<u8>(*it));
                        }
                        auto size = MIRConstant::make_Uint({U128(bytestr.size()), HIRCoreType::Usize});
                        return MIRRValue::make_MakeDst({MIRConstant(mv$(bytestr)), std::move(size)});
                    } else if (te.inner->is_Array() && te.inner->as_Array().inner == HIRCoreType::U8) {
                        // TODO: How does this differ at codegen to the above?
                        std::vector<u8> bytestr;
                        for (auto it = s; it != e; ++it) {
                            bytestr.push_back(static_cast<u8>(*it));
                        }
                        return MIRConstant(mv$(bytestr));
                    } else if (te.inner == HIRCoreType::Str) {
                        return MIRConstant::make_StaticString(std::string(s, e));
                    } else {
                        std::vector<u8> bytestr;
                        for (auto it = s; it != e; ++it) {
                            bytestr.push_back(static_cast<u8>(*it));
                        }
                        auto size = MIRConstant::make_Uint({U128(bytestr.size()), HIRCoreType::Usize});
                        auto ptr1 = MIRRValue::make_MakeDst({MIRConstant(mv$(bytestr)), std::move(size)});
                        auto lval = mutator.inTemporary(state.crate.types.pointer(HIRBorrowType::Shared, state.crate.types.slice(state.crate.types.primitive(HIRCoreType::U8))), mv$(ptr1));
                        auto rawPtrTy = state.crate.types.pointer(HIRBorrowType::Shared, te.inner);
                        auto lval2 = mutator.inTemporary(rawPtrTy, MIRRValue::make_Cast({mv$(lval), rawPtrTy}));
                        return MIRRValue::make_Borrow({HIRBorrowType::Shared, false, MIRLValue::newDeref(mv$(lval2))});
                    }
                }
                break;
            }
            case HIRTypeData::TAG_NamedFunction: {
                auto& te = (*ty).as_NamedFunction();
                return MIRConstant::make_Function({box$(te.path.clone())});
            }
            case HIRTypeData::TAG_Function: {
                const auto* dataReloc = lit.getReloc();
                MIR_ASSERT(state, dataReloc, "Function with no relocation?!");
                MIR_ASSERT(state, dataReloc->p, "");
                return MIRConstant::make_ItemAddr(box$(dataReloc->p->clone()));
            }
        }
        UNREACHABLE();
    }

    MIRLValue MIRCleanupVirtualize(const Span& sp, const MIRTypeResolve& state, MirMutator& mutator, MIRLValue& receiverLvp, const HIRPath::Data::Data_UfcsKnown& pe) {
        assert(pe.type->is_TraitObject());
        const HIRTypeData::Data_TraitObject& te = pe.type->as_TraitObject();
        assert(te.trait.traitPtr);
        const auto& trait = *te.trait.traitPtr;

        unsigned int vtableIdx = trait.getVtableValueIndex(pe.trait.path, pe.item);
        if (vtableIdx == 0) {
            BUG(sp, "Calling method '" << pe.item << "' from " << pe.trait << " through " << te.trait.path << " which isn't in the vtable");
        }

        auto vtableTy = state.crate.types.pointer(HIRBorrowType::Shared, getVtableType(sp, state.resolve, te));

        const auto& fnDef = state.crate.getTraitByPath(sp, pe.trait.path).values.at(pe.item).as_Function();
        if (fnDef.receiver == HIRFunction::Receiver::Value) {
            receiverLvp = mutator.inTemporary(state.crate.types.borrow(HIRBorrowType::Owned, pe.type), MIRRValue::make_Borrow({HIRBorrowType::Owned, false, mv$(receiverLvp)}));
        }

        auto vtableLv = mutator.newTemporary(mv$(vtableTy));
        auto fcnLval = MIRLValue::newField(MIRLValue::newDeref(vtableLv.clone()), vtableIdx);
        HIRTypeRef tmp;
        const auto& ty = state.getLvalueType(tmp, fcnLval);
        auto receiver = ty->as_Function().argTypes.at(0);

        struct H {
            static MIRLValue getUnitPtr(const MIRTypeResolve& state, MirMutator& mutator, HIRTypeRef ty, MIRLValue lv, MIRLValue& outInnerPtr) {
                if (ty->is_Path()) {
                    const auto& te = ty->as_Path();
                    MIR_ASSERT(state, te.binding.is_Struct(), "");
                    const auto& tyPath = te.path.data.as_Generic();
                    const auto& str = *te.binding.as_Struct();
                    HIRTypeRef tmp;
                    auto monomorph = [&](const auto& t) {
                        return MonomorphStatePtr(state.crate.types, ty, &tyPath.params, nullptr).monomorphType(state.sp, t);
                    };
                    std::vector<MIRParam> vals;
                    switch (str.data.tag()) {
                        case HIRStructData::TAG_Unit: {
                            break;
                        }
                        case HIRStructData::TAG_Tuple: {
                            auto& se = str.data.as_Tuple();
                            for (unsigned int i = 0; i < se.size(); i++) {
                                auto val = MIRLValue::newField((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                                if (i == str.structMarkings.coerceUnsizedIndex) {
                                    vals.push_back(H::getUnitPtr(state, mutator, monomorph(se[i].ent), mv$(val), outInnerPtr));
                                } else {
                                    vals.push_back(mv$(val));
                                }
                            }
                            break;
                        }
                        case HIRStructData::TAG_Named: {
                            auto& se = str.data.as_Named();
                            for (unsigned int i = 0; i < se.size(); i++) {
                                auto val = MIRLValue::newField((i == se.size() - 1 ? mv$(lv) : lv.clone()), i);
                                if (i == str.structMarkings.coerceUnsizedIndex) {
                                    vals.push_back(H::getUnitPtr(state, mutator, monomorph(se[i].ty), mv$(val), outInnerPtr));
                                } else {
                                    vals.push_back(mv$(val));
                                }
                            }
                            break;
                        }
                    }

                    auto newPath = tyPath.clone();
                    return mutator.inTemporary(mv$(ty), MIRRValue::make_Struct({mv$(newPath), mv$(vals)}));
                } else if (ty->is_Borrow() || ty->is_Pointer()) {
                    outInnerPtr = lv.clone();
                    return mutator.inTemporary(mv$(ty), MIRRValue::make_DstPtr({mv$(lv)}));
                } else {
                    MIR_BUG(state, "Unexpected type coerce_unsize in receiver - " << ty);
                }
            }
        };

        MIRLValue receiverPtr;
        MIRLValue innerDynPtr;

        if (receiver->is_Path() && receiver->as_Path().binding.is_Struct() && receiver->as_Path().binding.as_Struct()->structMarkings.coerceUnsized != HIRStructMarkings::Coerce::None) {
            receiverLvp = H::getUnitPtr(state, mutator, std::move(receiver), receiverLvp.clone(), innerDynPtr);
        } else if (receiver->is_Borrow() || receiver->is_Pointer()) {
            innerDynPtr = receiverLvp.clone();
            auto ptrRval = MIRRValue::make_DstPtr({receiverLvp.clone()});

            auto ptrLv = mutator.newTemporary(state.crate.types.pointer(HIRBorrowType::Shared, state.crate.types.unit()));
            mutator.pushStatement(MIRStatement::make_Assign({ptrLv.clone(), mv$(ptrRval)}));
            receiverLvp = mv$(ptrLv);
        } else {
            // TODO: How to handle `Pin`?

            MIR_TODO(state, "Handle virtual call through " << receiver);
        }

        auto vtableRval = MIRRValue::make_DstMeta({mv$(innerDynPtr)});
        mutator.pushStatement(MIRStatement::make_Assign({vtableLv.clone(), mv$(vtableRval)}));

        return fcnLval;
    }

    bool MIRCleanupUnsizeGetMetadata(const MIRTypeResolve& state, MirMutator& mutator, const HIRTypeData* dstTy, const HIRTypeData* srcTy, const MIRLValue& ptrValue, MIRParam& outMetaVal, HIRTypeRef& outMetaTy, bool& outSrcIsDst) {
        switch ((*dstTy).tag()) {
            default:
                MIR_TODO(state, "Obtain metadata converting to " << dstTy);
                break;
            case HIRTypeData::TAG_Generic: {
                // TODO: What should be returned to indicate "no conversion"
                return false;
            }
            case HIRTypeData::TAG_Path: {
                auto& de = (*dstTy).as_Path();
                if (de.binding.is_Opaque()) {
                    return false;
                }

                MIR_ASSERT(state, srcTy->is_Path(), "Unsize to path from non-path - " << srcTy);
                const auto& se = srcTy->as_Path();
                MIR_ASSERT(state, de.binding.tag() == se.binding.tag(), "Unsize between mismatched types - " << dstTy << " and " << srcTy);
                MIR_ASSERT(state, de.binding.is_Struct(), "Unsize to non-struct - " << dstTy);
                MIR_ASSERT(state, de.binding.as_Struct() == se.binding.as_Struct(), "Unsize between mismatched types - " << dstTy << " and " << srcTy);
                const auto& str = *de.binding.as_Struct();
                MIR_ASSERT(state, str.structMarkings.unsizedField != ~0u, "Unsize on type that doesn't implement have a ?Sized field - " << dstTy);

                auto monomorphCbD = MonomorphStatePtr(state.crate.types, dstTy, &de.path.data.as_Generic().params, nullptr);
                auto monomorphCbS = MonomorphStatePtr(state.crate.types, srcTy, &se.path.data.as_Generic().params, nullptr);
                auto monomorphField = [&](const MonomorphStatePtr& monomorph, const HIRTypeData* fieldTpl) {
                    auto fieldTy = monomorph.monomorphType(state.sp, fieldTpl, false);
                    state.resolve.expandAssociatedTypes(state.sp, fieldTy);
                    return fieldTy;
                };

                switch (str.data.tag()) {
                    case HIRStructData::TAG_Unit: {
                        MIR_BUG(state, "Unit-like struct Unsize is impossible - " << srcTy);
                        break;
                    }
                    case HIRStructData::TAG_Tuple: {
                        auto& se = str.data.as_Tuple();
                        const auto& tyTpl = se.at(str.structMarkings.unsizedField).ent;
                        auto tyD = monomorphField(monomorphCbD, tyTpl);
                        auto tyS = monomorphField(monomorphCbS, tyTpl);

                        return MIRCleanupUnsizeGetMetadata(state, mutator, tyD, tyS, ptrValue, outMetaVal, outMetaTy, outSrcIsDst);
                    }
                    case HIRStructData::TAG_Named: {
                        auto& se = str.data.as_Named();
                        const auto& tyTpl = se.at(str.structMarkings.unsizedField).ty;
                        auto tyD = monomorphField(monomorphCbD, tyTpl);
                        auto tyS = monomorphField(monomorphCbS, tyTpl);

                        return MIRCleanupUnsizeGetMetadata(state, mutator, tyD, tyS, ptrValue, outMetaVal, outMetaTy, outSrcIsDst);
                    }
                }
                UNREACHABLE();
            }
            case HIRTypeData::TAG_Slice: {
                if (srcTy->is_Array()) {
                    const auto& inArray = srcTy->as_Array();
                    if (!inArray.size.is_Known()) {
                        return false;
                    }
                    outMetaTy = state.crate.types.primitive(HIRCoreType::Usize);
                    outMetaVal = MIRConstant::make_Uint({U128(inArray.size.as_Known()), HIRCoreType::Usize});
                    return true;
                } else if (srcTy->is_Generic() || (srcTy->is_Path() && srcTy->as_Path().binding.is_Opaque())) {
                    return false;
                } else {
                    MIR_BUG(state, "Unsize to slice from non-array - " << srcTy);
                }
                break;
            }
            case HIRTypeData::TAG_TraitObject: {
                auto& de = (*dstTy).as_TraitObject();
                auto vtableTy = de.trait.path != HIRSimplePath() ? de.trait.traitPtr->getVtableType(state.sp, state.crate, de) : state.crate.types.unit();
                outMetaTy = state.crate.types.pointer(HIRBorrowType::Shared, vtableTy);

                if (const auto* se = srcTy->opt_TraitObject()) {
                    outSrcIsDst = true;
                    if (de.trait.path == HIRSimplePath()) {
                        outMetaVal = mutator.inTemporary(outMetaTy, MIRRValue::make_DstMeta({ptrValue.clone()}));
                    } else if (se->trait.traitPtr != de.trait.traitPtr) {
                        assert(se->trait.traitPtr);
                        const auto& trait = *se->trait.traitPtr;
                        auto vtableTy = trait.getVtableType(state.sp, state.crate, *se);
                        auto inMetaTy = state.crate.types.pointer(HIRBorrowType::Shared, vtableTy);

                        auto parentTraitField = trait.getVtableParentIndex(state.crate.types, state.sp, se->trait.path.params, de.trait.path);
                        MIR_ASSERT(state, parentTraitField != 0, "Unable to find parent trait for trait object upcast - " << se->trait.path << " in " << de.trait.path);
                        auto inMetaVal = mutator.inTemporary(mv$(inMetaTy), MIRRValue::make_DstMeta({ptrValue.clone()}));
                        outMetaVal = MIRLValue::newField(MIRLValue::newDeref(mv$(inMetaVal)), parentTraitField);
                    } else {
                        outMetaVal = mutator.inTemporary(outMetaTy, MIRRValue::make_DstMeta({ptrValue.clone()}));
                    }
                } else {
                    MIR_ASSERT(state, state.resolve.typeIsSized(state.sp, srcTy), "Attempting to get vtable for unsized type - " << srcTy);
                    outMetaVal = createVtable(srcTy, de.trait, operationsContext(state).vtableName);
                }
                return true;
            }
        }
        UNREACHABLE();
    }

    MIRRValue MIRCleanupUnsize(const MIRTypeResolve& state, MirMutator& mutator, const HIRTypeData* dstTy, const HIRTypeData* srcTyInner, MIRLValue ptrValue) {
        const auto& dstTyInner = (dstTy->is_Borrow() ? dstTy->as_Borrow().inner : dstTy->as_Pointer().inner);

        HIRTypeRef metaType;
        MIRParam metaValue;
        bool sourceIsDst = false;
        if (MIRCleanupUnsizeGetMetadata(state, mutator, dstTyInner, srcTyInner, ptrValue, metaValue, metaType, sourceIsDst)) {
            if (sourceIsDst) {
                auto tyUnitPtr = state.crate.types.pointer(HIRBorrowType::Shared, state.crate.types.unit());
                auto thinPtrLval = mutator.inTemporary(mv$(tyUnitPtr), MIRRValue::make_DstPtr({mv$(ptrValue)}));

                return MIRRValue::make_MakeDst({mv$(thinPtrLval), mv$(metaValue)});
            } else {
                return MIRRValue::make_MakeDst({mv$(ptrValue), mv$(metaValue)});
            }
        } else {
            return MIRRValue::make_MakeDst({mv$(ptrValue), MIRConstant::make_ItemAddr({})});
        }
    }

    MIRRValue MIRCleanupCoerceUnsized(const MIRTypeResolve& state, MirMutator& mutator, const HIRTypeData* dstTy, const HIRTypeData* srcTy, MIRLValue value) {
        if (dstTy == srcTy) {
            return MIRRValue::make_Use(mv$(value));
        }

        if (dstTy->is_Path()) {
            MIR_ASSERT(state, srcTy->is_Path(), "CoerceUnsized to Path must have a Path source - " << srcTy << " to " << dstTy);
            const auto& dte = dstTy->as_Path();
            const auto& ste = srcTy->as_Path();

            MIR_ASSERT(state, dte.binding.is_Struct(), "Note, can't CoerceUnsized non-structs");
            MIR_ASSERT(state, dte.binding.tag() == ste.binding.tag(), "Note, can't CoerceUnsized mismatched structs - " << srcTy << " to " << dstTy);
            MIR_ASSERT(state, dte.binding.as_Struct() == ste.binding.as_Struct(), "Note, can't CoerceUnsized mismatched structs - " << srcTy << " to " << dstTy);
            const auto& str = *dte.binding.as_Struct();
            MIR_ASSERT(state, str.structMarkings.coerceUnsizedIndex != ~0u, "Struct " << srcTy << " doesn't impl CoerceUnsized");

            auto monomorphCbD = MonomorphStatePtr(state.crate.types, dstTy, &dte.path.data.as_Generic().params, nullptr);
            auto monomorphCbS = MonomorphStatePtr(state.crate.types, srcTy, &ste.path.data.as_Generic().params, nullptr);

            std::vector<MIRParam> ents;
            switch (str.data.tag()) {
                case HIRStructData::TAG_Unit: {
                    MIR_BUG(state, "Unit-like struct CoerceUnsized is impossible - " << srcTy);
                    break;
                }
                case HIRStructData::TAG_Tuple: {
                    auto& se = str.data.as_Tuple();
                    ents.reserve(se.size());
                    for (unsigned int i = 0; i < se.size(); i++) {
                        if (i == str.structMarkings.coerceUnsizedIndex) {
                            auto tyD = monomorphCbD.monomorphType(state.sp, se[i].ent, false);
                            auto tyS = monomorphCbS.monomorphType(state.sp, se[i].ent, false);

                            auto newRval = MIRCleanupCoerceUnsized(state, mutator, tyD, tyS, MIRLValue::newField(value.clone(), i));
                            auto newLval = mutator.inTemporary(mv$(tyD), mv$(newRval));

                            ents.push_back(mv$(newLval));
                        } else if (state.resolve.isTypePhantomData(se[i].ent)) {
                            auto tyD = monomorphCbD.monomorphType(state.sp, se[i].ent, false);

                            auto newRval = MIRRValue::make_Struct({tyD->as_Path().path.data.as_Generic().clone(), {}});
                            auto newLval = mutator.inTemporary(mv$(tyD), mv$(newRval));

                            ents.push_back(mv$(newLval));
                        } else {
                            ents.push_back(MIRLValue::newField(value.clone(), i));
                        }
                    }
                    break;
                }
                case HIRStructData::TAG_Named: {
                    auto& se = str.data.as_Named();
                    ents.reserve(se.size());
                    for (unsigned int i = 0; i < se.size(); i++) {
                        if (i == str.structMarkings.coerceUnsizedIndex) {
                            auto tyD = monomorphCbD.monomorphType(state.sp, se[i].ty, false);
                            auto tyS = monomorphCbS.monomorphType(state.sp, se[i].ty, false);

                            auto newRval = MIRCleanupCoerceUnsized(state, mutator, tyD, tyS, MIRLValue::newField(value.clone(), i));
                            auto newLval = mutator.newTemporary(mv$(tyD));
                            mutator.pushStatement(MIRStatement::make_Assign({newLval.clone(), mv$(newRval)}));

                            ents.push_back(mv$(newLval));
                        } else if (state.resolve.isTypePhantomData(se[i].ty)) {
                            auto tyD = monomorphCbD.monomorphType(state.sp, se[i].ty, false);

                            auto newRval = MIRRValue::make_Struct({tyD->as_Path().path.data.as_Generic().clone(), {}});
                            auto newLval = mutator.inTemporary(mv$(tyD), mv$(newRval));

                            ents.push_back(mv$(newLval));
                        } else {
                            ents.push_back(MIRLValue::newField(value.clone(), i));
                        }
                    }
                    break;
                }
            }
            return MIRRValue::make_Struct({dte.path.data.as_Generic().clone(), mv$(ents)});
        }

        if (dstTy->is_Borrow()) {
            MIR_ASSERT(state, srcTy->is_Borrow(), "CoerceUnsized to Borrow must have a Borrow source - " << srcTy << " to " << dstTy);
            const auto& dte = dstTy->as_Borrow();
            const auto& ste = srcTy->as_Borrow();

            if (dte.inner == ste.inner && dte.type != ste.type) {
                return MIRRValue::make_Borrow({dte.type, false, MIRLValue::newDeref(mv$(value))});
            }

            return MIRCleanupUnsize(state, mutator, dstTy, ste.inner, mv$(value));
        }

        if (dstTy->is_Pointer()) {
            MIR_ASSERT(state, srcTy->is_Pointer(), "CoerceUnsized to Pointer must have a Pointer source - " << srcTy << " to " << dstTy);
            const auto& dte = dstTy->as_Pointer();
            const auto& ste = srcTy->as_Pointer();

            if (dte.type == ste.type) {
                return MIRCleanupUnsize(state, mutator, dstTy, ste.inner, mv$(value));
            } else {
                MIR_ASSERT(state, dte.inner == ste.inner, "TODO: Can pointer CoerceUnsized unsize? " << srcTy << " to " << dstTy);
                MIR_ASSERT(state, dte.type < ste.type, "CoerceUnsize attempting to raise pointer type");

                return MIRRValue::make_Cast({mv$(value), dstTy});
            }
        }

        MIR_BUG(state, "Unknown CoerceUnsized target " << dstTy << " from " << srcTy);
        UNREACHABLE();
    }

    void MIRCleanupLValue(const MIRTypeResolve& state, MirMutator& mutator, MIRLValue& lval) {
        switch (lval.root.tag()) {
            case MIRLValue::Storage::TAG_Return: {
                break;
            }
            case MIRLValue::Storage::TAG_Argument: {
                break;
            }
            case MIRLValue::Storage::TAG_Local: {
                break;
            }
            case MIRLValue::Storage::TAG_Static: {
                break;
            }
        }

        for (size_t i = 0; i < lval.wrappers.size(); i++) {
            if (!lval.wrappers[i].is_Deref()) {
                continue;
            }

            HIRTypeRef tmp;
            const auto& ty = state.getLvalueType(tmp, lval, lval.wrappers.size() - i);
            if (state.resolve.isTypeOwnedBox(ty)) {
                unsigned numInjectedFldZeros = 0;

                auto typ = ty;
                while (typ->is_Path()) {
                    const auto& te = typ->as_Path();
                    MIR_ASSERT(state, te.binding.is_Struct(), "Box contained a non-struct");
                    const auto& str = *te.binding.as_Struct();
                    const HIRTypeData* tyTpl = nullptr;
                    switch (str.data.tag()) {
                        case HIRStructData::TAG_Unit: {
                            MIR_BUG(state, "Box contained a unit-like struct");
                            break;
                        }
                        case HIRStructData::TAG_Tuple: {
                            auto& se = str.data.as_Tuple();
                            MIR_ASSERT(state, se.size() > 0, "Box contained an empty tuple struct");
                            tyTpl = se[0].ent;
                            break;
                        }
                        case HIRStructData::TAG_Named: {
                            auto& se = str.data.as_Named();
                            MIR_ASSERT(state, se.size() > 0, "Box contained an empty named struct");
                            tyTpl = se[0].ty;
                            break;
                        }
                    }
                    tmp = MonomorphStatePtr(state.crate.types, typ, &te.path.data.as_Generic().params, nullptr).monomorphType(state.sp, tyTpl);
                    typ = tmp;

                    numInjectedFldZeros++;
                }
                MIR_ASSERT(state, typ->is_Pointer(), "First non-path field in Box wasn't a pointer - " << typ);

                while (numInjectedFldZeros--) {
                    lval.wrappers.insert(lval.wrappers.begin() + i, MIRLValue::Wrapper::newField(0));
                }
            } else {
            }
        }
    }

    void MIRCleanupConstant(const MIRTypeResolve& state, MirMutator& mutator, MIRConstant& p) {
        if (auto* e = p.opt_Uint()) {
            switch (e->t) {
                case HIRCoreType::Usize:
                    if (TargetGetPointerBits() == 32) {
                        e->v &= U128(0xFFFFFFFF);
                    }
                    break;
                default:
                    break;
            }
        }
    }

    void MIRCleanupParam(const MIRTypeResolve& state, MirMutator& mutator, MIRParam& p);

    static void MIRCleanupAsmConst(const MIRTypeResolve& state, MirMutator& mutator, MIRAsmParam& p) {
        auto param = MIRParam(std::move(p.as_Const()));
        MIRCleanupParam(state, mutator, param);
        if (param.is_Constant()) {
            p = MIRAsmParam::make_Const(std::move(param.as_Constant()));
        }
    }

    void MIRCleanupParam(const MIRTypeResolve& state, MirMutator& mutator, MIRParam& p) {
        switch (p.tag()) {
            case MIRParam::TAG_LValue: {
                auto& e = p.as_LValue();
                MIRCleanupLValue(state, mutator, e);
                break;
            }
            case MIRParam::TAG_Borrow: {
                auto& e = p.as_Borrow();
                MIRCleanupLValue(state, mutator, e.val);
                break;
            }
            case MIRParam::TAG_Constant: {
                auto& e = p.as_Constant();
                MIRCleanupConstant(state, mutator, e);
                break;
            }
        }

        if (p.is_Constant() && p.as_Constant().is_Const()) {
            const auto& ce = p.as_Constant().as_Const();
            HIRTypeRef cTy;
            MonomorphState params(state.crate.types);
            const auto* litPtr = MIRCleanupGetConstant(state, *ce.p, cTy, params);
            if (litPtr) {
                auto newRval = MIRCleanupLiteralToRValue(state, mutator, *litPtr, cTy, params, mv$(*ce.p));
                if (auto* lv = newRval.opt_Use()) {
                    p = MIRParam::make_LValue(std::move(*lv));
                } else if (auto* c = newRval.opt_Constant()) {
                    MIRCleanupConstant(state, mutator, *c);
                    p = MIRParam::make_Constant(std::move(*c));
                } else {
                    auto tmpLv = mutator.inTemporary(mv$(cTy), mv$(newRval));
                    p = MIRParam::make_LValue(std::move(tmpLv));
                }
            } else {
            }
        }
    }

    bool MIROptimiseBlockSimplify(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseInlining(MIRTypeResolve& state, MIRFunction& fcn, bool minimal, const TransList* list = nullptr);
    bool MIROptimiseSplitAggregates(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimisePropagateSingleAssignments(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimisePropagateKnownValues(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseDeTemporary(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseUnifyTemporaries(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseCommonStatements(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseUnifyBlocks(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseConstPropagate(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseDeadDropFlags(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseDeadAssignments(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseNoopRemoval(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseGotoAssign(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseUselessReborrows(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseGarbageCollectPartial(MIRTypeResolve& state, MIRFunction& fcn);
    bool MIROptimiseGarbageCollect(MIRTypeResolve& state, MIRFunction& fcn);

    bool MIROptimiseInline(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType, const TransList& list, unsigned optLevel) {
        Span sp;
        bool rv = false;
        auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
            os << path;
        });
        MIRTypeResolve state{sp, resolve, pathCallback, retType, args, fcn};

        while (MIROptimiseInlining(state, fcn, false, &list)) {
            MIRCleanup(resolve, path, fcn, args, retType);
            rv = true;
        }

        if (rv) {
            MIROptimise(resolve, path, fcn, args, retType, optLevel, /*do_inline=*/false);
        }

        return rv;
    }

    bool optVisitMirLvaluesInner(const MIRLValue& lv, MIRValUsage u, LvalueVisitor& cb) {
        for (const auto& w : lv.wrappers) {
            if (w.is_Index()) {
                if (cb.visitLvalue(MIRLValue::newLocal(w.as_Index()), MIRValUsage::Read)) {
                    return true;
                }
            } else if (w.is_Deref()) {
            }
        }
        return cb.visitLvalue(lv, u);
    }

    bool visitMirLvalueMut(MIRLValue& lv, MIRValUsage u, LvalueRefVisitorMut& cb) {
        auto lvr = MIRLValue::MRef(lv);
        do {
            if (cb.visitLvalue(lvr, u)) {
                return true;
            }
            // TODO: Use a TU_MATCH?
            if (lvr.is_Index()) {
                auto ilv = MIRLValue::newLocal(lvr.as_Index());
                auto ilvR = MIRLValue::MRef(ilv);
                bool rv = cb.visitLvalue(ilvR, MIRValUsage::Read);
                assert(ilv.is_Local() && ilv.as_Local() == lvr.as_Index());
                if (rv) {
                    return true;
                }
            } else if (lvr.is_Field()) {
                // HACK: If "moving", use a "Read" value usage (covers some quirks)
                if (u == MIRValUsage::Move) {
                    u = MIRValUsage::Read;
                }
            } else if (lvr.is_Deref()) {
                // TODO: Is this right?
                if (u == MIRValUsage::Borrow) {
                    u = MIRValUsage::Read;
                }
            } else {
            }
        } while (lvr.tryUnwrap());
        return false;
    }

    bool visitMirLvalueRawMut(MIRLValue& lv, MIRValUsage u, LvalueVisitorMut& cb) {
        return cb.visitLvalue(lv, u);
    }

    bool visitMirLvalueMut(MIRParam& p, MIRValUsage u, LvalueVisitorMut& cb) {
        if (auto* e = p.opt_LValue()) {
            return visitMirLvalueRawMut(*e, u, cb);
        } else {
            return false;
        }
    }

    bool optVisitMirLvaluesMut(MIRRValue& rval, LvalueVisitorMut& cb) {
        bool rv = false;
        switch (rval.tag()) {
            case MIRRValue::TAG_Use: {
                auto& se = rval.as_Use();
                rv |= visitMirLvalueRawMut(se, MIRValUsage::Move, cb);
                break;
            }
            case MIRRValue::TAG_Constant: {
                break;
            }
            case MIRRValue::TAG_SizedArray: {
                auto& se = rval.as_SizedArray();
                rv |= visitMirLvalueMut(se.val, MIRValUsage::Read, cb);
                break;
            }
            case MIRRValue::TAG_Borrow: {
                auto& se = rval.as_Borrow();
                rv |= visitMirLvalueRawMut(se.val, MIRValUsage::Borrow, cb);
                break;
            }
            case MIRRValue::TAG_Cast: {
                auto& se = rval.as_Cast();
                rv |= visitMirLvalueRawMut(se.val, MIRValUsage::Read, cb);
                break;
            }
            case MIRRValue::TAG_BinOp: {
                auto& se = rval.as_BinOp();
                rv |= visitMirLvalueMut(se.valL, MIRValUsage::Read, cb);
                rv |= visitMirLvalueMut(se.valR, MIRValUsage::Read, cb);
                break;
            }
            case MIRRValue::TAG_UniOp: {
                auto& se = rval.as_UniOp();
                rv |= visitMirLvalueRawMut(se.val, MIRValUsage::Read, cb);
                break;
            }
            case MIRRValue::TAG_DstMeta: {
                auto& se = rval.as_DstMeta();
                rv |= visitMirLvalueRawMut(se.val, MIRValUsage::Read, cb);
                break;
            }
            case MIRRValue::TAG_DstPtr: {
                auto& se = rval.as_DstPtr();
                rv |= visitMirLvalueRawMut(se.val, MIRValUsage::Read, cb);
                break;
            }
            case MIRRValue::TAG_MakeDst: {
                auto& se = rval.as_MakeDst();
                rv |= visitMirLvalueMut(se.ptrVal, MIRValUsage::Move, cb);
                rv |= visitMirLvalueMut(se.metaVal, MIRValUsage::Read, cb);
                break;
            }
            case MIRRValue::TAG_Tuple: {
                auto& se = rval.as_Tuple();
                for (auto& v : se.vals) {
                    rv |= visitMirLvalueMut(v, MIRValUsage::Move, cb);
                }
                break;
            }
            case MIRRValue::TAG_Array: {
                auto& se = rval.as_Array();
                for (auto& v : se.vals) {
                    rv |= visitMirLvalueMut(v, MIRValUsage::Move, cb);
                }
                break;
            }
            case MIRRValue::TAG_UnionVariant: {
                auto& se = rval.as_UnionVariant();
                rv |= visitMirLvalueMut(se.val, MIRValUsage::Move, cb);
                break;
            }
            case MIRRValue::TAG_EnumVariant: {
                auto& se = rval.as_EnumVariant();
                for (auto& v : se.vals) {
                    rv |= visitMirLvalueMut(v, MIRValUsage::Move, cb);
                }
                break;
            }
            case MIRRValue::TAG_Struct: {
                auto& se = rval.as_Struct();
                for (auto& v : se.vals) {
                    rv |= visitMirLvalueMut(v, MIRValUsage::Move, cb);
                }
                break;
            }
        }
        return rv;
    }

    bool optVisitMirLvalues(const MIRRValue& rval, LvalueVisitor& cb) {
        LvalueConstAdapter adapter{cb};
        return optVisitMirLvaluesMut(const_cast<MIRRValue&>(rval), adapter);
    }

    bool optVisitMirLvaluesMut(MIRStatement& stmt, LvalueVisitorMut& cb) {
        bool rv = false;
        switch (stmt.tag()) {
            case MIRStatement::TAG_Assign: {
                auto& e = stmt.as_Assign();
                rv |= optVisitMirLvaluesMut(e.src, cb);
                rv |= visitMirLvalueRawMut(e.dst, MIRValUsage::Write, cb);
                break;
            }
            case MIRStatement::TAG_Asm: {
                auto& e = stmt.as_Asm();
                for (auto& v : e.inputs) {
                    rv |= visitMirLvalueRawMut(v.second, MIRValUsage::Read, cb);
                }
                for (auto& v : e.outputs) {
                    rv |= visitMirLvalueRawMut(v.second, MIRValUsage::Write, cb);
                }
                break;
            }
            case MIRStatement::TAG_Asm2: {
                auto& e = stmt.as_Asm2();
                for (auto& p : e.params) {
                    switch (p.tag()) {
                        case MIRAsmParam::TAG_Const: {
                            break;
                        }
                        case MIRAsmParam::TAG_Sym: {
                            break;
                        }
                        case MIRAsmParam::TAG_Reg: {
                            auto& v = p.as_Reg();
                            if (v.input) {
                                rv |= visitMirLvalueMut(*v.input, MIRValUsage::Read, cb);
                            }
                            if (v.output) {
                                rv |= visitMirLvalueRawMut(*v.output, MIRValUsage::Write, cb);
                            }
                            break;
                        }
                        case MIRAsmParam::TAG_Label: {
                            break;
                        }
                    }
                }
                break;
            }
            case MIRStatement::TAG_SetDropFlag: {
                break;
            }
            case MIRStatement::TAG_SaveDropFlag: {
                auto& e = stmt.as_SaveDropFlag();
                rv |= visitMirLvalueRawMut(e.slot, MIRValUsage::Write, cb);
                break;
            }
            case MIRStatement::TAG_LoadDropFlag: {
                auto& e = stmt.as_LoadDropFlag();
                rv |= visitMirLvalueRawMut(e.slot, MIRValUsage::Read, cb);
                break;
            }
            case MIRStatement::TAG_ScopeEnd: {
                break;
            }
        }
        return rv;
    }

    bool optVisitMirLvalues(const MIRStatement& stmt, LvalueVisitor& cb) {
        LvalueConstAdapter adapter{cb};
        return optVisitMirLvaluesMut(const_cast<MIRStatement&>(stmt), adapter);
    }

    bool optVisitMirLvaluesMut(MIRTerminator& term, LvalueVisitorMut& cb) {
        bool rv = false;
        switch (term.tag()) {
            case MIRTerminator::TAG_Incomplete: {
                break;
            }
            case MIRTerminator::TAG_Return: {
                break;
            }
            case MIRTerminator::TAG_UnwindResume: {
                break;
            }
            case MIRTerminator::TAG_UnwindTerminate: {
                break;
            }
            case MIRTerminator::TAG_Unreachable: {
                break;
            }
            case MIRTerminator::TAG_Goto: {
                break;
            }
            case MIRTerminator::TAG_If: {
                auto& e = term.as_If();
                rv |= visitMirLvalueRawMut(e.cond, MIRValUsage::Read, cb);
                break;
            }
            case MIRTerminator::TAG_Switch: {
                auto& e = term.as_Switch();
                rv |= visitMirLvalueRawMut(e.val, MIRValUsage::Read, cb);
                break;
            }
            case MIRTerminator::TAG_SwitchValue: {
                auto& e = term.as_SwitchValue();
                rv |= visitMirLvalueRawMut(e.val, MIRValUsage::Read, cb);
                break;
            }
            case MIRTerminator::TAG_Drop: {
                auto& e = term.as_Drop();
                rv |= visitMirLvalueRawMut(e.slot, MIRValUsage::Move, cb);
                break;
            }
            case MIRTerminator::TAG_Call: {
                auto& e = term.as_Call();
                if (e.fcn.is_Value()) {
                    rv |= visitMirLvalueRawMut(e.fcn.as_Value(), MIRValUsage::Read, cb);
                }
                for (auto& v : e.args) {
                    rv |= visitMirLvalueMut(v, MIRValUsage::Move, cb);
                }
                rv |= visitMirLvalueRawMut(e.retVal, MIRValUsage::Write, cb);
                break;
            }
            case MIRTerminator::TAG_TailCall: {
                auto& e = term.as_TailCall();
                if (e.fcn.is_Value()) {
                    rv |= visitMirLvalueRawMut(e.fcn.as_Value(), MIRValUsage::Read, cb);
                }
                for (auto& v : e.args) {
                    rv |= visitMirLvalueMut(v, MIRValUsage::Move, cb);
                }
                break;
            }
            case MIRTerminator::TAG_Asm2: {
                auto& e = term.as_Asm2();
                for (auto& p : e.params) {
                    if (auto* reg = p.opt_Reg()) {
                        if (reg->input) {
                            rv |= visitMirLvalueMut(*reg->input, MIRValUsage::Read, cb);
                        }
                        if (reg->output) {
                            rv |= visitMirLvalueRawMut(*reg->output, MIRValUsage::Write, cb);
                        }
                    }
                }
                break;
            }
        }
        return rv;
    }

    bool optVisitMirLvalues(const MIRTerminator& term, LvalueVisitor& cb) {
        LvalueConstAdapter adapter{cb};
        return optVisitMirLvaluesMut(const_cast<MIRTerminator&>(term), adapter);
    }

    void optVisitMirLvaluesMut(MIRTypeResolve& state, MIRFunction& fcn, LvalueVisitorMut& cb) {
        for (unsigned int blockIdx = 0; blockIdx < fcn.blocks.size(); blockIdx++) {
            auto& block = fcn.blocks[blockIdx];
            for (auto& stmt : block.statements) {
                state.setCurStmt(blockIdx, (&stmt - &block.statements.front()));
                optVisitMirLvaluesMut(stmt, cb);
            }
            if (block.terminator.isDead()) {
                continue;
            }
            state.setCurStmtTerm(blockIdx);
            optVisitMirLvaluesMut(block.terminator, cb);
        }
    }

    void optVisitMirLvalues(MIRTypeResolve& state, const MIRFunction& fcn, LvalueVisitor& cb) {
        LvalueConstAdapter adapter{cb};
        optVisitMirLvaluesMut(state, const_cast<MIRFunction&>(fcn), adapter);
    }

    const MIRFunction* getCalledMir(const MIRTypeResolve& state, const TransList* list, const HIRPath& path, ParamsSet& params) {
        MonomorphState outParams(state.resolve.hirCrate().types);
        StaticTraitResolve::ResolvedTraitImplPath traitImplPath;
        auto e = state.resolve.getValue(state.sp, path, outParams, /*sig_only*/ false, &params.implParamsDef, &traitImplPath);
        params.fcnParams = outParams.getMethodParams();
        params.implParams = outParams.ppImpl == nullptr ? HIRPathParams() : outParams.ppImpl == &outParams.ppImplData ? std::move(outParams.ppImplData) : outParams.ppImpl->clone();

        if (e.is_Function() && e.as_Function()->markings.isRustcIntrinsic) {
            return nullptr;
        }

        if (list) {
            auto canonicalPath = path.clone();
            if (traitImplPath.type) {
                auto& pe = canonicalPath.data.as_UfcsKnown();
                pe.type = traitImplPath.type;
                pe.trait.params = mv$(traitImplPath.traitParams);
            }
            const auto* transFcn = list->findFunction(canonicalPath);
            if (!transFcn) {
                MIR_BUG(state, "Enumeration failure - Function " << canonicalPath << " not in TransList");
            }
            // TODO: Need identity params for most, but lifetime params need to be from the input.

            const auto& hirFcn = *transFcn->ptr;
            if (transFcn->monomorphised.code) {
                return &*transFcn->monomorphised.code;
            } else if (const auto* mir = hirFcn.code.getMirOpt()) {
                MIR_ASSERT(state, hirFcn.params.types.empty(), "Enumeration failure - Function had params, but wasn't monomorphised - " << path);
                // TODO: Check for trait methods too?
                return mir;
            } else {
                MIR_ASSERT(state, !hirFcn.code, "LowerMIR failure - No MIR but HIR is present?! - " << path);
                return nullptr;
            }
        }

        switch (path.data.tag()) {
            case HIRPathData::TAG_Generic: {
                params.selfTy = nullptr;
                break;
            }
            case HIRPathData::TAG_UfcsKnown: {
                auto& pe = path.data.as_UfcsKnown();
                params.selfTy = pe.type;
                break;
            }
            case HIRPathData::TAG_UfcsInherent: {
                auto& pe = path.data.as_UfcsInherent();
                params.selfTy = pe.type;
                break;
            }
            case HIRPathData::TAG_UfcsUnknown: {
                MIR_BUG(state, "UfcsUnknown hit - " << path);
                break;
            }
        }

        switch (e.tag()) {
            default:
                MIR_BUG(state, "MIR Call of " << e.tagStr() << " - " << path);
                break;
            case TypeckValuePtr::TAG_NotFound: {
                auto& _ = e.as_NotFound();
                return nullptr;
            }
            case TypeckValuePtr::TAG_NotYetKnown: {
                auto& _ = e.as_NotYetKnown();
                return nullptr;
            }
            case TypeckValuePtr::TAG_Function: {
                auto& f = e.as_Function();
                params.fcnParamsDef = &f->params;
                return f->code.getMirOpt();
            }
        }
        return nullptr;
    }

    void visitBlocksMut(MIRTypeResolve& state, MIRFunction& fcn, const MIRBlockCallback& cb) {
        auto& context = operationsContext(state);
        auto& visited = context.visitedBlocks;
        auto& toVisit = context.pendingBlocks;
        ASSERT_BUG(Span(), !context.visitingBlocks, "visitBlocksMut re-entered");
        context.visitingBlocks = true;
        STD_DEFER {
            context.visitingBlocks = false;
        };
        visited.assign(fcn.blocks.size(), false);
        toVisit.clear();
        toVisit.push_back(0);
        while (toVisit.size() > 0) {
            auto bb = toVisit.back();
            toVisit.pop_back();
            if (visited[bb]) {
                continue;
            }
            visited[bb] = true;
            auto& block = fcn.blocks[bb];

            cb.run(bb, block);

            struct QueueUnvisited final: public MIRTargetVisitor {
                const std::vector<bool>& visited;
                std::vector<MIRBasicBlockId>& toVisit;

                QueueUnvisited(const std::vector<bool>& visited, std::vector<MIRBasicBlockId>& toVisit)
                    : visited(visited)
                    , toVisit(toVisit)
                {
                }

                void visitTarget(const MIRBasicBlockId& target) override {
                    if (!visited[target]) {
                        toVisit.push_back(target);
                    }
                }
            } queueUnvisited{visited, toVisit};

            visitTerminatorTarget(block.terminator, queueUnvisited);
        }
    }

    void visitBlocks(MIRTypeResolve& state, const MIRFunction& fcn, const MIRBlockConstCallback& cb) {
        visitBlocksMut(state, const_cast<MIRFunction&>(fcn), makeCallable<MIRBlockCb>([&cb](MIRBasicBlockId id, MIRBasicBlock& blk) {
            cb.run(id, blk);
        }));
    }

    MIRRValue paramToRvalue(MIRParam param) {
        switch (param.tag()) {
            case MIRParam::TAG_LValue: {
                auto& lv = param.as_LValue();
                return mv$(lv);
            }
            case MIRParam::TAG_Borrow: {
                auto& e = param.as_Borrow();
                return MIRRValue::make_Borrow({e.type, false, mv$(e.val)});
            }
            case MIRParam::TAG_Constant: {
                auto& c = param.as_Constant();
                return mv$(c);
            }
        }
        throw std::runtime_error("Corrupted MIR::Param");
    }

    bool MIROptimiseBlockSimplify(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;

        struct H {
            static MIRBasicBlockId getNewTarget(const MIRTypeResolve& state, MIRBasicBlockId bb) {
                const auto& target = state.getBlock(bb);
                if (target.statements.size() != 0) {
                    return bb;
                } else if (!target.terminator.is_Goto()) {
                    return bb;
                } else {
                    // Make sure we don't infinite loop (TODO: What about mutual recursion?)
                    if (bb == target.terminator.as_Goto()) {
                        return bb;
                    }

                    return getNewTarget(state, target.terminator.as_Goto());
                }
            }
        };

        for (auto& block : fcn.blocks) {
            struct RewriteGotoChains final: public MIRTargetVisitorMut {
                const MIRTypeResolve& state;
                MIRFunction& fcn;
                const MIRBasicBlock& block;
                bool& changed;

                RewriteGotoChains(const MIRTypeResolve& state, MIRFunction& fcn, const MIRBasicBlock& block, bool& changed)
                    : state(state)
                    , fcn(fcn)
                    , block(block)
                    , changed(changed)
                {
                }

                void visitTarget(MIRBasicBlockId& target) override {
                    if (&fcn.blocks[target] != &block) {
                        auto newBb = H::getNewTarget(state, target);
                        if (newBb != target) {
                            target = newBb;
                            changed = true;
                        }
                    }
                }
            } rewriteGotoChains{state, fcn, block, changed};

            visitTerminatorTargetMut(block.terminator, rewriteGotoChains);

            if (auto* te = block.terminator.opt_Switch()) {
                if (te->validFlag != ~0u) {
                    continue;
                }
                for (auto& t : te->targets) {
                    auto idx = &t - &te->targets.front();
                    if (fcn.blocks[t].statements.empty() && fcn.blocks[t].terminator.is_Switch()) {
                        const auto& nTe = fcn.blocks[t].terminator.as_Switch();
                        if (nTe.validFlag == ~0u && nTe.val == te->val) {
                            t = nTe.targets[idx];
                            changed = true;
                        }
                    }
                }
            }
        }

        for (auto& block : fcn.blocks) {
            if (block.statements.size() > 1) {
                for (auto it = block.statements.begin() + 1; it != block.statements.end();) {
                    if ((it - 1)->is_ScopeEnd() && it->is_ScopeEnd()) {
                        auto& dst = (it - 1)->as_ScopeEnd();
                        const auto& src = it->as_ScopeEnd();
                        for (auto v : src.slots) {
                            dst.slots.push_back(v);
                        }
                        std::sort(dst.slots.begin(), dst.slots.end());
                        it = block.statements.erase(it);
                        changed = true;
                    } else {
                        ++it;
                    }
                }
            }
        }

        {
            std::vector<bool> visited(fcn.blocks.size());
            std::vector<unsigned int> uses(fcn.blocks.size());
            std::vector<MIRBasicBlockId> toVisit;
            toVisit.push_back(0);
            uses[0]++;
            while (toVisit.size() > 0) {
                auto bb = toVisit.back();
                toVisit.pop_back();
                if (visited[bb]) {
                    continue;
                }
                visited[bb] = true;
                const auto& block = fcn.blocks[bb];

                struct CountUses final: public MIRTargetVisitor {
                    const std::vector<bool>& visited;
                    std::vector<MIRBasicBlockId>& toVisit;
                    std::vector<unsigned>& uses;

                    CountUses(const std::vector<bool>& visited, std::vector<MIRBasicBlockId>& toVisit, std::vector<unsigned>& uses)
                        : visited(visited)
                        , toVisit(toVisit)
                        , uses(uses)
                    {
                    }

                    void visitTarget(const MIRBasicBlockId& target) override {
                        if (!visited[target]) {
                            toVisit.push_back(target);
                        }
                        uses[target]++;
                    }
                } countUses{visited, toVisit, uses};

                visitTerminatorTarget(block.terminator, countUses);
            }

            unsigned int i = 0;
            for (auto& block : fcn.blocks) {
                if (visited[i]) {
                    while (block.terminator.is_Goto()) {
                        auto tgt = block.terminator.as_Goto();
                        if (uses[tgt] != 1) {
                            break;
                        }
                        if (tgt == i) {
                            break;
                        }

                        assert(&fcn.blocks[tgt] != &block);
                        auto srcBlock = mv$(fcn.blocks[tgt]);
                        fcn.blocks[tgt].terminator = MIRTerminator::make_Incomplete({});

                        for (auto& stmt : srcBlock.statements) {
                            block.statements.push_back(mv$(stmt));
                        }
                        block.terminator = mv$(srcBlock.terminator);
                        changed = true;
                    }
                }
                i++;
            }
        }

        for (auto& block : fcn.blocks) {
            state.setCurStmtTerm(&block - &fcn.blocks.front());
            if (block.terminator.is_Goto()) {
                auto tgt = block.terminator.as_Goto();
                if (!fcn.blocks[tgt].statements.empty()) {
                } else if (fcn.blocks[tgt].terminator.is_Return()) {
                    block.terminator = MIRTerminator::make_Return({});
                    changed = true;
                } else if (fcn.blocks[tgt].terminator.is_UnwindResume()) {
                    block.terminator = MIRTerminator::make_UnwindResume({});
                    changed = true;
                } else if (fcn.blocks[tgt].terminator.is_UnwindTerminate()) {
                    block.terminator = MIRTerminator::make_UnwindTerminate({});
                    changed = true;
                } else if (fcn.blocks[tgt].terminator.is_Unreachable()) {
                    block.terminator = MIRTerminator::make_Unreachable({});
                    changed = true;
                } else {
                }
            }
        }

        return false;
    }

    bool MIROptimiseInlining(MIRTypeResolve& state, MIRFunction& fcn, bool minimal, const TransList* list /*=nullptr*/) {
        bool inlineHappened = false;

        struct InlineEvent {
            HIRPath path;
            std::vector<size_t> bbList;

            InlineEvent(HIRPath p)
                : path(std::move(p))
            {
            }

            bool hasBb(size_t i) const {
                return std::find(this->bbList.begin(), this->bbList.end(), i) != this->bbList.end();
            }

            void addRange(size_t start, size_t count) {
                for (size_t j = 0; j < count; j++) {
                    this->bbList.push_back(start + j);
                }
            }
        };

        std::vector<InlineEvent> inlinedFunctions;

        struct H {
            struct Source {
                unsigned bbIdx;
                unsigned stmtIdx;
                const MIRStatement* stmt;

                Source(unsigned bbIdx, unsigned stmtIdx, const MIRStatement* stmt = nullptr)
                    : bbIdx(bbIdx)
                    , stmtIdx(stmtIdx)
                    , stmt(stmt)
                {
                }
            };

            static Source findSource(const MIRFunction& fcn, unsigned bbIdx, unsigned stmtIdx, const MIRLValue& val) {
                if (!val.wrappers.empty()) {
                    return Source(bbIdx, stmtIdx);
                }
                const auto& bb = fcn.blocks.at(bbIdx);
                while (stmtIdx--) {
                    const auto& stmt = bb.statements[stmtIdx];
                    if (stmt.is_Asm()) {
                        return Source(bbIdx, stmtIdx);
                    }
                    if (stmt.is_Assign()) {
                        const auto& se = stmt.as_Assign();
                        if (se.dst == val) {
                            return Source(bbIdx, stmtIdx, &stmt);
                        }
                    }
                }
                return Source(bbIdx, 0);
            }

            static bool valueIsConst(const MIRFunction& fcn, unsigned bbIdx, unsigned stmtIdx, const MIRLValue& val, const std::vector<MIRParam>& params) {
                if (val.root.is_Argument()) {
                    auto a = val.root.as_Argument();
                    return params[a].is_Constant() && !params[a].as_Constant().is_Const();
                }

                auto src = H::findSource(fcn, bbIdx, stmtIdx, val);
                if (src.stmt) {
                    if (const auto* se = src.stmt->opt_Assign()) {
                        if (se->src.is_Use()) {
                            return valueIsConst(fcn, src.bbIdx, src.stmtIdx, se->src.as_Use(), params);
                        }
                        if (const auto* rve = se->src.opt_BinOp()) {
                            return valueIsConst(fcn, src.bbIdx, src.stmtIdx, rve->valL, params) && valueIsConst(fcn, src.bbIdx, src.stmtIdx, rve->valR, params);
                        }
                    }
                }

                return false;
            }

            static bool valueIsConst(const MIRFunction& fcn, unsigned bbIdx, unsigned stmtIdx, const MIRParam& val, const std::vector<MIRParam>& params) {
                if (val.is_LValue()) {
                    return valueIsConst(fcn, bbIdx, stmtIdx, val.as_LValue(), params);
                } else {
                    return val.is_Constant() && !val.as_Constant().is_Const();
                }
            }

            static bool canInline(const HIRPath& path, const MIRFunction& fcn, const std::vector<MIRParam>& params, bool minimal) {
                // TODO: If the function is marked as `inline(always)`, then inline it regardless of the contents
                // TODO: If the function is marked as `inline(never)`, then don't inline
                // TODO: Take a monomorph helper so recursion can be detected

                if (minimal) {
                    return false;
                }

                // TODO: Allow functions that are just a switch on an input.
                if (fcn.blocks.size() == 1) {
                    return fcn.blocks[0].statements.size() < 10 && !fcn.blocks[0].terminator.is_Goto();
                } else if (fcn.blocks.size() == 2 && fcn.blocks[0].terminator.is_Call()) {
                    const auto& blk0Te = fcn.blocks[0].terminator.as_Call();
                    if (!(fcn.blocks[1].terminator.is_UnwindResume() || fcn.blocks[1].terminator.is_UnwindTerminate() || fcn.blocks[1].terminator.is_Unreachable())) {
                        return false;
                    }
                    if (fcn.blocks[0].statements.size() + fcn.blocks[1].statements.size() > 10) {
                        return false;
                    }

                    // TODO: This is the pre-monomorph path, but we're comparing with the post-monomorph path
                    if (blk0Te.fcn.is_Path() && blk0Te.fcn.as_Path() == path) {
                        return false;
                    }
                    return true;
                } else if (fcn.blocks.size() == 3 && fcn.blocks[0].terminator.is_Call()) {
                    const auto& blk0Te = fcn.blocks[0].terminator.as_Call();
                    if (!(fcn.blocks[1].terminator.is_UnwindResume() || fcn.blocks[1].terminator.is_UnwindTerminate() || fcn.blocks[1].terminator.is_Unreachable() || fcn.blocks[1].terminator.is_Return())) {
                        return false;
                    }
                    if (!(fcn.blocks[2].terminator.is_UnwindResume() || fcn.blocks[2].terminator.is_UnwindTerminate() || fcn.blocks[2].terminator.is_Unreachable() || fcn.blocks[2].terminator.is_Return())) {
                        return false;
                    }
                    if (fcn.blocks[0].statements.size() + fcn.blocks[1].statements.size() + fcn.blocks[2].statements.size() > 10) {
                        return false;
                    }

                    // TODO: This is the pre-monomorph path, but we're comparing with the post-monomorph path
                    if (blk0Te.fcn.is_Path() && blk0Te.fcn.as_Path() == path) {
                        return false;
                    }
                    return true;
                } else {
                }

                // TODO: If all inputs are known, then allow larger/complex functions (e.g. allow one call and any number of blocks?)

                if (canInlineSwitchWrapper(path, fcn, params)) {
                    return true;
                }
                if (canInlineSwitchValueWrapper(path, fcn, params)) {
                    return true;
                }
                return false;
            }

            static bool canInlineSwitchWrapper(const HIRPath& path, const MIRFunction& fcn, const std::vector<MIRParam>& params) {
                if (fcn.blocks.size() <= 1) {
                    return false;
                }
                if (!fcn.blocks[0].terminator.is_Switch()) {
                    return false;
                }
                const auto& teSwitch = fcn.blocks[0].terminator.as_Switch();
                if (fcn.blocks.size() != teSwitch.targets.size() + 3) {
                    return false;
                }
                if (!valueIsConst(fcn, 0, fcn.blocks[0].statements.size(), teSwitch.val, params)) {
                    return false;
                }
                for (const auto& tgt : teSwitch.targets) {
                    if (std::find(teSwitch.targets.begin() + (1 + &tgt - teSwitch.targets.data()), teSwitch.targets.end(), tgt) != teSwitch.targets.end()) {
                        return false;
                    }
                }
                for (size_t i = 1; i < fcn.blocks.size(); i++) {
                    if (fcn.blocks[i].terminator.is_Call()) {
                        const auto& te = fcn.blocks[i].terminator.as_Call();
                        if (te.fcn.is_Path() && te.fcn.as_Path() == path) {
                            return false;
                        }
                        if (!te.fcn.is_Intrinsic()) {
                            return false;
                        }
                    }
                }
                return true;
            }

            static bool canInlineSwitchValueWrapper(const HIRPath& path, const MIRFunction& fcn, const std::vector<MIRParam>& params) {
                if (fcn.blocks.size() <= 1) {
                    return false;
                }
                if (!fcn.blocks[0].terminator.is_SwitchValue()) {
                    return false;
                }
                const auto& teSwitch = fcn.blocks[0].terminator.as_SwitchValue();
                if (fcn.blocks.size() != teSwitch.targets.size() + 1 + 3) {
                    return false;
                }
                if (!valueIsConst(fcn, 0, fcn.blocks[0].statements.size(), teSwitch.val, params)) {
                    return false;
                }

                if (std::find(teSwitch.targets.begin(), teSwitch.targets.end(), teSwitch.defTarget) != teSwitch.targets.end()) {
                    return false;
                }
                for (const auto& tgt : teSwitch.targets) {
                    if (std::find(teSwitch.targets.begin() + (1 + &tgt - teSwitch.targets.data()), teSwitch.targets.end(), tgt) != teSwitch.targets.end()) {
                        return false;
                    }
                }

                for (size_t i = 1; i < fcn.blocks.size(); i++) {
                    if (fcn.blocks[i].terminator.is_Call()) {
                        const auto& te = fcn.blocks[i].terminator.as_Call();
                        if (te.fcn.is_Path() && te.fcn.as_Path() == path) {
                            return false;
                        }
                        if (!te.fcn.is_Intrinsic()) {
                            return false;
                        }
                    }
                }
                return true;
            }
        };

        // TODO: Can this use the code in `monomorphise.cpp`?
        struct Cloner: public MIRCloner {
            const ::StaticTraitResolve& resolve_;
            const MIRTerminator::Data_Call& te;
            std::vector<unsigned> copyArgs;
            ParamsSet params;
            unsigned int bbBase = ~0u;
            unsigned int varBase = ~0u;
            unsigned int dfBase = ~0u;

            size_t tmpEnd = 0;
            mutable std::vector<MIRParam> constAssignments;

            MIRLValue retval;

            Cloner(const Span& sp, const ::StaticTraitResolve& resolve, MIRTerminator::Data_Call& te)
                : MIRCloner(sp, resolve.hirCrate().types)
                , resolve_(resolve)
                , te(te)
                , params(resolve.hirCrate().types)
                , copyArgs(te.args.size(), ~0u)
            {
            }

            MIRBasicBlockId mapBbIdx(MIRBasicBlockId idx) const override {
                return this->bbBase + idx;
            }

            unsigned mapLocal(unsigned f) const override {
                return this->varBase + f;
            }

            unsigned mapDropFlag(unsigned f) const override {
                return this->dfBase + f;
            }

            const HIRTypeData* valueGenericType(HIRGenericRef ce) const override {
                const HIRGenericParams* p;
                switch (ce.group()) {
                    case 0:
                        p = params.implParamsDef;
                        break;
                    case 1:
                        p = params.fcnParamsDef;
                        break;
                    default:
                        TODO(sp, "Typecheck const generics - look up the type");
                }
                ASSERT_BUG(sp, p, "No generic list for " << ce);
                ASSERT_BUG(sp, ce.idx() < p->values.size(), "Generic param index out of range");
                return p->values.at(ce.idx()).type;
            }

            const Monomorphiser& monomorphiser() const override {
                return params;
            }

            const StaticTraitResolve* resolve() const override {
                return &this->resolve_;
            }

            MIRBasicBlock cloneBb(const MIRBasicBlock& src, unsigned srcIdx, unsigned newIdx) const {
                MIRBasicBlock rv;
                rv.isCleanup = src.isCleanup;
                rv.statements.reserve(src.statements.size());
                for (const auto& stmt : src.statements) {
                    rv.statements.push_back(this->cloneStmt(stmt));
                }
                if (src.terminator.is_Return()) {
                    rv.statements.push_back(MIRStatement::make_Assign({this->te.retVal.clone(), this->retval.clone()}));
                }
                rv.terminator = this->cloneTerm(src.terminator);
                return rv;
            }

            MIRTerminator cloneTerm(const MIRTerminator& src) const override {
                if (src.is_Return()) {
                    return MIRTerminator::make_Goto(this->te.retBlock);
                } else if (src.is_UnwindResume()) {
                    switch (this->te.unwind.tag()) {
                        case MIRUnwindAction::TAG_Continue: {
                            return MIRTerminator::make_UnwindResume({});
                        }
                        case MIRUnwindAction::TAG_Cleanup: {
                            auto& ue = this->te.unwind.as_Cleanup();
                            return MIRTerminator::make_Goto(ue);
                        }
                        case MIRUnwindAction::TAG_Terminate: {
                            return MIRTerminator::make_UnwindTerminate({});
                        }
                        case MIRUnwindAction::TAG_Unreachable: {
                            return MIRTerminator::make_Unreachable({});
                        }
                    }
                    UNREACHABLE();
                } else {
                    return MIRCloner::cloneTerm(src);
                }
            }

            MIRLValue cloneLval(const MIRLValue& src) const override {
                auto rv = MIRCloner::cloneLval(src);
                if (rv.root.is_Return()) {
                    return this->retval.cloneWrapped(std::move(rv.wrappers));
                }
                if (rv.root.is_Argument()) {
                    auto se = rv.root.as_Argument();
                    const auto& arg = this->te.args.at(se);
                    if (this->copyArgs[se] != ~0u) {
                        return MIRLValue(MIRLValue::Storage::newLocal(this->copyArgs[se]), std::move(rv.wrappers));
                    } else {
                        assert(!arg.is_Constant());
                        return arg.as_LValue().cloneWrapped(std::move(rv.wrappers));
                    }
                }
                return rv;
            }
        };

        for (unsigned int i = 0; i < fcn.blocks.size(); i++) {
            state.setCurStmtTerm(i);
            if (auto* te = fcn.blocks[i].terminator.opt_Call()) {
                if (!te->fcn.is_Path()) {
                    continue;
                }
                const auto& path = te->fcn.as_Path();

                for (const auto& e : inlinedFunctions) {
                    if (path == e.path && e.hasBb(i)) {
                        MIR_BUG(state, "Recursive inline of " << path);
                    }
                }

                Cloner cloner{state.sp, state.resolve, *te};
                const auto* calledMir = getCalledMir(state, list, path, cloner.params);
                if (!calledMir) {
                    continue;
                }
                if (calledMir == &fcn) {
                    continue;
                }
                if (cloner.params.hasUnevaluatedValues()) {
                    continue;
                }

                if (!H::canInline(path, *calledMir, te->args, minimal)) {
                    continue;
                }

                {
                    cloner.retval = MIRLValue::newLocal(fcn.locals.size());
                    HIRTypeRef tmpTy;
                    fcn.locals.push_back(state.getLvalueType(tmpTy, te->retVal));
                }

                cloner.varBase = fcn.locals.size();
                for (const auto& ty : calledMir->locals) {
                    fcn.locals.push_back(cloner.monomorph(ty));
                }
                cloner.tmpEnd = fcn.locals.size();

                cloner.dfBase = fcn.dropFlags.size();
                fcn.dropFlags.insert(fcn.dropFlags.end(), calledMir->dropFlags.begin(), calledMir->dropFlags.end());
                cloner.bbBase = fcn.blocks.size();

                for (size_t i = 0; i < te->args.size(); i++) {
                    const auto& a = te->args[i];
                    if (!a.is_LValue() || state.lvalueIsCopy(a.as_LValue())) {
                        cloner.copyArgs[i] = cloner.tmpEnd + cloner.constAssignments.size();
                        cloner.constAssignments.push_back(a.clone());
                    }
                }

                std::vector<MIRBasicBlock> newBlocks;
                newBlocks.reserve(calledMir->blocks.size());
                for (const auto& bb : calledMir->blocks) {
                    newBlocks.push_back(cloner.cloneBb(bb, (&bb - calledMir->blocks.data()), fcn.blocks.size() + newBlocks.size()));
                }

                for (auto& val : cloner.constAssignments) {
                    HIRTypeRef tmp;
                    auto ty = val.is_Constant() ? state.getConstType(val.as_Constant()) : state.getLvalueType(tmp, val.as_LValue());
                    auto lv = MIRLValue::newLocal(static_cast<unsigned>(fcn.locals.size()));
                    fcn.locals.push_back(mv$(ty));
                    auto rval = val.is_Constant() ? MIRRValue(mv$(val.as_Constant())) : MIRRValue(mv$(val.as_LValue()));
                    auto stmt = MIRStatement::make_Assign({mv$(lv), mv$(rval)});
                    newBlocks[0].statements.insert(newBlocks[0].statements.begin(), mv$(stmt));
                }
                cloner.constAssignments.clear();

                for (auto& e : inlinedFunctions) {
                    if (e.hasBb(i)) {
                        e.addRange(cloner.bbBase, newBlocks.size());
                    }
                }
                inlinedFunctions.push_back(InlineEvent(path.clone()));
                inlinedFunctions.back().addRange(cloner.bbBase, newBlocks.size());

                fcn.blocks.reserve(fcn.blocks.size() + newBlocks.size());
                for (auto& b : newBlocks) {
                    fcn.blocks.push_back(mv$(b));
                }
                fcn.blocks[i].terminator = MIRTerminator::make_Goto(cloner.bbBase);
                inlineHappened = true;

                // TODO: Store the inlined path along with the start and end BBs, and then use that to detect recursive
            }
        }
        return inlineHappened;
    }

    std::ostream& operator<<(std::ostream& os, const OptimiseStmtRef& x) {
        return os << "BB" << x.bbIdx << "/" << x.stmtIdx;
    }

    enum class IterPathRes {
        Abort,
        EarlyTrue,
        Complete,
    };

    IterPathRes iterPathWith(const MIRFunction& fcn, const OptimiseStmtRef& start, const OptimiseStmtRef& end, IterPathCallback& cb) {
        if (start.bbIdx == end.bbIdx) {
            assert(start.stmtIdx <= end.stmtIdx);
        }

        auto vistedBbs = std::set<unsigned>();
        for (auto ref = start; ref.bbIdx != end.bbIdx || ref.stmtIdx < end.stmtIdx;) {
            const auto& bb = fcn.blocks.at(ref.bbIdx);
            if (ref.stmtIdx < bb.statements.size()) {
                if (cb.visitStatement(ref, bb.statements.at(ref.stmtIdx))) {
                    return IterPathRes::EarlyTrue;
                }

                ref.stmtIdx++;
            } else {
                if (cb.visitTerminator(ref, bb.terminator)) {
                    return IterPathRes::EarlyTrue;
                }

                if (ref.bbIdx == end.bbIdx) {
                    break;
                }

                if (const auto* te = bb.terminator.opt_Goto()) {
                    if (!vistedBbs.insert(*te).second) {
                        return IterPathRes::Abort;
                    }
                    ref.stmtIdx = 0;
                    ref.bbIdx = *te;
                } else if (const auto* te = bb.terminator.opt_Call()) {
                    if (!vistedBbs.insert(te->retBlock).second) {
                        return IterPathRes::Abort;
                    }
                    ref.stmtIdx = 0;
                    ref.bbIdx = te->retBlock;
                } else {
                    return IterPathRes::Abort;
                }
            }
        }
        return IterPathRes::Complete;
    }

    template <typename S, typename T>
    IterPathRes iterPath(const MIRFunction& fcn, const OptimiseStmtRef& start, const OptimiseStmtRef& end, S statement, T terminator) {
        IterPathCb<S, T> cb(statement, terminator);
        return iterPathWith(fcn, start, end, cb);
    }

    bool checkInvalidatesLvalue(const MIRStatement& stmt, const MIRLValue& val, bool isCopy, bool alsoRead = false) {
        CheckInvalidatesLvalue cb{val, isCopy, alsoRead};
        return optVisitMirLvalues(stmt, cb);
    }

    bool checkInvalidatesLvalue(const MIRTerminator& term, const MIRLValue& val, bool isCopy, bool alsoRead = false) {
        CheckInvalidatesLvalue cb{val, isCopy, alsoRead};
        return optVisitMirLvalues(term, cb);
    }

    bool MIROptimiseDeTemporarySingleSetAndUse(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;

        struct LocalUsage {
            unsigned nWrite;
            unsigned nRead;
            unsigned nBorrow;
            OptimiseStmtRef setLoc;
            OptimiseStmtRef useLoc;

            LocalUsage()
                : nWrite(0)
                , nRead(0)
                , nBorrow(0)
            {
            }
        };

        std::vector<LocalUsage> usageInfo(fcn.locals.size());

        {
            struct CountUsage final: public LvalueVisitor {
                MIRTypeResolve& state;
                decltype(usageInfo)& usageInfo;

                CountUsage(MIRTypeResolve& state, decltype(usageInfo)& usageInfo)
                    : state(state)
                    , usageInfo(usageInfo)
                {
                }

                OptimiseStmtRef getCurLoc() const {
                    return OptimiseStmtRef(state.getCurBlock(), state.getCurStmtOfs());
                }

                bool visitLvalue(const MIRLValue& lv, MIRValUsage vu) override {
                    if (!lv.wrappers.empty()) {
                        vu = MIRValUsage::Read;
                    }
                    for (const auto& w : lv.wrappers) {
                        if (w.is_Index()) {
                            auto& slot = usageInfo[w.as_Index()];
                            slot.nRead += 1;
                            slot.useLoc = getCurLoc();
                        }
                    }
                    if (lv.root.is_Local()) {
                        auto& slot = usageInfo[lv.root.as_Local()];
                        switch (vu) {
                            case MIRValUsage::Write:
                                slot.nWrite += 1;
                                slot.setLoc = getCurLoc();
                                break;
                            case MIRValUsage::Move:
                                slot.nRead += 1;
                                slot.useLoc = getCurLoc();
                                break;
                            case MIRValUsage::Read:
                            case MIRValUsage::Borrow:
                                slot.nBorrow += 1;
                                break;
                        }
                    }
                    return false;
                }
            } visitCb{state, usageInfo};

            optVisitMirLvalues(state, fcn, visitCb);
        }

        for (size_t varIdx = 0; varIdx < fcn.locals.size(); varIdx++) {
            const auto& slot = usageInfo[varIdx];
            auto thisVar = MIRLValue::newLocal(varIdx);
            if (slot.nWrite == 1 && slot.nRead == 1 && slot.nBorrow == 0) {
                auto& useBb = fcn.blocks[slot.useLoc.bbIdx];
                auto& setBb = fcn.blocks[slot.setLoc.bbIdx];

                auto setLocNext = slot.setLoc;
                if (slot.setLoc.stmtIdx < setBb.statements.size()) {
                    setLocNext.stmtIdx += 1;
                } else {
                    setLocNext.bbIdx = setBb.terminator.as_Call().retBlock;
                    setLocNext.stmtIdx = 0;
                }

                if (slot.useLoc.stmtIdx < useBb.statements.size() && (useBb.statements[slot.useLoc.stmtIdx].is_Assign() && useBb.statements[slot.useLoc.stmtIdx].as_Assign().src.is_Use() && useBb.statements[slot.useLoc.stmtIdx].as_Assign().src.as_Use() == thisVar)) {
                    const auto& dst = useBb.statements[slot.useLoc.stmtIdx].as_Assign().dst;

                    // TODO: If the destination slot was ever borrowed mutably, don't move.

                    // TODO: What if the set location is a call?
                    bool invalidated = IterPathRes::Complete != iterPath(
                                                                    fcn,
                                                                    setLocNext,
                                                                    slot.useLoc,
                                                                    // TODO: What about a mutable borrow?
                                                                    [&](auto loc, const auto& stmt) -> bool {
                        return checkInvalidatesLvalue(stmt, dst, false, /*also_read=*/true);
                    },
                                                                    [&](auto loc, const auto& term) -> bool {
                        return checkInvalidatesLvalue(term, dst, false, /*also_read=*/true);
                    }
                                                                );
                    if (!invalidated) {
                        if (slot.setLoc.stmtIdx < setBb.statements.size()) {
                            auto& setStmt = setBb.statements[slot.setLoc.stmtIdx];
                            switch (setStmt.tag()) {
                                case MIRStatement::TAG_Assign: {
                                    auto& se = setStmt.as_Assign();
                                    MIR_ASSERT(state, se.dst == MIRLValue::newLocal(varIdx), "Impossibility: Value set but isn't destination in " << setStmt);
                                    se.dst = dst.clone();
                                    useBb.statements[slot.useLoc.stmtIdx] = MIRStatement();
                                    changed = true;
                                    break;
                                }
                                case MIRStatement::TAG_Asm: {
                                    break;
                                }
                                case MIRStatement::TAG_Asm2: {
                                    auto& se = setStmt.as_Asm2();

                                    // TODO: Replace the output variable
                                    for (auto& e : se.params) {
                                        if (const auto* ep = e.opt_Reg()) {
                                            if (ep->output) {
                                                if (*ep->output == MIRLValue::newLocal(varIdx)) {
                                                    *ep->output = dst.clone();
                                                    useBb.statements[slot.useLoc.stmtIdx] = MIRStatement();
                                                    changed = true;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                    if (!changed) {
                                        MIR_BUG(state, "Failed to find usage of _" << varIdx << " in asm! statement");
                                    }
                                    break;
                                } break;
                                default:
                                    MIR_BUG(state, "Impossibility: Value set in " << setStmt);
                            }
                        } else {
                            auto& setTerm = setBb.terminator;
                            MIR_ASSERT(state, setTerm.is_Call(), "Impossibility: Value set using non-call");
                            auto& te = setTerm.as_Call();
                            te.retVal = dst.clone();
                            useBb.statements[slot.useLoc.stmtIdx] = MIRStatement();
                            changed = true;
                        }
                    } else {
                    }
                    continue;
                }

                if (slot.setLoc.stmtIdx < setBb.statements.size() && (setBb.statements[slot.setLoc.stmtIdx].is_Assign() && (setBb.statements[slot.setLoc.stmtIdx].as_Assign().src.is_Use()))) {
                    auto& setStmt = setBb.statements[slot.setLoc.stmtIdx];
                    const auto& src = setStmt.as_Assign().src.as_Use();
                    bool srcCopy = src.wrappers.empty() && state.lvalueIsCopy(src);

                    auto useLocInc = slot.useLoc;
                    useLocInc.stmtIdx += 1;
                    bool invalidated = IterPathRes::Complete != iterPath(fcn, setLocNext, useLocInc, [&](auto loc, const auto& stmt) -> bool {
                        return checkInvalidatesLvalue(stmt, src, srcCopy) || (stmt.is_Assign() && stmt.as_Assign().src.is_Borrow() && stmt.as_Assign().src.as_Borrow().type != HIRBorrowType::Shared);
                    }, [&](auto loc, const auto& term) -> bool {
                        return checkInvalidatesLvalue(term, src, srcCopy);
                    });
                    if (!invalidated && std::any_of(src.wrappers.begin(), src.wrappers.end(), [](const MIRLValue::Wrapper& w) {
                        return w.is_Deref();
                    })) {
                        struct CheckMoves final: public LvalueVisitor {
                            MIRTypeResolve& state;
                            const MIRLValue& thisVar;
                            bool stop = false;

                            CheckMoves(MIRTypeResolve& state, const MIRLValue& thisVar)
                                : state(state)
                                , thisVar(thisVar)
                            {
                            }

                            bool visitLvalue(const MIRLValue& lv, MIRValUsage vu) override {
                                if (lv == thisVar) {
                                    stop = true;
                                    return false;
                                }
                                if (stop) {
                                    return false;
                                }
                                if (vu == MIRValUsage::Move) {
                                    return !state.lvalueIsCopy(lv);
                                }
                                return false;
                            }
                        } checkCb{state, thisVar};

                        struct FindThisVar final: public LvalueVisitor {
                            const MIRLValue& thisVar;

                            explicit FindThisVar(const MIRLValue& thisVar)
                                : thisVar(thisVar)
                            {
                            }

                            bool visitLvalue(const MIRLValue& lv, MIRValUsage /*vu*/) override {
                                return lv == thisVar;
                            }
                        } findThisVar{thisVar};

                        invalidated = IterPathRes::Complete != iterPath(fcn, setLocNext, useLocInc, [&](auto loc, const auto& stmt) -> bool {
                            return optVisitMirLvalues(stmt, checkCb);
                        }, [&](auto loc, const auto& term) -> bool {
                            return (term.is_Call() && !optVisitMirLvalues(term, findThisVar)) || optVisitMirLvalues(term, checkCb);
                        });
                    }
                    if (!invalidated) {
                        struct ReplaceVar final: public LvalueVisitorMut {
                            MIRTypeResolve& state;
                            const MIRLValue& thisVar;
                            const MIRLValue& src;

                            ReplaceVar(MIRTypeResolve& state, const MIRLValue& thisVar, const MIRLValue& src)
                                : state(state)
                                , thisVar(thisVar)
                                , src(src)
                            {
                            }

                            bool visitLvalue(MIRLValue& slot, MIRValUsage /*vu*/) override {
                                if (slot.root == thisVar.root) {
                                    if (src.wrappers.empty()) {
                                        slot.root = src.root.clone();
                                    } else if (slot.wrappers.empty()) {
                                        slot = src.clone();
                                    } else {
                                        MIR_TODO(state, "Replace inner of " << slot << " with " << src);
                                    }
                                    return true;
                                }
                                return false;
                            }
                        } replaceCb{state, thisVar, src};

                        if (slot.useLoc.stmtIdx < useBb.statements.size()) {
                            auto& useStmt = useBb.statements[slot.useLoc.stmtIdx];
                            bool found = optVisitMirLvaluesMut(useStmt, replaceCb);
                            if (found) {
                                setStmt = MIRStatement();
                                changed = true;
                            }
                        } else {
                            auto& useTerm = useBb.terminator;
                            bool found = optVisitMirLvaluesMut(useTerm, replaceCb);
                            if (found) {
                                setStmt = MIRStatement();
                                changed = true;
                            }
                        }
                    }
                    continue;
                }

                // TODO: If the source is a Borrow and the use is a Deref, then propagate forwards
            }
        }

        return changed;
    }

    bool MIROptimiseDeTemporaryBorrows(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;

        struct LocalUsage {
            unsigned nWrite;
            unsigned nOtherRead;
            unsigned nDerefRead;
            OptimiseStmtRef setLoc;
            std::vector<OptimiseStmtRef> dropLocs;

            LocalUsage()
                : nWrite(0)
                , nOtherRead(0)
                , nDerefRead(0)
            {
            }
        };

        std::vector<LocalUsage> usageInfo(fcn.locals.size());
        for (size_t i = 0; i < fcn.locals.size(); i++) {
            auto& u = usageInfo[i];
            u.nWrite = 0;
            u.nOtherRead = 0;
            u.nDerefRead = 0;
            u.setLoc = OptimiseStmtRef();
            u.dropLocs.clear();
        }

        struct CountBorrowUsage final: public LvalueVisitor {
            const MIRFunction& fcn;
            decltype(usageInfo)& usageInfo;
            OptimiseStmtRef curLoc;

            CountBorrowUsage(const MIRFunction& fcn, decltype(usageInfo)& usageInfo)
                : fcn(fcn)
                , usageInfo(usageInfo)
            {
            }

            bool visitLvalue(const MIRLValue& lv, MIRValUsage vu) override {
                if (lv.root.is_Local()) {
                    auto& slot = usageInfo[lv.root.as_Local()];
                    if (!lv.wrappers.empty() && lv.wrappers.front().is_Deref()) {
                        slot.nDerefRead++;
                        if (fcn.locals[lv.root.as_Local()]->is_Borrow()) {
                        }
                    } else if (lv.wrappers.empty() && vu == MIRValUsage::Write) {
                        slot.nWrite++;
                        slot.setLoc = curLoc;
                    } else {
                        slot.nOtherRead++;
                    }
                }
                return false;
            }
        } visitCb{fcn, usageInfo};

        for (const auto& bb : fcn.blocks) {
            for (const auto& stmt : bb.statements) {
                visitCb.curLoc = OptimiseStmtRef(&bb - &fcn.blocks.front(), &stmt - &bb.statements.front());

                optVisitMirLvalues(stmt, visitCb);
            }
            auto curLoc = OptimiseStmtRef(&bb - &fcn.blocks.front(), bb.statements.size());
            visitCb.curLoc = curLoc;
            if (const auto* drop = bb.terminator.opt_Drop(); drop && drop->slot.root.is_Local() && drop->slot.wrappers.empty()) {
                usageInfo[drop->slot.root.as_Local()].dropLocs.push_back(curLoc);
            } else {
                optVisitMirLvalues(bb.terminator, visitCb);
            }
        }

        for (size_t varIdx = 0; varIdx < fcn.locals.size(); varIdx++) {
            const auto& slot = usageInfo[varIdx];
            auto thisVar = MIRLValue::newLocal(varIdx);

            if (slot.nWrite != 1) {
                continue;
            }
            if (slot.nDerefRead == 0) {
                continue;
            }

            auto& srcBb = fcn.blocks[slot.setLoc.bbIdx];
            if (!(slot.setLoc.stmtIdx < srcBb.statements.size() && (srcBb.statements[slot.setLoc.stmtIdx].is_Assign() && (srcBb.statements[slot.setLoc.stmtIdx].as_Assign().src.is_Borrow())))) {
                continue;
            }
            const auto& srcBorrow = srcBb.statements[slot.setLoc.stmtIdx].as_Assign().src.as_Borrow();
            const auto& srcLv = srcBorrow.val;
            if (slot.nDerefRead > 1 && srcLv.wrappers.size() >= 2) {
                continue;
            }
            if (std::count_if(srcLv.wrappers.begin(), srcLv.wrappers.end(), [](const MIRLValue::Wrapper& w) {
                return w.is_Deref();
            }) > 1) {
                continue;
            }
            if (slot.nDerefRead + slot.nOtherRead > 1 && srcBorrow.type != HIRBorrowType::Shared) {
                continue;
            }
            bool srcCopy = state.lvalueIsCopy(srcLv);

            auto curLoc = slot.setLoc;
            curLoc.stmtIdx++;

            struct ReplaceDerefs final: public LvalueVisitorMut {
                MIRTypeResolve& state;
                const MIRLValue& thisVar;
                const MIRLValue& srcLv;
                const OptimiseStmtRef& curLoc;
                const LocalUsage& slot;
                unsigned numReplaced = 0;

                ReplaceDerefs(MIRTypeResolve& state, const MIRLValue& thisVar, const MIRLValue& srcLv, const OptimiseStmtRef& curLoc, const LocalUsage& slot)
                    : state(state)
                    , thisVar(thisVar)
                    , srcLv(srcLv)
                    , curLoc(curLoc)
                    , slot(slot)
                {
                }

                bool visitLvalue(MIRLValue& lv, MIRValUsage /*vu*/) override {
                    if (lv.root == thisVar.root && !lv.wrappers.empty()) {
                        ASSERT_BUG(Span(), !lv.wrappers.empty(), curLoc << " " << lv);
                        MIR_ASSERT(state, lv.wrappers.front().is_Deref(), "Use of a replacable value that isn't via a deref - " << lv);
                        {
                            auto lvr = MIRLValue::MRef(lv);
                            while (lvr.wrapperCount() > 1) {
                                lvr.tryUnwrap();
                            }
                            lvr.replace(srcLv.clone());
                        }
                        assert(lv.root != thisVar.root);
                        assert(numReplaced < slot.nDerefRead);
                        numReplaced += 1;
                    }
                    return false;
                }
            } replaceCb{state, thisVar, srcLv, curLoc, slot};

            for (bool stop = false; !stop;) {
                auto& curBb = fcn.blocks[curLoc.bbIdx];
                for (; curLoc.stmtIdx < curBb.statements.size(); curLoc.stmtIdx++) {
                    auto& stmt = curBb.statements[curLoc.stmtIdx];
                    bool invalidates = checkInvalidatesLvalue(stmt, srcLv, srcCopy);
                    if (invalidates) {
                        stop = true;
                        break;
                    }
                    optVisitMirLvaluesMut(stmt, replaceCb);
                    if (replaceCb.numReplaced == slot.nDerefRead) {
                        stop = true;
                        break;
                    }
                }
                if (stop) {
                    break;
                }
                optVisitMirLvaluesMut(curBb.terminator, replaceCb);
                if (replaceCb.numReplaced == slot.nDerefRead) {
                    stop = true;
                    break;
                }
                if (checkInvalidatesLvalue(curBb.terminator, srcLv, srcCopy)) {
                    stop = true;
                    break;
                }

                switch (curBb.terminator.tag()) {
                    default:
                        stop = true;
                        break;
                        // TODO: History is needed to avoid infinite loops from triggering infinite looping here.

                        // TODO: Fork state to handle multi-tagets
                }
            }

            if (srcLv.root.is_Local() && !srcLv.wrappers.empty() && srcLv.wrappers.front().is_Deref()) {
                usageInfo[srcLv.root.as_Local()].nDerefRead += replaceCb.numReplaced;
                if (replaceCb.numReplaced == slot.nDerefRead) {
                    usageInfo[srcLv.root.as_Local()].nDerefRead -= 1;
                }
            }

            if (replaceCb.numReplaced == slot.nDerefRead + slot.nOtherRead) {
                srcBb.statements[slot.setLoc.stmtIdx] = MIRStatement();
                for (const auto& dropLoc : slot.dropLocs) {
                    auto& dropBb = fcn.blocks[dropLoc.bbIdx];
                    MIR_ASSERT(state, dropLoc.stmtIdx == dropBb.statements.size() && dropBb.terminator.is_Drop(), "Recorded drop is no longer a terminator");
                    auto target = dropBb.terminator.as_Drop().target;
                    dropBb.terminator = MIRTerminator::make_Goto(target);
                }
            } else {
            }

            if (replaceCb.numReplaced > 0) {
                changed = true;
                return changed;
            }
        }

        return changed;
    }

    bool MIROptimiseDeTemporaryReborrowOfUnused(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;

        struct Poss {
            OptimiseStmtRef pos;
            MIRLValue::Storage slot;
            MIRLValue::Storage replace;
            bool used;

            Poss(OptimiseStmtRef pos, MIRLValue::Storage slot, MIRLValue::Storage replace)
                : pos(pos)
                , slot(mv$(slot))
                , replace(mv$(replace))
                , used(false)
            {
            }
        };

        std::vector<Poss> possible;
        for (const auto& blk : fcn.blocks) {
            for (const auto& stmt : blk.statements) {
                state.setCurStmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());

                if (!stmt.is_Assign()) {
                    continue;
                }
                const auto& se = stmt.as_Assign();
                if (!se.dst.is_Local()) {
                    continue;
                }
                if (!se.src.is_Borrow()) {
                    continue;
                }
                const auto& re = se.src.as_Borrow();
                if (!(re.val.root.is_Local() || re.val.root.is_Argument())) {
                    continue;
                }
                if (!(re.val.wrappers.size() == 1 && re.val.wrappers[0].is_Deref())) {
                    continue;
                }
                const auto& srcTy = re.val.root.is_Local() ? fcn.locals[re.val.root.as_Local()] : state.args[re.val.root.as_Argument()].second;
                const auto& dstTy = fcn.locals[se.dst.as_Local()];
                if (srcTy != dstTy) {
                    continue;
                }

                auto pos = OptimiseStmtRef(state.getCurBlock(), state.getCurStmtOfs());
                possible.push_back(Poss(pos, re.val.root.clone(), se.dst.root.clone()));
            }
        }
        if (possible.size() == 0) {
            return false;
        }
        {
            std::vector<unsigned int> incomingEdges(fcn.blocks.size());
            for (const auto& block : fcn.blocks) {
                struct CountIncoming final: public MIRTargetVisitor {
                    std::vector<unsigned int>& incomingEdges;

                    explicit CountIncoming(std::vector<unsigned int>& incomingEdges)
                        : incomingEdges(incomingEdges)
                    {
                    }

                    void visitTarget(const MIRBasicBlockId& target) override {
                        incomingEdges[target]++;
                    }
                } countIncoming{incomingEdges};

                visitTerminatorTarget(block.terminator, countIncoming);
            }
            std::vector<unsigned int> acyclicBlocks;
            acyclicBlocks.reserve(fcn.blocks.size());
            for (unsigned int i = 0; i < incomingEdges.size(); i++) {
                if (incomingEdges[i] == 0) {
                    acyclicBlocks.push_back(i);
                }
            }
            for (size_t i = 0; i < acyclicBlocks.size(); i++) {
                struct PeelAcyclic final: public MIRTargetVisitor {
                    std::vector<unsigned int>& incomingEdges;
                    std::vector<unsigned int>& acyclicBlocks;

                    PeelAcyclic(std::vector<unsigned int>& incomingEdges, std::vector<unsigned int>& acyclicBlocks)
                        : incomingEdges(incomingEdges)
                        , acyclicBlocks(acyclicBlocks)
                    {
                    }

                    void visitTarget(const MIRBasicBlockId& target) override {
                        if (--incomingEdges[target] == 0) {
                            acyclicBlocks.push_back(target);
                        }
                    }
                } peelAcyclic{incomingEdges, acyclicBlocks};

                visitTerminatorTarget(fcn.blocks[acyclicBlocks[i]].terminator, peelAcyclic);
            }

            if (acyclicBlocks.size() != fcn.blocks.size()) {
                std::vector<bool> loops(fcn.blocks.size(), false);
                {
                    const size_t nBlocks = fcn.blocks.size();
                    std::vector<unsigned> index(nBlocks, ~0u);
                    std::vector<unsigned> lowlink(nBlocks, 0);
                    std::vector<bool> onStack(nBlocks, false);
                    std::vector<unsigned> sccStack;
                    unsigned nextIndex = 0;

                    std::vector<std::vector<unsigned>> succs(nBlocks);
                    for (const auto& block : fcn.blocks) {
                        unsigned bbIdx = &block - fcn.blocks.data();

                        struct CollectSuccs final: public MIRTargetVisitor {
                            size_t nBlocks;
                            unsigned bbIdx;
                            std::vector<std::vector<unsigned>>& succs;
                            std::vector<bool>& loops;

                            CollectSuccs(size_t nBlocks, unsigned bbIdx, std::vector<std::vector<unsigned>>& succs, std::vector<bool>& loops)
                                : nBlocks(nBlocks)
                                , bbIdx(bbIdx)
                                , succs(succs)
                                , loops(loops)
                            {
                            }

                            void visitTarget(const MIRBasicBlockId& target) override {
                                if (target < nBlocks) {
                                    succs[bbIdx].push_back(target);
                                    if (target == bbIdx) {
                                        loops[bbIdx] = true;
                                    }
                                }
                            }
                        } collectSuccs{nBlocks, bbIdx, succs, loops};

                        visitTerminatorTarget(block.terminator, collectSuccs);
                    }

                    struct Frame {
                        unsigned bb;
                        size_t next;
                    };

                    std::vector<Frame> callStack;
                    for (unsigned root = 0; root < nBlocks; root++) {
                        if (index[root] != ~0u) {
                            continue;
                        }
                        callStack.push_back({root, 0});
                        index[root] = lowlink[root] = nextIndex++;
                        sccStack.push_back(root);
                        onStack[root] = true;
                        while (!callStack.empty()) {
                            auto& fr = callStack.back();
                            if (fr.next < succs[fr.bb].size()) {
                                unsigned w = succs[fr.bb][fr.next++];
                                if (index[w] == ~0u) {
                                    index[w] = lowlink[w] = nextIndex++;
                                    sccStack.push_back(w);
                                    onStack[w] = true;
                                    callStack.push_back({w, 0});
                                } else if (onStack[w]) {
                                    lowlink[fr.bb] = std::min(lowlink[fr.bb], index[w]);
                                }
                            } else {
                                unsigned v = fr.bb;
                                callStack.pop_back();
                                if (!callStack.empty()) {
                                    auto& parent = callStack.back();
                                    lowlink[parent.bb] = std::min(lowlink[parent.bb], lowlink[v]);
                                }
                                if (lowlink[v] == index[v]) {
                                    size_t vPos = sccStack.size();
                                    while (vPos > 0 && sccStack[vPos - 1] != v) {
                                        vPos--;
                                    }
                                    assert(vPos > 0);
                                    vPos--;
                                    bool multi = (sccStack.size() - vPos) > 1;
                                    for (size_t i = vPos; i < sccStack.size(); i++) {
                                        unsigned m = sccStack[i];
                                        onStack[m] = false;
                                        if (multi) {
                                            loops[m] = true;
                                        }
                                    }
                                    sccStack.resize(vPos);
                                }
                            }
                        }
                    }
                }
                for (auto& poss : possible) {
                    poss.used |= loops[poss.pos.bbIdx];
                }
            }
        }

        std::unordered_map<uintptr_t, std::vector<size_t>> possibleBySource;
        for (size_t i = 0; i < possible.size(); i++) {
            possibleBySource[possible[i].slot.getInner()].push_back(i);
        }

        struct MarkUsed final: public LvalueVisitor {
            MIRTypeResolve& state;
            decltype(possible)& possible;
            const decltype(possibleBySource)& possibleBySource;
            const OptimiseStmtRef* pos = nullptr;
            const MIRTerminator::Data_Drop* dropped = nullptr;

            MarkUsed(MIRTypeResolve& state, decltype(possible)& possible, decltype(possibleBySource) possibleBySource)
                : state(state)
                , possible(possible)
                , possibleBySource(possibleBySource)
            {
            }

            bool visitLvalue(const MIRLValue& lv, MIRValUsage /*vu*/) override {
                if (!(lv.root.is_Local() || lv.root.is_Argument())) {
                    return false;
                }
                auto it = possibleBySource.find(lv.root.getInner());
                if (it == possibleBySource.end()) {
                    return false;
                }
                if (dropped && dropped->slot.wrappers.empty() && dropped->slot.root.getInner() == lv.root.getInner()) {
                    return false;
                }
                for (auto possibleIdx : it->second) {
                    auto& p = possible[possibleIdx];
                    if (!(pos && *pos == p.pos)) {
                        p.used = true;
                    }
                }
                return false;
            }
        } markUsed{state, possible, possibleBySource};

        for (const auto& blk : fcn.blocks) {
            for (const auto& stmt : blk.statements) {
                state.setCurStmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());
                auto pos = OptimiseStmtRef(state.getCurBlock(), state.getCurStmtOfs());
                markUsed.pos = &pos;
                markUsed.dropped = nullptr;
                optVisitMirLvalues(stmt, markUsed);
            }
            markUsed.pos = nullptr;
            markUsed.dropped = blk.terminator.opt_Drop();
            optVisitMirLvalues(blk.terminator, markUsed);
        }

        {
            auto ne = std::remove_if(possible.begin(), possible.end(), [&](const Poss& p) {
                return p.used;
            });
            possible.erase(ne, possible.end());
        }
        if (possible.size() == 0) {
            return false;
        }
        std::unordered_set<uintptr_t> sourceSlots;
        std::unordered_map<uintptr_t, uintptr_t> replacements;
        for (auto it = possible.rbegin(); it != possible.rend(); ++it) {
            const auto source = it->slot.getInner();
            const auto destination = it->replace.getInner();
            sourceSlots.insert(source);
            auto next = replacements.find(source);
            replacements[destination] = next == replacements.end() ? source : next->second;
            fcn.blocks[it->pos.bbIdx].statements[it->pos.stmtIdx] = MIRStatement();
        }

        struct ReplaceRoots final: public LvalueVisitorMut {
            MIRTypeResolve& state;
            const decltype(replacements)& replacements;

            ReplaceRoots(MIRTypeResolve& state, decltype(replacements) replacements)
                : state(state)
                , replacements(replacements)
            {
            }

            bool visitLvalue(MIRLValue& lv, MIRValUsage /*vu*/) override {
                if (lv.root.is_Local()) {
                    auto it = replacements.find(lv.root.getInner());
                    if (it != replacements.end()) {
                        lv.root = MIRLValue::Storage::fromInner(it->second);
                    }
                }
                return false;
            }
        } replaceRoots{state, replacements};

        for (auto& blk : fcn.blocks) {
            for (auto& stmt : blk.statements) {
                state.setCurStmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());
                optVisitMirLvaluesMut(stmt, replaceRoots);
            }

            if (auto* drop = blk.terminator.opt_Drop(); drop && drop->slot.wrappers.empty() && (drop->slot.root.is_Local() || drop->slot.root.is_Argument()) && sourceSlots.count(drop->slot.root.getInner()) != 0) {
                auto target = drop->target;
                blk.terminator = MIRTerminator::make_Goto(target);
            }
            optVisitMirLvaluesMut(blk.terminator, replaceRoots);
        }
        changed = true;
        return changed;
    }

    bool MIROptimiseDeTemporary(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;

        changed |= MIROptimiseDeTemporarySingleSetAndUse(state, fcn);
        if (changed) {
            return changed;
        }
        changed |= MIROptimiseDeTemporaryBorrows(state, fcn);
        if (changed) {
            return changed;
        }
        changed |= MIROptimiseDeTemporaryReborrowOfUnused(state, fcn);

        for (unsigned int bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
            auto& bb = fcn.blocks[bbIdx];
            std::map<unsigned, unsigned> localAssignments;
            // TODO: Keep track of what variables would invalidate a local (and compound on assignment)
            std::vector<unsigned> statementsToRemove;

            struct CheckInvalidate final: public LvalueVisitor {
                MIRTypeResolve& state;
                const MIRBasicBlock& bb;
                decltype(localAssignments)& localAssignments;

                CheckInvalidate(MIRTypeResolve& state, const MIRBasicBlock& bb, decltype(localAssignments)& localAssignments)
                    : state(state)
                    , bb(bb)
                    , localAssignments(localAssignments)
                {
                }

                struct SourceMentions final: public LvalueVisitor {
                    const MIRLValue& lv;
                    bool hit = false;

                    explicit SourceMentions(const MIRLValue& lv)
                        : lv(lv)
                    {
                    }

                    bool visitLvalue(const MIRLValue& sLv, MIRValUsage /*vu*/) override {
                        if (sLv.root == lv.root) {
                            hit = true;
                            return true;
                        }
                        return false;
                    }
                };

                bool visitLvalue(const MIRLValue& lv, MIRValUsage vu) override {
                    for (auto it = localAssignments.begin(); it != localAssignments.end();) {
                        bool invalidated = false;
                        const auto& srcRvalue = bb.statements[it->second].as_Assign().src;

                        if (lv.root.is_Local() && it->first == lv.root.as_Local()) {
                            switch (vu) {
                                case MIRValUsage::Borrow:
                                case MIRValUsage::Write:
                                    invalidated = true;
                                    break;
                                default:
                                    break;
                            }
                        } else {
                            switch (vu) {
                                case MIRValUsage::Borrow:
                                case MIRValUsage::Write:
                                case MIRValUsage::Move: {
                                    SourceMentions mentions{lv};
                                    optVisitMirLvalues(srcRvalue, mentions);
                                    if (mentions.hit) {
                                        invalidated = true;
                                    }
                                    break;
                                }
                                case MIRValUsage::Read:
                                    break;
                            }
                        }

                        if (invalidated) {
                            it = localAssignments.erase(it);
                        } else {
                            ++it;
                        }
                    }
                    return false;
                }
            } cbCheckInvalidate{state, bb, localAssignments};

            struct ApplyReplacements final: public LvalueVisitorMut {
                MIRTypeResolve& state;
                const MIRBasicBlock& bb;
                decltype(localAssignments)& localAssignments;
                decltype(statementsToRemove)& statementsToRemove;
                bool& changed;

                ApplyReplacements(MIRTypeResolve& state, const MIRBasicBlock& bb, decltype(localAssignments)& localAssignments, decltype(statementsToRemove)& statementsToRemove, bool& changed)
                    : state(state)
                    , bb(bb)
                    , localAssignments(localAssignments)
                    , statementsToRemove(statementsToRemove)
                    , changed(changed)
                {
                }

                bool visitLvalue(MIRLValue& topLv, MIRValUsage topUsage) override {
                    // TODO: Handle partial moves (only delete assignment if the value is fully used)

                    if (topLv.root.is_Local()) {
                        bool topLevel = topLv.wrappers.empty();
                        auto ilv = MIRLValue::newLocal(topLv.root.as_Local());
                        auto it = localAssignments.find(topLv.root.as_Local());
                        if (it != localAssignments.end()) {
                            const auto& newVal = bb.statements[it->second].as_Assign().src.as_Use();
                            if (state.lvalueIsCopy(ilv)) {
                                topLv = newVal.cloneWrapped(topLv.wrappers.begin(), topLv.wrappers.end());
                                changed = true;
                            } else if (topLevel && topUsage == MIRValUsage::Move) {
                                // TODO: DstMeta/DstPtr _doesn't_ move, so shouldn't trigger this.
                                topLv = newVal.clone();
                                statementsToRemove.push_back(it->second);
                                localAssignments.erase(it);
                                changed = true;
                            } else {
                                localAssignments.erase(it);
                            }
                        }
                    }
                    return true;
                }
            } cbApplyReplacements{state, bb, localAssignments, statementsToRemove, changed};

            for (unsigned int stmtIdx = 0; stmtIdx < bb.statements.size(); stmtIdx++) {
                auto& stmt = bb.statements[stmtIdx];
                state.setCurStmt(bbIdx, stmtIdx);

                optVisitMirLvalues(stmt, cbCheckInvalidate);

                optVisitMirLvaluesMut(stmt, cbApplyReplacements);

                if (stmt.is_Assign() && stmt.as_Assign().dst.is_Local() && stmt.as_Assign().src.is_Use()) {
                    const auto& dstLv = stmt.as_Assign().dst;
                    const auto& srcLv = stmt.as_Assign().src.as_Use();

                    struct SameRoot final: public LvalueVisitor {
                        const MIRLValue& dstLv;

                        explicit SameRoot(const MIRLValue& dstLv)
                            : dstLv(dstLv)
                        {
                        }

                        bool visitLvalue(const MIRLValue& lv, MIRValUsage /*vu*/) override {
                            return lv.root == dstLv.root;
                        }
                    } sameRoot{dstLv};

                    if (!optVisitMirLvaluesInner(srcLv, MIRValUsage::Read, sameRoot) && !std::any_of(srcLv.wrappers.begin(), srcLv.wrappers.end(), [](const auto& w) {
                        return w.is_Deref();
                    })) {
                        localAssignments.insert(std::make_pair(stmt.as_Assign().dst.as_Local(), stmtIdx));
                    }
                }
            }

            state.setCurStmtTerm(bbIdx);
            optVisitMirLvalues(bb.terminator, cbCheckInvalidate);
            if (!bb.terminator.is_Switch()) {
                optVisitMirLvaluesMut(bb.terminator, cbApplyReplacements);
            }

            std::sort(statementsToRemove.begin(), statementsToRemove.end());
            while (!statementsToRemove.empty()) {
                // TODO: Handle partial moves here?
                // TODO: Is there some edge case I'm missing where the assignment shouldn't be removed?

                bb.statements.erase(bb.statements.begin() + statementsToRemove.back());
                statementsToRemove.pop_back();

                changed = true;
            }
        }

        return changed;
    }

    bool MIROptimiseCommonStatements(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;

        for (size_t bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
            state.setCurStmt(bbIdx, 0);

            bool skip = false;
            std::vector<size_t> sources;
            for (size_t bb2Idx = 0; bb2Idx < fcn.blocks.size() && !skip; bb2Idx++) {
                const auto& blk = fcn.blocks[bb2Idx];
                // TODO: Handle non-Goto branches? (e.g. calls)
                if (blk.terminator.is_Goto() && blk.terminator.as_Goto() == bbIdx) {
                    if (blk.statements.empty()) {
                        skip = true;
                        break;
                    }
                    if (!sources.empty()) {
                        if (blk.statements.back() != fcn.blocks[sources.front()].statements.back()) {
                            skip = true;
                            break;
                        }
                    }
                    sources.push_back(bb2Idx);
                } else {
                    struct CheckPointsBack final: public MIRTargetVisitor {
                        const MIRTypeResolve& state;
                        const MIRBasicBlock& blk;
                        size_t bbIdx;
                        size_t bb2Idx;
                        bool& skip;

                        CheckPointsBack(const MIRTypeResolve& state, const MIRBasicBlock& blk, size_t bbIdx, size_t bb2Idx, bool& skip)
                            : state(state)
                            , blk(blk)
                            , bbIdx(bbIdx)
                            , bb2Idx(bb2Idx)
                            , skip(skip)
                        {
                        }

                        void visitTarget(const MIRBasicBlockId& target) override {
                            if (target == bbIdx) {
                                skip = true;
                            }
                        }
                    } checkPointsBack{state, blk, bbIdx, bb2Idx, skip};

                    visitTerminatorTarget(blk.terminator, checkPointsBack);
                }
            }

            if (!skip && sources.size() > 1) {
                // TODO: Should this search for any common statements?

                auto stmt = std::move(fcn.blocks[sources.front()].statements.back());
                for (auto idx : sources) {
                    fcn.blocks[idx].statements.pop_back();
                }
                fcn.blocks[bbIdx].statements.insert(fcn.blocks[bbIdx].statements.begin(), std::move(stmt));
            }
        }
        return changed;
    }

    bool MIROptimiseUnifyTemporaries(MIRTypeResolve& state, MIRFunction& fcn) {
        bool replacementNeeded = false;
        std::vector<bool> replacable(fcn.locals.size());
        {
            unsigned int nFound = 0;
            for (unsigned int tmpidx = 0; tmpidx < fcn.locals.size(); tmpidx++) {
                if (replacable[tmpidx]) {
                    continue;
                }
                for (unsigned int i = tmpidx + 1; i < fcn.locals.size(); i++) {
                    if (replacable[i]) {
                        continue;
                    }
                    if (fcn.locals[i] == fcn.locals[tmpidx]) {
                        replacable[i] = true;
                        replacable[tmpidx] = true;
                        nFound++;
                    }
                }
            }
            if (nFound == 0) {
                return false;
            }
        }

        // TODO: Only calculate lifetimes for replacable locals
        auto lifetimes = MIRHelperGetLifetimes(state, fcn, /*dump_debug=*/true, /*mask=*/&replacable);
        std::vector<MIRValueLifetime> slotLifetimes = mv$(lifetimes.slots);

        std::map<unsigned int, unsigned int> replacements;
        std::vector<bool> visited(fcn.locals.size());
        for (unsigned int localIdx = 0; localIdx < fcn.locals.size(); localIdx++) {
            if (!replacable[localIdx]) {
                continue;
            }
            if (visited[localIdx]) {
                continue;
            }
            if (!slotLifetimes[localIdx].isUsed()) {
                continue;
            }
            visited[localIdx] = true;

            for (unsigned int i = localIdx + 1; i < fcn.locals.size(); i++) {
                if (!replacable[i]) {
                    continue;
                }
                if (fcn.locals[i] != fcn.locals[localIdx]) {
                    continue;
                }
                if (!slotLifetimes[i].isUsed()) {
                    continue;
                }
                if (slotLifetimes[localIdx].overlaps(slotLifetimes[i])) {
                    continue;
                }
                slotLifetimes[localIdx].unify(slotLifetimes[i]);
                replacements[i] = localIdx;
                replacementNeeded = true;
                visited[i] = true;
            }
        }

        if (replacementNeeded) {
            struct ReplaceLocals final: public LvalueVisitorMut {
                MIRTypeResolve& state;
                const decltype(replacements)& replacements;

                ReplaceLocals(MIRTypeResolve& state, decltype(replacements) replacements)
                    : state(state)
                    , replacements(replacements)
                {
                }

                bool visitLvalue(MIRLValue& lv, MIRValUsage /*vu*/) override {
                    if (lv.root.is_Local()) {
                        auto it = replacements.find(lv.root.as_Local());
                        if (it != replacements.end()) {
                            lv.root = MIRLValue::Storage::newLocal(it->second);
                            return true;
                        }
                    }
                    return false;
                }
            } replaceLocals{state, replacements};

            optVisitMirLvaluesMut(state, fcn, replaceLocals);

            // TODO: Replace in ScopeEnd too?
        }

        return replacementNeeded;
    }

    bool MIROptimiseUnifyBlocks(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;

        struct H {
            static size_t blockHash(const MIRBasicBlock& block) {
                size_t rv = block.statements.size();
                auto add = [&](size_t v) {
                    rv ^= v + 0x9e3779b9 + (rv << 6) + (rv >> 2);
                };
                for (const auto& statement : block.statements) {
                    add(statement.tag());
                }
                add(block.terminator.tag());

                struct HashTargets final: public MIRTargetVisitor {
                    decltype(add)& add;

                    explicit HashTargets(decltype(add)& add)
                        : add(add)
                    {
                    }

                    void visitTarget(const MIRBasicBlockId& target) override {
                        add(target);
                    }
                } hashTargets{add};

                visitTerminatorTarget(block.terminator, hashTargets);
                return rv;
            }

            static bool blocksEqual(const MIRBasicBlock& a, const MIRBasicBlock& b) {
                if (a.isCleanup != b.isCleanup) {
                    return false;
                }
                if (a.statements.size() != b.statements.size()) {
                    return false;
                }
                for (unsigned int i = 0; i < a.statements.size(); i++) {
                    if (a.statements[i].tag() != b.statements[i].tag()) {
                        return false;
                    }
                    {
                        auto& tuMatch = a.statements[i];
                        auto& tuMatch2 = b.statements[i];
                        switch (tuMatch.tag()) {
                            case MIRStatement::TAG_Assign: {
                                auto& ae = tuMatch.as_Assign();
                                auto& be = tuMatch2.as_Assign();
                                if (ae.dst != be.dst) {
                                    return false;
                                }
                                if (ae.src != be.src) {
                                    return false;
                                }
                                break;
                            }
                            case MIRStatement::TAG_Asm: {
                                auto& ae = tuMatch.as_Asm();
                                auto& be = tuMatch2.as_Asm();
                                if (ae.tpl != be.tpl) {
                                    return false;
                                }
                                if (ae.outputs != be.outputs) {
                                    return false;
                                }
                                if (ae.inputs != be.inputs) {
                                    return false;
                                }
                                if (ae.clobbers != be.clobbers) {
                                    return false;
                                }
                                if (ae.flags != be.flags) {
                                    return false;
                                }
                                break;
                            }
                            case MIRStatement::TAG_Asm2: {
                                auto& ae = tuMatch.as_Asm2();
                                auto& be = tuMatch2.as_Asm2();
                                if (ae.lines != be.lines) {
                                    return false;
                                }
                                if (!(ae.options == be.options)) {
                                    return false;
                                }
                                if (ae.params != be.params) {
                                    return false;
                                }
                                break;
                            }
                            case MIRStatement::TAG_SetDropFlag: {
                                auto& ae = tuMatch.as_SetDropFlag();
                                auto& be = tuMatch2.as_SetDropFlag();
                                if (ae.idx != be.idx) {
                                    return false;
                                }
                                if (ae.newVal != be.newVal) {
                                    return false;
                                }
                                if (ae.other != be.other) {
                                    return false;
                                }
                                break;
                            }
                            case MIRStatement::TAG_LoadDropFlag: {
                                auto& ae = tuMatch.as_LoadDropFlag();
                                auto& be = tuMatch2.as_LoadDropFlag();
                                if (ae.idx != be.idx) {
                                    return false;
                                }
                                if (ae.slot != be.slot) {
                                    return false;
                                }
                                if (ae.bitIndex != be.bitIndex) {
                                    return false;
                                }
                                break;
                            }
                            case MIRStatement::TAG_SaveDropFlag: {
                                auto& ae = tuMatch.as_SaveDropFlag();
                                auto& be = tuMatch2.as_SaveDropFlag();
                                if (ae.idx != be.idx) {
                                    return false;
                                }
                                if (ae.slot != be.slot) {
                                    return false;
                                }
                                if (ae.bitIndex != be.bitIndex) {
                                    return false;
                                }
                                break;
                            }
                            case MIRStatement::TAG_ScopeEnd: {
                                auto& ae = tuMatch.as_ScopeEnd();
                                auto& be = tuMatch2.as_ScopeEnd();
                                if (ae.slots != be.slots) {
                                    return false;
                                }
                                break;
                            }
                        }
                    }
                }
                return a.terminator == b.terminator;
            }
        };

        std::map<unsigned int, unsigned int> replacements;

        struct HashedBlock {
            size_t hash;
            unsigned int bbIdx;
        };

        ThinVector<HashedBlock> hashedBlocks;
        ThinVector<unsigned int> groupReps;
        for (;;) {
            replacements.clear();
            hashedBlocks.clear();
            for (unsigned int bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
                if (fcn.blocks[bbIdx].terminator.isDead()) {
                    continue;
                }
                if (fcn.blocks[bbIdx].terminator.is_Incomplete() && fcn.blocks[bbIdx].statements.size() == 0) {
                    continue;
                }
                hashedBlocks.push_back(HashedBlock{H::blockHash(fcn.blocks[bbIdx]), bbIdx});
            }
            std::sort(hashedBlocks.begin(), hashedBlocks.end(), [](const HashedBlock& a, const HashedBlock& b) {
                return a.hash != b.hash ? a.hash < b.hash : a.bbIdx < b.bbIdx;
            });
            for (size_t i = 0; i < hashedBlocks.size();) {
                size_t j = i;
                while (j < hashedBlocks.size() && hashedBlocks[j].hash == hashedBlocks[i].hash) {
                    j++;
                }
                if (j - i > 1) {
                    groupReps.clear();
                    for (size_t k = i; k < j; k++) {
                        auto bbIdx = hashedBlocks[k].bbIdx;
                        bool found = false;
                        for (auto candidate : groupReps) {
                            if (H::blocksEqual(fcn.blocks[candidate], fcn.blocks[bbIdx])) {
                                replacements[bbIdx] = candidate;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            groupReps.push_back(bbIdx);
                        }
                    }
                }
                i = j;
            }

            if (replacements.empty()) {
                break;
            }

            auto patchTgt = [&replacements](MIRBasicBlockId& tgt) {
                auto it = replacements.find(tgt);
                if (it != replacements.end()) {
                    tgt = it->second;
                }
            };
            for (auto& bb : fcn.blocks) {
                if (bb.terminator.isDead()) {
                    continue;
                }

                struct PatchTargets final: public MIRTargetVisitorMut {
                    decltype(patchTgt)& patchTgt;

                    explicit PatchTargets(decltype(patchTgt)& patchTgt)
                        : patchTgt(patchTgt)
                    {
                    }

                    void visitTarget(MIRBasicBlockId& target) override {
                        patchTgt(target);
                    }
                } patchTargets{patchTgt};

                visitTerminatorTargetMut(bb.terminator, patchTargets);
            }

            for (const auto& r : replacements) {
                fcn.blocks[r.first] = MIRBasicBlock{};
            }

            changed = true;
        }
        return changed;
    }

    // TODO: Is this needed now that SplitAggregates exists?

    bool MIROptimisePropagateKnownValues(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changeHappend = false;
        std::vector<size_t> blockOrigins(fcn.blocks.size(), SIZE_MAX);
        {
            std::vector<unsigned int> blockUses(fcn.blocks.size());
            std::vector<bool> visited(fcn.blocks.size());
            std::vector<MIRBasicBlockId> toVisit;
            toVisit.push_back(0);
            blockUses[0]++;
            while (toVisit.size() > 0) {
                auto bb = toVisit.back();
                toVisit.pop_back();
                if (visited[bb]) {
                    continue;
                }
                visited[bb] = true;
                const auto& block = fcn.blocks[bb];

                struct RecordOrigins final: public MIRTargetVisitor {
                    const std::vector<bool>& visited;
                    std::vector<MIRBasicBlockId>& toVisit;
                    std::vector<unsigned int>& blockUses;
                    std::vector<size_t>& blockOrigins;
                    MIRBasicBlockId bb;

                    RecordOrigins(const std::vector<bool>& visited, std::vector<MIRBasicBlockId>& toVisit, std::vector<unsigned int>& blockUses, std::vector<size_t>& blockOrigins, MIRBasicBlockId bb)
                        : visited(visited)
                        , toVisit(toVisit)
                        , blockUses(blockUses)
                        , blockOrigins(blockOrigins)
                        , bb(bb)
                    {
                    }

                    void visitTarget(const MIRBasicBlockId& idx) override {
                        if (!visited[idx]) {
                            toVisit.push_back(idx);
                        }
                        if (blockUses[idx] == 0) {
                            blockOrigins[idx] = bb;
                        } else {
                            blockOrigins[idx] = SIZE_MAX;
                        }
                        blockUses[idx]++;
                    }
                } recordOrigins{visited, toVisit, blockUses, blockOrigins, bb};

                visitTerminatorTarget(block.terminator, recordOrigins);
            }
        }

        auto getField = [&](const MIRLValue& slotLvalue, unsigned field, size_t startBbIdx, size_t startStmtIdx) -> const MIRLValue* {
            bool slotCopy = state.lvalueIsCopy(slotLvalue);
            auto bbIdx = startBbIdx;
            auto stmtIdx = startStmtIdx;
            for (;;) {
                const auto& bb = fcn.blocks[bbIdx];
                while (stmtIdx--) {
                    if (stmtIdx == bb.statements.size()) {
                        if (checkInvalidatesLvalue(bb.terminator, slotLvalue, slotCopy)) {
                            return nullptr;
                        }
                        continue;
                    }
                    const auto& stmt = bb.statements[stmtIdx];
                    if (const auto* se = stmt.opt_Assign()) {
                        if (se->dst == slotLvalue) {
                            if (!se->src.is_Tuple()) {
                                return nullptr;
                            }
                            const auto& srcParam = se->src.as_Tuple().vals.at(field);
                            // TODO: Support returning a Param
                            if (!srcParam.is_LValue()) {
                                return nullptr;
                            }
                            const auto& srcLval = srcParam.as_LValue();
                            bool srcCopy = state.lvalueIsCopy(srcLval);
                            auto endBbIdx = bbIdx;
                            auto endStmtIdx = stmtIdx;
                            bbIdx = startBbIdx;
                            stmtIdx = startStmtIdx;
                            for (;;) {
                                const auto& bb = fcn.blocks[bbIdx];
                                while (stmtIdx--) {
                                    if (bbIdx == endBbIdx && stmtIdx == endStmtIdx) {
                                        return &srcLval;
                                    }
                                    if (stmtIdx == bb.statements.size()) {
                                        if (checkInvalidatesLvalue(bb.terminator, srcLval, srcCopy)) {
                                            return nullptr;
                                        }
                                        continue;
                                    }
                                    if (checkInvalidatesLvalue(bb.statements[stmtIdx], srcLval, srcCopy)) {
                                        return nullptr;
                                    }
                                }
                                assert(blockOrigins[bbIdx] != SIZE_MAX);
                                bbIdx = blockOrigins[bbIdx];
                                stmtIdx = fcn.blocks[bbIdx].statements.size() + 1;
                            }
                            UNREACHABLE();
                        }
                    }

                    if (checkInvalidatesLvalue(stmt, slotLvalue, slotCopy)) {
                        return nullptr;
                    }
                }
                if (blockOrigins[bbIdx] == SIZE_MAX) {
                    break;
                }
                bbIdx = blockOrigins[bbIdx];
                stmtIdx = fcn.blocks[bbIdx].statements.size() + 1;
            }
            return nullptr;
        };
        for (auto& block : fcn.blocks) {
            size_t bbIdx = &block - &fcn.blocks.front();
            for (size_t i = 0; i < block.statements.size(); i++) {
                state.setCurStmt(bbIdx, i);

                struct FieldOriginRewrite final: public LvalueVisitorMut {
                    MIRTypeResolve& state;
                    decltype(getField)& getField;
                    size_t bbIdx;
                    size_t i;
                    bool& changeHappend;

                    FieldOriginRewrite(MIRTypeResolve& state, decltype(getField)& getField, size_t bbIdx, size_t i, bool& changeHappend)
                        : state(state)
                        , getField(getField)
                        , bbIdx(bbIdx)
                        , i(i)
                        , changeHappend(changeHappend)
                    {
                    }

                    bool visitLvalue(MIRLValue& lv, MIRValUsage vu) override {
                        if (vu == MIRValUsage::Read && lv.wrappers.size() > 1 && lv.wrappers.front().is_Field() && lv.root.is_Local()) {
                            auto fieldIndex = lv.wrappers.front().as_Field();
                            auto innerLv = MIRLValue::newLocal(lv.root.as_Local());
                            auto outerLv = MIRLValue::newField(innerLv.clone(), fieldIndex);
                            // TODO: This value _must_ be Copy for this optimisation to work.

                            HIRTypeRef tmp;
                            if (!state.resolve.typeIsCopy(state.sp, state.getLvalueType(tmp, innerLv))) {
                                return false;
                            }
                            const auto* sourceLvalue = getField(innerLv, fieldIndex, bbIdx, i);
                            if (sourceLvalue) {
                                if (outerLv != *sourceLvalue) {
                                    lv = sourceLvalue->cloneWrapped(lv.wrappers.begin() + 1, lv.wrappers.end());
                                    changeHappend = true;
                                } else {
                                }
                                return false;
                            }
                        }
                        return false;
                    }
                } fieldOriginRewrite{state, getField, bbIdx, i, changeHappend};

                optVisitMirLvaluesMut(block.statements[i], fieldOriginRewrite);
            }
        }
        return changeHappend;
    }

    bool MIROptimiseConstPropagate(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;
        auto roundFloatValue = [&](FloatValue value, HIRCoreType type) {
            switch (type) {
                case HIRCoreType::F16:
                    return FloatValue(static_cast<double>(static_cast<float>(F16(value))));
                case HIRCoreType::F32:
                    return FloatValue(static_cast<double>(static_cast<float>(value)));
                case HIRCoreType::F64:
                    return FloatValue(static_cast<double>(value));
                case HIRCoreType::F128:
                    return value;
                default:
                    MIR_BUG(state, "Rounding non-float constant " << type);
            }
        };
        auto makeFloatArithmeticResult = [&](FloatValue value, HIRCoreType type) {
            value = roundFloatValue(value, type);
            if (floatValueIsNan(value)) {
                value = positiveNanFloatValue();
            }
            return MIRConstant::make_Float({value, type});
        };

        for (auto& bb : fcn.blocks) {
            state.setCurStmtTerm(bb);
            if (!bb.terminator.is_Call()) {
                continue;
            }
            auto& te = bb.terminator.as_Call();
            if (!te.fcn.is_Intrinsic()) {
                continue;
            }
            const auto& tef = te.fcn.as_Intrinsic();
            if (tef.name == "size_of") {
                size_t sizeVal = 0;
                if (TargetGetSizeOf(state.sp, state.resolve, tef.params.types.at(0), sizeVal)) {
                    auto val = MIRConstant::make_Uint({U128(sizeVal), HIRCoreType::Usize});
                    bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), mv$(val)}));
                    bb.terminator = MIRTerminator::make_Goto(te.retBlock);
                    changed = true;
                }
            } else if (tef.name == "size_of_val") {
                size_t sizeVal = 0, tmp;
                if (TargetGetSizeAndAlignOf(state.sp, state.resolve, tef.params.types.at(0), sizeVal, tmp) && sizeVal != SIZE_MAX) {
                    auto val = MIRConstant::make_Uint({U128(sizeVal), HIRCoreType::Usize});
                    bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), mv$(val)}));
                    bb.terminator = MIRTerminator::make_Goto(te.retBlock);
                    changed = true;
                }
            } else if (tef.name == "align_of" || tef.name == "min_align_of") {
                size_t alignVal = 0;
                if (TargetGetAlignOf(state.sp, state.resolve, tef.params.types.at(0), alignVal)) {
                    auto val = MIRConstant::make_Uint({U128(alignVal), HIRCoreType::Usize});
                    bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), mv$(val)}));
                    bb.terminator = MIRTerminator::make_Goto(te.retBlock);
                    changed = true;
                }
            } else if (tef.name == "min_align_of_val") {
                size_t alignVal = 0;
                size_t sizeVal = 0;
                if (TargetGetSizeAndAlignOf(state.sp, state.resolve, tef.params.types.at(0), sizeVal, alignVal) && alignVal > 0) {
                    auto val = MIRConstant::make_Uint({U128(alignVal), HIRCoreType::Usize});
                    bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), mv$(val)}));
                    bb.terminator = MIRTerminator::make_Goto(te.retBlock);
                    changed = true;
                }
            } else if (tef.name == "bswap" && (tef.params.types.at(0) == HIRCoreType::U8 || tef.params.types.at(0) == HIRCoreType::I8)) {
                if (auto* e = te.args.at(0).opt_LValue()) {
                    bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), mv$(*e)}));
                } else {
                    bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), mv$(te.args.at(0).as_Constant())}));
                }
                bb.terminator = MIRTerminator::make_Goto(te.retBlock);
                changed = true;
            } else if (tef.name == "trustme_slice_len") {
                MIR_ASSERT(state, te.args.at(0).is_LValue(), "Argument to `trustme_slice_len` must be a lvalue");
                auto& e = te.args.at(0).as_LValue();
                bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), MIRRValue::make_DstMeta({mv$(e)})}));
                bb.terminator = MIRTerminator::make_Goto(te.retBlock);
                changed = true;
            } else if (tef.name == "needs_drop") {
                const auto& ty = tef.params.types.at(0);
                if (!visitTyWith(ty, [](const HIRTypeData* ty) -> bool {
                    return ty->is_Generic() || ((*ty).is_Path() && ((*ty).as_Path().binding.is_Unbound()));
                })) {
                    bool needsDrop = state.resolve.typeNeedsDropGlue(state.sp, ty);
                    bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), MIRRValue::make_Constant(MIRConstant::make_Bool({needsDrop}))}));
                    bb.terminator = MIRTerminator::make_Goto(te.retBlock);
                    changed = true;
                }
            } else {
            }
        }

        // TODO: Use ValState to do full constant propagation across blocks

        for (auto& bb : fcn.blocks) {
            auto bbidx = &bb - &fcn.blocks.front();

            std::map<MIRLValue, MIRConstant> knownValues;
            std::map<MIRLValue, unsigned> knownValuesVar;
            std::map<unsigned, bool> knownDropFlags;

            auto checkLv = [&](const MIRLValue& lv) -> MIRConstant {
                auto it = knownValues.find(lv);
                if (it != knownValues.end()) {
                    return it->second.clone();
                }

                // TODO: If the inner of the value is known,

                //        // TODO: Use HIR::Literal instead so composites can be handled.

                if (lv.wrappers.empty() && lv.root.is_Static()) {
                    MonomorphState ms(state.resolve.hirCrate().types);
                    auto v = state.resolve.getValue(state.sp, lv.root.as_Static(), ms);
                    if (v.is_Static()) {
                        const auto& stat = *v.as_Static();
                        if (stat.valueGenerated && !stat.isMut && state.resolve.typeIsInteriorMutable(state.sp, stat.type) == HIRCompare::Unequal) {
                            const auto el = EncodedLiteralSlice(stat.valueRes);
                            if (stat.type->is_Primitive()) {
                                auto ty = stat.type->as_Primitive();
                                switch (ty) {
                                    case HIRCoreType::Char:
                                    case HIRCoreType::Usize:
                                    case HIRCoreType::U128:
                                    case HIRCoreType::U64:
                                    case HIRCoreType::U32:
                                    case HIRCoreType::U16:
                                    case HIRCoreType::U8:
                                        return MIRConstant::make_Uint({el.readUint(el.size), ty});
                                    case HIRCoreType::Bool:
                                        return MIRConstant::make_Bool({el.readUint(el.size) != 0});
                                    case HIRCoreType::Isize:
                                    case HIRCoreType::I128:
                                    case HIRCoreType::I64:
                                    case HIRCoreType::I32:
                                    case HIRCoreType::I16:
                                    case HIRCoreType::I8:
                                        return MIRConstant::make_Int({el.readSint(el.size), ty});
                                    case HIRCoreType::F16:
                                    case HIRCoreType::F32:
                                    case HIRCoreType::F64:
                                    case HIRCoreType::F128:
                                        return MIRConstant::make_Float({el.readFloat(el.size), ty});
                                    case HIRCoreType::Str:
                                        MIR_BUG(state, "Constant of type `str`?");
                                }
                            }
                            if (stat.type->is_Borrow()) {
                                // TODO: Read the borrow, and store
                            }
                        }
                    }
                }

                return MIRConstant::make_ItemAddr({});
            };
            auto checkParam = [&](MIRParam& p) {
                if (const auto* pe = p.opt_LValue()) {
                    auto nv = checkLv(*pe);
                    if (nv.is_ItemAddr() && !nv.as_ItemAddr()) {
                    } else {
                        p = mv$(nv);
                        changed = true;
                    }
                }
            };

            struct EditLval final: public LvalueVisitorMut {
                MIRTypeResolve& state;
                decltype(knownValues)& knownValues;
                bool& changed;

                EditLval(MIRTypeResolve& state, decltype(knownValues)& knownValues, bool& changed)
                    : state(state)
                    , knownValues(knownValues)
                    , changed(changed)
                {
                }

                bool visitLvalue(MIRLValue& lv, MIRValUsage /*vu*/) override {
                    for (auto& w : lv.wrappers) {
                        if (w.is_Index()) {
                            auto it = knownValues.find(MIRLValue::newLocal(w.as_Index()));
                            if (it != knownValues.find(lv) && !it->second.is_Const() && !it->second.is_Generic()) {
                                MIR_ASSERT(state, it->second.is_Uint(), "Indexing with non-Uint constant - " << it->second);
                                MIR_ASSERT(state, it->second.as_Uint().t == HIRCoreType::Usize, "Indexing with non-usize constant - " << it->second);
                                auto idx = it->second.as_Uint().v;
                                MIR_ASSERT(state, idx < (1 << 30), "Known index is excessively large");
                                w = MIRLValue::Wrapper::newField(idx.truncateU64());
                                changed = true;
                            }
                        }
                    }

                    if (!lv.wrappers.empty() && lv.wrappers.front().is_Deref() && !lv.root.is_Static()) {
                        auto ilv = MIRLValue(lv.root.clone(), {});
                        auto it = knownValues.find(ilv);
                        if (it != knownValues.find(lv)) {
                            if (it->second.is_ItemAddr() && it->second.as_ItemAddr().offset == U128(0)) {
                                lv.wrappers.erase(lv.wrappers.begin());
                                lv.root = MIRLValue::Storage::newStatic(it->second.as_ItemAddr()->clone());
                                changed = true;
                            }
                        }
                    }
                    return true;
                }
            } editLval{state, knownValues, changed};

            for (auto& stmt : bb.statements) {
                auto stmtidx = &stmt - &bb.statements.front();
                state.setCurStmt(bbidx, stmtidx);

                optVisitMirLvaluesMut(stmt, editLval);

                if (auto* e = stmt.opt_Assign()) {
                    struct H {
                        static S128 truncateS(HIRCoreType ct, S128 v) {
                            auto u = H::truncateU(ct, v.getInner());
                            switch (ct) {
                                case HIRCoreType::I8:
                                    return sext(u, 8);
                                case HIRCoreType::I16:
                                    return sext(u, 16);
                                case HIRCoreType::I32:
                                    return sext(u, 32);
                                case HIRCoreType::I64:
                                    return sext(u, 64);
                                case HIRCoreType::I128:
                                    return v;
                                case HIRCoreType::Isize:
                                    return sext(u, TargetGetPointerBits());
                                default:
                                    break;
                            }
                            return v;
                        }

                        static S128 sext(U128 v, unsigned bits) {
                            if ((v >> (bits - 1)) != 0) {
                                return S128(v | (U128::max() << bits));
                            } else {
                                return S128(v);
                            }
                        }

                        static U128 truncateU(HIRCoreType ct, U128 v) {
                            switch (ct) {
                                case HIRCoreType::I8:
                                case HIRCoreType::U8:
                                    return v & U128(0xFF);
                                case HIRCoreType::I16:
                                case HIRCoreType::U16:
                                    return v & U128(0xFFFF);
                                case HIRCoreType::I32:
                                case HIRCoreType::U32:
                                    return v & U128(0xFFFFFFFF);
                                case HIRCoreType::I64:
                                case HIRCoreType::U64:
                                    return v & U128(UINT64_MAX);
                                case HIRCoreType::I128:
                                case HIRCoreType::U128:
                                    return v;
                                case HIRCoreType::Isize:
                                case HIRCoreType::Usize:
                                    if (TargetGetPointerBits() < 64) {
                                        return v & U128(UINT64_MAX >> (64 - TargetGetPointerBits()));
                                    }
                                    return v & U128(UINT64_MAX);
                                case HIRCoreType::Char:
                                    break;
                                default:
                                    break;
                            }
                            return v;
                        }
                    };

                    switch (e->src.tag()) {
                        case MIRRValue::TAG_Use: {
                            auto& se = e->src.as_Use();
                            auto nv = checkLv(se);
                            if (nv.is_ItemAddr() && !nv.as_ItemAddr()) {
                            } else {
                                e->src = MIRRValue::make_Constant(mv$(nv));
                                changed = true;
                            }
                            break;
                        }
                        case MIRRValue::TAG_Constant: {
                            break;
                        }
                        case MIRRValue::TAG_SizedArray: {
                            auto& se = e->src.as_SizedArray();
                            checkParam(se.val);
                            break;
                        }
                        case MIRRValue::TAG_Borrow: {
                            auto& se = e->src.as_Borrow();
                            if (se.type == HIRBorrowType::Shared && se.val.wrappers.empty() && se.val.root.is_Static()) {
                                e->src = MIRRValue::make_Constant(MIRConstant::make_ItemAddr({box$(se.val.root.as_Static())}));
                                changed = true;
                            } else if (se.type == HIRBorrowType::Unique) {
                                knownValues.erase(se.val);
                                knownValuesVar.erase(se.val);
                            }
                            break;
                        }
                        case MIRRValue::TAG_Cast: {
                            auto& se = e->src.as_Cast();
                            MIRConstant newValue;

                            auto nv = checkLv(se.val);
                            if (!nv.is_ItemAddr()) {
                                if (const auto* te = se.type->opt_Primitive()) {
                                    switch (*te) {
                                        case HIRCoreType::U8:
                                        case HIRCoreType::U16:
                                        case HIRCoreType::U32:
                                        case HIRCoreType::U64:
                                        case HIRCoreType::U128:
                                        case HIRCoreType::Usize:
                                            if (const auto* vp = nv.opt_Uint()) {
                                                newValue = MIRConstant::make_Uint({H::truncateU(*te, vp->v), *te});
                                            } else if (const auto* vp = nv.opt_Int()) {
                                                newValue = MIRConstant::make_Uint({H::truncateU(*te, vp->v.getInner()), *te});
                                            } else if (const auto* vp = nv.opt_Bool()) {
                                                newValue = MIRConstant::make_Uint({U128(vp->v ? 1u : 0u), *te});
                                            } else if (const auto* vp = nv.opt_Float()) {
                                                const auto value = roundFloatValue(vp->v, vp->t);
                                                if (FloatValue() <= value && value < FloatValue(18446744073709551616.0)) {
                                                    newValue = MIRConstant::make_Uint({H::truncateU(*te, U128(static_cast<u64>(value))), *te});
                                                } else {
                                                }
                                            } else {
                                            }
                                            break;
                                        case HIRCoreType::I8:
                                        case HIRCoreType::I16:
                                        case HIRCoreType::I32:
                                        case HIRCoreType::I64:
                                        case HIRCoreType::I128:
                                        case HIRCoreType::Isize:
                                            if (const auto* vp = nv.opt_Uint()) {
                                                newValue = MIRConstant::make_Int({H::truncateS(*te, vp->v), *te});
                                            } else if (const auto* vp = nv.opt_Int()) {
                                                newValue = MIRConstant::make_Int({H::truncateS(*te, vp->v), *te});
                                            } else if (const auto* vp = nv.opt_Bool()) {
                                                newValue = MIRConstant::make_Int({S128(vp->v ? 1 : 0), *te});
                                            } else {
                                            }
                                            break;
                                        case HIRCoreType::F16:
                                        case HIRCoreType::F32:
                                        case HIRCoreType::F64:
                                        case HIRCoreType::F128:
                                            // TODO: Cast to float
                                            break;
                                        case HIRCoreType::Char:
                                            // TODO: Only `u8` can be casted to char
                                            break;
                                        case HIRCoreType::Bool:
                                            break;
                                        case HIRCoreType::Str:
                                            MIR_BUG(state, "Casting to str");
                                    }
                                }
                            } else if (knownValuesVar.count(se.val)) {
                                auto variantIdx = knownValuesVar.at(se.val);
                                MIR_ASSERT(state, se.type->is_Primitive(), "Casting enum to non-primitive - " << se.type);

                                HIRTypeRef tmp;
                                const auto& srcTy = state.getLvalueType(tmp, se.val);
                                const HIREnum& enm = *srcTy->as_Path().binding.as_Enum();

                                ConvertHIRConstantEvaluateEnumVariant(state.resolve.board(), state.resolve.hirCrate(), srcTy->as_Path().path.data.as_Generic().path, enm, variantIdx);
                                auto v = enm.getDiscriminant(variantIdx);
                                const auto tagPrimitive = HIREnum::getReprType(enm.tagRepr);

                                auto value = S128(U128(v));
                                switch (tagPrimitive) {
                                    case HIRCoreType::I8:
                                    case HIRCoreType::I16:
                                    case HIRCoreType::I32:
                                    case HIRCoreType::I64:
                                    case HIRCoreType::I128:
                                    case HIRCoreType::Isize:
                                        value = H::truncateS(tagPrimitive, value);
                                        break;
                                    default:
                                        value = S128(H::truncateU(tagPrimitive, value.getInner()));
                                        break;
                                }

                                auto ct = se.type->as_Primitive();
                                switch (ct) {
                                    case HIRCoreType::U8:
                                    case HIRCoreType::U16:
                                    case HIRCoreType::U32:
                                    case HIRCoreType::U64:
                                    case HIRCoreType::U128:
                                    case HIRCoreType::Usize:
                                        newValue = MIRConstant::make_Uint({H::truncateU(ct, value.getInner()), ct});
                                        break;
                                    case HIRCoreType::I8:
                                    case HIRCoreType::I16:
                                    case HIRCoreType::I32:
                                    case HIRCoreType::I64:
                                    case HIRCoreType::I128:
                                    case HIRCoreType::Isize:
                                        newValue = MIRConstant::make_Int({H::truncateS(ct, value), ct});
                                        break;
                                    case HIRCoreType::F16:
                                    case HIRCoreType::F32:
                                    case HIRCoreType::F64:
                                    case HIRCoreType::F128:
                                        // TODO: Cast to float (can variants be casted to float?)
                                        break;
                                    case HIRCoreType::Char:
                                        // TODO: Only `u8` can be casted to char (what about a u8 discriminator?)
                                        break;
                                    case HIRCoreType::Bool:
                                        break;
                                    case HIRCoreType::Str:
                                        MIR_BUG(state, "Casting to str");
                                }
                            } else {
                            }

                            if (newValue != MIRConstant()) {
                                e->src = mv$(newValue);
                                changed = true;
                            }
                            break;
                        }
                        case MIRRValue::TAG_BinOp: {
                            auto& se = e->src.as_BinOp();
                            checkParam(se.valL);
                            checkParam(se.valR);

                            if (se.valL.is_Constant() && se.valR.is_Constant()) {
                                const auto& valL = se.valL.as_Constant();
                                const auto& valR = se.valR.as_Constant();

                                if (valL.is_Const() || valR.is_Const()) {
                                } else if (valL.is_Generic() || valR.is_Generic()) {
                                } else {
                                    MIRConstant newValue;
                                    switch (se.op) {
                                        case MIRBinOp::EQ:
                                            if (!valL.is_Float()) {
                                                newValue = MIRConstant::make_Bool({valL == valR});
                                            }
                                            break;
                                        case MIRBinOp::NE:
                                            if (!valL.is_Float()) {
                                                newValue = MIRConstant::make_Bool({valL != valR});
                                            }
                                            break;
                                        case MIRBinOp::LT:
                                            if (!valL.is_Float()) {
                                                newValue = MIRConstant::make_Bool({valL < valR});
                                            }
                                            break;
                                        case MIRBinOp::LE:
                                            if (!valL.is_Float()) {
                                                newValue = MIRConstant::make_Bool({valL <= valR});
                                            }
                                            break;
                                        case MIRBinOp::GT:
                                            if (!valL.is_Float()) {
                                                newValue = MIRConstant::make_Bool({valL > valR});
                                            }
                                            break;
                                        case MIRBinOp::GE:
                                            if (!valL.is_Float()) {
                                                newValue = MIRConstant::make_Bool({valL >= valR});
                                            }
                                            break;

                                        case MIRBinOp::ADD:
                                            MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::ADD - " << valL << " + " << valR);
                                            switch (valL.tag()) {
                                                default:
                                                    break;
                                                case MIRConstant::TAG_Float: {
                                                    auto& le = valL.as_Float();
                                                    auto& re = valR.as_Float();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::ADD - " << valL << " / " << valR);
                                                    newValue = makeFloatArithmeticResult(roundFloatValue(le.v, le.t) + roundFloatValue(re.v, re.t), le.t);
                                                    break;
                                                }
                                                case MIRConstant::TAG_Int: {
                                                    auto& le = valL.as_Int();
                                                    auto& re = valR.as_Int();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::ADD - " << valL << " + " << valR);
                                                    newValue = MIRConstant::make_Int({H::truncateS(le.t, le.v + re.v), le.t});
                                                    break;
                                                }
                                                case MIRConstant::TAG_Uint: {
                                                    auto& le = valL.as_Uint();
                                                    auto& re = valR.as_Uint();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::ADD - " << valL << " + " << valR);
                                                    newValue = MIRConstant::make_Uint({H::truncateU(le.t, le.v + re.v), le.t});
                                                    break;
                                                }
                                            }
                                            break;
                                        case MIRBinOp::SUB:
                                            MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::SUB - " << valL << " + " << valR);
                                            switch (valL.tag()) {
                                                default:
                                                    break;
                                                case MIRConstant::TAG_Float: {
                                                    auto& le = valL.as_Float();
                                                    auto& re = valR.as_Float();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::SUB - " << valL << " / " << valR);
                                                    newValue = makeFloatArithmeticResult(roundFloatValue(le.v, le.t) - roundFloatValue(re.v, re.t), le.t);
                                                    break;
                                                }
                                                case MIRConstant::TAG_Int: {
                                                    auto& le = valL.as_Int();
                                                    auto& re = valR.as_Int();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::SUB - " << valL << " - " << valR);
                                                    newValue = MIRConstant::make_Int({H::truncateS(le.t, le.v - re.v), le.t});
                                                    break;
                                                }
                                                case MIRConstant::TAG_Uint: {
                                                    auto& le = valL.as_Uint();
                                                    auto& re = valR.as_Uint();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::SUB - " << valL << " - " << valR);
                                                    newValue = MIRConstant::make_Uint({H::truncateU(le.t, le.v - re.v), le.t});
                                                    break;
                                                }
                                            }
                                            break;
                                        case MIRBinOp::MUL:
                                            MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::MUL - " << valL << " * " << valR);
                                            switch (valL.tag()) {
                                                default:
                                                    break;
                                                case MIRConstant::TAG_Float: {
                                                    auto& le = valL.as_Float();
                                                    auto& re = valR.as_Float();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MUL - " << valL << " / " << valR);
                                                    newValue = makeFloatArithmeticResult(roundFloatValue(le.v, le.t) * roundFloatValue(re.v, re.t), le.t);
                                                    break;
                                                }
                                                case MIRConstant::TAG_Int: {
                                                    auto& le = valL.as_Int();
                                                    auto& re = valR.as_Int();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MUL - " << valL << " * " << valR);
                                                    newValue = MIRConstant::make_Int({H::truncateS(le.t, le.v * re.v), le.t});
                                                    break;
                                                }
                                                case MIRConstant::TAG_Uint: {
                                                    auto& le = valL.as_Uint();
                                                    auto& re = valR.as_Uint();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MUL - " << valL << " * " << valR);
                                                    newValue = MIRConstant::make_Uint({H::truncateU(le.t, le.v * re.v), le.t});
                                                    break;
                                                }
                                            }
                                            break;
                                        case MIRBinOp::DIV:
                                            MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::DIV - " << valL << " / " << valR);
                                            switch (valL.tag()) {
                                                default:
                                                    break;
                                                case MIRConstant::TAG_Float: {
                                                    auto& le = valL.as_Float();
                                                    auto& re = valR.as_Float();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::DIV - " << valL << " / " << valR);
                                                    newValue = makeFloatArithmeticResult(roundFloatValue(le.v, le.t) / roundFloatValue(re.v, re.t), le.t);
                                                    break;
                                                }
                                                case MIRConstant::TAG_Int: {
                                                    auto& le = valL.as_Int();
                                                    auto& re = valR.as_Int();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::DIV - " << valL << " / " << valR);
                                                    if (re.v != 0) {
                                                        newValue = MIRConstant::make_Int({H::truncateS(le.t, le.v / re.v), le.t});
                                                    }
                                                    break;
                                                }
                                                case MIRConstant::TAG_Uint: {
                                                    auto& le = valL.as_Uint();
                                                    auto& re = valR.as_Uint();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::DIV - " << valL << " / " << valR);
                                                    if (re.v != 0) {
                                                        newValue = MIRConstant::make_Uint({H::truncateU(le.t, le.v / re.v), le.t});
                                                    }
                                                    break;
                                                }
                                            }
                                            break;
                                        case MIRBinOp::MOD:
                                            MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::MOD - " << valL << " % " << valR);
                                            switch (valL.tag()) {
                                                default:
                                                    break;
                                                case MIRConstant::TAG_Int: {
                                                    auto& le = valL.as_Int();
                                                    auto& re = valR.as_Int();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MOD - " << valL << " % " << valR);
                                                    MIR_ASSERT(state, re.v != 0, "Const eval error: Constant division by zero");
                                                    newValue = MIRConstant::make_Int({H::truncateS(le.t, le.v % re.v), le.t});
                                                    break;
                                                }
                                                case MIRConstant::TAG_Uint: {
                                                    auto& le = valL.as_Uint();
                                                    auto& re = valR.as_Uint();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::MOD - " << valL << " % " << valR);
                                                    MIR_ASSERT(state, re.v != 0, "Const eval error: Constant division by zero");
                                                    newValue = MIRConstant::make_Uint({H::truncateU(le.t, le.v % re.v), le.t});
                                                    break;
                                                }
                                            }
                                            break;

                                        case MIRBinOp::BIT_AND:
                                            MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::BIT_AND - " << valL << " & " << valR);
                                            switch (valL.tag()) {
                                                default:
                                                    break;
                                                case MIRConstant::TAG_Bool: {
                                                    auto& le = valL.as_Bool();
                                                    auto& re = valR.as_Bool();
                                                    newValue = MIRConstant::make_Bool({le.v && re.v});
                                                    break;
                                                }
                                                case MIRConstant::TAG_Int: {
                                                    auto& le = valL.as_Int();
                                                    auto& re = valR.as_Int();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_AND - " << valL << " ^ " << valR);
                                                    newValue = MIRConstant::make_Int({H::truncateS(le.t, le.v & re.v), le.t});
                                                    break;
                                                }
                                                case MIRConstant::TAG_Uint: {
                                                    auto& le = valL.as_Uint();
                                                    auto& re = valR.as_Uint();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_AND - " << valL << " ^ " << valR);
                                                    newValue = MIRConstant::make_Uint({H::truncateU(le.t, le.v & re.v), le.t});
                                                    break;
                                                }
                                            }
                                            break;
                                        case MIRBinOp::BIT_OR:
                                            MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::BIT_OR - " << valL << " | " << valR);
                                            switch (valL.tag()) {
                                                default:
                                                    break;
                                                case MIRConstant::TAG_Bool: {
                                                    auto& le = valL.as_Bool();
                                                    auto& re = valR.as_Bool();
                                                    newValue = MIRConstant::make_Bool({le.v || re.v});
                                                    break;
                                                }
                                                case MIRConstant::TAG_Int: {
                                                    auto& le = valL.as_Int();
                                                    auto& re = valR.as_Int();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_OR - " << valL << " | " << valR);
                                                    newValue = MIRConstant::make_Int({H::truncateS(le.t, le.v | re.v), le.t});
                                                    break;
                                                }
                                                case MIRConstant::TAG_Uint: {
                                                    auto& le = valL.as_Uint();
                                                    auto& re = valR.as_Uint();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_OR - " << valL << " | " << valR);
                                                    newValue = MIRConstant::make_Uint({H::truncateU(le.t, le.v | re.v), le.t});
                                                    break;
                                                }
                                            }
                                            break;
                                        case MIRBinOp::BIT_XOR:
                                            MIR_ASSERT(state, valL.tag() == valR.tag(), "Mismatched types for eBinOp::BIT_XOR - " << valL << " ^ " << valR);
                                            switch (valL.tag()) {
                                                default:
                                                    break;
                                                case MIRConstant::TAG_Bool: {
                                                    auto& le = valL.as_Bool();
                                                    auto& re = valR.as_Bool();
                                                    newValue = MIRConstant::make_Bool({le.v != re.v});
                                                    break;
                                                }
                                                case MIRConstant::TAG_Int: {
                                                    auto& le = valL.as_Int();
                                                    auto& re = valR.as_Int();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_XOR - " << valL << " ^ " << valR);
                                                    newValue = MIRConstant::make_Int({H::truncateS(le.t, le.v ^ re.v), le.t});
                                                    break;
                                                }
                                                case MIRConstant::TAG_Uint: {
                                                    auto& le = valL.as_Uint();
                                                    auto& re = valR.as_Uint();
                                                    MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::BIT_XOR - " << valL << " ^ " << valR);
                                                    newValue = MIRConstant::make_Uint({H::truncateU(le.t, le.v ^ re.v), le.t});
                                                    break;
                                                }
                                            }
                                            break;

                                        case MIRBinOp::BIT_SHL: {
                                            U128 shiftLenR;
                                            switch (valR.tag()) {
                                                default:
                                                    MIR_BUG(state, "Mismatched types for eBinOp::BIT_SHL - " << valL << " >> " << valR);
                                                    break;
                                                case MIRConstant::TAG_Int: {
                                                    auto& re = valR.as_Int();
                                                    shiftLenR = re.v.getInner();
                                                    break;
                                                }
                                                case MIRConstant::TAG_Uint: {
                                                    auto& re = valR.as_Uint();
                                                    shiftLenR = re.v;
                                                    break;
                                                }
                                            }
                                            MIR_ASSERT(state, shiftLenR <= 128, "Const eval error: Over-sized eBinOp::BIT_SHL - " << valL << " << " << valR);
                                            auto shiftLen = shiftLenR.truncateU64();
                                            switch (valL.tag()) {
                                                default:
                                                    break;
                                                case MIRConstant::TAG_Int: {
                                                    auto& le = valL.as_Int();
                                                    newValue = MIRConstant::make_Int({H::truncateS(le.t, le.v << shiftLen), le.t});
                                                    break;
                                                }
                                                case MIRConstant::TAG_Uint: {
                                                    auto& le = valL.as_Uint();
                                                    newValue = MIRConstant::make_Uint({H::truncateU(le.t, le.v << shiftLen), le.t});
                                                    break;
                                                }
                                            }
                                        } break;
                                        case MIRBinOp::BIT_SHR: {
                                            U128 shiftLenR;
                                            switch (valR.tag()) {
                                                default:
                                                    MIR_BUG(state, "Mismatched types for eBinOp::BIT_SHR - " << valL << " >> " << valR);
                                                    break;
                                                case MIRConstant::TAG_Int: {
                                                    auto& re = valR.as_Int();
                                                    shiftLenR = re.v.getInner();
                                                    break;
                                                }
                                                case MIRConstant::TAG_Uint: {
                                                    auto& re = valR.as_Uint();
                                                    shiftLenR = re.v;
                                                    break;
                                                }
                                            }
                                            MIR_ASSERT(state, shiftLenR <= 128, "Const eval error: Over-sized shift - " << valL << " >> " << valR);
                                            auto shiftLen = shiftLenR.truncateU64();
                                            switch (valL.tag()) {
                                                default:
                                                    break;
                                                case MIRConstant::TAG_Int: {
                                                    auto& le = valL.as_Int();
                                                    newValue = MIRConstant::make_Int({H::truncateS(le.t, le.v >> shiftLen), le.t});
                                                    break;
                                                }
                                                case MIRConstant::TAG_Uint: {
                                                    auto& le = valL.as_Uint();
                                                    newValue = MIRConstant::make_Uint({H::truncateU(le.t, le.v >> shiftLen), le.t});
                                                    break;
                                                }
                                            }
                                        } break;
                                        // TODO: Other binary operations
                                        // Could emit a TODO?
                                        default:
                                            break;
                                    }

                                    if (newValue != MIRConstant()) {
                                        e->src = mv$(newValue);
                                        changed = true;
                                    }
                                }
                            } else {
                                MIRParam newValue;
                                switch (se.op) {
                                    case MIRBinOp::ADD:
                                    case MIRBinOp::SUB:
                                        if (se.valR.is_Constant() && se.valR.as_Constant().is_Uint() && se.valR.as_Constant().as_Uint().v == 0) {
                                            newValue = mv$(se.valL);
                                        }
                                        break;
                                    case MIRBinOp::MOD:
                                        if (se.valR.is_Constant() && se.valR.as_Constant().is_Uint() && se.valR.as_Constant().as_Uint().v == 1) {
                                            newValue = MIRConstant::make_Uint({U128(0), se.valR.as_Constant().as_Uint().t});
                                        }
                                        break;
                                    case MIRBinOp::DIV:
                                        if (se.valR.is_Constant() && se.valR.as_Constant().is_Uint() && se.valR.as_Constant().as_Uint().v == 1) {
                                            newValue = mv$(se.valL);
                                        }
                                        break;
                                    case MIRBinOp::MUL:
                                        if (se.valR.is_Constant() && se.valR.as_Constant().is_Uint()) {
                                            auto& v = se.valR.as_Constant().as_Uint();
                                            if (v.v == 0) {
                                                newValue = MIRConstant::make_Uint({U128(0), v.t});
                                            } else if (v.v == 1) {
                                                newValue = mv$(se.valL);
                                            } else {
                                            }
                                        }
                                        if (se.valL.is_Constant() && se.valL.as_Constant().is_Uint()) {
                                            auto& v = se.valL.as_Constant().as_Uint();
                                            if (v.v == 0) {
                                                newValue = MIRConstant::make_Uint({U128(0), v.t});
                                            } else if (v.v == 1) {
                                                newValue = mv$(se.valR);
                                            } else {
                                            }
                                        }
                                        break;
                                    default:
                                        break;
                                }
                                if (newValue != MIRParam()) {
                                    switch (newValue.tag()) {
                                        case MIRParam::TAG_LValue: {
                                            auto& v = newValue.as_LValue();
                                            e->src = mv$(v);
                                            break;
                                        }
                                        case MIRParam::TAG_Borrow: {
                                            auto& _ = newValue.as_Borrow();
                                            UNREACHABLE();
                                        }
                                        case MIRParam::TAG_Constant: {
                                            auto& v = newValue.as_Constant();
                                            e->src = mv$(v);
                                            break;
                                        }
                                    }
                                    changed = true;
                                }
                            }
                            break;
                        }
                        case MIRRValue::TAG_UniOp: {
                            auto& se = e->src.as_UniOp();
                            auto it = knownValues.find(se.val);
                            if (it != knownValues.end()) {
                                const auto& val = it->second;
                                MIRConstant newValue;
                                bool replace = false;
                                switch (se.op) {
                                    case MIRUniOp::INV:
                                        switch (val.tag()) {
                                            case MIRConstant::TAG_Uint: {
                                                auto& ve = val.as_Uint();
                                                auto val = ve.v;
                                                replace = true;
                                                switch (ve.t) {
                                                    case HIRCoreType::U8:
                                                    case HIRCoreType::U16:
                                                    case HIRCoreType::U32:
                                                    case HIRCoreType::Usize:
                                                    case HIRCoreType::U64:
                                                        val = H::truncateU(ve.t, ~val);
                                                        break;
                                                    case HIRCoreType::U128:
                                                        replace = false;
                                                        break;
                                                    case HIRCoreType::Char:
                                                        MIR_BUG(state, "Invalid use of ! on char");
                                                        break;
                                                    default:
                                                        replace = false;
                                                        break;
                                                }
                                                newValue = MIRConstant::make_Uint({val, ve.t});
                                                break;
                                            }
                                            case MIRConstant::TAG_Int: {
                                                auto& ve = val.as_Int();
                                                auto val = ve.v;
                                                switch (ve.t) {
                                                    case HIRCoreType::I8:
                                                    case HIRCoreType::I16:
                                                    case HIRCoreType::I32:
                                                    case HIRCoreType::Isize:
                                                    case HIRCoreType::I64:
                                                        val = H::truncateS(ve.t, ~val);
                                                        replace = true;
                                                        break;
                                                    case HIRCoreType::I128:
                                                        // TODO: Are there any cases where sign extension stops being correct here?
                                                        val = H::truncateS(ve.t, ~val);
                                                        replace = true;
                                                        break;
                                                    case HIRCoreType::Char:
                                                        MIR_BUG(state, "Invalid use of ! on char");
                                                        break;
                                                    default:
                                                        replace = false;
                                                        break;
                                                }
                                                newValue = MIRConstant::make_Int({val, ve.t});
                                                break;
                                            }
                                            case MIRConstant::TAG_Float: {
                                                break;
                                            }
                                            case MIRConstant::TAG_Bool: {
                                                auto& ve = val.as_Bool();
                                                newValue = MIRConstant::make_Bool({!ve.v});
                                                replace = true;
                                                break;
                                            }
                                            case MIRConstant::TAG_Bytes: {
                                                break;
                                            }
                                            case MIRConstant::TAG_StaticString: {
                                                break;
                                            }
                                            case MIRConstant::TAG_Encoded: {
                                                break;
                                            }
                                            case MIRConstant::TAG_Const: {
                                                // TODO:
                                                break;
                                            }
                                            case MIRConstant::TAG_Generic: {
                                                break;
                                            }
                                            case MIRConstant::TAG_Function: {
                                                break;
                                            }
                                            case MIRConstant::TAG_ItemAddr: {
                                                break;
                                            }
                                        }
                                        break;
                                    case MIRUniOp::NEG:
                                        switch (val.tag()) {
                                            case MIRConstant::TAG_Uint: {
                                                break;
                                            }
                                            case MIRConstant::TAG_Int: {
                                                auto& ve = val.as_Int();
                                                newValue = MIRConstant::make_Int({H::truncateS(ve.t, -ve.v), ve.t});
                                                replace = true;
                                                break;
                                            }
                                            case MIRConstant::TAG_Float: {
                                                auto& ve = val.as_Float();
                                                if (!floatValueIsNan(ve.v)) {
                                                    newValue = MIRConstant::make_Float({-ve.v, ve.t});
                                                    replace = true;
                                                }
                                                break;
                                            }
                                            case MIRConstant::TAG_Bool: {
                                                break;
                                            }
                                            case MIRConstant::TAG_Bytes: {
                                                break;
                                            }
                                            case MIRConstant::TAG_StaticString: {
                                                break;
                                            }
                                            case MIRConstant::TAG_Encoded: {
                                                break;
                                            }
                                            case MIRConstant::TAG_Const: {
                                                // TODO:
                                                break;
                                            }
                                            case MIRConstant::TAG_Generic: {
                                                break;
                                            }
                                            case MIRConstant::TAG_Function: {
                                                break;
                                            }
                                            case MIRConstant::TAG_ItemAddr: {
                                                break;
                                            }
                                        }
                                        break;
                                }
                                if (replace) {
                                    e->src = mv$(newValue);
                                    changed = true;
                                }
                            }
                            break;
                        }
                        case MIRRValue::TAG_DstMeta: {
                            break;
                        }
                        case MIRRValue::TAG_DstPtr: {
                            break;
                        }
                        case MIRRValue::TAG_MakeDst: {
                            auto& se = e->src.as_MakeDst();
                            if ((se.metaVal.is_Constant() && se.metaVal.as_Constant().is_ItemAddr() && se.metaVal.as_Constant().as_ItemAddr().get() == nullptr)) {
                            } else {
                                checkParam(se.ptrVal);
                                checkParam(se.metaVal);
                            }
                            break;
                        }
                        case MIRRValue::TAG_Tuple: {
                            auto& se = e->src.as_Tuple();
                            for (auto& p : se.vals) {
                                checkParam(p);
                            }
                            break;
                        }
                        case MIRRValue::TAG_Array: {
                            auto& se = e->src.as_Array();
                            for (auto& p : se.vals) {
                                checkParam(p);
                            }
                            break;
                        }
                        case MIRRValue::TAG_UnionVariant: {
                            auto& se = e->src.as_UnionVariant();
                            checkParam(se.val);
                            break;
                        }
                        case MIRRValue::TAG_EnumVariant: {
                            auto& se = e->src.as_EnumVariant();
                            for (auto& p : se.vals) {
                                checkParam(p);
                            }
                            break;
                        }
                        case MIRRValue::TAG_Struct: {
                            auto& se = e->src.as_Struct();
                            for (auto& p : se.vals) {
                                checkParam(p);
                            }
                            break;
                        }
                    }
                } else if (const auto* se = stmt.opt_SetDropFlag()) {
                    if (se->other == ~0u) {
                        knownDropFlags[se->idx] = se->newVal;
                    } else {
                        auto it = knownDropFlags.find(se->other);
                        if (it != knownDropFlags.end()) {
                            knownDropFlags[se->idx] = se->newVal != it->second;
                        } else {
                            knownDropFlags.erase(se->idx);
                        }
                    }
                }

                struct ForgetWritten final: public LvalueVisitor {
                    decltype(knownValues)& knownValues;
                    decltype(knownValuesVar)& knownValuesVar;

                    ForgetWritten(decltype(knownValues)& knownValues, decltype(knownValuesVar)& knownValuesVar)
                        : knownValues(knownValues)
                        , knownValuesVar(knownValuesVar)
                    {
                    }

                    bool visitLvalue(const MIRLValue& lv, MIRValUsage vu) override {
                        if (vu == MIRValUsage::Write) {
                            knownValues.erase(lv);
                            knownValuesVar.erase(lv);
                        }
                        return false;
                    }
                } forgetWritten{knownValues, knownValuesVar};

                optVisitMirLvalues(stmt, forgetWritten);

                if (const auto* e = stmt.opt_Assign()) {
                    if (e->dst.is_Local()) {
                        if (const auto* ce = e->src.opt_Constant()) {
                            knownValues.insert(std::make_pair(e->dst.clone(), ce->clone()));
                        } else if (const auto* ce = e->src.opt_EnumVariant()) {
                            knownValuesVar.insert(std::make_pair(e->dst.clone(), ce->index));
                        } else if (const auto* ce = e->src.opt_Use()) {
                            if (ce->is_Local()) {
                                auto it1 = knownValues.find(*ce);
                                auto it2 = knownValuesVar.find(*ce);
                                assert(!(it1 != knownValues.end() && it2 != knownValuesVar.end()));
                                if (it1 != knownValues.end()) {
                                    knownValues.insert(std::make_pair(e->dst.clone(), it1->second.clone()));
                                } else if (it2 != knownValuesVar.end()) {
                                    knownValuesVar.insert(std::make_pair(e->dst.clone(), it2->second));
                                } else {
                                }
                            }
                        } else {
                        }
                    }
                }
            }

            state.setCurStmtTerm(bbidx);
            if (auto* te = bb.terminator.opt_Drop()) {
                if (te->flagIdx != ~0u) {
                    auto it = knownDropFlags.find(te->flagIdx);
                    if (it != knownDropFlags.end()) {
                        if (it->second) {
                            te->flagIdx = ~0u;
                        } else {
                            auto target = te->target;
                            bb.terminator = MIRTerminator::make_Goto(target);
                        }
                        changed = true;
                    }
                }
            }
            optVisitMirLvaluesMut(bb.terminator, editLval);
            switch (bb.terminator.tag()) {
                break;
                case MIRTerminator::TAG_Switch: {
                    auto& te = bb.terminator.as_Switch();
                    auto it = knownValuesVar.find(te.val);
                    if (it != knownValuesVar.end()) {
                        MIR_ASSERT(state, it->second < te.targets.size(), "Terminator::Switch with known variant index out of bounds" << " (#" << it->second << " with " << bb.terminator << ")");
                        auto newBb = te.targets.at(it->second);
                        bb.terminator = MIRTerminator::make_Goto(newBb);

                        changed = true;
                    }
                } break;
                    break;
                case MIRTerminator::TAG_If: {
                    auto& te = bb.terminator.as_If();
                    auto it = knownValues.find(te.cond);
                    if (it != knownValues.end()) {
                        if (it->second.is_Const() || it->second.is_Generic()) {
                        } else {
                            MIR_ASSERT(state, it->second.is_Bool(), "Terminator::If with known value not Bool - " << it->second);
                            auto newBb = (it->second.as_Bool().v ? te.bbTrue : te.bbFalse);
                            bb.terminator = MIRTerminator::make_Goto(newBb);

                            changed = true;
                        }
                    }
                } break;
                    break;
                case MIRTerminator::TAG_Call: {
                    auto& te = bb.terminator.as_Call();
                    for (auto& a : te.args) {
                        checkParam(a);
                    }
                } break;
                default:
                    break;
            }
        }

        // TODO: Is this now defunct after the handling of Terminator::If above?
        for (auto& bb : fcn.blocks) {
            auto bbidx = &bb - &fcn.blocks.front();
            if (!bb.terminator.is_If()) {
                continue;
            }
            const auto& te = bb.terminator.as_If();

            if (te.cond.is_Local())
                ;
            else {
                continue;
            }

            struct HasCond final: public LvalueVisitor {
                const MIRLValue& cond;

                explicit HasCond(const MIRLValue& cond)
                    : cond(cond)
                {
                }

                bool visitLvalue(const MIRLValue& lv, MIRValUsage /*vu*/) override {
                    return lv == cond;
                }
            } hasCond{te.cond};

            bool valKnown = false;
            bool knownVal;
            for (unsigned int i = bb.statements.size(); i--;) {
                if (bb.statements[i].is_Assign()) {
                    const auto& se = bb.statements[i].as_Assign();

                    // TODO: What if the condition is a field/index and something else is edited?
                    if (optVisitMirLvalues(se.src, hasCond)) {
                        break;
                    }

                    if (se.dst != te.cond) {
                        continue;
                    }
                    if (se.src.is_Constant() && se.src.as_Constant().is_Bool()) {
                        valKnown = true;
                        knownVal = se.src.as_Constant().as_Bool().v;
                    } else {
                        valKnown = false;
                    }
                    break;
                } else {
                    if (optVisitMirLvalues(bb.statements[i], hasCond)) {
                        break;
                    }
                }
            }
            if (valKnown) {
                bb.terminator = MIRTerminator::make_Goto(knownVal ? te.bbTrue : te.bbFalse);
                changed = true;
            }
        }

        return changed;
    }

    bool MIROptimiseSplitAggregates(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;

        struct Potential {
            size_t srcBbIdx;
            size_t srcStmtIdx;
            unsigned variantIdx;

            bool isDirectUsed;
            unsigned nWrite;
            std::vector<unsigned> replacements;

            Potential(size_t srcBbIdx, size_t srcStmtIdx, unsigned variantIdx = ~0u)
                : srcBbIdx(srcBbIdx)
                , srcStmtIdx(srcStmtIdx)
                , variantIdx(variantIdx)
                , isDirectUsed(false)
                , nWrite(0)
            {
            }
        };

        std::map<unsigned, Potential> potentials;

        for (const auto& block : fcn.blocks) {
            size_t bbIdx = &block - &fcn.blocks.front();
            for (size_t i = 0; i < block.statements.size(); i++) {
                const auto& stmt = block.statements[i];
                if (const auto* se = stmt.opt_Assign()) {
                    if (!se->dst.is_Local()) {
                        continue;
                    }

                    if (auto* sse = se->src.opt_Struct()) {
                        if (sse->vals.size() == 0) {
                            continue;
                        }
                    } else if (auto* sse = se->src.opt_Tuple()) {
                        if (sse->vals.size() == 0) {
                            continue;
                        }
                    } else if (auto* sse = se->src.opt_Array()) {
                        if (sse->vals.size() == 0) {
                            continue;
                        }
                    } else if (auto* sse = se->src.opt_EnumVariant()) {
                        if (sse->vals.size() == 0) {
                            continue;
                        }
                        potentials.insert(std::make_pair(se->dst.as_Local(), Potential(bbIdx, i, sse->index)));
                        continue;
                    } else {
                        continue;
                    }

                    potentials.insert(std::make_pair(se->dst.as_Local(), Potential(bbIdx, i)));
                }
            }
        }
        if (potentials.empty()) {
            return false;
        }

        struct CheckPotentials final: public LvalueVisitor {
            MIRTypeResolve& state;
            const MIRFunction& fcn;
            decltype(potentials)& potentials;

            CheckPotentials(MIRTypeResolve& state, const MIRFunction& fcn, decltype(potentials)& potentials)
                : state(state)
                , fcn(fcn)
                , potentials(potentials)
            {
            }

            bool visitLvalue(const MIRLValue& lv, MIRValUsage vu) override {
                if (lv.root.is_Local()) {
                    auto it = potentials.find(lv.root.as_Local());
                    if (it != potentials.end()) {
                        if (lv.wrappers.empty()) {
                            if (vu == MIRValUsage::Write) {
                                it->second.nWrite += 1;
                            } else {
                                it->second.isDirectUsed = true;
                            }
                        } else if (lv.wrappers.front().is_Field()) {
                            // TODO: Find out what code makes the assumption that `&foo.0` is a good stand-in for `&foo`
                            if (lv.wrappers.front().as_Field() == 0 && vu == MIRValUsage::Borrow) {
                                it->second.isDirectUsed = true;
                            }
                        } else if (lv.wrappers.front().is_Downcast()) {
                            if (lv.wrappers.front().as_Downcast() != it->second.variantIdx || lv.wrappers.size() < 2 || !lv.wrappers[1].is_Field()) {
                                it->second.isDirectUsed = true;
                            }
                        } else {
                            it->second.isDirectUsed = true;
                        }

                        if (it->second.isDirectUsed || it->second.nWrite > 1) {
                            const auto& stmt = fcn.blocks[it->second.srcBbIdx].statements[it->second.srcStmtIdx];
                            potentials.erase(it);
                        }
                    }
                }
                return true;
            }
        } checkPotentials{state, fcn, potentials};

        optVisitMirLvalues(state, fcn, checkPotentials);
        if (potentials.empty()) {
            return false;
        }

        for (auto& p : potentials) {
            auto bbIdx = p.second.srcBbIdx;
            auto stmtIdx = p.second.srcStmtIdx;
            state.setCurStmt(bbIdx, stmtIdx);
            auto& block = fcn.blocks[bbIdx];

            std::vector<MIRParam> vals;
            {
                auto& src = block.statements[stmtIdx].as_Assign().src;
                if (auto* se = src.opt_Struct()) {
                    vals = std::move(se->vals);
                } else if (auto* se = src.opt_Tuple()) {
                    vals = std::move(se->vals);
                } else if (auto* se = src.opt_Array()) {
                    vals = std::move(se->vals);
                } else if (auto* se = src.opt_EnumVariant()) {
                    vals = std::move(se->vals);
                } else if (auto* se = src.opt_UnionVariant()) {
                    vals.push_back(mv$(se->val));
                } else {
                    MIR_BUG(state, "Unexpected rvalue type in SplitAggregates - " << src);
                }
            }
            MIR_ASSERT(state, vals.size() > 0, "Optimisation can't apply to empty lists");
            auto offset = vals.size() - 1;

            if (offset > 0) {
                block.statements.resize(block.statements.size() + offset);
                std::move_backward(block.statements.begin() + stmtIdx + 1, block.statements.end() - offset, block.statements.end());
            }

            auto newLocalBase = fcn.locals.size();
            fcn.locals.resize(fcn.locals.size() + vals.size());
            p.second.replacements.resize(vals.size());
            for (size_t i = 0; i < vals.size(); i++) {
                auto newLocal = static_cast<unsigned>(newLocalBase + i);
                HIRTypeRef tmp;
                fcn.locals[newLocal] = state.getParamType(tmp, vals[i]);
                p.second.replacements[i] = newLocal;
                block.statements[stmtIdx + i] = MIRStatement::make_Assign({MIRLValue::newLocal(newLocal), paramToRvalue(mv$(vals[i]))});
            }

            if (offset > 0) {
                for (auto& otherP : potentials) {
                    if (otherP.second.srcBbIdx == bbIdx && otherP.second.srcStmtIdx > stmtIdx) {
                        otherP.second.srcStmtIdx += offset;
                    }
                }
            }
        }

        struct ReplaceAggregateUses final: public LvalueVisitorMut {
            MIRTypeResolve& state;
            decltype(potentials)& potentials;

            ReplaceAggregateUses(MIRTypeResolve& state, decltype(potentials)& potentials)
                : state(state)
                , potentials(potentials)
            {
            }

            bool visitLvalue(MIRLValue& lv, MIRValUsage /*vu*/) override {
                if (lv.root.is_Local()) {
                    auto it = potentials.find(lv.root.as_Local());
                    if (it != potentials.end()) {
                        size_t ndel;
                        size_t fieldIdx;
                        if (it->second.variantIdx == ~0u) {
                            fieldIdx = lv.wrappers.front().as_Field();
                            ndel = 1;
                        } else {
                            MIR_ASSERT(state, lv.wrappers[0].is_Downcast(), lv);
                            MIR_ASSERT(state, lv.wrappers[1].is_Field(), lv);
                            fieldIdx = lv.wrappers[1].as_Field();
                            ndel = 2;
                        }
                        auto newWrappers = std::vector<MIRLValue::Wrapper>(lv.wrappers.begin() + ndel, lv.wrappers.end());
                        auto newRoot = MIRLValue::Storage::newLocal(it->second.replacements.at(fieldIdx));
                        auto newLv = MIRLValue(mv$(newRoot), mv$(newWrappers));
                        lv = mv$(newLv);
                    }
                }
                return true;
            }
        } replaceAggregateUses{state, potentials};

        optVisitMirLvaluesMut(state, fcn, replaceAggregateUses);

        changed = true;
        return true;
    }

    bool MIROptimisePropagateSingleAssignments(MIRTypeResolve& state, MIRFunction& fcn) {
        bool replacementHappend;

        // TODO: This requires kowing that doing so has no effect.

        struct ValUse {
            unsigned int read = 0;
            unsigned int write = 0;
            unsigned int borrow = 0;
        };

        struct {
            std::vector<ValUse> localUses;

            void useLvalue(const MIRLValue& lv, MIRValUsage ut) {
                for (const auto& w : lv.wrappers) {
                    if (w.is_Index()) {
                        localUses[w.as_Index()].borrow += 1;
                    }
                }
                if (lv.root.is_Local()) {
                    auto& vu = localUses[lv.root.as_Local()];
                    switch (ut) {
                        case MIRValUsage::Move:
                        case MIRValUsage::Read:
                            vu.read += 1;
                            break;
                        case MIRValUsage::Write:
                            vu.write += 1;
                            break;
                        case MIRValUsage::Borrow:
                            vu.borrow += 1;
                            break;
                    }
                }
            }
        } valUses = {std::vector<ValUse>(fcn.locals.size())};

        struct CollectValUses final: public LvalueVisitor {
            decltype(valUses)& valUses;

            explicit CollectValUses(decltype(valUses)& valUses)
                : valUses(valUses)
            {
            }

            bool visitLvalue(const MIRLValue& lv, MIRValUsage ut) override {
                valUses.useLvalue(lv, ut);
                return false;
            }
        } collectValUses{valUses};

        optVisitMirLvalues(state, fcn, collectValUses);

        {
            std::vector<std::pair<MIRLValue, MIRRValue>> replacements;
            auto replacementsFind = [&replacements](const MIRLValue::CRef& lv) {
                return std::find_if(replacements.begin(), replacements.end(), [&](const auto& e) {
                    return lv == e.first;
                });
            };
            for (const auto& block : fcn.blocks) {
                if (block.terminator.isDead()) {
                    continue;
                }

                for (unsigned int stmtIdx = 0; stmtIdx < block.statements.size(); stmtIdx++) {
                    state.setCurStmt(&block - &fcn.blocks.front(), stmtIdx);
                    const auto& stmt = block.statements[stmtIdx];
                    if (!stmt.is_Assign()) {
                        continue;
                    }
                    const auto& e = stmt.as_Assign();

                    if (e.dst.is_Local()) {
                        const auto& vu = valUses.localUses[e.dst.as_Local()];
                        // TODO: Allow write many?

                        if (!(vu.read == 1 && vu.write == 1 && vu.borrow == 0)) {
                            continue;
                        }
                    } else {
                        continue;
                    }
                    bool onlyOne = false;
                    if (e.src.is_Use()) {
                        const auto* srcp = &e.src.as_Use();
                        if (std::any_of(srcp->wrappers.begin(), srcp->wrappers.end(), [](auto& w) {
                            return !w.is_Field() && !w.is_Downcast();
                        })) {
                            onlyOne = true;
                            continue;
                        }
                        // TODO: Why is this limited to locals only?
                        if (!srcp->root.is_Local()) {
                            continue;
                        }

                        if (replacementsFind(*srcp) != replacements.end()) {
                            continue;
                        }
                    } else {
                        continue;
                    }
                    bool srcIsLvalue = e.src.is_Use();

                    struct IsLvalueUsage final: public LvalueVisitor {
                        const MIRLValue& dst;
                        bool found = false;

                        explicit IsLvalueUsage(const MIRLValue& dst)
                            : dst(dst)
                        {
                        }

                        bool visitLvalue(const MIRLValue& lv, MIRValUsage /*vu*/) override {
                            if (lv.root == dst.root) {
                                found = true;
                                return true;
                            }
                            return false;
                        }
                    } isLvalueUsage{e.dst};

                    bool stop = false;
                    bool found = false;
                    for (unsigned int si2 = stmtIdx + 1; si2 < block.statements.size(); si2++) {
                        state.setCurStmt(&block - &fcn.blocks.front(), si2);
                        const auto& stmt2 = block.statements[si2];

                        if (checkInvalidatesLvalue(stmt2, e.src.as_Use(), false)) {
                            stop = true;
                            break;
                        }

                        if (optVisitMirLvalues(stmt2, isLvalueUsage)) {
                            if (!srcIsLvalue) {
                                if (stmt2.is_Assign() && stmt2.as_Assign().src.is_Use()) {
                                } else {
                                    stop = true;
                                    break;
                                }
                            }
                            found = true;
                            stop = true;
                            break;
                        }

                        if (onlyOne) {
                            stop = true;
                        }
                    }
                    if (!stop) {
                        if (checkInvalidatesLvalue(block.terminator, e.src.as_Use(), false)) {
                            stop = true;
                        }
                    }
                    if (!stop) {
                        state.setCurStmtTerm(&block - &fcn.blocks.front());
                        if (srcIsLvalue) {
                            isLvalueUsage.found = false;
                            optVisitMirLvalues(block.terminator, isLvalueUsage);
                            found |= isLvalueUsage.found;
                        }
                        switch (block.terminator.tag()) {
                            case MIRTerminator::TAG_Incomplete: {
                                break;
                            }
                            case MIRTerminator::TAG_Return: {
                                break;
                            }
                            case MIRTerminator::TAG_UnwindResume: {
                                break;
                            }
                            case MIRTerminator::TAG_UnwindTerminate: {
                                break;
                            }
                            case MIRTerminator::TAG_Unreachable: {
                                break;
                            }
                            case MIRTerminator::TAG_Goto: {
                                break;
                            }
                            case MIRTerminator::TAG_If: {
                                stop = true;
                                break;
                            }
                            case MIRTerminator::TAG_Switch: {
                                stop = true;
                                break;
                            }
                            case MIRTerminator::TAG_SwitchValue: {
                                stop = true;
                                break;
                            }
                            case MIRTerminator::TAG_Drop: {
                                stop = true;
                                break;
                            }
                            case MIRTerminator::TAG_Call: {
                                stop = true;
                                break;
                            }
                            case MIRTerminator::TAG_TailCall: {
                                stop = true;
                                break;
                            }
                            case MIRTerminator::TAG_Asm2: {
                                stop = true;
                                break;
                            }
                        }
                    }
                    if (found) {
                        replacements.push_back(std::make_pair(e.dst.clone(), e.src.clone()));
                    }
                }
            }

            for (;;) {
                unsigned int innerReplacedCount = 0;

                struct InnerReplace final: public LvalueRefVisitorMut {
                    decltype(replacements)& replacements;
                    decltype(replacementsFind)& replacementsFind;
                    unsigned int& innerReplacedCount;

                    InnerReplace(decltype(replacements)& replacements, decltype(replacementsFind)& replacementsFind, unsigned int& innerReplacedCount)
                        : replacements(replacements)
                        , replacementsFind(replacementsFind)
                        , innerReplacedCount(innerReplacedCount)
                    {
                    }

                    bool visitLvalue(MIRLValue::MRef& lvr, MIRValUsage /*vu*/) override {
                        auto it = replacementsFind(lvr);
                        if (it != replacements.end() && it->second.is_Use()) {
                            lvr.replace(it->second.as_Use().clone());
                            innerReplacedCount++;
                        }
                        return false;
                    }
                } innerReplace{replacements, replacementsFind, innerReplacedCount};

                struct InnerReplaceTop final: public LvalueVisitorMut {
                    InnerReplace& inner;

                    explicit InnerReplaceTop(InnerReplace& inner)
                        : inner(inner)
                    {
                    }

                    bool visitLvalue(MIRLValue& lv, MIRValUsage vu) override {
                        if (vu == MIRValUsage::Read || vu == MIRValUsage::Move) {
                            visitMirLvalueMut(lv, vu, inner);
                        }
                        return false;
                    }
                } innerReplaceTop{innerReplace};

                for (auto& r : replacements) {
                    optVisitMirLvaluesMut(r.second, innerReplaceTop);
                }
                if (innerReplacedCount == 0) {
                    break;
                }
            }

            unsigned int replaced = 0;
            while (replaced < replacements.size()) {
                auto oldReplaced = replaced;

                struct ReplaceReads final: public LvalueRefVisitorMut {
                    MIRTypeResolve& state;
                    decltype(replacements)& replacements;
                    decltype(replacementsFind)& replacementsFind;
                    unsigned int& replaced;

                    ReplaceReads(MIRTypeResolve& state, decltype(replacements)& replacements, decltype(replacementsFind)& replacementsFind, unsigned int& replaced)
                        : state(state)
                        , replacements(replacements)
                        , replacementsFind(replacementsFind)
                        , replaced(replaced)
                    {
                    }

                    bool visitLvalue(MIRLValue::MRef& lv, MIRValUsage vu) override {
                        if (vu == MIRValUsage::Read || vu == MIRValUsage::Move) {
                            auto it = replacementsFind(lv);
                            if (it != replacements.end()) {
                                MIR_ASSERT(state, !it->second.isDead(), "Replacement of  " << lv << " fired twice");
                                MIR_ASSERT(state, it->second.is_Use(), "Replacing a lvalue with a rvalue - " << lv << " with " << it->second);
                                auto rval = std::move(it->second);
                                lv.replace(std::move(rval.as_Use()));
                                replaced += 1;
                            }
                        }
                        return false;
                    }
                } replaceReads{state, replacements, replacementsFind, replaced};

                struct ReplaceReadsTop final: public LvalueVisitorMut {
                    ReplaceReads& inner;

                    explicit ReplaceReadsTop(ReplaceReads& inner)
                        : inner(inner)
                    {
                    }

                    bool visitLvalue(MIRLValue& lv, MIRValUsage vu) override {
                        return visitMirLvalueMut(lv, vu, inner);
                    }
                } cb{replaceReads};

                for (unsigned int blockIdx = 0; blockIdx < fcn.blocks.size(); blockIdx++) {
                    auto& block = fcn.blocks[blockIdx];
                    if (block.terminator.isDead()) {
                        continue;
                    }
                    for (auto& stmt : block.statements) {
                        state.setCurStmt(blockIdx, (&stmt - &block.statements.front()));
                        {
                            optVisitMirLvaluesMut(stmt, cb);
                        }
                    }
                    state.setCurStmtTerm(blockIdx);
                    optVisitMirLvaluesMut(block.terminator, cb);
                }
                MIR_ASSERT(state, replaced > oldReplaced, "Temporary eliminations didn't advance");
            }
            for (auto& block : fcn.blocks) {
                for (auto it = block.statements.begin(); it != block.statements.end();) {
                    state.setCurStmt(&block - &fcn.blocks.front(), (it - block.statements.begin()));

                    auto it2 = replacements.end();
                    if (it->is_Assign() && (it2 = replacementsFind(it->as_Assign().dst)) != replacements.end()) {
                        it = block.statements.erase(it);
                    } else {
                        MIR_ASSERT(state, !(it->is_Assign() && it->as_Assign().src.isDead()), "");
                        ++it;
                    }
                }
            }
            replacementHappend = (replaced > 0);
        }
        {
            for (auto& block : fcn.blocks) {
                for (auto it = block.statements.begin(); it != block.statements.end(); ++it) {
                    state.setCurStmt(&block - &fcn.blocks.front(), it - block.statements.begin());
                    if (!it->is_Assign()) {
                        continue;
                    }
                    if (it->as_Assign().src.isDead()) {
                        continue;
                    }
                    auto& toReplaceLval = it->as_Assign().dst;
                    if (toReplaceLval.is_Local()) {
                        const auto& vu = valUses.localUses[toReplaceLval.as_Local()];
                        if (!(vu.read == 1 && vu.write == 1 && vu.borrow == 0)) {
                            continue;
                        }
                    } else {
                        continue;
                    }

                    for (auto it2 = it + 1; it2 != block.statements.end(); ++it2) {
                        if (!it2->is_Assign()) {
                            continue;
                        }
                        if (it2->as_Assign().src.isDead()) {
                            continue;
                        }
                        if (!it2->as_Assign().src.is_Use()) {
                            continue;
                        }
                        if (it2->as_Assign().src.as_Use() != toReplaceLval) {
                            continue;
                        }
                        const auto& newDstLval = it2->as_Assign().dst;

                        // TODO: Ensure that the target isn't borrowed.
                        if (newDstLval.is_Local()) {
                            const auto& vu = valUses.localUses[newDstLval.as_Local()];
                            if (!(vu.read == 1 && vu.write == 1 && vu.borrow == 0)) {
                                break;
                            }
                        } else if (newDstLval.is_Return()) {
                        } else {
                            break;
                        }

                        bool wasInvalidated = false;
                        for (auto it3 = it + 1; it3 != it2; it3++) {
                            struct IsLvalueInVal final: public LvalueVisitor {
                                const MIRLValue& newDstLval;

                                explicit IsLvalueInVal(const MIRLValue& newDstLval)
                                    : newDstLval(newDstLval)
                                {
                                }

                                bool visitLvalue(const MIRLValue& lv, MIRValUsage /*vu*/) override {
                                    return lv.root == newDstLval.root;
                                }
                            } isLvalueInVal{newDstLval};

                            if (optVisitMirLvalues(*it3, isLvalueInVal)) {
                                wasInvalidated = true;
                                break;
                            }
                        }

                        if (!wasInvalidated) {
                            it->as_Assign().dst = mv$(it2->as_Assign().dst);
                            block.statements.erase(it2);
                            replacementHappend = true;
                            break;
                        }
                    }
                }
            }
        }

        {
            for (auto& block : fcn.blocks) {
                if (block.terminator.isDead()) {
                    continue;
                }

                if (block.terminator.is_Call()) {
                    // TODO: What if the destination located here is a 1:1 and its usage is listed to be replaced by the return value.
                    auto& e = block.terminator.as_Call();
                    if (!e.retVal.is_Local()) {
                        continue;
                    }
                    const auto& vu = valUses.localUses[e.retVal.as_Local()];
                    if (!(vu.read == 1 && vu.write == 1 && vu.borrow == 0)) {
                        continue;
                    }

                    const MIRLValue* newDst = nullptr;
                    auto& blk2 = fcn.blocks.at(e.retBlock);
                    for (const auto& stmt : blk2.statements) {
                        if (stmt.is_Assign() && stmt.as_Assign().src.is_Use() && stmt.as_Assign().src.as_Use() == e.retVal) {
                            newDst = &stmt.as_Assign().dst;
                            break;
                        }
                    }

                    if (newDst) {
                        auto lvalueImpactsDst = [&](const MIRLValue& lv) -> bool {
                            // TODO: Could restrict based on the presence of deref/field accesses?

                            if (lv.is_Local()) {
                                for (const auto& w : newDst->wrappers) {
                                    if (w.is_Index() && w.as_Index() == lv.as_Local()) {
                                        return true;
                                    }
                                }
                            }
                            return lv.root == newDst->root;
                        };
                        for (auto it = blk2.statements.begin(); it != blk2.statements.end(); ++it) {
                            state.setCurStmt(&blk2 - &fcn.blocks.front(), it - blk2.statements.begin());
                            const auto& stmt = *it;
                            if (stmt.is_Assign() && stmt.as_Assign().src.is_Use() && stmt.as_Assign().src.as_Use() == e.retVal) {
                                e.retVal = newDst->clone();
                                // TODO: Invalidate the entry, instead of deleting?
                                it = blk2.statements.erase(it);
                                replacementHappend = true;
                                break;
                            }

                            struct DstTouched final: public LvalueVisitor {
                                const MIRLValue& newDst;
                                decltype(lvalueImpactsDst)& lvalueImpactsDst;

                                DstTouched(const MIRLValue& newDst, decltype(lvalueImpactsDst)& lvalueImpactsDst)
                                    : newDst(newDst)
                                    , lvalueImpactsDst(lvalueImpactsDst)
                                {
                                }

                                bool visitLvalue(const MIRLValue& lv, MIRValUsage vu) override {
                                    return lv == newDst || (vu == MIRValUsage::Write && lvalueImpactsDst(lv));
                                }
                            } dstTouched{*newDst, lvalueImpactsDst};

                            if (optVisitMirLvalues(stmt, dstTouched)) {
                                break;
                            }
                        }
                    }
                }
            }
        }

        {
            for (auto& block : fcn.blocks) {
                for (auto it = block.statements.begin(); it != block.statements.end(); ++it) {
                    state.setCurStmt(&block - &fcn.blocks.front(), it - block.statements.begin());
                    if (const auto& se = it->opt_Assign()) {
                        if (const auto* srcE = se->src.opt_Use()) {
                            if (se->dst == *srcE) {
                                it = block.statements.erase(it) - 1;
                                continue;
                            }
                        }

                        if (se->dst.is_Local()) {
                            const auto& vu = valUses.localUses[se->dst.as_Local()];
                            if (vu.write == 1 && vu.read == 0 && vu.borrow == 0) {
                                it = block.statements.erase(it) - 1;
                            }
                        }
                    }
                }
            }
        }

        // TODO: Run special case replacements for when there's `tmp/var = arg` and `rv = tmp/var`

        return replacementHappend;
    }

    bool MIROptimiseDeadDropFlags(MIRTypeResolve& state, MIRFunction& fcn) {
        bool removedStatement = false;
        std::vector<bool> usedDropFlags(fcn.dropFlags.size());
        {
            std::vector<bool> readDropFlags(fcn.dropFlags.size());
            visitBlocks(state, fcn, makeCallable<MIRBlockConstCb>([&readDropFlags, &usedDropFlags](auto, const MIRBasicBlock& block) {
                for (const auto& stmt : block.statements) {
                    if (const auto* e = stmt.opt_SetDropFlag()) {
                        if (e->other != ~0u) {
                            readDropFlags[e->other] = true;
                            usedDropFlags[e->other] = true;
                        }
                        usedDropFlags[e->idx] = true;
                    } else if (const auto* e = stmt.opt_SaveDropFlag()) {
                        readDropFlags[e->idx] = true;
                        usedDropFlags[e->idx] = true;
                    } else if (const auto* e = stmt.opt_LoadDropFlag()) {
                        usedDropFlags[e->idx] = true;
                    }
                }
                if (const auto* e = block.terminator.opt_Drop()) {
                    if (e->flagIdx != ~0u) {
                        readDropFlags[e->flagIdx] = true;
                        usedDropFlags[e->flagIdx] = true;
                    }
                }
                if (const auto* e = block.terminator.opt_Switch()) {
                    if (e->validFlag != ~0u) {
                        readDropFlags[e->validFlag] = true;
                        usedDropFlags[e->validFlag] = true;
                    }
                }
            }));
            visitBlocksMut(state, fcn, makeCallable<MIRBlockCb>([&readDropFlags, &removedStatement](auto _id, auto& block) {
                for (auto it = block.statements.begin(); it != block.statements.end();) {
                    if (it->is_SetDropFlag() && !readDropFlags[it->as_SetDropFlag().idx]) {
                        removedStatement = true;
                        it = block.statements.erase(it);
                    } else if (it->is_LoadDropFlag() && !readDropFlags[it->as_LoadDropFlag().idx]) {
                        removedStatement = true;
                        it = block.statements.erase(it);
                    } else {
                        ++it;
                    }
                }
            }));
        }

        {
            std::vector<bool> editedDropFlags(fcn.dropFlags.size());
            visitBlocks(state, fcn, makeCallable<MIRBlockConstCb>([&editedDropFlags, &fcn](auto, const MIRBasicBlock& block) {
                for (const auto& stmt : block.statements) {
                    if (const auto* e = stmt.opt_SetDropFlag()) {
                        if (e->other != ~0u) {
                            editedDropFlags[e->idx] = true;
                        } else if (e->newVal != fcn.dropFlags[e->idx]) {
                            editedDropFlags[e->idx] = true;
                        } else {
                        }
                    }
                }
            }));
            visitBlocksMut(state, fcn, makeCallable<MIRBlockCb>([&editedDropFlags, &removedStatement, &fcn](auto _id, auto& block) {
                for (auto it = block.statements.begin(); it != block.statements.end();) {
                    if (const auto* e = it->opt_SetDropFlag()) {
                        if (!editedDropFlags[e->idx]) {
                            assert(e->newVal == fcn.dropFlags[e->idx]);
                            removedStatement = true;
                            it = block.statements.erase(it);
                        } else {
                            ++it;
                        }
                    } else {
                        ++it;
                    }
                }
            }));
        }

        return removedStatement;
    }

    bool MIROptimiseDeadAssignments(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;

        std::vector<bool> readLocals(fcn.locals.size());
        std::vector<bool> droppedLocals(fcn.locals.size());

        struct RecordReads final: public LvalueVisitor {
            decltype(readLocals)& readLocals;

            explicit RecordReads(decltype(readLocals)& readLocals)
                : readLocals(readLocals)
            {
            }

            bool visitLvalue(const MIRLValue& lv, MIRValUsage /*vu*/) override {
                if (lv.root.is_Local()) {
                    readLocals[lv.root.as_Local()] = true;
                }
                for (const auto& w : lv.wrappers) {
                    if (w.is_Index()) {
                        readLocals[w.as_Index()] = true;
                    }
                }
                return false;
            }
        } cb{readLocals};

        for (const auto& bb : fcn.blocks) {
            for (const auto& stmt : bb.statements) {
                if (stmt.is_Assign() && stmt.as_Assign().dst.is_Local()) {
                    optVisitMirLvalues(stmt.as_Assign().src, cb);
                } else {
                    optVisitMirLvalues(stmt, cb);
                }
            }
            if (const auto* drop = bb.terminator.opt_Drop(); drop && drop->slot.is_Local()) {
                droppedLocals[drop->slot.as_Local()] = true;
            } else {
                optVisitMirLvalues(bb.terminator, cb);
            }
        }

        for (auto& bb : fcn.blocks) {
            for (auto it = bb.statements.begin(); it != bb.statements.end();) {
                state.setCurStmt(&bb - &fcn.blocks.front(), it - bb.statements.begin());

                if (!(it->is_Assign() && it->as_Assign().dst.is_Local())) {
                    ++it;
                    continue;
                }
                auto idx = it->as_Assign().dst.as_Local();
                if (readLocals[idx]) {
                    ++it;
                    continue;
                }
                // If the local was dropped, then ignore IF it's not a borrow (TODO: Only if there's drop glue?)
                if (droppedLocals[idx] && !fcn.locals[idx]->is_Borrow()) {
                    ++it;
                    continue;
                }
                it = bb.statements.erase(it);
                changed = true;
            }
            if (auto* drop = bb.terminator.opt_Drop(); drop && drop->slot.is_Local()) {
                auto idx = drop->slot.as_Local();
                if (!readLocals[idx] && fcn.locals[idx]->is_Borrow()) {
                    auto target = drop->target;
                    bb.terminator = MIRTerminator::make_Goto(target);
                    changed = true;
                }
            }
        }

        return changed;
    }

    bool MIROptimiseNoopRemoval(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;

        HIRTypeRef tmpTy;
        for (auto& bb : fcn.blocks) {
            for (auto it = bb.statements.begin(); it != bb.statements.end(); ++it) {
                state.setCurStmt(&bb - fcn.blocks.data(), it - bb.statements.begin());
                if (it->is_Assign() && it->as_Assign().dst.is_Local() && it->as_Assign().src.is_Borrow() && it->as_Assign().src.as_Borrow().val.is_Deref()) {
                    const auto& dstLv = it->as_Assign().dst;
                    auto srcLv = it->as_Assign().src.as_Borrow().val.cloneUnwrapped();
                    for (auto it2 = it + 1; it2 != bb.statements.end(); ++it2) {
                        if (it2->is_Assign() && it2->as_Assign().src.is_Cast() && it2->as_Assign().src.as_Cast().val == dstLv) {
                            const auto& dstTy = it2->as_Assign().src.as_Cast().type;
                            HIRTypeRef tmp;
                            const auto& origTy = state.getLvalueType(tmp, srcLv);
                            if (origTy == dstTy) {
                                it2->as_Assign().src = std::move(srcLv);
                                break;
                            }
                        }
                        if (checkInvalidatesLvalue(*it2, srcLv, false)) {
                            break;
                        }
                    }
                }

                if (it->is_Assign() && it->as_Assign().dst.is_Local() && it->as_Assign().src.is_Cast() && it->as_Assign().src.as_Cast().type->is_Pointer()) {
                    const auto& dstLv = it->as_Assign().dst;
                    const auto& srcLv = it->as_Assign().src.as_Cast().val;
                    for (auto it2 = it + 1; it2 != bb.statements.end(); ++it2) {
                        if (it2->is_Assign() && it2->as_Assign().src.is_Cast() && it2->as_Assign().src.as_Cast().val == dstLv) {
                            const auto& dstTy = it2->as_Assign().src.as_Cast().type;
                            HIRTypeRef tmp;
                            const auto& origTy = state.getLvalueType(tmp, srcLv);
                            if (origTy == dstTy) {
                                it2->as_Assign().src = srcLv.clone();
                                break;
                            }
                        }
                        if (checkInvalidatesLvalue(*it2, srcLv, false)) {
                            break;
                        }
                    }
                }
            }

            for (auto it = bb.statements.begin(); it != bb.statements.end();) {
                state.setCurStmt(&bb - fcn.blocks.data(), it - bb.statements.begin());

                if (*it == MIRStatement::make_Asm({})) {
                    it = bb.statements.erase(it);
                    changed = true;

                    continue;
                }

                if (it->is_Assign() && it->as_Assign().src.is_Use() && it->as_Assign().src.as_Use() == it->as_Assign().dst) {
                    it = bb.statements.erase(it);
                    changed = true;

                    continue;
                }

                if (it->is_Assign() && it->as_Assign().src.is_Use() && state.getLvalueType(tmpTy, it->as_Assign().src.as_Use()) == state.crate.types.unit()) {
                    it->as_Assign().src = MIRRValue::make_Tuple({});
                    changed = true;

                    ++it;
                    continue;
                }

                if (it->is_Assign() && it->as_Assign().src.is_Borrow() && it->as_Assign().src.as_Borrow().val.is_Deref() && it->as_Assign().src.as_Borrow().val.cloneUnwrapped() == it->as_Assign().dst) {
                    it = bb.statements.erase(it);
                    changed = true;

                    continue;
                }

                if (it->is_Assign() && it->as_Assign().src.is_Cast() && it->as_Assign().src.as_Cast().type == state.getLvalueType(tmpTy, it->as_Assign().src.as_Cast().val)) {
                    auto v = mv$(it->as_Assign().src.as_Cast().val);
                    it->as_Assign().src = MIRRValue::make_Use({mv$(v)});
                    changed = true;

                    ++it;
                    continue;
                }

                ++it;
            }
            state.setCurStmtTerm(&bb - fcn.blocks.data());
            if (auto* drop = bb.terminator.opt_Drop(); drop && state.lvalueIsCopy(drop->slot)) {
                auto target = drop->target;
                bb.terminator = MIRTerminator::make_Goto(target);
                changed = true;
            }
        }

        return changed;
    }

    bool MIROptimiseGotoAssign(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;

        auto& blockPreds = operationsContext(state).blockPredecessors;
        if (blockPreds.size() < fcn.blocks.size()) {
            blockPreds.resize(fcn.blocks.size());
        }
        for (size_t i = 0; i < fcn.blocks.size(); i++) {
            blockPreds[i].clear();
        }
        for (const auto& srcBb : fcn.blocks) {
            unsigned srcIdx = &srcBb - fcn.blocks.data();

            struct CollectPreds final: public MIRTargetVisitor {
                std::vector<std::vector<unsigned>>& blockPreds;
                unsigned srcIdx;

                CollectPreds(std::vector<std::vector<unsigned>>& blockPreds, unsigned srcIdx)
                    : blockPreds(blockPreds)
                    , srcIdx(srcIdx)
                {
                }

                void visitTarget(const MIRBasicBlockId& target) override {
                    if (target < blockPreds.size()) {
                        blockPreds[target].push_back(srcIdx);
                    }
                }
            } collectPreds{blockPreds, srcIdx};

            visitTerminatorTarget(srcBb.terminator, collectPreds);
        }

        std::vector<unsigned> localReads(fcn.locals.size(), 0);
        std::vector<unsigned> localBorrows(fcn.locals.size(), 0);

        struct CountLocalUses final: public LvalueVisitor {
            decltype(localReads)& localReads;
            decltype(localBorrows)& localBorrows;

            CountLocalUses(decltype(localReads)& localReads, decltype(localBorrows)& localBorrows)
                : localReads(localReads)
                , localBorrows(localBorrows)
            {
            }

            bool visitLvalue(const MIRLValue& lv, MIRValUsage vu) override {
                if (lv.root.is_Local()) {
                    switch (vu) {
                        case MIRValUsage::Read:
                        case MIRValUsage::Move:
                            localReads[lv.root.as_Local()]++;
                            break;
                        case MIRValUsage::Borrow:
                            localBorrows[lv.root.as_Local()]++;
                            break;
                        case MIRValUsage::Write:
                            break;
                    }
                }
                for (const auto& w : lv.wrappers) {
                    if (w.is_Index()) {
                        localReads[w.as_Index()]++;
                    }
                }
                return true;
            }
        } countLocalUses{localReads, localBorrows};

        optVisitMirLvalues(state, fcn, countLocalUses);

        for (auto& dstBb : fcn.blocks) {
            if (dstBb.statements.empty()) {
                continue;
            }
            auto bbIdx = &dstBb - fcn.blocks.data();
            state.setCurStmt(bbIdx, 0);
            auto& stmt = dstBb.statements[0];
            if (!stmt.is_Assign()) {
                continue;
            }
            if (!stmt.as_Assign().src.is_Use()) {
                continue;
            }
            auto& dst = stmt.as_Assign().dst;
            auto& src = stmt.as_Assign().src.as_Use();

            if (!dst.wrappers.empty() || dst.root.is_Static()) {
                continue;
            }
            if (!src.is_Local()) {
                continue;
            }
            unsigned nRead = localReads[src.as_Local()];
            unsigned nBorrow = localBorrows[src.as_Local()];
            if (nRead > 1 || nBorrow > 0) {
                continue;
            }

            std::vector<unsigned> sources;
            unsigned numUsed = 0;
            const auto& preds = blockPreds[state.getCurBlock()];
            for (size_t predI = 0; predI < preds.size(); predI++) {
                unsigned bbIdx = preds[predI];
                const auto& srcBb = fcn.blocks[bbIdx];
                sources.push_back(bbIdx);
                if (predI > 0 && preds[predI - 1] == bbIdx) {
                    continue;
                }
                {
                    switch (srcBb.terminator.tag()) {
                        case MIRTerminator::TAG_Goto: {
                            if (!srcBb.statements.empty() && srcBb.statements.back().is_Assign() && srcBb.statements.back().as_Assign().dst == src) {
                                numUsed += 1;
                            }
                            break;
                        }
                        case MIRTerminator::TAG_Call: {
                            auto& e = srcBb.terminator.as_Call();
                            if (e.retBlock == state.getCurBlock() && e.retVal == src) {
                                numUsed += 1;
                            }
                            break;
                        } break;
                        default:
                            break;
                    }
                }
            }

            // TODO: Allow if one arm doesn't update?

            if (numUsed < sources.size()) {
                continue;
            }

            changed = true;

            for (auto bbIdx : sources) {
                auto& srcBb = fcn.blocks[bbIdx];

                if ((srcBb.terminator.is_Call() && (srcBb.terminator.as_Call().retVal == src))) {
                    srcBb.terminator.as_Call().retVal = dst.clone();
                } else if (!srcBb.statements.empty() && (srcBb.statements.back().is_Assign() && (srcBb.statements.back().as_Assign().dst == src))) {
                    srcBb.statements.back().as_Assign().dst = dst.clone();
                } else {
                    MIR_TODO(state, "Handle copying assignment to source");
                }
            }
            if (false && state.lvalueIsCopy(dst)) {
                auto d = dst.clone();
                dst = mv$(src);
                src = mv$(d);
            } else {
                stmt = MIRStatement();
            }
        }

        return changed;
    }

    // TODO: Could allow multiple uses if it's a shared borrow

    bool MIROptimiseUselessReborrows(MIRTypeResolve& state, MIRFunction& fcn) {
        bool changed = false;

        // TODO: This doesn't work if the assignment happens in a loop (can lead to multiple moves)

        return changed;
    }

    bool MIROptimiseGarbageCollectPartial(MIRTypeResolve& state, MIRFunction& fcn) {
        bool rv = false;
        std::vector<bool> visited(fcn.blocks.size());
        visitBlocks(state, fcn, makeCallable<MIRBlockConstCb>([&visited](auto bb, const auto& /*block*/) {
            assert(!visited[bb]);
            visited[bb] = true;
        }));
        for (unsigned int i = 0; i < visited.size(); i++) {
            auto& blk = fcn.blocks[i];
            if (!(blk.terminator.is_Incomplete() && blk.statements.empty()) && !visited[i]) {
                blk.statements.clear();
                blk.terminator = MIRTerminator::make_Incomplete({});
                rv = true;
            }
        }
        return rv;
    }

    bool MIROptimiseGarbageCollect(MIRTypeResolve& state, MIRFunction& fcn) {
        std::vector<bool> usedLocals(fcn.locals.size());
        std::vector<bool> usedDfs(fcn.dropFlags.size());
        std::vector<bool> visited(fcn.blocks.size());

        visitBlocks(state, fcn, makeCallable<MIRBlockConstCb>([&](auto bb, const auto& block) {
            visited[bb] = true;

            auto assignedLval = [&](const MIRLValue& lv) {
                // TODO: Consume through indexing/field accesses
                for (const auto& w : lv.wrappers) {
                    if (w.is_Field()) {
                    } else {
                        return;
                    }
                }
                if (lv.root.is_Local()) {
                    usedLocals[lv.root.as_Local()] = true;
                }
            };

            for (const auto& stmt : block.statements) {
                if (stmt.is_Assign()) {
                    auto& e = stmt.as_Assign();
                    assignedLval(e.dst);
                } else if (const auto* e = stmt.opt_Asm()) {
                    for (const auto& val : e->outputs) {
                        assignedLval(val.second);
                    }
                } else if (const auto* e = stmt.opt_Asm2()) {
                    for (const auto& p : e->params) {
                        if (p.is_Reg() && p.as_Reg().output) {
                            assignedLval(*p.as_Reg().output);
                        }
                    }
                } else if (const auto* e = stmt.opt_SetDropFlag()) {
                    if (e->other != ~0u) {
                        usedDfs.at(e->other) = true;
                    }
                    usedDfs.at(e->idx) = true;
                } else if (const auto* e = stmt.opt_LoadDropFlag()) {
                    usedDfs.at(e->idx) = true;
                }
            }

            if (const auto* te = block.terminator.opt_Call()) {
                assignedLval(te->retVal);
            } else if (const auto* te = block.terminator.opt_Drop()) {
                if (te->flagIdx != ~0u) {
                    usedDfs.at(te->flagIdx) = true;
                }
            } else if (const auto* te = block.terminator.opt_Switch()) {
                if (te->validFlag != ~0u) {
                    usedDfs.at(te->validFlag) = true;
                }
            }
        }));

        std::vector<unsigned int> localRewriteTable;
        unsigned int nLocals = fcn.locals.size();
        for (unsigned int i = 0, j = 0; i < nLocals; i++) {
            if (!usedLocals[i]) {
                fcn.locals.erase(fcn.locals.begin() + j);
            }
            localRewriteTable.push_back(usedLocals[i] ? j++ : ~0u);
        }
        std::vector<unsigned int> dfRewriteTable;
        unsigned int nDf = fcn.dropFlags.size();
        for (unsigned int i = 0, j = 0; i < nDf; i++) {
            dfRewriteTable.push_back(usedDfs[i] ? j++ : ~0u);
        }

        auto it = fcn.blocks.begin();
        for (unsigned int i = 0; i < visited.size(); i++) {
            if (visited[i]) {
                struct RewriteLocals final: public LvalueVisitorMut {
                    MIRTypeResolve& state;
                    const decltype(localRewriteTable)& localRewriteTable;

                    RewriteLocals(MIRTypeResolve& state, decltype(localRewriteTable) localRewriteTable)
                        : state(state)
                        , localRewriteTable(localRewriteTable)
                    {
                    }

                    bool visitLvalue(MIRLValue& lv, MIRValUsage /*vu*/) override {
                        if (lv.root.is_Local()) {
                            auto e = lv.root.as_Local();
                            MIR_ASSERT(state, e < localRewriteTable.size(), "Variable out of range - " << lv);
                            MIR_ASSERT(state, localRewriteTable.at(e) != ~0u, "LValue " << lv << " incorrectly marked as unused");
                            lv.root = MIRLValue::Storage::newLocal(localRewriteTable.at(e));
                        }
                        for (auto& w : lv.wrappers) {
                            if (w.is_Index()) {
                                w = MIRLValue::Wrapper::newIndex(localRewriteTable.at(w.as_Index()));
                            }
                        }
                        return false;
                    }
                } lvalueCb{state, localRewriteTable};

                std::vector<bool> toRemoveStatements(it->statements.size());
                for (auto& stmt : it->statements) {
                    auto stmtIdx = &stmt - &it->statements.front();
                    state.setCurStmt(i, stmtIdx);

                    if (stmt == MIRStatement()) {
                        toRemoveStatements[stmtIdx] = true;
                        continue;
                    }

                    optVisitMirLvaluesMut(stmt, lvalueCb);
                    if (auto* se = stmt.opt_SetDropFlag()) {
                        if (dfRewriteTable[se->idx] == ~0u) {
                            toRemoveStatements[stmtIdx] = true;
                            continue;
                        }
                        se->idx = dfRewriteTable[se->idx];
                        if (se->other != ~0u) {
                            se->other = dfRewriteTable[se->other];
                        }
                    } else if (auto* se = stmt.opt_LoadDropFlag()) {
                        se->idx = dfRewriteTable[se->idx];
                    } else if (auto* se = stmt.opt_SaveDropFlag()) {
                        se->idx = dfRewriteTable[se->idx];
                    } else if (auto* se = stmt.opt_ScopeEnd()) {
                        for (auto it = se->slots.begin(); it != se->slots.end();) {
                            if (localRewriteTable.at(*it) == ~0u) {
                                it = se->slots.erase(it);
                            } else {
                                *it = localRewriteTable.at(*it);
                                ++it;
                            }
                        }

                        if (se->slots.empty()) {
                            toRemoveStatements[stmtIdx] = true;
                            continue;
                        }
                    }
                }
                state.setCurStmtTerm(i);
                if (auto* drop = it->terminator.opt_Drop()) {
                    bool removeDrop = false;
                    if (drop->flagIdx != ~0u && dfRewriteTable[drop->flagIdx] == ~0u) {
                        if (fcn.dropFlags.at(drop->flagIdx)) {
                            drop->flagIdx = ~0u;
                        } else {
                            removeDrop = true;
                        }
                    }
                    if (drop->slot.is_Local() && localRewriteTable[drop->slot.as_Local()] == ~0u) {
                        removeDrop = true;
                    }
                    if (removeDrop) {
                        auto target = drop->target;
                        it->terminator = MIRTerminator::make_Goto(target);
                    }
                }
                optVisitMirLvaluesMut(it->terminator, lvalueCb);
                if (auto* drop = it->terminator.opt_Drop()) {
                    if (drop->flagIdx != ~0u) {
                        drop->flagIdx = dfRewriteTable[drop->flagIdx];
                    }
                } else if (auto* sw = it->terminator.opt_Switch()) {
                    if (sw->validFlag != ~0u) {
                        sw->validFlag = dfRewriteTable[sw->validFlag];
                    }
                }

                assert(it->statements.size() == toRemoveStatements.size());
                auto newEnd = std::remove_if(it->statements.begin(), it->statements.end(), [&](const auto& s) {
                    size_t stmtIdx = (&s - &it->statements.front());
                    return toRemoveStatements[stmtIdx];
                });
                it->statements.erase(newEnd, it->statements.end());
            }
            ++it;
        }

        visited.assign(fcn.blocks.size(), false);
        visitBlocks(state, fcn, makeCallable<MIRBlockConstCb>([&](auto bb, const auto&) {
            visited[bb] = true;
        }));

        std::vector<unsigned int> blockRewriteTable;
        for (unsigned int i = 0, j = 0; i < fcn.blocks.size(); i++) {
            blockRewriteTable.push_back(visited[i] ? j++ : ~0u);
        }
        for (unsigned int i = 0; i < fcn.blocks.size(); i++) {
            if (!visited[i]) {
                continue;
            }

            struct ApplyRewriteTable final: public MIRTargetVisitorMut {
                const MIRTypeResolve& state;
                const std::vector<unsigned int>& blockRewriteTable;

                ApplyRewriteTable(const MIRTypeResolve& state, const std::vector<unsigned int>& blockRewriteTable)
                    : state(state)
                    , blockRewriteTable(blockRewriteTable)
                {
                }

                void visitTarget(MIRBasicBlockId& target) override {
                    MIR_ASSERT(state, target < blockRewriteTable.size(), "Block target out of range - bb" << target);
                    MIR_ASSERT(state, blockRewriteTable[target] != ~0u, "Reachable block targets unreachable bb" << target);
                    target = blockRewriteTable[target];
                }
            } applyRewriteTable{state, blockRewriteTable};

            visitTerminatorTargetMut(fcn.blocks[i].terminator, applyRewriteTable);
        }

        auto newBlocksEnd = std::remove_if(fcn.blocks.begin(), fcn.blocks.end(), [&](const auto& bb) {
            size_t i = &bb - &fcn.blocks.front();
            return !visited[i];
        });
        fcn.blocks.erase(newBlocksEnd, fcn.blocks.end());

        for (unsigned int i = 0, j = 0; i < nDf; i++) {
            if (!usedDfs[i]) {
                fcn.dropFlags.erase(fcn.dropFlags.begin() + j);
            } else {
                j++;
            }
        }

        // TODO: Detect if any optimisations happened, and return true in that case
        return false;
    }
}

void MIRCreateOperationsContext(WireBoard& wb, ObjPool& pool) {
    wb.mirOperations = pool.make<MirOperationsContext>();
}

void MIRCleanup(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType) {
    Span sp;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << path;
    });
    MIRTypeResolve state{sp, resolve, pathCallback, retType, args, fcn};

    MirMutator mutator{fcn, 0, 0};
    for (auto& block : fcn.blocks) {
        for (auto it = block.statements.begin(); it != block.statements.end(); ++it) {
            mutator.updateState(state);
            auto& stmt = *it;

            HIRTypeRef tmp;
            if ((stmt.is_Assign() && (stmt.as_Assign().src.is_Borrow())) && state.getLvalueType(tmp, stmt.as_Assign().src.as_Borrow().val)->is_Diverge()) {
            } else {
                if (visitMirLvalues(stmt, [&](const auto& lv, auto /*vu*/) {
                    return state.getLvalueType(tmp, lv)->is_Diverge();
                })) {
                    block.statements.erase(it, block.statements.end());
                    block.terminator = MIRTerminator::make_Unreachable({});
                    break;
                }
            }
            switch (stmt.tag()) {
                case MIRStatement::TAG_SetDropFlag: {
                    break;
                }
                case MIRStatement::TAG_SaveDropFlag: {
                    auto& se = stmt.as_SaveDropFlag();
                    MIRCleanupLValue(state, mutator, se.slot);
                    break;
                }
                case MIRStatement::TAG_LoadDropFlag: {
                    auto& se = stmt.as_LoadDropFlag();
                    MIRCleanupLValue(state, mutator, se.slot);
                    break;
                }
                case MIRStatement::TAG_ScopeEnd: {
                    break;
                }
                case MIRStatement::TAG_Asm: {
                    auto& se = stmt.as_Asm();
                    for (auto& v : se.inputs) {
                        MIRCleanupLValue(state, mutator, v.second);
                    }
                    for (auto& v : se.outputs) {
                        MIRCleanupLValue(state, mutator, v.second);
                    }
                    break;
                }
                case MIRStatement::TAG_Asm2: {
                    auto& e = stmt.as_Asm2();
                    for (auto& p : e.params) {
                        switch (p.tag()) {
                            case MIRAsmParam::TAG_Const: {
                                MIRCleanupAsmConst(state, mutator, p);
                                break;
                            }
                            case MIRAsmParam::TAG_Sym: {
                                break;
                            }
                            case MIRAsmParam::TAG_Reg: {
                                auto& v = p.as_Reg();
                                if (v.input) {
                                    MIRCleanupParam(state, mutator, *v.input);
                                }
                                if (v.output) {
                                    MIRCleanupLValue(state, mutator, *v.output);
                                }
                                break;
                            }
                            case MIRAsmParam::TAG_Label: {
                                break;
                            }
                        }
                    }
                    break;
                }
                case MIRStatement::TAG_Assign: {
                    auto& se = stmt.as_Assign();
                    MIRCleanupLValue(state, mutator, se.dst);
                    switch (se.src.tag()) {
                        case MIRRValue::TAG_Use: {
                            auto& re = se.src.as_Use();
                            MIRCleanupLValue(state, mutator, re);
                            break;
                        }
                        case MIRRValue::TAG_Constant: {
                            auto& re = se.src.as_Constant();
                            MIRCleanupConstant(state, mutator, re);
                            break;
                        }
                        case MIRRValue::TAG_SizedArray: {
                            auto& re = se.src.as_SizedArray();
                            MIRCleanupParam(state, mutator, re.val);
                            break;
                        }
                        case MIRRValue::TAG_Borrow: {
                            auto& re = se.src.as_Borrow();
                            MIRCleanupLValue(state, mutator, re.val);
                            break;
                        }
                        case MIRRValue::TAG_Cast: {
                            auto& re = se.src.as_Cast();
                            MIRCleanupLValue(state, mutator, re.val);
                            break;
                        }
                        case MIRRValue::TAG_BinOp: {
                            auto& re = se.src.as_BinOp();
                            MIRCleanupParam(state, mutator, re.valL);
                            MIRCleanupParam(state, mutator, re.valR);
                            break;
                        }
                        case MIRRValue::TAG_UniOp: {
                            auto& re = se.src.as_UniOp();
                            MIRCleanupLValue(state, mutator, re.val);
                            break;
                        }
                        case MIRRValue::TAG_DstMeta: {
                            auto& re = se.src.as_DstMeta();
                            HIRTypeRef tmp;
                            const auto& ty = state.getLvalueType(tmp, re.val);

                            if (const auto* array = ty->opt_Array()) {
                                MIRCleanupLValue(state, mutator, re.val);
                                if (array->size.is_Known()) {
                                    se.src = MIRConstant::make_Uint({U128(array->size.as_Known()), HIRCoreType::Usize});
                                } else if (const auto* value = array->size.as_Unevaluated().opt_Evaluated()) {
                                    se.src = MIRConstant::make_Uint({U128((*value)->readUsize(0)), HIRCoreType::Usize});
                                }
                                break;
                            }

                            if (!state.resolve.typeIsSized(state.sp, ty)) {
                                MIRCleanupLValue(state, mutator, re.val);
                                break;
                            }

                            re.val.wrappers.push_back(MIRLValue::Wrapper::newDeref());
                            MIRCleanupLValue(state, mutator, re.val);
                            re.val.wrappers.pop_back();

                            HIRTypeRef cleanedTmp;
                            const auto* cleanedTy = state.getLvalueType(cleanedTmp, re.val);
                            const HIRTypeData* ityP;
                            if (const auto* te = cleanedTy->opt_Borrow()) {
                                ityP = te->inner;
                            } else if (const auto* te = cleanedTy->opt_Pointer()) {
                                ityP = te->inner;
                            } else if (cleanedTy->is_TraitObject()) {
                                ityP = cleanedTy;
                                MIR_ASSERT(state, !re.val.wrappers.empty() && re.val.wrappers.back().is_Deref(), "DstMeta on bare trait object with no deref: " << re.val);
                                re.val.wrappers.pop_back();
                            } else {
                                BUG(Span(), "Unexpected input type for DstMeta - " << cleanedTy);
                            }
                            break;
                        }
                        case MIRRValue::TAG_DstPtr: {
                            auto& re = se.src.as_DstPtr();
                            HIRTypeRef tmp;
                            const auto& ty = state.getLvalueType(tmp, re.val);
                            if (!state.resolve.typeIsSized(state.sp, ty)) {
                                MIRCleanupLValue(state, mutator, re.val);
                                break;
                            }

                            re.val.wrappers.push_back(MIRLValue::Wrapper::newDeref());
                            MIRCleanupLValue(state, mutator, re.val);
                            re.val.wrappers.pop_back();

                            HIRTypeRef cleanedTmp;
                            const auto* cleanedTy = state.getLvalueType(cleanedTmp, re.val);
                            const HIRTypeData* ityP;
                            if (const auto* te = cleanedTy->opt_Borrow()) {
                                ityP = te->inner;
                            } else if (const auto* te = cleanedTy->opt_Pointer()) {
                                ityP = te->inner;
                            } else if (cleanedTy->is_TraitObject()) {
                                ityP = cleanedTy;
                                MIR_ASSERT(state, !re.val.wrappers.empty() && re.val.wrappers.back().is_Deref(), "DstPtr on bare trait object with no deref: " << re.val);
                                re.val.wrappers.pop_back();
                            } else {
                                BUG(Span(), "Unexpected input type for DstMeta - " << cleanedTy);
                            }
                            (void)ityP; // TODO: What is this needed for?
                            break;
                        }
                        case MIRRValue::TAG_MakeDst: {
                            auto& re = se.src.as_MakeDst();
                            MIRCleanupParam(state, mutator, re.ptrVal);
                            MIRCleanupParam(state, mutator, re.metaVal);
                            break;
                        }
                        case MIRRValue::TAG_Tuple: {
                            auto& re = se.src.as_Tuple();
                            for (auto& lv : re.vals) {
                                MIRCleanupParam(state, mutator, lv);
                            }
                            break;
                        }
                        case MIRRValue::TAG_Array: {
                            auto& re = se.src.as_Array();
                            for (auto& lv : re.vals) {
                                MIRCleanupParam(state, mutator, lv);
                            }
                            break;
                        }
                        case MIRRValue::TAG_UnionVariant: {
                            auto& re = se.src.as_UnionVariant();
                            MIRCleanupParam(state, mutator, re.val);
                            break;
                        }
                        case MIRRValue::TAG_EnumVariant: {
                            auto& re = se.src.as_EnumVariant();
                            for (auto& lv : re.vals) {
                                MIRCleanupParam(state, mutator, lv);
                            }
                            break;
                        }
                        case MIRRValue::TAG_Struct: {
                            auto& re = se.src.as_Struct();
                            for (auto& lv : re.vals) {
                                MIRCleanupParam(state, mutator, lv);
                            }
                            break;
                        }
                    }
                    break;
                }
            }

            if (stmt.is_Assign()) {
                auto& se = stmt.as_Assign();

                if (auto* e = se.src.opt_Constant()) {
                    if (auto* ce = e->opt_Const()) {
                        MonomorphState params(state.crate.types);
                        HIRTypeRef ty;
                        const auto* litPtr = MIRCleanupGetConstant(state, *ce->p, ty, params);
                        if (litPtr) {
                            se.src = MIRCleanupLiteralToRValue(state, mutator, *litPtr, mv$(ty), params, mv$(*ce->p));
                            if (auto* p = se.src.opt_Constant()) {
                                MIRCleanupConstant(state, mutator, *p);
                            }
                        } else {
                        }
                    }
                }

                if (auto* e = se.src.opt_MakeDst()) {
                    if ((e->metaVal.is_Constant() && e->metaVal.as_Constant().is_ItemAddr() && e->metaVal.as_Constant().as_ItemAddr().get() == nullptr)) {
                        HIRTypeRef tmp, tmp2;
                        const auto& srcTy = state.getParamType(tmp, e->ptrVal);
                        const auto& dstTy = state.getLvalueType(tmp2, se.dst);
                        MIR_ASSERT(state, e->ptrVal.is_LValue(), "BUG: MakeDst with no metadata should be LValue");
                        se.src = MIRCleanupCoerceUnsized(state, mutator, dstTy, srcTy, mv$(e->ptrVal.as_LValue()));
                    }
                }

                if (auto* e = se.src.opt_MakeDst()) {
                    if ((e->metaVal.is_Constant() && e->metaVal.as_Constant().is_ItemAddr() && e->metaVal.as_Constant().as_ItemAddr().get() == nullptr)) {
                        // TODO: Check the validity?

                        HIRTypeRef tmp;
                        const auto& srcTy = state.getParamType(tmp, e->ptrVal);
                        MIR_ASSERT(state, monomorphiseTypeNeeded(srcTy), "MakeDst Unsize with known source - " << srcTy);
                    }
                }
            }

            it = mutator.flushStmt();
        }

        mutator.updateState(state);

        switch (block.terminator.tag()) {
            case MIRTerminator::TAG_Incomplete: {
                break;
            }
            case MIRTerminator::TAG_Return: {
                break;
            }
            case MIRTerminator::TAG_UnwindResume: {
                break;
            }
            case MIRTerminator::TAG_UnwindTerminate: {
                break;
            }
            case MIRTerminator::TAG_Unreachable: {
                break;
            }
            case MIRTerminator::TAG_Goto: {
                break;
            }
            case MIRTerminator::TAG_If: {
                auto& e = block.terminator.as_If();
                MIRCleanupLValue(state, mutator, e.cond);
                break;
            }
            case MIRTerminator::TAG_Switch: {
                auto& e = block.terminator.as_Switch();
                MIRCleanupLValue(state, mutator, e.val);
                break;
            }
            case MIRTerminator::TAG_SwitchValue: {
                auto& e = block.terminator.as_SwitchValue();
                MIRCleanupLValue(state, mutator, e.val);
                break;
            }
            case MIRTerminator::TAG_Drop: {
                auto& e = block.terminator.as_Drop();
                MIRCleanupLValue(state, mutator, e.slot);
                break;
            }
            case MIRTerminator::TAG_Call: {
                auto& e = block.terminator.as_Call();
                MIRCleanupLValue(state, mutator, e.retVal);
                if (e.fcn.is_Value()) {
                    MIRCleanupLValue(state, mutator, e.fcn.as_Value());
                }
                for (auto& lv : e.args) {
                    MIRCleanupParam(state, mutator, lv);
                }
                break;
            }
            case MIRTerminator::TAG_TailCall: {
                auto& e = block.terminator.as_TailCall();
                if (e.fcn.is_Value()) {
                    MIRCleanupLValue(state, mutator, e.fcn.as_Value());
                }
                for (auto& param : e.args) {
                    MIRCleanupParam(state, mutator, param);
                }
                break;
            }
            case MIRTerminator::TAG_Asm2: {
                auto& e = block.terminator.as_Asm2();
                for (auto& p : e.params) {
                    if (auto* reg = p.opt_Reg()) {
                        if (reg->input) {
                            MIRCleanupParam(state, mutator, *reg->input);
                        }
                        if (reg->output) {
                            MIRCleanupLValue(state, mutator, *reg->output);
                        }
                    } else if (p.is_Const()) {
                        MIRCleanupAsmConst(state, mutator, p);
                    }
                }
                break;
            }
        }

        if (auto* ep = block.terminator.opt_Call()) {
            auto& e = *ep;
            if (auto* pathP = e.fcn.opt_Path()) {
                auto& path = *pathP;
                if (path.data.is_UfcsKnown() && path.data.as_UfcsKnown().type->is_TraitObject()) {
                    const auto& pe = path.data.as_UfcsKnown();
                    const auto& te = pe.type->as_TraitObject();
                    // TODO: What if the method is from a supertrait?

                    if (!te.trait.traitPtr || e.args.empty()) {
                    } else if (te.trait.path == pe.trait || resolve.findNamedTraitInTrait(sp, pe.trait.path, pe.trait.params, *te.trait.traitPtr, te.trait.path.path, te.trait.path.params, pe.type, [](const auto&, auto) {
                        return true;
                    })) {
                        auto tgtLvalue = MIRCleanupVirtualize(sp, state, mutator, e.args.front().as_LValue(), pe);
                        e.fcn = mv$(tgtLvalue);
                    }
                } else if (path.data.is_UfcsKnown() && path.data.as_UfcsKnown().type->is_Function()) {
                    const auto& pe = path.data.as_UfcsKnown();
                    const auto& fcnTy = pe.type->as_Function();
                    if (pe.trait.path == resolve.langFn() || pe.trait.path == resolve.langFnMut() || pe.trait.path == resolve.langFnOnce()) {
                        MIR_ASSERT(state, e.args.size() == 2, "Fn* call requires two arguments");
                        auto fcnLvalue = mv$(e.args[0].as_LValue());
                        auto argsLvalue = mv$(e.args[1].as_LValue());

                        e.args.clear();
                        e.args.reserve(fcnTy.argTypes.size());
                        for (unsigned int i = 0; i < fcnTy.argTypes.size(); i++) {
                            e.args.push_back(MIRLValue::newField(argsLvalue.clone(), i));
                        }
                        if (pe.trait.path == resolve.langFnOnce()) {
                            e.fcn = mv$(fcnLvalue);
                        } else {
                            e.fcn = MIRLValue::newDeref(mv$(fcnLvalue));
                        }
                    }
                } else if (path.data.is_UfcsKnown() && path.data.as_UfcsKnown().type->is_NamedFunction()) {
                    const auto& pe = path.data.as_UfcsKnown();
                    const auto& fcnTy = pe.type->as_NamedFunction();
                    if (pe.trait.path == resolve.langFn() || pe.trait.path == resolve.langFnMut() || pe.trait.path == resolve.langFnOnce()) {
                        auto nArgs = fcnTy.decay(state.crate.types, state.sp).argTypes.size();
                        MIR_ASSERT(state, e.args.size() == 2, "Fn* call requires two arguments");
                        auto fcnLvalue = mv$(e.args[0].as_LValue());
                        auto argsLvalue = mv$(e.args[1].as_LValue());

                        e.args.clear();
                        e.args.reserve(nArgs);
                        for (unsigned int i = 0; i < nArgs; i++) {
                            e.args.push_back(MIRLValue::newField(argsLvalue.clone(), i));
                        }
                        switch (fcnTy.def.tag()) {
                            case HIRTypeDataNamedFunctionTy::TAG_Function: {
                                e.fcn = fcnTy.path.clone();
                                break;
                            }
                            case HIRTypeDataNamedFunctionTy::TAG_StructConstructor: {
                                block.statements.push_back(MIRStatement::make_Assign({std::move(e.retVal), MIRRValue::make_Struct({fcnTy.path.data.as_Generic().clone(), std::move(e.args)})}));
                                block.terminator = MIRTerminator::make_Goto(e.retBlock);
                                break;
                            }
                            case HIRTypeDataNamedFunctionTy::TAG_EnumConstructor: {
                                auto& ve = fcnTy.def.as_EnumConstructor();
                                auto enmPath = fcnTy.path.data.as_Generic().clone();
                                enmPath.path.popComponent();
                                block.statements.push_back(MIRStatement::make_Assign({std::move(e.retVal), MIRRValue::make_EnumVariant({std::move(enmPath), static_cast<unsigned>(ve.v), std::move(e.args)})}));
                                block.terminator = MIRTerminator::make_Goto(e.retBlock);
                                break;
                            }
                        }
                    }
                }
            }

            if (e.fcn.is_Intrinsic() && e.fcn.as_Intrinsic().name == "read_via_copy") {
                // TODO: Replace with `res = *ptr;`
                block.statements.push_back(MIRStatement::make_Assign({std::move(e.retVal), MIRLValue::newDeref(std::move(e.args.at(0).as_LValue()))}));
                block.terminator = MIRTerminator::make_Goto(e.retBlock);
            }
            if (e.fcn.is_Intrinsic() && e.fcn.as_Intrinsic().name == "write_via_move") {
                // TODO: Replace with `*ptr = arg;`
                block.statements.push_back(MIRStatement::make_Assign({MIRLValue::newDeref(std::move(e.args.at(0).as_LValue())), std::move(e.args.at(1).as_LValue())}));
                block.statements.push_back(MIRStatement::make_Assign({std::move(e.retVal), MIRRValue::make_Tuple({})}));
                block.terminator = MIRTerminator::make_Goto(e.retBlock);
            }
        }

        if (auto* ep = block.terminator.opt_TailCall()) {
            auto& e = *ep;
            if (auto* pathP = e.fcn.opt_Path()) {
                auto& path = *pathP;
                if (path.data.is_UfcsKnown() && path.data.as_UfcsKnown().type->is_TraitObject()) {
                    const auto& pe = path.data.as_UfcsKnown();
                    const auto& traitObject = pe.type->as_TraitObject();
                    if (traitObject.trait.path == pe.trait || resolve.findNamedTraitInTrait(sp, pe.trait.path, pe.trait.params, *traitObject.trait.traitPtr, traitObject.trait.path.path, traitObject.trait.path.params, pe.type, [](const auto&, auto) {
                        return true;
                    })) {
                        e.fcn = MIRCleanupVirtualize(sp, state, mutator, e.args.front().as_LValue(), pe);
                    }
                } else if (path.data.is_UfcsKnown() && path.data.as_UfcsKnown().type->is_Function()) {
                    const auto& pe = path.data.as_UfcsKnown();
                    const auto& fcnTy = pe.type->as_Function();
                    if (pe.trait.path == resolve.langFn() || pe.trait.path == resolve.langFnMut() || pe.trait.path == resolve.langFnOnce()) {
                        MIR_ASSERT(state, e.args.size() == 2, "Fn* tail call requires two arguments");
                        auto fcnLvalue = mv$(e.args[0].as_LValue());
                        auto argsLvalue = mv$(e.args[1].as_LValue());
                        e.args.clear();
                        e.args.reserve(fcnTy.argTypes.size());
                        for (unsigned int i = 0; i < fcnTy.argTypes.size(); i++) {
                            e.args.push_back(MIRLValue::newField(argsLvalue.clone(), i));
                        }
                        e.fcn = pe.trait.path == resolve.langFnOnce() ? mv$(fcnLvalue) : MIRLValue::newDeref(mv$(fcnLvalue));
                    }
                } else if (path.data.is_UfcsKnown() && path.data.as_UfcsKnown().type->is_NamedFunction()) {
                    const auto& pe = path.data.as_UfcsKnown();
                    const auto& fcnTy = pe.type->as_NamedFunction();
                    if (pe.trait.path == resolve.langFn() || pe.trait.path == resolve.langFnMut() || pe.trait.path == resolve.langFnOnce()) {
                        auto nArgs = fcnTy.decay(state.crate.types, state.sp).argTypes.size();
                        MIR_ASSERT(state, e.args.size() == 2, "Named function tail call requires two arguments");
                        auto argsLvalue = mv$(e.args[1].as_LValue());
                        e.args.clear();
                        e.args.reserve(nArgs);
                        for (unsigned int i = 0; i < nArgs; i++) {
                            e.args.push_back(MIRLValue::newField(argsLvalue.clone(), i));
                        }
                        switch (fcnTy.def.tag()) {
                            case HIRTypeDataNamedFunctionTy::TAG_Function: {
                                auto& _ = fcnTy.def.as_Function();
                                e.fcn = fcnTy.path.clone();
                                break;
                            }
                            case HIRTypeDataNamedFunctionTy::TAG_StructConstructor: {
                                auto& _ = fcnTy.def.as_StructConstructor();
                                MIR_BUG(state, "Struct constructor used as an explicit tail-call target");
                                break;
                            }
                            case HIRTypeDataNamedFunctionTy::TAG_EnumConstructor: {
                                auto& _ = fcnTy.def.as_EnumConstructor();
                                MIR_BUG(state, "Enum constructor used as an explicit tail-call target");
                                break;
                            }
                        }
                    }
                }
            }
        }

        mutator.flushBlock();
    }
}

void MIRCleanupCrate(const WireBoard& wb, HIRCrate& crate) {
    auto callback = makeCallable<MIRExprCb>([&](const auto& res, const auto& p, HIRExprPtr& exprPtr, const auto& args, const auto& ty) {
        if (exprPtr) {
            MIRCleanup(res, p, exprPtr.getMirOrErrorMut(Span()), args, ty);
        }
    });
    MIROuterVisitor ov{wb, crate, callback};
    ov.visitCrate(crate);
}

void MIROptimiseMin(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType) {
    Span sp;
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << path;
    });
    MIRTypeResolve state{sp, resolve, pathCallback, retType, args, fcn};

    while (MIROptimiseInlining(state, fcn, true)) {
        MIRCleanup(resolve, path, fcn, args, retType);
    }

    MIROptimiseBlockSimplify(state, fcn);
    MIROptimiseUnifyBlocks(state, fcn);

    MIROptimiseGarbageCollect(state, fcn);
    MIRSortBlocks(resolve, path, fcn);

    return;
}

void MIROptimise(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType, unsigned optLevel, bool doInline /*=true*/, bool validate /*=true*/) {
    Span sp;
    assert(optLevel > 0);
    auto pathCallback = makeCallable<MIRPathCb>([&](auto& os) {
        os << path;
    });
    MIRTypeResolve state{sp, resolve, pathCallback, retType, args, fcn};

    bool changeHappened;
    unsigned int passNum = 0;
    do {
        MIR_ASSERT(state, passNum < 100, "Too many MIR optimisation iterations");

        changeHappened = false;

        if (MIROptimiseBlockSimplify(state, fcn)) {
        }

        if (MIROptimiseConstPropagate(state, fcn)) {
            changeHappened = true;
        }

        if (MIROptimiseDeTemporary(state, fcn)) {
            while (MIROptimiseDeTemporary(state, fcn)) {
            }
            changeHappened = true;
        }

        if (optLevel >= 2 && MIROptimiseSplitAggregates(state, fcn)) {
            changeHappened = true;
        }

        if (optLevel >= 2 && MIROptimisePropagateKnownValues(state, fcn)) {
            changeHappened = true;
        }

        // TODO: Convert `&mut *mut_foo` into `mut_foo` if the source is movable and not used afterwards

        if (MIROptimisePropagateSingleAssignments(state, fcn)) {
            while (MIROptimisePropagateSingleAssignments(state, fcn)) {
            }
            changeHappened = true;
        }

        if (MIROptimiseUnifyBlocks(state, fcn)) {
            changeHappened = true;
        }
        if (MIROptimiseDeadDropFlags(state, fcn)) {
            changeHappened = true;
        }
        if (optLevel >= 2 && MIROptimiseDeadAssignments(state, fcn)) {
            changeHappened = true;
        }
        if (MIROptimiseNoopRemoval(state, fcn)) {
            changeHappened = true;
        }

        if (MIROptimiseUselessReborrows(state, fcn)) {
            changeHappened = true;
        }

        if (MIROptimiseGotoAssign(state, fcn)) {
            changeHappened = true;
        }

        if (doInline && !changeHappened) {
            if (MIROptimiseInlining(state, fcn, /*minimal=*/false)) {
                MIRCleanup(resolve, path, fcn, args, retType);
                changeHappened = true;
            }
        }

        if (MIROptimiseGarbageCollectPartial(state, fcn)) {
            changeHappened = true;
        }

        passNum += 1;
    } while (changeHappened);

    MIROptimiseGarbageCollect(state, fcn);

    MIRSortBlocks(resolve, path, fcn);
}

void MIRSortBlocks(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn) {
    std::vector<bool> visited(fcn.blocks.size());
    std::vector<std::pair<unsigned, unsigned>> depths(fcn.blocks.size());

    struct Todo {
        size_t bbIdx;
        unsigned branchCount;
        unsigned level;
    };

    unsigned int branches = 0;
    std::vector<Todo> todo;
    todo.push_back(Todo{0, 0, 0});

    while (!todo.empty()) {
        auto info = todo.back();
        todo.pop_back();
        if (visited[info.bbIdx]) {
            continue;
        }

        visited[info.bbIdx] = true;
        depths[info.bbIdx] = std::make_pair(info.branchCount, info.level);
        const auto& bb = fcn.blocks[info.bbIdx];

        switch (bb.terminator.tag()) {
            case MIRTerminator::TAG_Incomplete: {
                break;
            }
            case MIRTerminator::TAG_Return: {
                break;
            }
            case MIRTerminator::TAG_UnwindResume: {
                break;
            }
            case MIRTerminator::TAG_UnwindTerminate: {
                break;
            }
            case MIRTerminator::TAG_Unreachable: {
                break;
            }
            case MIRTerminator::TAG_Goto: {
                auto& te = bb.terminator.as_Goto();
                todo.push_back(Todo{te, info.branchCount, info.level + 1});
                break;
            }
            case MIRTerminator::TAG_If: {
                auto& te = bb.terminator.as_If();
                todo.push_back(Todo{te.bbTrue, ++branches, info.level + 1});
                todo.push_back(Todo{te.bbFalse, ++branches, info.level + 1});
                break;
            }
            case MIRTerminator::TAG_Switch: {
                auto& te = bb.terminator.as_Switch();
                for (auto dst : te.targets) {
                    todo.push_back(Todo{dst, ++branches, info.level + 1});
                }
                if (te.validFlag != ~0u) {
                    todo.push_back(Todo{te.invalidTarget, ++branches, info.level + 1});
                }
                break;
            }
            case MIRTerminator::TAG_SwitchValue: {
                auto& te = bb.terminator.as_SwitchValue();
                for (auto dst : te.targets) {
                    todo.push_back(Todo{dst, ++branches, info.level + 1});
                }
                todo.push_back(Todo{te.defTarget, info.branchCount, info.level + 1});
                break;
            }
            case MIRTerminator::TAG_Drop: {
                auto& te = bb.terminator.as_Drop();
                todo.push_back(Todo{te.target, info.branchCount, info.level + 1});
                if (te.unwind.is_Cleanup()) {
                    auto& target = te.unwind.as_Cleanup();
                    todo.push_back(Todo{target, ++branches, info.level + 1});
                }
                break;
            }
            case MIRTerminator::TAG_Call: {
                auto& te = bb.terminator.as_Call();
                todo.push_back(Todo{te.retBlock, info.branchCount, info.level + 1});
                if (te.unwind.is_Cleanup()) {
                    auto& target = te.unwind.as_Cleanup();
                    todo.push_back(Todo{target, ++branches, info.level + 1});
                }
                break;
            }
            case MIRTerminator::TAG_TailCall: {
                break;
            }
            case MIRTerminator::TAG_Asm2: {
                auto& te = bb.terminator.as_Asm2();
                if (te.retBlock != ~0u) {
                    todo.push_back(Todo{te.retBlock, info.branchCount, info.level + 1});
                }
                for (const auto& p : te.params) {
                    if (const auto* dst = p.opt_Label()) {
                        todo.push_back(Todo{*dst, ++branches, info.level + 1});
                    }
                }
                break;
            }
        }
    }

    std::vector<size_t> idxes;
    idxes.reserve(fcn.blocks.size());
    for (size_t i = 0; i < fcn.blocks.size(); i++) {
        idxes.push_back(i);
    }
    std::sort(idxes.begin(), idxes.end(), [&](auto a, auto b) {
        return depths.at(a) < depths.at(b);
    });

    decltype(fcn.blocks) newBlockList;
    newBlockList.reserve(fcn.blocks.size());
    for (auto idx : idxes) {
        auto fixBbIdx = [&](auto idx) {
            return std::find(idxes.begin(), idxes.end(), idx) - idxes.begin();
        };
        newBlockList.push_back(mv$(fcn.blocks[idx]));
        newBlockList.back().statements.shrink_to_fit();

        struct Renumber final: public MIRTargetVisitorMut {
            decltype(fixBbIdx)& fixBbIdx;

            explicit Renumber(decltype(fixBbIdx)& fixBbIdx)
                : fixBbIdx(fixBbIdx)
            {
            }

            void visitTarget(MIRBasicBlockId& target) override {
                target = fixBbIdx(target);
            }
        } renumber{fixBbIdx};

        visitTerminatorTargetMut(newBlockList.back().terminator, renumber);
    }
    fcn.blocks = mv$(newBlockList);
}

void MIROptimiseCrate(const WireBoard& wb, HIRCrate& crate, unsigned optLevel, bool enableInlining) {
    auto callback = makeCallable<MIRExprCb>([optLevel, enableInlining](const auto& res, const auto& p, auto& expr, const auto& args, const auto& ty) {
        auto& mir = expr.getMirOrErrorMut(Span());
        if (optLevel == 0) {
            MIROptimiseMin(res, p, mir, args, ty);
        } else {
            MIROptimise(res, p, mir, args, ty, optLevel, enableInlining, /*validate=*/getenv("TRUSTME_MIR_CHECK") != nullptr);
        }
        MIRCleanup(res, p, mir, args, ty);
    });
    MIROuterVisitor ov{wb, crate, callback};
    ov.visitCrate(crate);
}

void MIROptimiseCrateInlining(const WireBoard& wb, const HIRCrate& crate, TransList& list, bool postSave, unsigned optLevel, bool enableInlining) {
    ::StaticTraitResolve resolve{wb};

    if (postSave) {
        for (auto& fcnEnt : list.functions) {
            auto& hirFcn = *const_cast<HIRFunction*>(fcnEnt.second->ptr);
            MIRFunction* fcnP;
            if (fcnEnt.second->monomorphised.code) {
                fcnP = &*fcnEnt.second->monomorphised.code;
            } else if (hirFcn.code.mir) {
                fcnP = &hirFcn.code.getMirOrErrorMut(Span());
            } else {
                continue;
            }

            auto& fcn = *fcnP;
            for (auto& block : fcn.blocks) {
                if (auto* te = block.terminator.opt_Call()) {
                    if (te->fcn.is_Intrinsic() && te->fcn.as_Intrinsic().name == "const_eval_select") {
                        size_t nArgs = te->fcn.as_Intrinsic().params.types.at(0)->as_Tuple().size();
                        const MIRLValue arg = te->args.at(0).as_LValue().clone();
                        const HIRPath& fcnPath = *te->args.at(2).as_Constant().as_Function().p;

                        te->fcn = fcnPath.clone();
                        te->args.clear();
                        te->args.reserve(nArgs);
                        for (size_t i = 0; i < nArgs; i++) {
                            te->args.push_back(MIRLValue::newField(arg.clone(), i));
                        }
                    }
                }
            }
        }
    }

    if (!enableInlining) {
        return;
    }

    const size_t maxIterations = optLevel >= 4 ? std::numeric_limits<size_t>::max() : 5;
    size_t numIterations = 0;
    bool didInlineOnPass;
    do {
        didInlineOnPass = false;

        for (auto& fcnEnt : list.functions) {
            const auto& path = fcnEnt.first;
            auto& hirFcn = *const_cast<HIRFunction*>(fcnEnt.second->ptr);
            auto& monoFcn = fcnEnt.second->monomorphised;

            std::string s = FMT(path);
            HIRItemPath ip(s);

            if (monoFcn.code) {
                didInlineOnPass |= MIROptimiseInline(resolve, ip, *monoFcn.code, monoFcn.argTys, monoFcn.retTy, list, optLevel);

                MIRCleanup(resolve, ip, *monoFcn.code, monoFcn.argTys, monoFcn.retTy);
            } else if (hirFcn.code) {
                auto& mir = hirFcn.code.getMirOrErrorMut(Span());
                bool didOpt = MIROptimiseInline(resolve, ip, mir, hirFcn.args, hirFcn.returnType, list, optLevel);
                mir.transEnumState = MIRFunction::MIREnumCachePtr();
                didInlineOnPass |= didOpt;

                MIRCleanup(resolve, ip, mir, hirFcn.args, hirFcn.returnType);
            } else {
            }
        }
        numIterations += 1;
    } while (didInlineOnPass && numIterations < maxIterations);
}

MirMutator::MirMutator(MIRFunction& fcn, unsigned int bb, unsigned int stmt)
    : fcn(fcn)
    , curBlock(bb)
    , curStmt(stmt)
{
}

auto MirMutator::updateState(MIRTypeResolve& state) -> void {
    if (this->curStmt == fcn.blocks[this->curBlock].statements.size()) {
        state.setCurStmtTerm(this->curBlock);
    } else {
        state.setCurStmt(this->curBlock, this->curStmt);
    }
}

auto MirMutator::newTemporary(HIRTypeRef ty) -> MIRLValue {
    auto rv = MIRLValue::newLocal(static_cast<unsigned int>(fcn.locals.size()));
    fcn.locals.push_back(mv$(ty));
    return rv;
}

auto MirMutator::pushStatement(MIRStatement stmt) -> void {
    newStatements.push_back(mv$(stmt));
}

auto MirMutator::inTemporary(HIRTypeRef ty, MIRRValue val) -> MIRLValue {
    auto rv = this->newTemporary(mv$(ty));
    pushStatement(MIRStatement::make_Assign({rv.clone(), mv$(val)}));
    return rv;
}

auto MirMutator::flushStmt() -> decltype(newStatements.begin()) {
    auto rv = flush();
    this->curStmt += 1;
    return rv;
}

auto MirMutator::flushBlock() -> void {
    flush();
    fcn.blocks.at(curBlock).statements.shrink_to_fit();
    this->curStmt = 0;
    this->curBlock += 1;
}

auto MirMutator::flush() -> decltype(newStatements.begin()) {
    auto& block = fcn.blocks.at(curBlock);
    assert(curStmt <= block.statements.size());
    auto it = block.statements.begin() + curStmt;
    if (newStatements.size() > 0) {
        for (auto& stmt : newStatements) {
            it = block.statements.insert(it, mv$(stmt));
            ++it;
            curStmt += 1;
        }
        newStatements.clear();
    }
    return it;
}

LvalueConstAdapter::LvalueConstAdapter(LvalueVisitor& inner)
    : inner(inner)
{
}

auto LvalueConstAdapter::visitLvalue(MIRLValue& lv, MIRValUsage u) -> bool {
    return inner.visitLvalue(lv, u);
}

ParamsSet::ParamsSet(HIRTypeInterner& types)
    : MonomorphiserPP(types)
    , fcnParams(nullptr)
    , selfTy(nullptr)
    , implParamsDef(nullptr)
    , fcnParamsDef(nullptr)
{
}

auto ParamsSet::getSelfType() const -> const HIRTypeData* {
    return selfTy;
}

auto ParamsSet::getImplParams() const -> const HIRPathParams* {
    return &implParams;
}

auto ParamsSet::getMethodParams() const -> const HIRPathParams* {
    return fcnParams;
}

auto ParamsSet::getHrbParams() const -> const HIRPathParams* {
    return nullptr;
}

auto ParamsSet::hasUnevaluatedValues() const -> bool {
    const auto check = [](const HIRPathParams& params) {
        return std::any_of(params.values.begin(), params.values.end(), [](const auto& value) {
            return value.is_Unevaluated() || value.is_Infer();
        });
    };
    return check(implParams) || (fcnParams && check(*fcnParams));
}

template <typename F>
MIRBlockCb<F>::MIRBlockCb(F f)
    : f(f)
{
}

template <typename F>
auto MIRBlockCb<F>::run(MIRBasicBlockId bb, MIRBasicBlock& block) const -> void {
    f(bb, block);
}

template <typename F>
MIRBlockConstCb<F>::MIRBlockConstCb(F f)
    : f(f)
{
}

template <typename F>
auto MIRBlockConstCb<F>::run(MIRBasicBlockId bb, const MIRBasicBlock& block) const -> void {
    f(bb, block);
}

OptimiseStmtRef::OptimiseStmtRef()
    : bbIdx(~0u)
    , stmtIdx(0)
{
}

OptimiseStmtRef::OptimiseStmtRef(unsigned b, unsigned s)
    : bbIdx(b)
    , stmtIdx(s)
{
}

auto OptimiseStmtRef::operator==(const OptimiseStmtRef& x) const -> bool {
    return bbIdx == x.bbIdx && stmtIdx == x.stmtIdx;
}

template <typename S, typename T>
IterPathCb<S, T>::IterPathCb(S statement, T terminator)
    : statement(statement)
    , terminator(terminator)
{
}

template <typename S, typename T>
auto IterPathCb<S, T>::visitStatement(OptimiseStmtRef location, const MIRStatement& value) -> bool {
    return statement(location, value);
}

template <typename S, typename T>
auto IterPathCb<S, T>::visitTerminator(OptimiseStmtRef location, const MIRTerminator& value) -> bool {
    return terminator(location, value);
}

CheckInvalidatesLvalue::CheckInvalidatesLvalue(const MIRLValue& val, bool isCopy, bool alsoRead)
    : val(val)
    , hasIndex(
          std::any_of(
              val.wrappers.begin(),
              val.wrappers.end(),
              [](const auto& w) {
                  return w.is_Index();
              }
          )
      )
    , isCopy(isCopy)
    , alsoRead(alsoRead)
{
}

auto CheckInvalidatesLvalue::visitLvalue(const MIRLValue& lv, MIRValUsage vu) -> bool {
    switch (vu) {
        case MIRValUsage::Write:
        case MIRValUsage::Borrow:
            if (lv.root == val.root) {
                return true;
            }
            if (hasIndex && lv.root.is_Local()) {
                for (const auto& w : val.wrappers) {
                    if (w.is_Index() && w.as_Index() == lv.root.as_Local()) {
                        return true;
                    }
                }
            }
            break;
        case MIRValUsage::Move:
            if (isCopy) {
            } else if (lv.root == val.root) {
                auto l = std::min(lv.wrappers.size(), val.wrappers.size());
                for (size_t i = 0; i < l; i++) {
                    if (lv.wrappers[i] != val.wrappers[i]) {
                        return false;
                    }
                }
                return true;
            }
            break;
        case MIRValUsage::Read:
            if (alsoRead) {
                if (lv.root == val.root) {
                    return true;
                }
            }
            break;
    }
    return false;
}
