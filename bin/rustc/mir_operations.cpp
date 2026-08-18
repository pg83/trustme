#include "mir_operations.h"
#include "mir_operations.h"

#include "mir_mir.h"
#include "hir_expr.h" // The optimiser section accesses complete HIR expression nodes.
#include "wire_board.h"
#include "hir_visitor.h"
#include "mir_helpers.h"
#include "trans_target.h"
#include "hir_conv_main_bindings.h"
#include "trans_trans_list.h" // Note: This is included for inlining after enumeration and monomorph
#include "hir_typeck_static.h"
#include "mir_main_bindings.h"
#include "mir_visit_crate_mir.h"

#include <cmath>
#include <limits>
#include <iomanip>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace {
    HIRTypeRef getMetadataType(const MIRTypeResolve& state, const HIRTypeData* unsizedTy) {
        static Span sp;
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
                    case HIRStructMarkings::DstType::Possible: {
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
                        throw "";
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
}

//template<typename T>
//::std::ostream& operator<<(::std::ostream& os, const T& v) {
//}

// --------------------------------------------------------------------

namespace {
    /// @brief Used to tell the constant replacement code that replacements should be available
    bool gIsPostMonomorph = false;
}

class MirMutator {
    MIRFunction& fcn;
    unsigned int curBlock;
    unsigned int curStmt;
    mutable ::std::vector<MIRStatement> newStatements;

public:
    MirMutator(MIRFunction& fcn, unsigned int bb, unsigned int stmt)
        : fcn(fcn)
        , curBlock(bb)
        , curStmt(stmt)
    {
    }

    void updateState(MIRTypeResolve& state) {
        if (this->curStmt == fcn.blocks[this->curBlock].statements.size()) {
            state.setCurStmtTerm(this->curBlock);
        } else {
            state.setCurStmt(this->curBlock, this->curStmt);
        }
    }

    MIRLValue newTemporary(HIRTypeRef ty) {
        auto rv = MIRLValue::newLocal(static_cast<unsigned int>(fcn.locals.size()));
        fcn.locals.push_back(mv$(ty));
        return rv;
    }

    void pushStatement(MIRStatement stmt) {
        newStatements.push_back(mv$(stmt));
    }

    MIRLValue inTemporary(HIRTypeRef ty, MIRRValue val) {
        auto rv = this->newTemporary(mv$(ty));
        pushStatement(MIRStatement::make_Assign({rv.clone(), mv$(val)}));
        return rv;
    }

    decltype(newStatements.begin()) flushStmt() {
        auto rv = flush();
        this->curStmt += 1;
        return rv;
    }

    void flushBlock() {
        flush();
        fcn.blocks.at(curBlock).statements.shrink_to_fit();
        this->curStmt = 0;
        this->curBlock += 1;
    }

private:
    decltype(newStatements.begin()) flush() {
        auto& block = fcn.blocks.at(curBlock);
        assert(curStmt <= block.statements.size());
        auto it = block.statements.begin() + curStmt;
        if (newStatements.size() > 0) {
            DEBUG("flush - BB" << curBlock << "/" << curStmt);
            for (auto& stmt : newStatements) {
                DEBUG("- Push stmt @" << curStmt << ": " << stmt);
                it = block.statements.insert(it, mv$(stmt));
                ++it;
                curStmt += 1;
            }
            newStatements.clear();
        }
        return it;
    }
};

void MIRCleanupLValue(const MIRTypeResolve& state, MirMutator& mutator, MIRLValue& lval);

namespace {
    HIRTypeRef getVtableType(const Span& sp, const ::StaticTraitResolve& resolve, const HIRTypeData::Data_TraitObject& te) {
        return te.trait.traitPtr->getVtableType(sp, resolve.hirCrate(), te);
    }
}

const EncodedLiteral* MIRCleanupGetConstant(const MIRTypeResolve& state, const HIRPath& path, HIRTypeRef& outTy, MonomorphState& params) {
    TRACE_FUNCTION_F(path);

    auto v = state.resolve.getValue(state.sp, path, params);
    if (const auto* e = v.opt_Constant()) {
        const auto& hirConst = **e;
        outTy = params.monomorphType(state.sp, hirConst.type);
        state.resolve.expandAssociatedTypes(state.sp, outTy);
        switch (hirConst.valueState) {
            case HIRConstant::ValueState::Known:
                return &hirConst.valueRes;
            case HIRConstant::ValueState::Generic: {
                // Do some form of lookup of a pre-cached evaluated monomorphised constant
                // - Maybe on the `Constant` entry there can be a list of pre-monomorphised values
                auto it = hirConst.monomorphCache.find(path);
                if (it == hirConst.monomorphCache.end()) {
                    // Emit a bug if the cache is empty? (or if this is in the post-monomorph pass)
                    if (gIsPostMonomorph && !monomorphisePathNeeded(path)) {
                        // NOTE: Dead code can trigger this :(
                        // - There's a check in hir/serialise.cpp that makes sure that this doesn't reach the saved MIR
                    }
                    DEBUG("Generic, but no cached monomorphisation: " << hirConst.monomorphCache.size() << " entries");
                    return nullptr;
                }
                return &it->second;
            }
            case HIRConstant::ValueState::Unknown:
                MIR_ASSERT(state, monomorphisePathNeeded(path), "Unevaluated constant - " << path);
                return nullptr;
        }
        throw "";
    } else if (v.is_NotYetKnown()) {
        auto v = state.resolve.getValue(state.sp, path, params, /*signature_only=*/true);
        if (const auto* e = v.opt_Constant()) {
            const auto& hirConst = **e;
            outTy = params.monomorphType(state.sp, hirConst.type);
            DEBUG("NotYetKnown");
        } else {
            MIR_BUG(state, "get_literal_for_const - Not a constant - " << path);
        }
        return nullptr;
    } else {
        MIR_BUG(state, "get_literal_for_const - Not a constant - " << path);
        return nullptr;
    }
}

namespace {
    const RcString rcstringVtable = RcString::newInterned("vtable#");

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

    MIRConstant createVtable(HIRTypeRef ty, const HIRTraitPath& trait) {
        auto vtablePath = HIRPath(mv$(ty), trait.path.clone(), rcstringVtable);
        return MIRConstant::make_ItemAddr(box$(vtablePath));
    }
}

MIRRValue MIRCleanupLiteralToRValue(const MIRTypeResolve& state, MirMutator& mutator, EncodedLiteralSlice lit, HIRTypeRef ty, const MonomorphState& params, HIRPath path) {
    TRACE_FUNCTION_F(ty << " <= " << lit);
    switch ((*ty).tag()) {
default:
        if( path == HIRGenericPath() )
            MIR_TODO(state, "Literal of type " << ty << " - " << lit);
        DEBUG("Unknown type " << ty << ", but a path was provided - Return ItemAddr " << path);
        return MIRConstant::make_ItemAddr(box$(path));
        case HIRTypeData::TAG_Tuple: {
            auto* repr = TargetGetTypeRepr(state.sp, state.resolve, ty);
            MIR_ASSERT(state, repr, "No type repr, but encoded value available? " << ty);

            ::std::vector<MIRParam> lvals;
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

            // If all of the literals are the same value, then optimise into a count-based initialisation
            if (isAllSame) {
                auto rval = MIRCleanupLiteralToRValue(state, mutator, lit.slice(0, size), te.inner, params, HIRGenericPath());
                auto dataLval = mutator.inTemporary(te.inner, mv$(rval));
                return MIRRValue::make_SizedArray({mv$(dataLval), static_cast<unsigned int>(count)});
            } else {
                ::std::vector<MIRParam> lvals;
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
                ::std::vector<MIRParam> lvals;
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
                    // Leave empty
                }
                return MIRRValue::make_EnumVariant({te.path.data.as_Generic().clone(), varIdx, mv$(vals)});
            } else if (te.binding.is_Union()) {
                unsigned varIdx = ~0u;
                const auto* repr = TargetGetTypeRepr(state.sp, state.resolve, ty);
                MIR_ASSERT(state, repr, "");
                // TODO: Find a way of storing backing information that specifies the variant (maybe as a relocation?)

                if (varIdx == ~0u) {
                    for (const auto& e : repr->fields) {
                        // A byte array covering the entire structure - can just emit
                        if (e.ty->is_Array() && e.ty->as_Array().inner == HIRCoreType::U8 && e.ty->as_Array().size.as_Known() == repr->size) {
                            DEBUG("Found an array covering the whole union");
                            varIdx = &e - &repr->fields.front();
                            break;
                        }
                    }
                }
                // MaybeUninit (1.39) - `union MaybeUninit<T> { uninit: (), data: T }`
                // - If the body is all zeroes, then emit `uninit` (as that's the default)
                // - Otherwise, emit the actual value
                if (varIdx == ~0u) {
                    if (repr->fields.size() == 2 && repr->fields[0].ty == state.crate.types.unit()) {
                        // If all zeroes, then emit the tuple field, otherwise the other field
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

                // If there's a POD field (pointer or integer) of size equal to the whole struct, use that
                if (varIdx == ~0u) {
                    for (const auto& e : repr->fields) {
                        if (e.ty->is_Pointer() || e.ty->is_Primitive()) {
                            // If there's a relocation, then we have to use a pointer field
                            if (lit.getReloc() && !e.ty->is_Pointer()) {
                                continue;
                            }

                            size_t fldSize = 0;
                            TargetGetSizeOf(state.sp, state.resolve, e.ty, fldSize);
                            if (fldSize == repr->size) {
                                // Found a suitable field!
                                DEBUG("Found a covering field");
                                varIdx = &e - &repr->fields.front();
                                break;
                            }
                        }
                    }
                }

                // A full-size aggregate made entirely from unrestricted scalar types can
                // represent the storage without knowing which union field initialized it.
                if (varIdx == ~0u) {
                    const auto literalEnd = lit.ofs + lit.size;
                    const bool hasRelocation = std::any_of(lit.base.relocations.begin(), lit.base.relocations.end(), [&](const auto& relocation) {
                        return relocation.ofs < literalEnd && lit.ofs < relocation.ofs + relocation.len;
                    });
                    if (!hasRelocation) {
                        for (const auto& e : repr->fields) {
                            size_t fieldSize = 0;
                            if (TargetGetSizeOf(state.sp, state.resolve, e.ty, fieldSize) && fieldSize == repr->size && typeAcceptsAllBitPatterns(state.sp, state.resolve, e.ty)) {
                                DEBUG("Found an unrestricted covering field");
                                varIdx = &e - &repr->fields.front();
                                break;
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
            throw "";
        }
        case HIRTypeData::TAG_Pattern: {
            auto& te = (*ty).as_Pattern();
            return MIRCleanupLiteralToRValue(state, mutator, lit, te.inner, params, mv$(path));
        }
        case HIRTypeData::TAG_Pointer: {
            auto& te = (*ty).as_Pointer();
            if (lit.getReloc()) {
                // Share logic with `Borrow` below, but wrap returned value in a cast op
                auto tyBorrow = state.crate.types.borrow(te.type, te.inner);
                auto rval = MIRCleanupLiteralToRValue(state, mutator, lit, tyBorrow, params, mv$(path));
                auto lval = mutator.inTemporary(mv$(tyBorrow), mv$(rval));
                return MIRRValue::make_Cast({mv$(lval), mv$(ty)});
            } else {
                const auto ptrSize = TargetGetPointerBits() / 8;
                auto v = lit.readUint(ptrSize);
                // A raw pointer to a slice carries a length that the address
                // alone cannot supply, so it is built rather than cast.
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
            MIR_ASSERT(state, dataReloc ? data_ptr >= EncodedLiteral::PTR_BASE : data_ptr != 0,
                "Bad pointer value - 0x" << std::hex << data_ptr);

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
                    case MetadataType::None:
                        return MIRRValue::make_Borrow({te.type, false, MIRLValue::newDeref(mv$(ptr))});
                    case MetadataType::Slice: {
                        const auto ptrSize = TargetGetPointerBits() / 8;
                        auto size = MIRConstant::make_Uint({lit.slice(ptrSize).readUint(ptrSize), HIRCoreType::Usize});
                        return MIRRValue::make_MakeDst({mv$(ptr), mv$(size)});
                    }
                    case MetadataType::TraitObject:
                    case MetadataType::Unknown:
                    case MetadataType::Zero:
                        MIR_TODO(state, "Integer-address borrow with metadata " << metadataType);
                }
            }

            const auto ofs = data_ptr - EncodedLiteral::PTR_BASE;
            if (dataReloc->p) {
                const auto& path = *dataReloc->p;
                auto ptrVal = MIRConstant::make_ItemAddr({box$(params.monomorphPath(state.sp, path)), ofs});
                DEBUG("ptr_val = " << ptrVal);
                HIRTypeRef tmp;
                const auto& srcTy = state.getStaticType(tmp, path);

                // Get the metadata type (for !Sized wrapper types)
                auto metaTy = state.resolve.metadataType(state.sp, te.inner);
                switch (metaTy) {
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

                        auto vtableVal = MIRParam(createVtable(srcTy, tep->trait));

                        return MIRRValue::make_MakeDst({MIRParam(mv$(ptrVal)), mv$(vtableVal)});
                        break;
                    }
                    case MetadataType::Unknown:
                        MIR_BUG(state, te.inner << " unknown metadata type");
                    case MetadataType::Zero:
                        MIR_TODO(state, "Zero metadata");
                }
            } else {
                // This is a borrow of a "string"
                MIR_ASSERT(state, ofs <= dataReloc->bytes.size(), "Offset out of range");
                auto s = dataReloc->bytes.begin() + ofs.truncateU64();
                auto e = dataReloc->bytes.end();

                if (te.inner->is_Slice() && te.inner->as_Slice().inner == HIRCoreType::U8) {
                    ::std::vector<u8> bytestr;
                    for (auto it = s; it != e; ++it) {
                        bytestr.push_back(static_cast<u8>(*it));
                    }
                    auto size = MIRConstant::make_Uint({U128(bytestr.size()), HIRCoreType::Usize});
                    return MIRRValue::make_MakeDst({MIRConstant(mv$(bytestr)), std::move(size)});
                } else if (te.inner->is_Array() && te.inner->as_Array().inner == HIRCoreType::U8) {
                    // TODO: How does this differ at codegen to the above?
                    ::std::vector<u8> bytestr;
                    for (auto it = s; it != e; ++it) {
                        bytestr.push_back(static_cast<u8>(*it));
                    }
                    return MIRConstant(mv$(bytestr));
                } else if (te.inner == HIRCoreType::Str) {
                    return MIRConstant::make_StaticString(std::string(s, e));
                } else {
                    // Get repr, assert that there's only one field and it's a `[u8]` or `str`
                    // Pointer cast
                    ::std::vector<u8> bytestr;
                    for (auto it = s; it != e; ++it) {
                        bytestr.push_back(static_cast<u8>(*it));
                    }
                    auto size = MIRConstant::make_Uint({U128(bytestr.size()), HIRCoreType::Usize});
                    // Make a `*const [u8]`
                    auto ptr1 = MIRRValue::make_MakeDst({MIRConstant(mv$(bytestr)), ::std::move(size)});
                    auto lval = mutator.inTemporary(state.crate.types.pointer(HIRBorrowType::Shared, state.crate.types.slice(state.crate.types.primitive(HIRCoreType::U8))), mv$(ptr1));
                    // Cast to `*const T`
                    auto rawPtrTy = state.crate.types.pointer(HIRBorrowType::Shared, te.inner);
                    auto lval2 = mutator.inTemporary(rawPtrTy, MIRRValue::make_Cast({mv$(lval), rawPtrTy}));
                    // Reborrow as `&T`
                    return MIRRValue::make_Borrow({HIRBorrowType::Shared, false, MIRLValue::newDeref(mv$(lval2))});
                }
            }
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& te = (*ty).as_NamedFunction();
            // Function items are zero-sized: their identity is carried by
            // the NamedFunction type, not by bytes or a relocation in the
            // evaluated literal.  Reconstruct the ZST function value instead
            // of treating the lifted inline constant itself as addressable.
            return MIRConstant::make_Function({box$(te.path.clone())});
        }
        case HIRTypeData::TAG_Function: {
            const auto* dataReloc = lit.getReloc();
            MIR_ASSERT(state, dataReloc, "Function with no relocation?!");
            MIR_ASSERT(state, dataReloc->p, "");
            return MIRConstant::make_ItemAddr(box$(dataReloc->p->clone()));
        }
    }
    throw "";
}

MIRLValue MIRCleanupVirtualize(const Span& sp, const MIRTypeResolve& state, MirMutator& mutator, MIRLValue& receiverLvp, const HIRPath::Data::Data_UfcsKnown& pe) {
    TRACE_FUNCTION_F("<" << pe.type << " as " << pe.trait << ">::" << pe.item << pe.params);

    assert(pe.type->is_TraitObject());
    const HIRTypeData::Data_TraitObject& te = pe.type->as_TraitObject();
    assert(te.trait.traitPtr);
    const auto& trait = *te.trait.traitPtr;

    // 1. Get the vtable index for this function
    unsigned int vtableIdx = trait.getVtableValueIndex(pe.trait.path, pe.item);
    if (vtableIdx == 0) {
        BUG(sp, "Calling method '" << pe.item << "' from " << pe.trait << " through " << te.trait.path << " which isn't in the vtable");
    }

    // 2. Load from the vtable
    auto vtableTy = state.crate.types.pointer(HIRBorrowType::Shared, getVtableType(sp, state.resolve, te));
    DEBUG("vtable_ty = " << vtableTy);

    // If the method is a by-value method, add a `&move`
    const auto& fnDef = state.crate.getTraitByPath(sp, pe.trait.path).values.at(pe.item).as_Function();
    if (fnDef.receiver == HIRFunction::Receiver::Value) {
        receiverLvp = mutator.inTemporary(state.crate.types.borrow(HIRBorrowType::Owned, pe.type), MIRRValue::make_Borrow({HIRBorrowType::Owned, false, mv$(receiverLvp)}));
    }

    // Allocate a temporary for the vtable pointer itself
    auto vtableLv = mutator.newTemporary(mv$(vtableTy));
    auto fcnLval = MIRLValue::newField(MIRLValue::newDeref(vtableLv.clone()), vtableIdx);
    HIRTypeRef tmp;
    const auto& ty = state.getLvalueType(tmp, fcnLval);
    DEBUG("callable type " << ty);
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
                ::std::vector<MIRParam> vals;
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
                return mutator.inTemporary( mv$(ty), MIRRValue::make_Struct({ mv$(newPath), mv$(vals) }) );
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
        // If the receiver is Box (or anything that implements CoerceUnsized), create a Foo<()> as the value.
        // - Requires de/restructuring the Box same as CoerceUnsized
        // - Can use the `coerce_unsized_index` field too
        receiverLvp = H::getUnitPtr(state, mutator, ::std::move(receiver), receiverLvp.clone(), innerDynPtr);
    } else if (receiver->is_Borrow() || receiver->is_Pointer()) {
        innerDynPtr = receiverLvp.clone();
        auto ptrRval = MIRRValue::make_DstPtr({receiverLvp.clone()});

        auto ptrLv = mutator.newTemporary(state.crate.types.pointer(HIRBorrowType::Shared, state.crate.types.unit()));
        mutator.pushStatement(MIRStatement::make_Assign({ptrLv.clone(), mv$(ptrRval)}));
        receiverLvp = mv$(ptrLv);
    } else {
        // TODO: How to handle `Pin`?
        // - Locate the pointer (similar to unsized path?)
        MIR_TODO(state, "Handle virtual call through " << receiver);
    }

    // - Load the vtable and store it
    auto vtableRval = MIRRValue::make_DstMeta({mv$(innerDynPtr)});
    mutator.pushStatement(MIRStatement::make_Assign({vtableLv.clone(), mv$(vtableRval)}));

    // Update the terminator with the new information.
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
            // Source must be Path and Unsize
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

                // Return GetMetadata on the inner type
            switch (str.data.tag()) {
                case HIRStructData::TAG_Unit: {
                    MIR_BUG(state, "Unit-like struct Unsize is impossible - " << srcTy);
                    break;
                }
                case HIRStructData::TAG_Tuple: {
                    auto& se = str.data.as_Tuple();
                    const auto& tyTpl = se.at(str.structMarkings.unsizedField).ent;
                    auto tyD = monomorphCbD.monomorphType(state.sp, tyTpl, false);
                    auto tyS = monomorphCbS.monomorphType(state.sp, tyTpl, false);

                    return MIRCleanupUnsizeGetMetadata(state, mutator, tyD, tyS, ptrValue, outMetaVal, outMetaTy, outSrcIsDst);
                }
                case HIRStructData::TAG_Named: {
                    auto& se = str.data.as_Named();
                    const auto& tyTpl = se.at(str.structMarkings.unsizedField).ty;
                    auto tyD = monomorphCbD.monomorphType(state.sp, tyTpl, false);
                    auto tyS = monomorphCbS.monomorphType(state.sp, tyTpl, false);

                    return MIRCleanupUnsizeGetMetadata(state, mutator, tyD, tyS, ptrValue, outMetaVal, outMetaTy, outSrcIsDst);
                }
            }
            throw "";
        }
        case HIRTypeData::TAG_Slice: {
            // Source must be an array (or generic)
            if (srcTy->is_Array()) {
                const auto& inArray = srcTy->as_Array();
                if (!inArray.size.is_Known()) {
                    DEBUG("Array size not yet known - " << inArray.size);
                    return false;
                }
                outMetaTy = state.crate.types.primitive(HIRCoreType::Usize);
                outMetaVal = MIRConstant::make_Uint({U128(inArray.size.as_Known()), HIRCoreType::Usize});
                return true;
            } else if (srcTy->is_Generic() || (srcTy->is_Path() && srcTy->as_Path().binding.is_Opaque())) {
                // Defer until monomorphisation supplies the concrete source array.
                return false;
            } else {
                MIR_BUG(state, "Unsize to slice from non-array - " << srcTy);
            }
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& de = (*dstTy).as_TraitObject();
            // Obtain vtable type `::"path"::to::Trait#vtable`
            auto vtableTy = de.trait.path != HIRSimplePath() ? de.trait.traitPtr->getVtableType(state.sp, state.crate, de) : state.crate.types.unit();
            outMetaTy = state.crate.types.pointer(HIRBorrowType::Shared, vtableTy);

            // If the data trait hasn't changed, return the vtable pointer
            if (const auto* se = srcTy->opt_TraitObject()) {
                outSrcIsDst = true;
                if (de.trait.path == HIRSimplePath()) {
                    // A trait object with only auto traits still carries the
                    // source vtable metadata for drop, size, and alignment.
                    // Its vtable type is represented as `()`, so retype the
                    // existing metadata instead of looking for a principal
                    // trait entry in the source vtable.
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
                outMetaVal = createVtable(srcTy, de.trait);
            }
            return true;
        }
    }
    throw "";
}

MIRRValue MIRCleanupUnsize(const MIRTypeResolve& state, MirMutator& mutator, const HIRTypeData* dstTy, const HIRTypeData* srcTyInner, MIRLValue ptrValue) {
    const auto& dstTyInner = (dstTy->is_Borrow() ? dstTy->as_Borrow().inner : dstTy->as_Pointer().inner);

    HIRTypeRef metaType;
    MIRParam metaValue;
    bool sourceIsDst = false;
    if (MIRCleanupUnsizeGetMetadata(state, mutator, dstTyInner, srcTyInner, ptrValue, metaValue, metaType, sourceIsDst)) {
        // There is a case where the source is already a fat pointer. In that case the pointer of the new DST must be the source DST pointer
        if (sourceIsDst) {
            auto tyUnitPtr = state.crate.types.pointer(HIRBorrowType::Shared, state.crate.types.unit());
            auto thinPtrLval = mutator.inTemporary(mv$(tyUnitPtr), MIRRValue::make_DstPtr({mv$(ptrValue)}));

            return MIRRValue::make_MakeDst({mv$(thinPtrLval), mv$(metaValue)});
        } else {
            return MIRRValue::make_MakeDst({mv$(ptrValue), mv$(metaValue)});
        }
    } else {
        // Re-emit the "unsize" pseudo-op
        return MIRRValue::make_MakeDst({mv$(ptrValue), MIRConstant::make_ItemAddr({})});
    }
}

MIRRValue MIRCleanupCoerceUnsized(const MIRTypeResolve& state, MirMutator& mutator, const HIRTypeData* dstTy, const HIRTypeData* srcTy, MIRLValue value) {
    TRACE_FUNCTION_F(dstTy << " <- " << srcTy << " ( " << value << " )");
    //  > Path -> Path = Unsize
    // (path being destination is otherwise invalid)
    if (dstTy->is_Path()) {
        MIR_ASSERT(state, srcTy->is_Path(), "CoerceUnsized to Path must have a Path source - " << srcTy << " to " << dstTy);
        const auto& dte = dstTy->as_Path();
        const auto& ste = srcTy->as_Path();

        // - Types must differ only by a single field, and be from the same definition
        MIR_ASSERT(state, dte.binding.is_Struct(), "Note, can't CoerceUnsized non-structs");
        MIR_ASSERT(state, dte.binding.tag() == ste.binding.tag(), "Note, can't CoerceUnsized mismatched structs - " << srcTy << " to " << dstTy);
        MIR_ASSERT(state, dte.binding.as_Struct() == ste.binding.as_Struct(), "Note, can't CoerceUnsized mismatched structs - " << srcTy << " to " << dstTy);
        const auto& str = *dte.binding.as_Struct();
        MIR_ASSERT(state, str.structMarkings.coerceUnsizedIndex != ~0u, "Struct " << srcTy << " doesn't impl CoerceUnsized");

        auto monomorphCbD = MonomorphStatePtr(state.crate.types, dstTy, &dte.path.data.as_Generic().params, nullptr);
        auto monomorphCbS = MonomorphStatePtr(state.crate.types, srcTy, &ste.path.data.as_Generic().params, nullptr);

        // - Destructure and restrucure with the unsized fields
        ::std::vector<MIRParam> ents;
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
        return MIRRValue::make_Struct({ dte.path.data.as_Generic().clone(), mv$(ents) });
    }

    if (dstTy->is_Borrow()) {
        MIR_ASSERT(state, srcTy->is_Borrow(), "CoerceUnsized to Borrow must have a Borrow source - " << srcTy << " to " << dstTy);
        const auto& dte = dstTy->as_Borrow();
        const auto& ste = srcTy->as_Borrow();

        // Only the mutability differs (`Pin<&mut T>` to `Pin<&T>`, say): that is
        // a reborrow, and the pointer is unchanged.
        if (dte.inner == ste.inner && dte.type != ste.type) {
            return MIRRValue::make_Borrow({dte.type, false, MIRLValue::newDeref(mv$(value))});
        }

        return MIRCleanupUnsize(state, mutator, dstTy, ste.inner, mv$(value));
    }

    // Pointer Coercion - Downcast and unsize
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
    throw "";
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

    for(size_t i = 0; i < lval.wrappers.size(); i ++)
    {
        if (!lval.wrappers[i].is_Deref()) {
            continue;
        }

        // If this is a deref of Box, unpack and deref the inner pointer
        HIRTypeRef tmp;
        const auto& ty = state.getLvalueType(tmp, lval, lval.wrappers.size() - i);
        if (state.resolve.isTypeOwnedBox(ty)) {
            unsigned numInjectedFldZeros = 0;

            // Handle Box by extracting it to its pointer.
            // - Locate (or remember) which field in Box is the pointer, and replace the inner by that field
            // > Dumb idea, assume it's always the first field. Keep accessing until located.

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

                numInjectedFldZeros ++;
            }
            MIR_ASSERT(state, typ->is_Pointer(), "First non-path field in Box wasn't a pointer - " << typ);
            // We have reached the pointer. Good.

            // Inject all of the field zero accesses (before the deref)
            while (numInjectedFldZeros--) {
                lval.wrappers.insert(lval.wrappers.begin() + i, MIRLValue::Wrapper::newField(0));
            }
        } else {
            // What about other types?
        }
    }
}

void MIRCleanupConstant(const MIRTypeResolve& state, MirMutator& mutator, MIRConstant& p) {
    if (auto* e = p.opt_Uint()) {
        switch (e->t) {
            // Constants use U128 storage; truncate usize values to the target pointer width.
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

    // Effectively a copy of the code that handles RValue::Constant below
    if( p.is_Constant() && p.as_Constant().is_Const() )
    {
        const auto& ce = p.as_Constant().as_Const();
        HIRTypeRef cTy;
        MonomorphState params(state.crate.types);
        const auto* litPtr = MIRCleanupGetConstant(state, *ce.p, cTy, params);
        if (litPtr) {
            DEBUG("Replace constant " << *ce.p << " with " << *litPtr);
            auto newRval = MIRCleanupLiteralToRValue(state, mutator, *litPtr, cTy, params, mv$(*ce.p));
            if (auto* lv = newRval.opt_Use()) {
                p = MIRParam::make_LValue(::std::move(*lv));
            } else if (auto* c = newRval.opt_Constant()) {
                MIRCleanupConstant(state, mutator, *c);
                p = MIRParam::make_Constant(::std::move(*c));
            } else {
                auto tmpLv = mutator.inTemporary(mv$(cTy), mv$(newRval));
                p = MIRParam::make_LValue(::std::move(tmpLv));
            }
        } else {
            DEBUG("No replacement for constant " << *ce.p);
        }
    }
}

void MIRCleanup(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType) {
    Span sp;
    TRACE_FUNCTION_F(path);
    MIRTypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), retType, args, fcn
    };

    MirMutator mutator{fcn, 0, 0};
    for (auto& block : fcn.blocks) {
        for (auto it = block.statements.begin(); it != block.statements.end(); ++it) {
            mutator.updateState(state);
            auto& stmt = *it;

            // >> Detect use of `!` as a value
            HIRTypeRef tmp;
            if ((stmt.is_Assign() && (stmt.as_Assign().src.is_Borrow())) && state.getLvalueType(tmp, stmt.as_Assign().src.as_Borrow().val)->is_Diverge()) {
                DEBUG(state << "Not killing block due to use of `!`, it's being borrowed");
            } else {
                if (visitMirLvalues(stmt, [&](const auto& lv, auto /*vu*/) {
                    return state.getLvalueType(tmp, lv)->is_Diverge();
                })) {
                    DEBUG(state << "Truncate entire block due to use of `!` as a value - " << stmt);
                    block.statements.erase(it, block.statements.end());
                    block.terminator = MIRTerminator::make_Unreachable({});
                    break;
                }
            }
            // >> Elaborate Box dereferences in all LValues
            DEBUG(state << stmt);
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

                            // A DstMeta on an array is its length. Generic MIR can
                            // retain the operation until monomorphisation makes the
                            // array size concrete; the second cleanup pass then
                            // replaces it with the integer constant.
                            if (const auto* array = ty->opt_Array()) {
                                MIRCleanupLValue(state, mutator, re.val);
                                if (array->size.is_Known()) {
                                    se.src = MIRConstant::make_Uint({U128(array->size.as_Known()), HIRCoreType::Usize});
                                } else if (const auto* value = array->size.as_Unevaluated().opt_Evaluated()) {
                                    se.src = MIRConstant::make_Uint({U128((*value)->readUsize(0)), HIRCoreType::Usize});
                                }
                                break;
                            }

                            // Unsized function arguments are already indirect places carrying
                            // their metadata. Unlike Box and pointer values, there is no hidden
                            // dereference to expose to the Box elaboration pass.
                            if (!state.resolve.typeIsSized(state.sp, ty)) {
                                MIRCleanupLValue(state, mutator, re.val);
                                break;
                            }

                            // DstMeta consumes the pointer represented by a Box, so expose
                            // its dereference to the Box elaboration pass before splitting it.
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
                            }
                            // NOTE: This can happen with calling a by-value method on a trait object, e.g. `<dyn Foo as FnOnce>::call_once`
                            // - That is handled with magic in trans, so needs magic here (for inlining)
                            else if (cleanedTy->is_TraitObject()) {
                                ityP = cleanedTy;
                                // Remove the deref so downstream doesn't need to care
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

                            // DstPtr consumes the pointer represented by a Box, so expose
                            // its dereference to the Box elaboration pass before splitting it.
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
                            }
                            // NOTE: This can happen with calling a by-value method on a trait object, e.g. `<dyn Foo as FnOnce>::call_once`
                            // - That is handled with magic in trans, so needs magic here (for inlining)
                            else if (cleanedTy->is_TraitObject()) {
                                ityP = cleanedTy;
                                // Remove the deref so downstream doesn't need to care
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

            // 2. RValue conversions
            if( stmt.is_Assign() )
            {
                auto& se = stmt.as_Assign();

                if (auto* e = se.src.opt_Constant()) {
                    // Replace `Const` with actual values
                    if (auto* ce = e->opt_Const()) {
                        // 1. Find the constant
                        MonomorphState params(state.crate.types);
                        HIRTypeRef ty;
                        const auto* litPtr = MIRCleanupGetConstant(state, *ce->p, ty, params);
                        if (litPtr) {
                            DEBUG("Replace constant " << *ce->p << " with " << *litPtr);
                            se.src = MIRCleanupLiteralToRValue(state, mutator, *litPtr, mv$(ty), params, mv$(*ce->p));
                            if (auto* p = se.src.opt_Constant()) {
                                MIRCleanupConstant(state, mutator, *p);
                            }
                        } else {
                            DEBUG("No replacement for constant " << *ce->p);
                        }
                    }
                }

                // Fix up coercions
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
                        // - Ensure that something is generic in either the destination or source
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
                    }
                }
                break;
            }
        }

        // VTable calls
        if(auto* ep = block.terminator.opt_Call())
        {
            auto& e = *ep;
            if (auto* pathP = e.fcn.opt_Path()) {
                auto& path = *pathP;
                // Detect calling `<Trait as Trait>::method()` and replace with vtable call
                if (path.data.is_UfcsKnown() && path.data.as_UfcsKnown().type->is_TraitObject()) {
                    const auto& pe = path.data.as_UfcsKnown();
                    const auto& te = pe.type->as_TraitObject();
                    // TODO: What if the method is from a supertrait?

                    if (te.trait.path == pe.trait || resolve.findNamedTraitInTrait(sp, pe.trait.path, pe.trait.params, *te.trait.traitPtr, te.trait.path.path, te.trait.path.params, pe.type, [](const auto&, auto) {
                        return true;
                    })) {
                        auto tgtLvalue = MIRCleanupVirtualize(sp, state, mutator, e.args.front().as_LValue(), pe);
                        e.fcn = mv$(tgtLvalue);
                    }
                }

                else if (path.data.is_UfcsKnown() && path.data.as_UfcsKnown().type->is_Function()) {
                    const auto& pe = path.data.as_UfcsKnown();
                    const auto& fcnTy = pe.type->as_Function();
                    if (pe.trait.path == resolve.langFn() || pe.trait.path == resolve.langFnMut() || pe.trait.path == resolve.langFnOnce()) {
                        MIR_ASSERT(state, e.args.size() == 2, "Fn* call requires two arguments");
                        auto fcnLvalue = mv$(e.args[0].as_LValue());
                        auto argsLvalue = mv$(e.args[1].as_LValue());

                        DEBUG("Convert function pointer call");

                        e.args.clear();
                        e.args.reserve(fcnTy.argTypes.size());
                        for (unsigned int i = 0; i < fcnTy.argTypes.size(); i++) {
                            e.args.push_back(MIRLValue::newField(argsLvalue.clone(), i));
                        }
                        // If the trait is Fn/FnMut, dereference the input value.
                        if (pe.trait.path == resolve.langFnOnce()) {
                            e.fcn = mv$(fcnLvalue);
                        } else {
                            e.fcn = MIRLValue::newDeref(mv$(fcnLvalue));
                        }
                    }
                }
                else if (path.data.is_UfcsKnown() && path.data.as_UfcsKnown().type->is_NamedFunction()) {
                    const auto& pe = path.data.as_UfcsKnown();
                    const auto& fcnTy = pe.type->as_NamedFunction();
                    if (pe.trait.path == resolve.langFn() || pe.trait.path == resolve.langFnMut() || pe.trait.path == resolve.langFnOnce()) {
                        auto nArgs = fcnTy.decay(state.crate.types, state.sp).argTypes.size();
                        MIR_ASSERT(state, e.args.size() == 2, "Fn* call requires two arguments");
                        auto fcnLvalue = mv$(e.args[0].as_LValue());
                        auto argsLvalue = mv$(e.args[1].as_LValue());

                        DEBUG("Convert named function pointer call");

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

            // NOTE: Would be nice to do this in `Lower_MIR` - but that confuses the validity checks
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
                    if (traitObject.trait.path == pe.trait
                        || resolve.findNamedTraitInTrait(sp, pe.trait.path, pe.trait.params, *traitObject.trait.traitPtr, traitObject.trait.path.path, traitObject.trait.path.params, pe.type, [](const auto&, auto) {
                            return true;
                        })) {
                        e.fcn = MIRCleanupVirtualize(sp, state, mutator, e.args.front().as_LValue(), pe);
                    }
                }

                else if (path.data.is_UfcsKnown() && path.data.as_UfcsKnown().type->is_Function()) {
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
                        e.fcn = pe.trait.path == resolve.langFnOnce()
                            ? mv$(fcnLvalue)
                            : MIRLValue::newDeref(mv$(fcnLvalue));
                    }
                }

                else if (path.data.is_UfcsKnown() && path.data.as_UfcsKnown().type->is_NamedFunction()) {
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
    MIROuterVisitor ov{wb, crate, [&](const auto& res, const auto& p, HIRExprPtr& exprPtr, const auto& args, const auto& ty) {
        if (exprPtr) {
            MIRCleanup(res, p, exprPtr.getMirOrErrorMut(Span()), args, ty);
        }
    }};
    ov.visitCrate(crate);
}

void MIRCleanupSetPostMonomorph() {
    gIsPostMonomorph = true;
}

#define DUMP_BEFORE_ALL 1
#define DUMP_BEFORE_CONSTPROPAGATE 0
#define DUMP_AFTER_PASS 1
#define DUMP_AFTER_ALL 0

#define DUMP_AFTER_DONE 1
#define CHECK_AFTER_DONE 2 // 1 = Check before GC, 2 = check before and after GC

// ----
// List of optimisations avaliable
// ----
bool MIROptimiseBlockSimplify(MIRTypeResolve& state, MIRFunction& fcn);
bool MIROptimiseInlining(MIRTypeResolve& state, MIRFunction& fcn, bool minimal, const TransList* list = nullptr);
bool MIROptimiseSplitAggregates(MIRTypeResolve& state, MIRFunction& fcn);
bool MIROptimisePropagateSingleAssignments(MIRTypeResolve& state, MIRFunction& fcn);
bool MIROptimisePropagateKnownValues(MIRTypeResolve& state, MIRFunction& fcn);
bool MIROptimiseDeTemporary(MIRTypeResolve& state, MIRFunction& fcn); // Eliminate useless temporaries
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

/// A minimum set of optimisations:
/// - Runs only the mandatory-inlining hook, not normal cost-based inlining
/// - Simplifies the call graph (by removing chained gotos)
/// - Sorts blocks into a rough flow order
void MIROptimiseMin(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType) {
    static Span sp;
    TRACE_FUNCTION_F(path);
    MIRTypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), retType, args, fcn
    };

    while (MIROptimiseInlining(state, fcn, true)) {
        MIRCleanup(resolve, path, fcn, args, retType);
    }

    MIROptimiseBlockSimplify(state, fcn);
    MIROptimiseUnifyBlocks(state, fcn);

    // NOTE: No check here, this version of optimise is pretty reliable
    //}
    MIROptimiseGarbageCollect(state, fcn);
    MIRSortBlocks(resolve, path, fcn);

#if CHECK_AFTER_DONE > 1
#endif
    return;
}

/// Perfom inlining only, using a list of monomorphised functions, then cleans up the flow graph
///
/// Returns true if any optimisation was performed
bool MIROptimiseInline(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType, const TransList& list, unsigned optLevel) {
    static Span sp;
    bool rv = false;
    TRACE_FUNCTION_FR(path, rv);
    MIRTypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), retType, args, fcn
    };

    while (MIROptimiseInlining(state, fcn, false, &list)) {
        MIRCleanup(resolve, path, fcn, args, retType);
        rv = true;
    }

    if (rv) {
        MIROptimise(resolve, path, fcn, args, retType, optLevel, /*do_inline=*/false);
    }

    return rv;
}

void MIROptimise(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn, const HIRFunction::argsT& args, const HIRTypeData* retType, unsigned optLevel, bool doInline /*=true*/, bool validate /*=true*/) {
    static Span sp;
    assert(optLevel > 0);
    TRACE_FUNCTION_F(path);
    MIRTypeResolve state {
        sp, resolve, FMT_CB(ss, ss << path;), retType, args, fcn
    };

    bool changeHappened;
    unsigned int passNum = 0;
    do {
        MIR_ASSERT(state, passNum < 100, "Too many MIR optimisation iterations");

        changeHappened = false;
        TRACE_FUNCTION_FR("Pass " << passNum, changeHappened);

        // >> Simplify call graph (removes gotos to blocks with a single use)
        if (MIROptimiseBlockSimplify(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            // NOTE: Don't set `change_happened`, as this is the first pass
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // >> Apply known constants
        if (MIROptimiseConstPropagate(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            changeHappened = true;
        }

        // >> Attempt to remove useless temporaries
        if (MIROptimiseDeTemporary(state, fcn)) {
            // - Run until no changes
            while (MIROptimiseDeTemporary(state, fcn)) {
            }
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            changeHappened = true;
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // Level 2 adds the more expensive whole-local/dataflow transformations,
        // matching rustc's split between basic level-1 cleanup and its SROA/GVN/DSE tier.
        // >> Split apart aggregates that are never used such (Written once, never used directly)
        if (optLevel >= 2 && MIROptimiseSplitAggregates(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            changeHappened = true;
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // >> Replace values from composites if they're known
        //   - Undoes the inefficiencies from the `match (a, b) { ... }` pattern
        if (optLevel >= 2 && MIROptimisePropagateKnownValues(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            changeHappened = true;
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // TODO: Convert `&mut *mut_foo` into `mut_foo` if the source is movable and not used afterwards

        // >> Propagate/remove dead assignments
        if (MIROptimisePropagateSingleAssignments(state, fcn)) {
            // - Run until no changes
            while (MIROptimisePropagateSingleAssignments(state, fcn)) {
            }
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            changeHappened = true;
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        // >> Move common statements (assignments) across gotos.
        //if( MIR_Optimise_CommonStatements(state, fcn) )
        //{
        //    }
        //}

        // >> Combine Duplicate Blocks
        if (MIROptimiseUnifyBlocks(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            changeHappened = true;
        }
        // >> Remove assignments of unsed drop flags
        if (MIROptimiseDeadDropFlags(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            changeHappened = true;
        }
        // >> Remove assignments that are never read
        if (optLevel >= 2 && MIROptimiseDeadAssignments(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            changeHappened = true;
        }
        // >> Remove no-op assignments
        if (MIROptimiseNoopRemoval(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            changeHappened = true;
        }

        // >> Remove re-borrow operations that don't need to exist
        if (MIROptimiseUselessReborrows(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            changeHappened = true;
        }

        // >> If the first statement of a block is an assignment, and the last op of the previous is to that assignment's source, move up.
        if (MIROptimiseGotoAssign(state, fcn)) {
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
            changeHappened = true;
        }

        // >> Inline short functions
        if (doInline && !changeHappened) {
            if (MIROptimiseInlining(state, fcn, /*minimal=*/false)) {
                // Apply cleanup again (as monomorpisation in inlining may have exposed a vtable call)
                MIRCleanup(resolve, path, fcn, args, retType);
#if DUMP_AFTER_ALL
                if (debugEnabled()) {
                    MIRDumpFcn(::std::cout, fcn);
                }
#endif
                changeHappened = true;
            }
        }

        if (changeHappened) {
#if DUMP_AFTER_PASS
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        if (MIROptimiseGarbageCollectPartial(state, fcn)) {
            changeHappened = true;
#if DUMP_AFTER_ALL
            if (debugEnabled()) {
                MIRDumpFcn(::std::cout, fcn);
            }
#endif
        }
        //else { MIR_Validate(resolve, path, fcn, args, ret_type); }

        passNum += 1;
    } while (changeHappened);

    // Run UnifyTemporaries last, then unify blocks, then run some
    // optimisations that might be affected

#if DUMP_AFTER_DONE
    if (debugEnabled()) {
        MIRDumpFcn(::std::cout, fcn);
    }
#endif
    // GC pass on blocks and variables
    // - Find unused blocks, then delete and rewrite all references.
    MIROptimiseGarbageCollect(state, fcn);

    MIRSortBlocks(resolve, path, fcn);
}

namespace {
    bool optVisitMirLvaluesInner(const MIRLValue& lv, MIRValUsage u, ::std::function<bool(const MIRLValue&, MIRValUsage)> cb) {
        for (const auto& w : lv.wrappers) {
            if (w.is_Index()) {
                if (cb(MIRLValue::newLocal(w.as_Index()), MIRValUsage::Read)) {
                    return true;
                }
            } else if (w.is_Deref()) {
            }
        }
        return cb(lv, u);
    }

    bool visitMirLvalueMut(MIRLValue& lv, MIRValUsage u, ::std::function<bool(MIRLValue::MRef&, MIRValUsage)> cb) {
        auto lvr = MIRLValue::MRef(lv);
        do {
            if (cb(lvr, u)) {
                return true;
            }
            // TODO: Use a TU_MATCH?
            if (lvr.is_Index()) {
                auto ilv = MIRLValue::newLocal(lvr.as_Index());
                auto ilvR = MIRLValue::MRef(ilv);
                bool rv = cb(ilvR, MIRValUsage::Read);
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
                // No change
            }
        } while (lvr.tryUnwrap());
        return false;
    }

    bool visitMirLvalueRawMut(MIRLValue& lv, MIRValUsage u, ::std::function<bool(MIRLValue&, MIRValUsage)> cb) {
        return cb(lv, u);
    }

    bool visitMirLvalueMut(MIRParam& p, MIRValUsage u, ::std::function<bool(MIRLValue&, MIRValUsage)> cb) {
        if (auto* e = p.opt_LValue()) {
            return visitMirLvalueRawMut(*e, u, cb);
        } else {
            return false;
        }
    }

    bool optVisitMirLvaluesMut(MIRRValue& rval, ::std::function<bool(MIRLValue&, MIRValUsage)> cb) {
        bool rv = false;
        switch (rval.tag()) {
            case MIRRValue::TAG_Use: {
                auto& se = rval.as_Use();
                rv |= visitMirLvalueRawMut(se, MIRValUsage::Move, cb); // Can move
                break;
            }
            case MIRRValue::TAG_Constant: {
                break;
            }
            case MIRRValue::TAG_SizedArray: {
                auto& se = rval.as_SizedArray();
                rv |= visitMirLvalueMut(se.val, MIRValUsage::Read, cb); // Has to be Read
                break;
            }
            case MIRRValue::TAG_Borrow: {
                auto& se = rval.as_Borrow();
                rv |= visitMirLvalueRawMut(se.val, MIRValUsage::Borrow, cb);
                break;
            }
            case MIRRValue::TAG_Cast: {
                auto& se = rval.as_Cast();
                rv |= visitMirLvalueRawMut(se.val, MIRValUsage::Read, cb); // Also has to be read
                break;
            }
            case MIRRValue::TAG_BinOp: {
                auto& se = rval.as_BinOp();
                rv |= visitMirLvalueMut(se.valL, MIRValUsage::Read, cb); // Same
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
                rv |= visitMirLvalueRawMut(se.val, MIRValUsage::Read, cb); // Reads
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
                rv |= visitMirLvalueMut(se.metaVal, MIRValUsage::Read, cb); // Note, metadata has to be Copy
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

    bool optVisitMirLvalues(const MIRRValue& rval, ::std::function<bool(const MIRLValue&, MIRValUsage)> cb) {
        return optVisitMirLvaluesMut(const_cast<MIRRValue&>(rval), [&](auto& lv, auto u) {
            return cb(lv, u);
        });
    }

    bool optVisitMirLvaluesMut(MIRStatement& stmt, ::std::function<bool(MIRLValue&, MIRValUsage)> cb) {
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

    bool optVisitMirLvalues(const MIRStatement& stmt, ::std::function<bool(const MIRLValue&, MIRValUsage)> cb) {
        return optVisitMirLvaluesMut(const_cast<MIRStatement&>(stmt), [&](auto& lv, auto im) {
            return cb(lv, im);
        });
    }

    bool optVisitMirLvaluesMut(MIRTerminator& term, ::std::function<bool(MIRLValue&, MIRValUsage)> cb) {
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

    bool optVisitMirLvalues(const MIRTerminator& term, ::std::function<bool(const MIRLValue&, MIRValUsage)> cb) {
        return optVisitMirLvaluesMut(const_cast<MIRTerminator&>(term), [&](auto& lv, auto im) {
            return cb(lv, im);
        });
    }

    void optVisitMirLvaluesMut(MIRTypeResolve& state, MIRFunction& fcn, ::std::function<bool(MIRLValue&, MIRValUsage)> cb) {
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

    void optVisitMirLvalues(MIRTypeResolve& state, const MIRFunction& fcn, ::std::function<bool(const MIRLValue&, MIRValUsage)> cb) {
        optVisitMirLvaluesMut(state, const_cast<MIRFunction&>(fcn), [&](auto& lv, auto im) {
            return cb(lv, im);
        });
    }

    struct ParamsSet: public MonomorphiserPP {
        HIRPathParams implParams;
        const HIRPathParams* fcnParams;
        const HIRTypeData* selfTy;
        const HIRGenericParams* implParamsDef;
        const HIRGenericParams* fcnParamsDef;

        HIRPathParams fcnParamsTmp;

        explicit ParamsSet(HIRTypeInterner& types)
            : MonomorphiserPP(types)
            , fcnParams(nullptr)
            , selfTy(nullptr)
            , implParamsDef(nullptr)
            , fcnParamsDef(nullptr)
        {
        }

        const HIRTypeData* getSelfType() const override {
            return selfTy;
        }

        const HIRPathParams* getImplParams() const override {
            return &implParams;
        }

        const HIRPathParams* getMethodParams() const override {
            return fcnParams;
        }

        const HIRPathParams* getHrbParams() const override {
            return nullptr;
        }

        bool hasUnevaluatedValues() const {
            const auto check = [](const HIRPathParams& params) {
                return ::std::any_of(params.values.begin(), params.values.end(), [](const auto& value) {
                    return value.is_Unevaluated() || value.is_Infer();
                });
            };
            return check(implParams) || (fcnParams && check(*fcnParams));
        }
    };

    const MIRFunction* getCalledMir(const MIRTypeResolve& state, const TransList* list, const HIRPath& path, ParamsSet& params) {
        MonomorphState outParams(state.resolve.hirCrate().types);
        auto e = state.resolve.getValue(state.sp, path, outParams, /*sig_only*/ false, &params.implParamsDef);
        DEBUG(e.tagStr() << " " << outParams);
        params.fcnParams = outParams.getMethodParams();
        params.implParams = outParams.ppImpl == nullptr ? HIRPathParams() : outParams.ppImpl == &outParams.ppImplData ? std::move(outParams.ppImplData) : outParams.ppImpl->clone();

        // A #[rustc_intrinsic] with a body has that body only as its runtime
        // fallback.  Inlining it would erase the call that CTFE replaces with
        // intrinsic semantics (notably const_allocate/const_make_global).
        if (e.is_Function() && e.as_Function()->markings.isRustcIntrinsic) {
            DEBUG("Not inlining #[rustc_intrinsic] " << path);
            return nullptr;
        }

        // If a TransList is avaliable, then all referenced functions must be in it.
        if (list) {
            const auto* transFcn = list->findFunction(path);
            if (!transFcn) {
                MIR_BUG(state, "Enumeration failure - Function " << path << " not in TransList");
            }
            // TODO: Need identity params for most, but lifetime params need to be from the input.
            // Except, everything should already be monomorphised, so no identity required!
            DEBUG("Found TransList " << path);
            DEBUG("impl_params = " << params.implParams);
            DEBUG("fcn_params = " << *params.fcnParams);

            const auto& hirFcn = *transFcn->ptr;
            if (transFcn->monomorphised.code) {
                return &*transFcn->monomorphised.code;
            } else if (const auto* mir = hirFcn.code.getMirOpt()) {
                MIR_ASSERT(state, hirFcn.params.types.empty(), "Enumeration failure - Function had params, but wasn't monomorphised - " << path);
                // TODO: Check for trait methods too?
                return mir;
            } else {
                DEBUG("No MIR");
                MIR_ASSERT(state, !hirFcn.code, "LowerMIR failure - No MIR but HIR is present?! - " << path);
                // External function (no MIR present)
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

    void visitBlocksMut(MIRTypeResolve& state, MIRFunction& fcn, ::std::function<void(MIRBasicBlockId, MIRBasicBlock&)> cb) {
        ::std::vector<bool> visited(fcn.blocks.size());
        ::std::vector<MIRBasicBlockId> toVisit;
        toVisit.push_back(0);
        while (toVisit.size() > 0) {
            auto bb = toVisit.back();
            toVisit.pop_back();
            if (visited[bb]) {
                continue;
            }
            visited[bb] = true;
            auto& block = fcn.blocks[bb];

            cb(bb, block);

            struct QueueUnvisited final: public MIRTargetVisitor {
                const ::std::vector<bool>& visited;
                ::std::vector<MIRBasicBlockId>& toVisit;

                QueueUnvisited(const ::std::vector<bool>& visited, ::std::vector<MIRBasicBlockId>& toVisit)
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

    void visitBlocks(MIRTypeResolve& state, const MIRFunction& fcn, ::std::function<void(MIRBasicBlockId, const MIRBasicBlock&)> cb) {
        visitBlocksMut(state, const_cast<MIRFunction&>(fcn), [cb](auto id, auto& blk) {
            cb(id, blk);
        });
    }

    /// Convert a MIR::Param into a MIR::RValue
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
} // namespace ""

// --------------------------------------------------------------------
// Performs basic simplications on the call graph (merging/removing blocks)
// --------------------------------------------------------------------
bool MIROptimiseBlockSimplify(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

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

    // >> Replace targets that point to a block that is just a goto
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
                        DEBUG("BB" << &block - fcn.blocks.data() << "/TERM: Rewrite bb reference " << target << " => " << newBb);
                        target = newBb;
                        changed = true;
                    }
                }
            }
        } rewriteGotoChains{state, fcn, block, changed};
        visitTerminatorTargetMut(block.terminator, rewriteGotoChains);

        // Handle chained switches of the same value
        // - Happens in libcore's atomics
        if (auto* te = block.terminator.opt_Switch()) {
            if (te->validFlag != ~0u) {
                continue;
            }
            for (auto& t : te->targets) {
                auto idx = &t - &te->targets.front();
                // The block must be a terminator only, and be a switch over the same value.
                if (fcn.blocks[t].statements.empty() && fcn.blocks[t].terminator.is_Switch()) {
                    const auto& nTe = fcn.blocks[t].terminator.as_Switch();
                    if (nTe.validFlag == ~0u && nTe.val == te->val) {
                        // If that's the case, then update this target with the equivalent from the new switch.
                        DEBUG("BB" << &block - fcn.blocks.data() << "/TERM: Update switch from BB" << t << " to BB" << nTe.targets[idx]);
                        t = nTe.targets[idx];
                        changed = true;
                    }
                }
            }
        }
    }

    // >> Unify sequential `ScopeEnd` statements
    for (auto& block : fcn.blocks) {
        if (block.statements.size() > 1) {
            for (auto it = block.statements.begin() + 1; it != block.statements.end();) {
                if ((it - 1)->is_ScopeEnd() && it->is_ScopeEnd()) {
                    auto& dst = (it - 1)->as_ScopeEnd();
                    const auto& src = it->as_ScopeEnd();
                    DEBUG("Unify " << *(it - 1) << " and " << *it);
                    for (auto v : src.slots) {
                        dst.slots.push_back(v);
                    }
                    ::std::sort(dst.slots.begin(), dst.slots.end());
                    it = block.statements.erase(it);
                    changed = true;
                } else {
                    ++it;
                }
            }
        }
    }

    // >> Merge blocks where a block goto-s to a single-use block.
    {
        ::std::vector<bool> visited(fcn.blocks.size());
        ::std::vector<unsigned int> uses(fcn.blocks.size());
        ::std::vector<MIRBasicBlockId> toVisit;
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
                const ::std::vector<bool>& visited;
                ::std::vector<MIRBasicBlockId>& toVisit;
                ::std::vector<unsigned>& uses;

                CountUses(const ::std::vector<bool>& visited, ::std::vector<MIRBasicBlockId>& toVisit, ::std::vector<unsigned>& uses)
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
                    DEBUG("Append bb " << tgt << " to bb" << i);

                    assert(&fcn.blocks[tgt] != &block);
                    // Move contents of source block, then revive the dead terminator as Incomplete
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

    // >> If a block GOTOs a block that is just a `RETURN` or `DIVERGE`, then change terminator
    for (auto& block : fcn.blocks) {
        state.setCurStmtTerm(&block - &fcn.blocks.front());
        if (block.terminator.is_Goto()) {
            auto tgt = block.terminator.as_Goto();
            if (!fcn.blocks[tgt].statements.empty()) {
            } else if (fcn.blocks[tgt].terminator.is_Return()) {
                DEBUG(state << " -> Return");
                block.terminator = MIRTerminator::make_Return({});
                changed = true;
            } else if (fcn.blocks[tgt].terminator.is_UnwindResume()) {
                DEBUG(state << " -> UnwindResume");
                block.terminator = MIRTerminator::make_UnwindResume({});
                changed = true;
            } else if (fcn.blocks[tgt].terminator.is_UnwindTerminate()) {
                DEBUG(state << " -> UnwindTerminate");
                block.terminator = MIRTerminator::make_UnwindTerminate({});
                changed = true;
            } else if (fcn.blocks[tgt].terminator.is_Unreachable()) {
                DEBUG(state << " -> Unreachable");
                block.terminator = MIRTerminator::make_Unreachable({});
                changed = true;
            } else {
                // No replace
            }
        }
    }

    // NOTE: Not strictly true, but these can't trigger other optimisations
    return false;
}

// --------------------------------------------------------------------
// If two temporaries don't overlap in lifetime (blocks in which they're valid), unify the two
// --------------------------------------------------------------------
bool MIROptimiseInlining(MIRTypeResolve& state, MIRFunction& fcn, bool minimal, const TransList* list /*=nullptr*/) {
    bool inlineHappened = false;
    TRACE_FUNCTION_FR("", inlineHappened);

    struct InlineEvent {
        HIRPath path;
        ::std::vector<size_t> bbList;

        InlineEvent(HIRPath p)
            : path(::std::move(p))
        {
        }

        bool hasBb(size_t i) const {
            return ::std::find(this->bbList.begin(), this->bbList.end(), i) != this->bbList.end();
        }

        void addRange(size_t start, size_t count) {
            for (size_t j = 0; j < count; j++) {
                this->bbList.push_back(start + j);
            }
        }
    };

    ::std::vector<InlineEvent> inlinedFunctions;

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

        /// Checks if the passed lvalue would optimise/expand to a constant value
        static bool valueIsConst(const MIRFunction& fcn, unsigned bbIdx, unsigned stmtIdx, const MIRLValue& val, const std::vector<MIRParam>& params) {
            if (val.root.is_Argument()) {
                auto a = val.root.as_Argument();
                return params[a].is_Constant() && !params[a].as_Constant().is_Const();
            }

            // Find the source of this lvalue, chase it backwards
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
                // Detect and avoid simple recursion.
                // - This won't detect mutual recursion - that also needs prevention.
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
                // Detect and avoid simple recursion.
                // - This won't detect mutual recursion - that also needs prevention.
                // TODO: This is the pre-monomorph path, but we're comparing with the post-monomorph path
                if (blk0Te.fcn.is_Path() && blk0Te.fcn.as_Path() == path) {
                    return false;
                }
                return true;
            } else {
            }

            // TODO: If all inputs are known, then allow larger/complex functions (e.g. allow one call and any number of blocks?)
            // - Seen `min_by(const, const, fcn)` - that would be a trivial optimisation

            if (canInlineSwitchWrapper(path, fcn, params)) {
                return true;
            }
            if (canInlineSwitchValueWrapper(path, fcn, params)) {
                return true;
            }
            return false;
        }

        /// Case: A Switch that has all distinct arms that just call a function AND the value is over (effectively) a literal
        static bool canInlineSwitchWrapper(const HIRPath& path, const MIRFunction& fcn, const std::vector<MIRParam>& params) {
            if (fcn.blocks.size() <= 1) {
                return false;
            }
            if (!fcn.blocks[0].terminator.is_Switch()) {
                return false;
            }
            const auto& teSwitch = fcn.blocks[0].terminator.as_Switch();
            // Setup + Arms + Return + Panic
            // - Handles the atomic wrappers
            if (fcn.blocks.size() != teSwitch.targets.size() + 3) {
                return false;
            }
            // Check for the switch value being an argument that is also a constant parameter being a Constant
            if (!valueIsConst(fcn, 0, fcn.blocks[0].statements.size(), teSwitch.val, params)) {
                return false;
            }
            // Check all arms of the switch are distinct
            for (const auto& tgt : teSwitch.targets) {
                if (std::find(teSwitch.targets.begin() + (1 + &tgt - teSwitch.targets.data()), teSwitch.targets.end(), tgt) != teSwitch.targets.end()) {
                    return false;
                }
            }
            // Check for recursion
            for (size_t i = 1; i < fcn.blocks.size(); i++) {
                if (fcn.blocks[i].terminator.is_Call()) {
                    const auto& te = fcn.blocks[i].terminator.as_Call();
                    // Recursion, don't inline.
                    if (te.fcn.is_Path() && te.fcn.as_Path() == path) {
                        return false;
                    }
                    // Only intrinsic wrapper calls are proven safe before ordinary call paths are monomorphised.
                    if (!te.fcn.is_Intrinsic()) {
                        return false;
                    }
                }
            }
            return true;
        }

        /// Case: A SwitchValue that has all distinct arms that just call a function AND the value is over (effectively) a literal
        static bool canInlineSwitchValueWrapper(const HIRPath& path, const MIRFunction& fcn, const std::vector<MIRParam>& params) {
            if (fcn.blocks.size() <= 1) {
                return false;
            }
            if (!fcn.blocks[0].terminator.is_SwitchValue()) {
                return false;
            }
            const auto& teSwitch = fcn.blocks[0].terminator.as_SwitchValue();
            // Setup + Arms(+default) + Return + Panic
            // - Handles some code in crc32-fast that emits a 256-arm SwitchValue
            if (fcn.blocks.size() != teSwitch.targets.size() + 1 + 3) {
                return false;
            }
            // Check for the switch value being an argument that is also a constant parameter being a Constant
            if (!valueIsConst(fcn, 0, fcn.blocks[0].statements.size(), teSwitch.val, params)) {
                return false;
            }

            // Check all arms of the switch are distinct
            if (std::find(teSwitch.targets.begin(), teSwitch.targets.end(), teSwitch.defTarget) != teSwitch.targets.end()) {
                return false;
            }
            for (const auto& tgt : teSwitch.targets) {
                if (std::find(teSwitch.targets.begin() + (1 + &tgt - teSwitch.targets.data()), teSwitch.targets.end(), tgt) != teSwitch.targets.end()) {
                    return false;
                }
            }

            // Check for recursion
            for (size_t i = 1; i < fcn.blocks.size(); i++) {
                if (fcn.blocks[i].terminator.is_Call()) {
                    const auto& te = fcn.blocks[i].terminator.as_Call();
                    // Recursion, don't inline.
                    if (te.fcn.is_Path() && te.fcn.as_Path() == path) {
                        return false;
                    }
                    // Only intrinsic wrapper calls are proven safe before ordinary call paths are monomorphised.
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
        ::std::vector<unsigned> copyArgs; // Local indexes containing copies of Copy args
        ParamsSet params;
        unsigned int bbBase = ~0u;
        unsigned int varBase = ~0u;
        unsigned int dfBase = ~0u;

        size_t tmpEnd = 0;
        mutable ::std::vector<MIRParam> constAssignments;

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

        virtual unsigned mapLocal(unsigned f) const {
            return this->varBase + f;
        }

        virtual unsigned mapDropFlag(unsigned f) const {
            return this->dfBase + f;
        }

        const HIRTypeData* valueGenericType(HIRGenericRef ce) const override {
            const HIRGenericParams* p;
            switch (ce.group()) {
                case 0: // impl level
                    p = params.implParamsDef;
                    break;
                case 1: // method level
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
                DEBUG("BB" << srcIdx << "->BB" << newIdx << "/" << rv.statements.size() << ": " << stmt);
                rv.statements.push_back(this->cloneStmt(stmt));
                DEBUG("-> " << rv.statements.back());
            }
            DEBUG("BB" << srcIdx << "->BB" << newIdx << "/" << rv.statements.size() << ": " << src.terminator);
            if (src.terminator.is_Return()) {
                rv.statements.push_back(MIRStatement::make_Assign({this->te.retVal.clone(), this->retval.clone()}));
                DEBUG("++ " << rv.statements.back());
            }
            rv.terminator = this->cloneTerm(src.terminator);
            DEBUG("-> " << rv.terminator);
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
                throw "";
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
                    assert(!arg.is_Constant()); // Should have been handled in the above
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
            DEBUG(state << fcn.blocks[i].terminator);

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
                DEBUG("Can't inline - recursion");
                continue;
            }
            if (cloner.params.hasUnevaluatedValues()) {
                DEBUG("Can't inline - const substitutions are not concrete");
                continue;
            }

            // Check the size of the target function.
            // Inline IF:
            // - First BB ends with a call and total count is 3
            // - Statement count smaller than 10
            if (!H::canInline(path, *calledMir, te->args, minimal)) {
                DEBUG("Can't inline " << path);
                continue;
            }
            TRACE_FUNCTION_F("Inline " << path);

            // Allocate a temporary for the return value
            {
                cloner.retval = MIRLValue::newLocal(fcn.locals.size());
                DEBUG("- Storing return value in " << cloner.retval);
                HIRTypeRef tmpTy;
                fcn.locals.push_back(state.getLvalueType(tmpTy, te->retVal));
            }

            // Monomorph locals and append
            cloner.varBase = fcn.locals.size();
            for (const auto& ty : calledMir->locals) {
                fcn.locals.push_back(cloner.monomorph(ty));
            }
            cloner.tmpEnd = fcn.locals.size();

            cloner.dfBase = fcn.dropFlags.size();
            fcn.dropFlags.insert(fcn.dropFlags.end(), calledMir->dropFlags.begin(), calledMir->dropFlags.end());
            cloner.bbBase = fcn.blocks.size();

            // Store all Copy lvalue arguments and Constants in variables
            for (size_t i = 0; i < te->args.size(); i++) {
                const auto& a = te->args[i];
                if (!a.is_LValue() || state.lvalueIsCopy(a.as_LValue())) {
                    cloner.copyArgs[i] = cloner.tmpEnd + cloner.constAssignments.size();
                    cloner.constAssignments.push_back(a.clone());
                    DEBUG("- Taking a copy of arg " << i << " (" << a << ") in Local(" << cloner.copyArgs[i] << ")");
                }
            }

            // Append monomorphised copy of all blocks.
            // > Arguments replaced by input lvalues
            ::std::vector<MIRBasicBlock> newBlocks;
            newBlocks.reserve(calledMir->blocks.size());
            for (const auto& bb : calledMir->blocks) {
                newBlocks.push_back(cloner.cloneBb(bb, (&bb - calledMir->blocks.data()), fcn.blocks.size() + newBlocks.size()));
            }

            // > Append new temporaries
            DEBUG("- Insert argument lval assignments");
            for (auto& val : cloner.constAssignments) {
                HIRTypeRef tmp;
                auto ty = val.is_Constant() ? state.getConstType(val.as_Constant()) : state.getLvalueType(tmp, val.as_LValue());
                auto lv = MIRLValue::newLocal(static_cast<unsigned>(fcn.locals.size()));
                fcn.locals.push_back(mv$(ty));
                auto rval = val.is_Constant() ? MIRRValue(mv$(val.as_Constant())) : MIRRValue(mv$(val.as_LValue()));
                auto stmt = MIRStatement::make_Assign({mv$(lv), mv$(rval)});
                DEBUG("++ " << stmt);
                newBlocks[0].statements.insert(newBlocks[0].statements.begin(), mv$(stmt));
            }
            cloner.constAssignments.clear();

            // Record the inline event
            for (auto& e : inlinedFunctions) {
                if (e.hasBb(i)) {
                    e.addRange(cloner.bbBase, newBlocks.size());
                }
            }
            inlinedFunctions.push_back(InlineEvent(path.clone()));
            inlinedFunctions.back().addRange(cloner.bbBase, newBlocks.size());

            // Apply
            DEBUG("- Append new blocks");
            fcn.blocks.reserve(fcn.blocks.size() + newBlocks.size());
            for (auto& b : newBlocks) {
                fcn.blocks.push_back(mv$(b));
            }
            fcn.blocks[i].terminator = MIRTerminator::make_Goto(cloner.bbBase);
            inlineHappened = true;

            // TODO: Store the inlined path along with the start and end BBs, and then use that to detect recursive
            // inlining
            // - Recursive inlining should be an immediate panic.
        }
    }
    return inlineHappened;
}

namespace {
    struct OptimiseStmtRef {
        unsigned bbIdx;
        unsigned stmtIdx;

        OptimiseStmtRef()
            : bbIdx(~0u)
            , stmtIdx(0)
        {
        }

        OptimiseStmtRef(unsigned b, unsigned s)
            : bbIdx(b)
            , stmtIdx(s)
        {
        }

        bool operator==(const OptimiseStmtRef& x) const {
            return bbIdx == x.bbIdx && stmtIdx == x.stmtIdx;
        }
    };

    ::std::ostream& operator<<(::std::ostream& os, const OptimiseStmtRef& x) {
        return os << "BB" << x.bbIdx << "/" << x.stmtIdx;
    }

    // Iterates the path between two positions, NOT visiting entry specified by `end`
    enum class IterPathRes {
        Abort,
        EarlyTrue,
        Complete,
    };

    IterPathRes iterPath(const MIRFunction& fcn, const OptimiseStmtRef& start, const OptimiseStmtRef& end, ::std::function<bool(OptimiseStmtRef, const MIRStatement&)> cbStmt, ::std::function<bool(OptimiseStmtRef, const MIRTerminator&)> cbTerm) {
        if (start.bbIdx == end.bbIdx) {
            assert(start.stmtIdx <= end.stmtIdx);
        }

        auto vistedBbs = ::std::set<unsigned>();
        // Loop while not equal (either not in the right block, or before the statement) to the end point
        for (auto ref = start; ref.bbIdx != end.bbIdx || ref.stmtIdx < end.stmtIdx;) {
            const auto& bb = fcn.blocks.at(ref.bbIdx);
            if (ref.stmtIdx < bb.statements.size()) {
                DEBUG(ref << " " << bb.statements.at(ref.stmtIdx));
                if (cbStmt(ref, bb.statements.at(ref.stmtIdx))) {
                    DEBUG("> Early true");
                    return IterPathRes::EarlyTrue;
                }

                ref.stmtIdx++;
            } else {
                DEBUG(ref << " " << bb.terminator);
                if (cbTerm(ref, bb.terminator)) {
                    DEBUG("> Early true");
                    return IterPathRes::EarlyTrue;
                }

                // If this is the end point, break out before checking the terminator for looping
                if (ref.bbIdx == end.bbIdx) {
                    // ^ don't need to check the statment index, this is the last "statement"
                    break;
                }

                // If this terminator is a Goto, follow it (tracking for loops)
                if (const auto* te = bb.terminator.opt_Goto()) {
                    // Possibly loop into the next block
                    if (!vistedBbs.insert(*te).second) {
                        DEBUG("> Loop abort");
                        return IterPathRes::Abort;
                    }
                    ref.stmtIdx = 0;
                    ref.bbIdx = *te;
                }
                // A call's panic edge cannot reach `end`, so only follow the
                // normal return edge while inspecting a path between two
                // positions.
                else if (const auto* te = bb.terminator.opt_Call()) {
                    // Possibly loop into the next block
                    if (!vistedBbs.insert(te->retBlock).second) {
                        DEBUG("> Loop abort");
                        return IterPathRes::Abort;
                    }
                    ref.stmtIdx = 0;
                    ref.bbIdx = te->retBlock;
                } else {
                    DEBUG("> Terminator abort");
                    return IterPathRes::Abort;
                }
            }
        }
        return IterPathRes::Complete;
    }

    ::std::function<bool(const MIRLValue&, MIRValUsage)> checkInvalidatesLvalueCb(const MIRLValue& val, bool isCopy, bool alsoRead = false) {
        bool hasIndex = ::std::any_of(val.wrappers.begin(), val.wrappers.end(), [](const auto& w) {
            return w.is_Index();
        });
        // Value is invalidated if it's used with MIRValUsage::Write or MIRValUsage::Borrow
        // - Same applies to any component of the lvalue
        return [&val, hasIndex, isCopy, alsoRead](const MIRLValue& lv, MIRValUsage vu) {
            switch (vu) {
                    // - Ideally this would check if it DOES invalidate
                case MIRValUsage::Write:
                case MIRValUsage::Borrow:
                    // (Possibly) mutating use, check if it impacts the root or one of the indexes
                    if (lv.root == val.root) {
                        return true;
                    }
                    // If the desired lvalue has an index in it's wrappers, AND the current lvalue is a local
                    if (hasIndex && lv.root.is_Local()) {
                        // Search for any wrapper on `val` that Index(lv)
                        for (const auto& w : val.wrappers) {
                            if (w.is_Index() && w.as_Index() == lv.root.as_Local()) {
                                // This lvalue is changed, so the index is invalidated
                                return true;
                            }
                        }
                    }
                    break;
                case MIRValUsage::Move: // A move can invalidate
                    if (isCopy) {
                    } else if (lv.root == val.root) {
                        // Check if `lv`'s wrappers are a subset of `val`'s
                        auto l = std::min(lv.wrappers.size(), val.wrappers.size());
                        for (size_t i = 0; i < l; i++) {
                            // A wrapper differs, won't invalidate
                            if (lv.wrappers[i] != val.wrappers[i]) {
                                return false;
                            }
                        }
                        return true;
                    }
                    break;
                case MIRValUsage::Read:
                    if (alsoRead) {
                        // NOTE: A read of the same root is a read of this value (what if they're disjoint fields?)
                        if (lv.root == val.root) {
                            return true;
                        }
                    }
                    break;
            }
            return false;
        };
    }

    bool checkInvalidatesLvalue(const MIRStatement& stmt, const MIRLValue& val, bool isCopy, bool alsoRead = false) {
        return optVisitMirLvalues(stmt, checkInvalidatesLvalueCb(val, isCopy, alsoRead));
    }

    bool checkInvalidatesLvalue(const MIRTerminator& term, const MIRLValue& val, bool isCopy, bool alsoRead = false) {
        return optVisitMirLvalues(term, checkInvalidatesLvalueCb(val, isCopy, alsoRead));
    }
}

// --------------------------------------------------------------------
// Locates locals that are only set/used once, and replaces them with
//  their source IF the source isn't invalidated
// --------------------------------------------------------------------
bool MIROptimiseDeTemporarySingleSetAndUse(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // Find all single-use/single-write locals
    // - IF the usage is a RValue::Use, AND the usage destination is not invalidated between set/use
    //  - Replace initialisation destination with usage destination (delete usage statement)
    // - IF the source a Use/Constant, AND is not invalidated between set/use
    //  - Replace usage with the original source
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

    auto usageInfo = ::std::vector<LocalUsage>(fcn.locals.size());

    // 1. Enumrate usage
    {
        auto getCurLoc = [&state]() {
            return OptimiseStmtRef(state.getCurBlock(), state.getCurStmtOfs());
        };
        auto visitCb = [&](const MIRLValue& lv, auto vu) {
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
        };
        optVisitMirLvalues(state, fcn, visitCb);
    }

    // 2. Find any local with 1 write, 1 read, and no borrows
    for (size_t varIdx = 0; varIdx < fcn.locals.size(); varIdx++) {
        const auto& slot = usageInfo[varIdx];
        auto thisVar = MIRLValue::newLocal(varIdx);
        DEBUG("_" << varIdx << ": " << slot.nWrite << "," << slot.nRead << "," << slot.nBorrow);
        if (slot.nWrite == 1 && slot.nRead == 1 && slot.nBorrow == 0) {
            // Single-use variable, now check how we can eliminate it
            DEBUG("Single-use: _" << varIdx << " - Set " << slot.setLoc << ", Use " << slot.useLoc);

            auto& useBb = fcn.blocks[slot.useLoc.bbIdx];
            auto& setBb = fcn.blocks[slot.setLoc.bbIdx];

            auto setLocNext = slot.setLoc;
            if (slot.setLoc.stmtIdx < setBb.statements.size()) {
                setLocNext.stmtIdx += 1;
            } else {
                setLocNext.bbIdx = setBb.terminator.as_Call().retBlock;
                setLocNext.stmtIdx = 0;
            }

            // If usage is direct assignment of the original value.
            // - In this case, we can move the usage upwards
            if (slot.useLoc.stmtIdx < useBb.statements.size() && (useBb.statements[slot.useLoc.stmtIdx].is_Assign() && useBb.statements[slot.useLoc.stmtIdx].as_Assign().src.is_Use() && useBb.statements[slot.useLoc.stmtIdx].as_Assign().src.as_Use() == thisVar)) {
                // Move the usage up to original assignment (if destination isn't invalidated)
                const auto& dst = useBb.statements[slot.useLoc.stmtIdx].as_Assign().dst;

                // TODO: If the destination slot was ever borrowed mutably, don't move.
                // - Maybe, if there's a drop skip? (as the drop could be &mut to the target value)

                // - Iterate the path(s) between the two statements to check if the destination would be invalidated
                //  > The iterate function doesn't (yet) support following BB chains, so assume invalidated if over a jump.
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
                    // destination not dependent on any statements between the two, move.
                    if (slot.setLoc.stmtIdx < setBb.statements.size()) {
                        auto& setStmt = setBb.statements[slot.setLoc.stmtIdx];
                        switch (setStmt.tag()) {
                            case MIRStatement::TAG_Assign: {
                                auto& se = setStmt.as_Assign();
                                MIR_ASSERT(state, se.dst == MIRLValue::newLocal(varIdx), "Impossibility: Value set but isn't destination in " << setStmt);
                                DEBUG("Move destination " << dst << " from " << useBb.statements[slot.useLoc.stmtIdx] << " to " << setStmt);
                                se.dst = dst.clone();
                                useBb.statements[slot.useLoc.stmtIdx] = MIRStatement();
                                changed = true;
                                break;
                            }
                            case MIRStatement::TAG_Asm: {
                                // Initialised from an ASM statement, find the variable in the output parameters
                                break;
                            }
                            case MIRStatement::TAG_Asm2: {
                                auto& se = setStmt.as_Asm2();
                                // Initialised from an ASM statement, find the variable in the output parameters
                                // TODO: Replace the output variable
                                for (auto& e : se.params) {
                                    if (const auto* ep = e.opt_Reg()) {
                                        if (ep->output) {
                                            if (*ep->output == MIRLValue::newLocal(varIdx)) {
                                                DEBUG("Move destination " << dst << " from " << useBb.statements[slot.useLoc.stmtIdx] << " to " << setStmt);
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
                            }
break;
                            default:
                                MIR_BUG(state, "Impossibility: Value set in " << setStmt);
                        }
                    } else {
                        auto& setTerm = setBb.terminator;
                        MIR_ASSERT(state, setTerm.is_Call(), "Impossibility: Value set using non-call");
                        auto& te = setTerm.as_Call();
                        DEBUG("Move destination " << dst << " from " << useBb.statements[slot.useLoc.stmtIdx] << " to " << setTerm);
                        te.retVal = dst.clone();
                        useBb.statements[slot.useLoc.stmtIdx] = MIRStatement();
                        changed = true;
                    }
                } else {
                    DEBUG("Destination invalidated");
                }
                continue;
            }

            // Can't move up, can we move down?
            // - If the source is an Assign(Use) then we can move down
            if (slot.setLoc.stmtIdx < setBb.statements.size() && (setBb.statements[slot.setLoc.stmtIdx].is_Assign() && (setBb.statements[slot.setLoc.stmtIdx].as_Assign().src.is_Use()))) {
                auto& setStmt = setBb.statements[slot.setLoc.stmtIdx];
                const auto& src = setStmt.as_Assign().src.as_Use();
                bool srcCopy = src.wrappers.empty() && state.lvalueIsCopy(src);

                // Check if the source of initial assignment is invalidated in the meantime.
                auto useLocInc = slot.useLoc;
                useLocInc.stmtIdx += 1;
                bool invalidated = IterPathRes::Complete != iterPath(
                                                                fcn,
                                                                setLocNext,
                                                                useLocInc,
                                                                // NOTE: If a mutable borrow happens, assume it invalidates the source
                                                                [&](auto loc, const auto& stmt) -> bool {
                    return checkInvalidatesLvalue(stmt, src, srcCopy) || (stmt.is_Assign() && stmt.as_Assign().src.is_Borrow() && stmt.as_Assign().src.as_Borrow().type != HIRBorrowType::Shared);
                },
                                                                [&](auto loc, const auto& term) -> bool {
                    return checkInvalidatesLvalue(term, src, srcCopy);
                }
                                                            );
                DEBUG("invalidated = " << invalidated);
                // If this is a deref, and there are move ops between definition and use - then invalidate
                if (!invalidated && std::any_of(src.wrappers.begin(), src.wrappers.end(), [](const MIRLValue::Wrapper& w) {
                    return w.is_Deref();
                })) {
                    // If there are any move ops between the set and the usage, invalidate
                    bool stop = false;
                    auto checkCb = [&](const MIRLValue& lv, MIRValUsage vu) {
                        if (lv == thisVar) {
                            stop = true;
                            return false;
                        }
                        if (stop) {
                            // Once the value is seen, ignore anything else
                            return false;
                        }
                        // If a move is seen, check if it's a move (and not a copy)
                        if (vu == MIRValUsage::Move) {
                            return !state.lvalueIsCopy(lv);
                        }
                        return false;
                    };
                    invalidated = IterPathRes::Complete != iterPath(fcn, setLocNext, useLocInc, [&](auto loc, const auto& stmt) -> bool {
                        return optVisitMirLvalues(stmt, checkCb);
                    }, [&](auto loc, const auto& term) -> bool {
                        return (term.is_Call() && !optVisitMirLvalues(term, [&](const MIRLValue& lv, MIRValUsage vu) {
                            return lv == thisVar;
                        })) || optVisitMirLvalues(term, checkCb);
                    });
                    DEBUG("invalidated = " << invalidated);
                }
                if (!invalidated) {
                    // Update the usage site and replace.
                    auto replaceCb = [&](MIRLValue& slot, MIRValUsage vu) -> bool {
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
                    };
                    if (slot.useLoc.stmtIdx < useBb.statements.size()) {
                        auto& useStmt = useBb.statements[slot.useLoc.stmtIdx];
                        DEBUG("Replace " << thisVar << " with " << src << " in BB" << slot.useLoc.bbIdx << "/" << slot.useLoc.stmtIdx << " " << useStmt);
                        bool found = optVisitMirLvaluesMut(useStmt, replaceCb);
                        if (!found) {
                            DEBUG("Can't find use of " << thisVar << " in " << useStmt);
                        } else {
                            setStmt = MIRStatement();
                            changed = true;
                        }
                    } else {
                        auto& useTerm = useBb.terminator;
                        DEBUG("Replace " << thisVar << " with " << src << " in " << useTerm);
                        bool found = optVisitMirLvaluesMut(useTerm, replaceCb);
                        if (!found) {
                            DEBUG("Can't find use of " << thisVar << " in " << useTerm);
                        } else {
                            setStmt = MIRStatement();
                            changed = true;
                        }
                    }
                } else {
                    DEBUG("Source invalidated");
                }
                continue;
            }

            // TODO: If the source is a Borrow and the use is a Deref, then propagate forwards
            // - This would be a simpler version of a var more compliciated algorithm

            DEBUG("Can't replace:");
            if (slot.setLoc.stmtIdx < setBb.statements.size()) {
                DEBUG("Set: " << setBb.statements[slot.setLoc.stmtIdx]);
            } else {
                DEBUG("Set: " << setBb.terminator);
            }
            if (slot.useLoc.stmtIdx < useBb.statements.size()) {
                DEBUG("Use: " << useBb.statements[slot.useLoc.stmtIdx]);
            } else {
                DEBUG("Use: " << useBb.terminator);
            }
        }
    }

    return changed;
}

// Remove useless borrows (locals assigned with a borrow, and never used by value)
// ```
// (*_$1).1 = 0x0;
// ```
bool MIROptimiseDeTemporaryBorrows(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // Find all single-assign borrows that are only ever used via Deref
    // - Direct drop is ignored for this purpose
    struct LocalUsage {
        unsigned nWrite;
        unsigned nOtherRead;
        unsigned nDerefRead;
        OptimiseStmtRef setLoc;
        ::std::vector<OptimiseStmtRef> dropLocs;

        LocalUsage()
            : nWrite(0)
            , nOtherRead(0)
            , nDerefRead(0)
        {
        }
    };

    auto usageInfo = ::std::vector<LocalUsage>(fcn.locals.size());
    for (const auto& bb : fcn.blocks) {
        OptimiseStmtRef curLoc;
        auto visitCb = [&](const MIRLValue& lv, auto vu) {
            if (lv.root.is_Local()) {
                auto& slot = usageInfo[lv.root.as_Local()];
                // NOTE: This pass doesn't care about indexing, as we're looking for values that are borrows (which aren't valid indexes)
                // > Inner-most wrapper is Deref - it's a deref of this variable
                if (!lv.wrappers.empty() && lv.wrappers.front().is_Deref()) {
                    slot.nDerefRead++;
                    if (fcn.locals[lv.root.as_Local()]->is_Borrow()) {
                        DEBUG(lv << " deref use " << curLoc);
                    }
                }
                // > Write with no wrappers - Assignment
                else if (lv.wrappers.empty() && vu == MIRValUsage::Write) {
                    slot.nWrite++;
                    slot.setLoc = curLoc;
                }
                // Anything else, count as a read
                else {
                    slot.nOtherRead++;
                }
            }
            return false;
        };
        for (const auto& stmt : bb.statements) {
            curLoc = OptimiseStmtRef(&bb - &fcn.blocks.front(), &stmt - &bb.statements.front());

            optVisitMirLvalues(stmt, visitCb);
        }
        curLoc = OptimiseStmtRef(&bb - &fcn.blocks.front(), bb.statements.size());
        if (const auto* drop = bb.terminator.opt_Drop(); drop && drop->slot.root.is_Local() && drop->slot.wrappers.empty()) {
            usageInfo[drop->slot.root.as_Local()].dropLocs.push_back(curLoc);
        } else {
            optVisitMirLvalues(bb.terminator, visitCb);
        }
    }

    // Look single-write/deref-only locals assigned with `_0 = Borrow`
    for (size_t varIdx = 0; varIdx < fcn.locals.size(); varIdx++) {
        const auto& slot = usageInfo[varIdx];
        auto thisVar = MIRLValue::newLocal(varIdx);

        // This rule only applies to single-write variables, with no use other than via derefs
        if (slot.nWrite != 1) {
            continue;
        }
        if (slot.nDerefRead == 0) {
            continue;
        }

        // Check that the source was a borrow statement
        auto& srcBb = fcn.blocks[slot.setLoc.bbIdx];
        if (!(slot.setLoc.stmtIdx < srcBb.statements.size() && (srcBb.statements[slot.setLoc.stmtIdx].is_Assign() && (srcBb.statements[slot.setLoc.stmtIdx].as_Assign().src.is_Borrow())))) {
            DEBUG(thisVar << " - Source is not a borrow op");
            continue;
        }
        const auto& srcBorrow = srcBb.statements[slot.setLoc.stmtIdx].as_Assign().src.as_Borrow();
        const auto& srcLv = srcBorrow.val;
        // Check that the borrow isn't too complex (if it's used multiple times)
        if (slot.nDerefRead > 1 && srcLv.wrappers.size() >= 2) {
            DEBUG(thisVar << " - Source is too complex - " << srcLv);
            continue;
        }
        // If there are multiple derefs, don't expand. More than one deref makes determining invalidation VERY hard
        if (std::count_if(srcLv.wrappers.begin(), srcLv.wrappers.end(), [](const MIRLValue::Wrapper& w) {
            return w.is_Deref();
        }) > 1) {
            DEBUG(thisVar << " - Source is too complex (deref) - " << srcLv);
            continue;
        }
        // Keep the complexity down (when not used only once)
        if (slot.nDerefRead + slot.nOtherRead > 1 && srcBorrow.type != HIRBorrowType::Shared) {
            DEBUG(thisVar << " - Multi-use non-shared borrow, too complex to do");
            continue;
        }
        DEBUG(thisVar << " - Borrow of " << srcLv << " at " << slot.setLoc << ", used " << slot.nDerefRead << " times (dropped {" << slot.dropLocs << "})");
        bool srcCopy = state.lvalueIsCopy(srcLv);

        // Locate usage sites (by walking forwards) and check for invalidation
        auto curLoc = slot.setLoc;
        curLoc.stmtIdx++;
        unsigned numReplaced = 0;
        auto replaceCb = [&](MIRLValue& lv, auto _vu) {
            if (lv.root == thisVar.root && !lv.wrappers.empty()) {
                ASSERT_BUG(Span(), !lv.wrappers.empty(), curLoc << " " << lv);
                MIR_ASSERT(state, lv.wrappers.front().is_Deref(), "Use of a replacable value that isn't via a deref - " << lv);
                // Make a LValue reference, then overwrite it
                {
                    auto lvr = MIRLValue::MRef(lv);
                    while (lvr.wrapperCount() > 1) {
                        lvr.tryUnwrap();
                    }
                    DEBUG(thisVar << " " << curLoc << " - Replace " << lvr << " with " << srcLv << " in " << lv);
                    lvr.replace(srcLv.clone());
                }
                DEBUG("= " << lv);
                assert(lv.root != thisVar.root);
                assert(numReplaced < slot.nDerefRead);
                numReplaced += 1;
            }
            return false;
        };
        for (bool stop = false; !stop;) {
            auto& curBb = fcn.blocks[curLoc.bbIdx];
            for (; curLoc.stmtIdx < curBb.statements.size(); curLoc.stmtIdx++) {
                auto& stmt = curBb.statements[curLoc.stmtIdx];
                DEBUG(curLoc << " " << stmt);
                // Check for invalidation (actual check done before replacement)
                bool invalidates = checkInvalidatesLvalue(stmt, srcLv, srcCopy);
                if (invalidates) {
                    // Invalidated, stop here.
                    DEBUG(thisVar << " - Source invalidated @ " << curLoc << " in " << stmt);
                    stop = true;
                    break;
                }
                // Replace usage
                optVisitMirLvaluesMut(stmt, replaceCb);
                if (numReplaced == slot.nDerefRead) {
                    stop = true;
                    break;
                }
            }
            if (stop) {
                break;
            }
            // Replace usage
            optVisitMirLvaluesMut(curBb.terminator, replaceCb);
            if (numReplaced == slot.nDerefRead) {
                stop = true;
                break;
            }
            // Check for invalidation
            if (checkInvalidatesLvalue(curBb.terminator, srcLv, srcCopy)) {
                DEBUG(thisVar << " - Source invalidated @ " << curLoc << " in " << curBb.terminator);
                stop = true;
                break;
            }

            switch (curBb.terminator.tag()) {
default:
                stop = true;
                break;
                // TODO: History is needed to avoid infinite loops from triggering infinite looping here.
                //    }
                // TODO: Fork state to handle multi-tagets
                // NOTE: `Call` can't work in the presense of unwinding, would need to traverse both paths
                //    }
            }
        }

        // If the source was an inner deref, update its counts
        if (srcLv.root.is_Local() && !srcLv.wrappers.empty() && srcLv.wrappers.front().is_Deref()) {
            usageInfo[srcLv.root.as_Local()].nDerefRead += numReplaced;
            if (numReplaced == slot.nDerefRead) {
                usageInfo[srcLv.root.as_Local()].nDerefRead -= 1;
            }
        }

        // If all usage sites were updated, then remove the original assignment
        // - Since this code works with `&mut`, can't just leave the assignment for DCE when mut
        if (numReplaced == slot.nDerefRead + slot.nOtherRead) {
            DEBUG(thisVar << " - Erase " << slot.setLoc << " as it is no longer used (" << srcBb.statements[slot.setLoc.stmtIdx] << ")");
            srcBb.statements[slot.setLoc.stmtIdx] = MIRStatement();
            for (const auto& dropLoc : slot.dropLocs) {
                DEBUG(thisVar << " - Drop at " << dropLoc);
                auto& dropBb = fcn.blocks[dropLoc.bbIdx];
                MIR_ASSERT(state, dropLoc.stmtIdx == dropBb.statements.size() && dropBb.terminator.is_Drop(), "Recorded drop is no longer a terminator");
                auto target = dropBb.terminator.as_Drop().target;
                dropBb.terminator = MIRTerminator::make_Goto(target);
            }
        } else {
            // The variable is still used, keep the source where it is
            DEBUG(thisVar << " - Keep " << slot.setLoc);
        }

        // Any replacements? Then there was an actionable change
        if (numReplaced > 0) {
            changed = true;
            // Return as soon as a variable has been changed, as this can invalidate the slot information
            return changed;
        }
    }

    return changed;
}

// --------------------------------------------------------------------
// Replaces reborrows where the source is never used again (except maybe
// ...
// --------------------------------------------------------------------
bool MIROptimiseDeTemporaryReborrowOfUnused(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

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

    ::std::vector<Poss> possible;
    // Locate reborrows with the same source/destination type
    // Source lvalue must be a local/argument
    for (const auto& blk : fcn.blocks) {
        for (const auto& stmt : blk.statements) {
            state.setCurStmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());

            if (!stmt.is_Assign()) {
                continue;
            }
            const auto& se = stmt.as_Assign();
            // Must be assigning to a local
            if (!se.dst.is_Local()) {
                continue;
            }
            // Soure must be a borrow
            if (!se.src.is_Borrow()) {
                continue;
            }
            const auto& re = se.src.as_Borrow();
            // Source must be `<local>*` or `<arg>*`
            if (!(re.val.root.is_Local() || re.val.root.is_Argument())) {
                continue;
            }
            if (!(re.val.wrappers.size() == 1 && re.val.wrappers[0].is_Deref())) {
                continue;
            }
            // Types must match (avoids decaying reborrows or raw pointer accesses)
            const auto& srcTy = re.val.root.is_Local() ? fcn.locals[re.val.root.as_Local()] : state.args[re.val.root.as_Argument()].second;
            const auto& dstTy = fcn.locals[se.dst.as_Local()];
            if (srcTy != dstTy) {
                continue;
            }

            // Record as a possible useless reborrow
            // - Depends on the usage of the source
            auto pos = OptimiseStmtRef(state.getCurBlock(), state.getCurStmtOfs());
            DEBUG(state << "Possible " << se.dst << " = " << re.val);
            possible.push_back(Poss(pos, re.val.root.clone(), se.dst.root.clone()));
        }
    }
    if (possible.size() == 0) {
        return false;
    }
    // The borrow must not be within a loop
    {
        std::vector<unsigned int> incomingEdges(fcn.blocks.size());
        for (const auto& block : fcn.blocks) {
            struct CountIncoming final: public MIRTargetVisitor {
                ::std::vector<unsigned int>& incomingEdges;

                explicit CountIncoming(::std::vector<unsigned int>& incomingEdges)
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
                ::std::vector<unsigned int>& incomingEdges;
                ::std::vector<unsigned int>& acyclicBlocks;

                PeelAcyclic(::std::vector<unsigned int>& incomingEdges, ::std::vector<unsigned int>& acyclicBlocks)
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
            // A block "loops" iff it is reachable from itself, i.e. it sits in a
            // strongly-connected component with more than one block, or has an
            // edge to itself. Compute that for every block in one pass
            // (iterative Tarjan) instead of running a whole-CFG search per
            // candidate block, which made this O(candidates * CFG).
            std::vector<bool> loops(fcn.blocks.size(), false);
            {
                const size_t nBlocks = fcn.blocks.size();
                std::vector<unsigned> index(nBlocks, ~0u);
                std::vector<unsigned> lowlink(nBlocks, 0);
                std::vector<bool> onStack(nBlocks, false);
                std::vector<unsigned> sccStack;
                unsigned nextIndex = 0;

                // Successor lists, so the walk below can resume mid-block.
                std::vector<std::vector<unsigned>> succs(nBlocks);
                for (const auto& block : fcn.blocks) {
                    unsigned bbIdx = &block - fcn.blocks.data();
                    struct CollectSuccs final: public MIRTargetVisitor {
                        size_t nBlocks;
                        unsigned bbIdx;
                        ::std::vector<::std::vector<unsigned>>& succs;
                        ::std::vector<bool>& loops;

                        CollectSuccs(size_t nBlocks, unsigned bbIdx, ::std::vector<::std::vector<unsigned>>& succs, ::std::vector<bool>& loops)
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
                                    loops[bbIdx] = true; // self-edge
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
                                // Pop one SCC (v and everything above it on the
                                // stack); sizes > 1 mean every member loops.
                                size_t vPos = sccStack.size();
                                while (vPos > 0 && sccStack[vPos - 1] != v) {
                                    vPos--;
                                }
                                assert(vPos > 0);
                                vPos--; // index of `v` itself
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

    // Must be the only use (apart from dropping) of the source lvalue
    ::std::unordered_map<uintptr_t, ::std::vector<size_t>> possibleBySource;
    for (size_t i = 0; i < possible.size(); i++) {
        possibleBySource[possible[i].slot.getInner()].push_back(i);
    }
    for (const auto& blk : fcn.blocks) {
        for (const auto& stmt : blk.statements) {
            state.setCurStmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());
            auto pos = OptimiseStmtRef(state.getCurBlock(), state.getCurStmtOfs());
            optVisitMirLvalues(stmt, [&](const MIRLValue& lv, MIRValUsage /*vu*/) {
                if (!(lv.root.is_Local() || lv.root.is_Argument())) {
                    return false;
                }
                auto it = possibleBySource.find(lv.root.getInner());
                if (it == possibleBySource.end()) {
                    return false;
                }
                for (auto possibleIdx : it->second) {
                    auto& p = possible[possibleIdx];
                    if (!(pos == p.pos)) {
                        DEBUG(state << p.slot << " Used - " << stmt);
                        p.used = true;
                    }
                }
                return false;
            });
        }
        const auto* dropped = blk.terminator.opt_Drop();
        optVisitMirLvalues(blk.terminator, [&](const MIRLValue& lv, MIRValUsage /*vu*/) {
            if (!(lv.root.is_Local() || lv.root.is_Argument())) {
                return false;
            }
            auto it = possibleBySource.find(lv.root.getInner());
            if (it != possibleBySource.end()) {
                if (dropped && dropped->slot.wrappers.empty() && dropped->slot.root.getInner() == lv.root.getInner()) {
                    return false;
                }
                for (auto possibleIdx : it->second) {
                    auto& p = possible[possibleIdx];
                    DEBUG(state << p.slot << " Used - " << blk.terminator);
                    p.used = true;
                }
            }
            return false;
        });
    }

    // Remove any marked with `used=true` from the list
    {
        auto ne = std::remove_if(possible.begin(), possible.end(), [&](const Poss& p) {
            return p.used;
        });
        possible.erase(ne, possible.end());
    }
    if (possible.size() == 0) {
        return false;
    }
    // Rewrite and erase
    ::std::unordered_set<uintptr_t> sourceSlots;
    ::std::unordered_map<uintptr_t, uintptr_t> replacements;
    for (auto it = possible.rbegin(); it != possible.rend(); ++it) {
        const auto source = it->slot.getInner();
        const auto destination = it->replace.getInner();
        sourceSlots.insert(source);
        auto next = replacements.find(source);
        replacements[destination] = next == replacements.end() ? source : next->second;
        fcn.blocks[it->pos.bbIdx].statements[it->pos.stmtIdx] = MIRStatement();
    }
    for (auto& blk : fcn.blocks) {
        for (auto& stmt : blk.statements) {
            state.setCurStmt(&blk - fcn.blocks.data(), &stmt - blk.statements.data());
            optVisitMirLvaluesMut(stmt, [&](MIRLValue& lv, MIRValUsage /*vu*/) {
                if (lv.root.is_Local()) {
                    auto it = replacements.find(lv.root.getInner());
                    if (it != replacements.end()) {
                        DEBUG(state << lv.root << " Replace");
                        lv.root = MIRLValue::Storage::fromInner(it->second);
                    }
                }
                return false;
            });
        }

        if (auto* drop = blk.terminator.opt_Drop(); drop && drop->slot.wrappers.empty() && (drop->slot.root.is_Local() || drop->slot.root.is_Argument()) && sourceSlots.count(drop->slot.root.getInner()) != 0) {
            DEBUG(state << drop->slot.root << " Erase drop");
            auto target = drop->target;
            blk.terminator = MIRTerminator::make_Goto(target);
        }
        optVisitMirLvaluesMut(blk.terminator, [&](MIRLValue& lv, MIRValUsage /*vu*/) {
            if (lv.root.is_Local()) {
                auto it = replacements.find(lv.root.getInner());
                if (it != replacements.end()) {
                    DEBUG(state << lv.root << " Replace");
                    lv.root = MIRLValue::Storage::fromInner(it->second);
                }
            }
            return false;
        });
    }
    changed = true;
    return changed;
}

// --------------------------------------------------------------------
// Replaces uses of stack slots with what they were assigned with (when
// possible)
// --------------------------------------------------------------------
bool MIROptimiseDeTemporary(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    changed |= MIROptimiseDeTemporarySingleSetAndUse(state, fcn);
    if (changed) {
        return changed;
    }
    changed |= MIROptimiseDeTemporaryBorrows(state, fcn);
    if (changed) {
        return changed;
    }
    changed |= MIROptimiseDeTemporaryReborrowOfUnused(state, fcn);

    // OLD ALGORITHM.
    for (unsigned int bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
        auto& bb = fcn.blocks[bbIdx];
        ::std::map<unsigned, unsigned> localAssignments; // Local number -> statement index
        // TODO: Keep track of what variables would invalidate a local (and compound on assignment)
        ::std::vector<unsigned> statementsToRemove; // List of statements that have to be removed

        // ----- Helper closures -----
        // > Check if a recorded assignment is no longer valid.
        auto cbCheckInvalidate = [&](const MIRLValue& lv, MIRValUsage vu) {
            for (auto it = localAssignments.begin(); it != localAssignments.end();) {
                bool invalidated = false;
                const auto& srcRvalue = bb.statements[it->second].as_Assign().src;

                // Destination invalidated?
                if (lv.root.is_Local() && it->first == lv.root.as_Local()) {
                    switch (vu) {
                        case MIRValUsage::Borrow:
                        case MIRValUsage::Write:
                            DEBUG(state << "> Mutate/Borrowed " << lv);
                            invalidated = true;
                            break;
                        default:
                            break;
                    }
                }
                // Source invalidated?
                else {
                    switch (vu) {
                        case MIRValUsage::Borrow: // Borrows are annoying, assume they invalidate anything used
                        case MIRValUsage::Write:  // Mutated? It's invalidated
                        case MIRValUsage::Move:   // Moved? Now invalid
                            optVisitMirLvalues(srcRvalue, [&](const MIRLValue& sLv, auto sVu) {
                                if (sLv.root == lv.root) {
                                    DEBUG(state << "> Invalidates source of Local(" << it->first << ") - " << srcRvalue);
                                    invalidated = true;
                                    return true;
                                }
                                return false;
                            });
                            break;
                        case MIRValUsage::Read: // Read is Ok
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
        };
        // ^^^ Check for invalidations
        auto cbApplyReplacements = [&](auto& topLv, auto topUsage) {
            // NOTE: Visits only the top-level LValues
            // - The inner `visit_mir_lvalue_mut` handles sub-values

            // TODO: Handle partial moves (only delete assignment if the value is fully used)
            // > For now, don't do the replacement if it would delete the assignment UNLESS it's directly being used)

            // 2. Search for replacements
            if (topLv.root.is_Local()) {
                bool topLevel = topLv.wrappers.empty();
                auto ilv = MIRLValue::newLocal(topLv.root.as_Local());
                auto it = localAssignments.find(topLv.root.as_Local());
                if (it != localAssignments.end()) {
                    const auto& newVal = bb.statements[it->second].as_Assign().src.as_Use();
                    // - Copy? All is good.
                    if (state.lvalueIsCopy(ilv)) {
                        topLv = newVal.cloneWrapped(topLv.wrappers.begin(), topLv.wrappers.end());
                        DEBUG(state << "> Replace (and keep) Local(" << it->first << ") with " << newVal);
                        changed = true;
                    }
                    // - Top-level (directly used) also good.
                    else if (topLevel && topUsage == MIRValUsage::Move) {
                        // TODO: DstMeta/DstPtr _doesn't_ move, so shouldn't trigger this.
                        topLv = newVal.clone();
                        DEBUG(state << "> Replace (and remove) Local(" << it->first << ") with " << newVal);
                        statementsToRemove.push_back(it->second);
                        localAssignments.erase(it);
                        changed = true;
                    }
                    // - Otherwise, remove the record.
                    else {
                        DEBUG(state << "> Non-copy value used within a LValue, remove record of Local(" << it->first << ")");
                        localAssignments.erase(it);
                    }
                }
            }
            // Return true to prevent recursion
            return true;
        };

        // ----- Top-level algorithm ------
        // - Find expressions matching the pattern `Local(N) = Use(...)`
        //  > Delete entry when destination is mutated
        //  > Delete entry when source is mutated or invalidated (moved)
        for (unsigned int stmtIdx = 0; stmtIdx < bb.statements.size(); stmtIdx++) {
            auto& stmt = bb.statements[stmtIdx];
            state.setCurStmt(bbIdx, stmtIdx);
            DEBUG(state << stmt);

            // - Check if this statement mutates or borrows a recorded local
            //  > (meaning that the slot isn't a temporary)
            // - Check if this statement mutates or moves the source
            //  > (thus making it invalid to move the source forwards)
            optVisitMirLvalues(stmt, cbCheckInvalidate);

            // - Apply known relacements
            optVisitMirLvaluesMut(stmt, cbApplyReplacements);

            // - Check if this is a new assignment
            if (stmt.is_Assign() && stmt.as_Assign().dst.is_Local() && stmt.as_Assign().src.is_Use()) {
                const auto& dstLv = stmt.as_Assign().dst;
                const auto& srcLv = stmt.as_Assign().src.as_Use();
                if (optVisitMirLvaluesInner(srcLv, MIRValUsage::Read, [&](const auto& lv, auto) {
                    return lv.root == dstLv.root;
                })) {
                    DEBUG(state << "> Don't record, self-referrential");
                } else if (::std::any_of(srcLv.wrappers.begin(), srcLv.wrappers.end(), [](const auto& w) {
                    return w.is_Deref();
                })) {
                    DEBUG(state << "> Don't record, dereference");
                } else {
                    localAssignments.insert(::std::make_pair(stmt.as_Assign().dst.as_Local(), stmtIdx));
                    DEBUG(state << "> Record assignment");
                }
            }
        } // for(stmt in bb.statements)

        // TERMINATOR
        state.setCurStmtTerm(bbIdx);
        DEBUG(state << bb.terminator);
        // > Check for invalidations (e.g. move of a source value)
        optVisitMirLvalues(bb.terminator, cbCheckInvalidate);
        // > THEN check for replacements
        if (!bb.terminator.is_Switch()) {
            optVisitMirLvaluesMut(bb.terminator, cbApplyReplacements);
        }

        // Remove assignments
        ::std::sort(statementsToRemove.begin(), statementsToRemove.end());
        while (!statementsToRemove.empty()) {
            // TODO: Handle partial moves here?
            // TODO: Is there some edge case I'm missing where the assignment shouldn't be removed?
            // > It isn't removed if it's used as a Copy, so that's not a problem.
            bb.statements.erase(bb.statements.begin() + statementsToRemove.back());
            statementsToRemove.pop_back();

            changed = true;
        }
    }

    return changed;
}

// --------------------------------------------------------------------
// Detect common statements between all source arms of a block
// --------------------------------------------------------------------
bool MIROptimiseCommonStatements(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    for (size_t bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
        state.setCurStmt(bbIdx, 0);

        bool skip = false;
        ::std::vector<size_t> sources;
        // Find source blocks
        for (size_t bb2Idx = 0; bb2Idx < fcn.blocks.size() && !skip; bb2Idx++) {
            const auto& blk = fcn.blocks[bb2Idx];
            // TODO: Handle non-Goto branches? (e.g. calls)
            if (blk.terminator.is_Goto() && blk.terminator.as_Goto() == bbIdx) {
                if (blk.statements.empty()) {
                    DEBUG(state << " BB" << bb2Idx << " empty");
                    skip = true;
                    break;
                }
                if (!sources.empty()) {
                    if (blk.statements.back() != fcn.blocks[sources.front()].statements.back()) {
                        DEBUG(state << " BB" << bb2Idx << " doesn't end with " << fcn.blocks[sources.front()].statements.back() << " instead " << blk.statements.back());
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
                        // If this terminator points to the current BB, don't attempt to merge
                        if (target == bbIdx) {
                            DEBUG(state << " BB" << bb2Idx << " doesn't end Goto - instead " << blk.terminator);
                            skip = true;
                        }
                    }
                } checkPointsBack{state, blk, bbIdx, bb2Idx, skip};
                visitTerminatorTarget(blk.terminator, checkPointsBack);
            }
        }

        if (!skip && sources.size() > 1) {
            // TODO: Should this search for any common statements?

            // Found a common assignment, add to the start and remove from sources.
            auto stmt = ::std::move(fcn.blocks[sources.front()].statements.back());
            MIR_DEBUG(state, "Move common final statements from " << sources << " to " << bbIdx << " - " << stmt);
            for (auto idx : sources) {
                fcn.blocks[idx].statements.pop_back();
            }
            fcn.blocks[bbIdx].statements.insert(fcn.blocks[bbIdx].statements.begin(), ::std::move(stmt));
        }
    }
    return changed;
}

// --------------------------------------------------------------------
// If two temporaries don't overlap in lifetime (blocks in which they're valid), unify the two
// --------------------------------------------------------------------
bool MIROptimiseUnifyTemporaries(MIRTypeResolve& state, MIRFunction& fcn) {
    bool replacementNeeded = false;
    TRACE_FUNCTION_FR("", replacementNeeded);
    ::std::vector<bool> replacable(fcn.locals.size());
    // 1. Enumerate which (if any) temporaries share the same type
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
    ::std::vector<MIRValueLifetime> slotLifetimes = mv$(lifetimes.slots);

    // 2. Unify variables of the same type with distinct non-overlapping lifetimes
    ::std::map<unsigned int, unsigned int> replacements;
    ::std::vector<bool> visited(fcn.locals.size());
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
            // Variables are of the same type, check if they overlap
            if (slotLifetimes[localIdx].overlaps(slotLifetimes[i])) {
                continue;
            }
            // They don't overlap, unify
            slotLifetimes[localIdx].unify(slotLifetimes[i]);
            replacements[i] = localIdx;
            replacementNeeded = true;
            visited[i] = true;
        }
    }

    if (replacementNeeded) {
        DEBUG("Replacing temporaries using {" << replacements << "}");
        optVisitMirLvaluesMut(state, fcn, [&](auto& lv, auto) {
            if (lv.root.is_Local()) {
                auto it = replacements.find(lv.root.as_Local());
                if (it != replacements.end()) {
                    MIR_DEBUG(state, lv << " => Local(" << it->second << ")");
                    lv.root = MIRLValue::Storage::newLocal(it->second);
                    return true;
                }
            }
            return false;
        });

        // TODO: Replace in ScopeEnd too?
    }

    return replacementNeeded;
}

// --------------------------------------------------------------------
// Combine identical blocks
// --------------------------------------------------------------------
bool MIROptimiseUnifyBlocks(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

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

    // Locate duplicate blocks and replace
    ::std::map<unsigned int, unsigned int> replacements;
    ::std::unordered_map<size_t, ::std::vector<unsigned int>> candidates;
    for (unsigned int bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
        if (fcn.blocks[bbIdx].terminator.isDead()) {
            continue;
        }
        if (fcn.blocks[bbIdx].terminator.is_Incomplete() && fcn.blocks[bbIdx].statements.size() == 0) {
            continue;
        }
        auto& bucket = candidates[H::blockHash(fcn.blocks[bbIdx])];
        bool found = false;
        for (auto candidate : bucket) {
            if (H::blocksEqual(fcn.blocks[candidate], fcn.blocks[bbIdx])) {
                replacements[bbIdx] = candidate;
                found = true;
                break;
            }
        }
        if (!found) {
            bucket.push_back(bbIdx);
        }
    }

    if (!replacements.empty()) {
        //MIR_TODO(state, "Unify blocks - " << replacements);
        DEBUG("Unify blocks (old: new) - " << replacements);
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

// --------------------------------------------------------------------
// Propagate source values when a composite (tuple) is read
// TODO: Is this needed now that SplitAggregates exists?
// --------------------------------------------------------------------
bool MIROptimisePropagateKnownValues(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changeHappend = false;
    TRACE_FUNCTION_FR("", changeHappend);
    // 1. Determine reference counts for blocks (allows reversing up BB tree)
    ::std::vector<size_t> blockOrigins(fcn.blocks.size(), SIZE_MAX);
    {
        ::std::vector<unsigned int> blockUses(fcn.blocks.size());
        ::std::vector<bool> visited(fcn.blocks.size());
        ::std::vector<MIRBasicBlockId> toVisit;
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
                const ::std::vector<bool>& visited;
                ::std::vector<MIRBasicBlockId>& toVisit;
                ::std::vector<unsigned int>& blockUses;
                ::std::vector<size_t>& blockOrigins;
                MIRBasicBlockId bb;

                RecordOrigins(const ::std::vector<bool>& visited, ::std::vector<MIRBasicBlockId>& toVisit, ::std::vector<unsigned int>& blockUses, ::std::vector<size_t>& blockOrigins, MIRBasicBlockId bb)
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

    // 2. Find any assignments (or function uses?) of the form FIELD(LOCAL, _)
    //  > Restricted to simplify logic (and because that's the inefficient pattern observed)
    // 3. Search backwards from that point until the referenced local is assigned
    auto getField = [&](const MIRLValue& slotLvalue, unsigned field, size_t startBbIdx, size_t startStmtIdx) -> const MIRLValue* {
        TRACE_FUNCTION_F(slotLvalue << "." << field << " BB" << startBbIdx << "/" << startStmtIdx);
        bool slotCopy = state.lvalueIsCopy(slotLvalue);
        // NOTE: An infinite loop is (theoretically) impossible.
        auto bbIdx = startBbIdx;
        auto stmtIdx = startStmtIdx;
        for (;;) {
            const auto& bb = fcn.blocks[bbIdx];
            while (stmtIdx--) {
                if (stmtIdx == bb.statements.size()) {
                    DEBUG("BB" << bbIdx << "/TERM - " << bb.terminator);
                    if (checkInvalidatesLvalue(bb.terminator, slotLvalue, slotCopy)) {
                        return nullptr;
                    }
                    continue;
                }
                const auto& stmt = bb.statements[stmtIdx];
                DEBUG("BB" << bbIdx << "/" << stmtIdx << " - " << stmt);
                if (const auto* se = stmt.opt_Assign()) {
                    if (se->dst == slotLvalue) {
                        if (!se->src.is_Tuple()) {
                            return nullptr;
                        }
                        const auto& srcParam = se->src.as_Tuple().vals.at(field);
                        DEBUG("> Found a source " << srcParam);
                        // TODO: Support returning a Param
                        if (!srcParam.is_LValue()) {
                            return nullptr;
                        }
                        const auto& srcLval = srcParam.as_LValue();
                        bool srcCopy = state.lvalueIsCopy(srcLval);
                        // Visit all statements between the start and here, checking for mutation of this value.
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
                                    DEBUG("BB" << bbIdx << "/TERM - " << bb.terminator);
                                    if (checkInvalidatesLvalue(bb.terminator, srcLval, srcCopy)) {
                                        // Invalidated: Return.
                                        return nullptr;
                                    }
                                    continue;
                                }
                                if (checkInvalidatesLvalue(bb.statements[stmtIdx], srcLval, srcCopy)) {
                                    // Invalidated: Return.
                                    return nullptr;
                                }
                            }
                            assert(blockOrigins[bbIdx] != SIZE_MAX);
                            bbIdx = blockOrigins[bbIdx];
                            stmtIdx = fcn.blocks[bbIdx].statements.size() + 1;
                        }
                        throw "";
                    }
                }

                // Check if the slot is invalidated (mutated)
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
            DEBUG(state << block.statements[i]);
            optVisitMirLvaluesMut(block.statements[i], [&](MIRLValue& lv, auto vu) {
                if (vu == MIRValUsage::Read && lv.wrappers.size() > 1 && lv.wrappers.front().is_Field() && lv.root.is_Local()) {
                    auto fieldIndex = lv.wrappers.front().as_Field();
                    auto innerLv = MIRLValue::newLocal(lv.root.as_Local());
                    auto outerLv = MIRLValue::newField(innerLv.clone(), fieldIndex);
                    // TODO: This value _must_ be Copy for this optimisation to work.
                    // - OR, it has to somehow invalidate the original tuple
                    DEBUG(state << "Locating origin of " << lv);
                    HIRTypeRef tmp;
                    if (!state.resolve.typeIsCopy(state.sp, state.getLvalueType(tmp, innerLv))) {
                        DEBUG(state << "- not Copy, can't optimise");
                        return false;
                    }
                    const auto* sourceLvalue = getField(innerLv, fieldIndex, bbIdx, i);
                    if (sourceLvalue) {
                        if (outerLv != *sourceLvalue) {
                            DEBUG(state << "Source is " << *sourceLvalue);
                            lv = sourceLvalue->cloneWrapped(lv.wrappers.begin() + 1, lv.wrappers.end());
                            changeHappend = true;
                        } else {
                            DEBUG(state << "No change");
                        }
                        return false;
                    }
                }
                return false;
            });
        }
    }
    return changeHappend;
}

// --------------------------------------------------------------------
// Propagate constants and eliminate known paths
// --------------------------------------------------------------------
bool MIROptimiseConstPropagate(MIRTypeResolve& state, MIRFunction& fcn) {
#if DUMP_BEFORE_ALL || DUMP_BEFORE_CONSTPROPAGATE
    if (debugEnabled()) {
        MIRDumpFcn(::std::cout, fcn);
    }
#endif
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);
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
    auto makeFloatArithmeticResult = [](FloatValue value, HIRCoreType type) {
        if (floatValueIsNan(value)) {
            value = positiveNanFloatValue();
        }
        return MIRConstant::make_Float({value, type});
    };

    // - Remove calls to `size_of` and `align_of` (replace with value if known)
    for (auto& bb : fcn.blocks) {
        state.setCurStmtTerm(bb);
        MIR_DEBUG(state, bb.terminator);
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
                DEBUG("size_of = " << sizeVal);
                auto val = MIRConstant::make_Uint({U128(sizeVal), HIRCoreType::Usize});
                bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), mv$(val)}));
                bb.terminator = MIRTerminator::make_Goto(te.retBlock);
                changed = true;
            }
        } else if (tef.name == "size_of_val") {
            size_t sizeVal = 0, tmp;
            if (TargetGetSizeAndAlignOf(state.sp, state.resolve, tef.params.types.at(0), sizeVal, tmp) && sizeVal != SIZE_MAX) {
                DEBUG("size_of_val = " << sizeVal);
                auto val = MIRConstant::make_Uint({U128(sizeVal), HIRCoreType::Usize});
                bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), mv$(val)}));
                bb.terminator = MIRTerminator::make_Goto(te.retBlock);
                changed = true;
            }
        } else if (tef.name == "align_of" || tef.name == "min_align_of") {
            size_t alignVal = 0;
            if (TargetGetAlignOf(state.sp, state.resolve, tef.params.types.at(0), alignVal)) {
                DEBUG("align_of = " << alignVal);
                auto val = MIRConstant::make_Uint({U128(alignVal), HIRCoreType::Usize});
                bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), mv$(val)}));
                bb.terminator = MIRTerminator::make_Goto(te.retBlock);
                changed = true;
            }
        } else if (tef.name == "min_align_of_val") {
            size_t alignVal = 0;
            size_t sizeVal = 0;
            // Note: Trait object returns align_val = 0 (slice-based types have an alignment)
            if (TargetGetSizeAndAlignOf(state.sp, state.resolve, tef.params.types.at(0), sizeVal, alignVal) && alignVal > 0) {
                DEBUG("min_align_of_val = " << alignVal);
                auto val = MIRConstant::make_Uint({U128(alignVal), HIRCoreType::Usize});
                bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), mv$(val)}));
                bb.terminator = MIRTerminator::make_Goto(te.retBlock);
                changed = true;
            }
        }
        // NOTE: Quick special-case for bswap<u8/i8> (a no-op)
        else if (tef.name == "bswap" && (tef.params.types.at(0) == HIRCoreType::U8 || tef.params.types.at(0) == HIRCoreType::I8)) {
            DEBUG("bswap<u8> is a no-op");
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
            // Returns `true` if the actual type given as `T` requires drop glue;
            // returns `false` if the actual type provided for `T` implements `Copy`. (Either otherwise)
            // NOTE: libarena assumes that this returns `true` iff T doesn't require drop glue.
            const auto& ty = tef.params.types.at(0);
            // - Only expand at this stage if there's no generics, and no unbound paths
            if (!visitTyWith(ty, [](const HIRTypeData* ty) -> bool {
                return ty->is_Generic() || ((*ty).is_Path() && ((*ty).as_Path().binding.is_Unbound()));
            })) {
                bool needsDrop = state.resolve.typeNeedsDropGlue(state.sp, ty);
                bb.statements.push_back(MIRStatement::make_Assign({mv$(te.retVal), MIRRValue::make_Constant(MIRConstant::make_Bool({needsDrop}))}));
                bb.terminator = MIRTerminator::make_Goto(te.retBlock);
                changed = true;
            }
        } else {
            // Ignore any other intrinsics
        }
    }

    // - Propage constants within BBs
    //  > Evaluate BinOp with known values
    //  > Understand intrinsics like overflowing_* (with correct semantics)
    //   > NOTE: No need to locally stitch blocks, next pass will do that
    // TODO: Use ValState to do full constant propagation across blocks

    // Remove redundant temporaries and evaluate known binops
    for (auto& bb : fcn.blocks) {
        auto bbidx = &bb - &fcn.blocks.front();

        ::std::map<MIRLValue, MIRConstant> knownValues;
        // Known enum variants
        ::std::map<MIRLValue, unsigned> knownValuesVar;
        ::std::map<unsigned, bool> knownDropFlags;

        auto checkLv = [&](const MIRLValue& lv) -> MIRConstant {
            auto it = knownValues.find(lv);
            if (it != knownValues.end()) {
                DEBUG(state << "Value " << lv << " known to be " << it->second);
                return it->second.clone();
            }

            // TODO: If the inner of the value is known,
            //   AND all indexes are known - expand
            //if( !lv.m_wrappers.empty() )
            //{
            //    if( it != known_values.end() )
            //    {
            //        // TODO: Use HIR::Literal instead so composites can be handled.
            //        for(const auto& w : lv.m_wrappers)
            //        {
            //        }
            //    }
            //}

            // Reads of statics
            if (lv.wrappers.empty() && lv.root.is_Static()) {
                DEBUG("Read of a static - " << lv.root.as_Static());
                // Look up this static, and see if it's not mutable, and a primitive
                // - If the static is an immutable primitive: read and save
                MonomorphState ms(state.resolve.hirCrate().types);
                auto v = state.resolve.getValue(state.sp, lv.root.as_Static(), ms);
                if (v.is_Static()) {
                    const auto& stat = *v.as_Static();
                    if (stat.valueGenerated && !stat.isMut && state.resolve.typeIsInteriorMutable(state.sp, stat.type) == HIRCompare::Unequal) {
                        // Convert the encoded literal into a `MIR::Const`
                        const auto el = EncodedLiteralSlice(stat.valueRes);
                        // Check the type
                        // - Primitives
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
                        // - Pointers
                        if (stat.type->is_Borrow()) {
                            // TODO: Read the borrow, and store
                        }
                        // - Could traverse the static via the wrappers too?
                    }
                }
            }

            // Not a known value, and not a known composite
            // - Use a nullptr ItemAddr to indicate this
            return MIRConstant::make_ItemAddr({});
        };
        auto checkParam = [&](MIRParam& p) {
            if (const auto* pe = p.opt_LValue()) {
                auto nv = checkLv(*pe);
                if (nv.is_ItemAddr() && !nv.as_ItemAddr()) {
                    // ItemAddr with a nullptr inner means "no expansion"
                } else {
                    p = mv$(nv);
                    changed = true;
                }
            }
        };

        // Convert known indexes into field acceses
        auto editLval = [&](MIRLValue& lv, MIRValUsage _vu) -> bool {
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

            // If a Deref of a known value is seen, replace with the source of that value.
            if (!lv.wrappers.empty() && lv.wrappers.front().is_Deref() && !lv.root.is_Static()) {
                auto ilv = MIRLValue(lv.root.clone(), {});
                auto it = knownValues.find(ilv);
                if (it != knownValues.find(lv)) {
                    DEBUG("Known deref source: " << ilv << " == " << it->second);
                    if (it->second.is_ItemAddr() && it->second.as_ItemAddr().offset == U128(0)) {
                        lv.wrappers.erase(lv.wrappers.begin());
                        lv.root = MIRLValue::Storage::newStatic(it->second.as_ItemAddr()->clone());
                        changed = true;
                    }
                }
            }
            return true;
        };

        for (auto& stmt : bb.statements) {
            auto stmtidx = &stmt - &bb.statements.front();
            state.setCurStmt(bbidx, stmtidx);

            optVisitMirLvaluesMut(stmt, editLval);

            // Scan statements forwards:
            // - If a known temporary is used as Param::LValue, replace LValue with the value
            // - If a UniOp has its input known, evaluate
            // - If a BinOp has both values known, evaluate
            if (auto* e = stmt.opt_Assign()) {
                struct H {
                    static S128 truncateS(HIRCoreType ct, S128 v) {
                        // Truncate unsigned, then sign extend
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
                            // usize/size - need to handle <64 pointer bits
                            case HIRCoreType::Isize:
                                if (TargetGetPointerBits() < 64) {
                                    return sext(u, TargetGetPointerBits());
                                }
                                return v;
                            default:
                                // Invalid type for `Constant::Int` literal
                                break;
                        }
                        return v;
                    }

                    static S128 sext(U128 v, unsigned bits) {
                        if (v >> (bits - 1) != 0) {
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
                            // usize/size - need to handle <64 pointer bits
                            case HIRCoreType::Isize:
                            case HIRCoreType::Usize:
                                if (TargetGetPointerBits() < 64) {
                                    return v & U128(UINT64_MAX >> (64 - TargetGetPointerBits()));
                                }
                                return v & U128(UINT64_MAX);
                            case HIRCoreType::Char:
                                break;
                            default:
                                // Invalid type for Uint literal
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
                            // ItemAddr with a nullptr inner means "no expansion"
                        } else {
                            e->src = MIRRValue::make_Constant(mv$(nv));
                            changed = true;
                        }
                        break;
                    }
                    case MIRRValue::TAG_Constant: {
                        // Ignore (knowledge done below)
                        break;
                    }
                    case MIRRValue::TAG_SizedArray: {
                        auto& se = e->src.as_SizedArray();
                        checkParam(se.val);
                        break;
                    }
                    case MIRRValue::TAG_Borrow: {
                        auto& se = e->src.as_Borrow();
                        // Shared borrows of statics can be better represented with the ItemAddr constant
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

                        // If casting a number to a number, do the cast and
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
                                            // NaN fails both comparisons and is left unfolded
                                            if (FloatValue() <= value && value < FloatValue(18446744073709551616.0)) {
                                                newValue = MIRConstant::make_Uint({H::truncateU(*te, U128(static_cast<u64>(value))), *te});
                                            } else {
                                                // UB: Casting float out of range?
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

                            // The value has to be evaluated first: reading a
                            // variant before then gives zero. Ask for that
                            // directly rather than for the layout, which a
                            // variant naming another variant of the same enum
                            // would ask for again.
                            if (!enm.discriminantsEvaluated) {
                                ConvertHIRConstantEvaluateEnum(state.resolve.board(), state.resolve.hirCrate(), srcTy->as_Path().path.data.as_Generic().path, enm);
                            }
                            auto v = enm.getDiscriminant(variantIdx);
                            // A cast reads the discriminant at the enum's declared
                            // representation, which is `isize` unless one is
                            // given. How narrow the tag ends up in memory is a
                            // layout choice that says nothing about the value.
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
                            DEBUG(state << " " << e->src << " = " << newValue);
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
                                // One of the arms is a named constant, can't check (they're not an actual value, just a
                                // reference to one)
                            } else if (valL.is_Generic() || valR.is_Generic()) {
                                // One of the arms is a generic, can't check either
                            } else {
                                MIRConstant newValue;
                                switch (se.op) {
                                    // Note: f32's bit accuracy is different to f64, so they can't be considered equivalent in behaviour
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
                                        newValue = makeFloatArithmeticResult(le.v + re.v, le.t);
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
                                        newValue = makeFloatArithmeticResult(le.v - re.v, le.t);
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
                                        newValue = makeFloatArithmeticResult(le.v * re.v, le.t);
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
                                        newValue = makeFloatArithmeticResult(le.v / re.v, le.t);
                                        break;
                                    }
                                    case MIRConstant::TAG_Int: {
                                        auto& le = valL.as_Int();
                                        auto& re = valR.as_Int();
                                        MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::DIV - " << valL << " / " << valR);
                                        if (re.v == 0) {
                                            DEBUG(state << "Const eval error: Constant division by zero");
                                        } else {
                                            newValue = MIRConstant::make_Int({H::truncateS(le.t, le.v / re.v), le.t});
                                        }
                                        break;
                                    }
                                    case MIRConstant::TAG_Uint: {
                                        auto& le = valL.as_Uint();
                                        auto& re = valR.as_Uint();
                                        MIR_ASSERT(state, le.t == re.t, "Mismatched types for eBinOp::DIV - " << valL << " / " << valR);
                                        if (re.v == 0) {
                                            DEBUG(state << "Const eval error: Constant division by zero");
                                        } else {
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

                            // --- Bit Shifts ---
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
                            case MIRBinOp::BIT_SHR:{
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
                                    DEBUG(state << " " << e->src << " = " << newValue);
                                    e->src = mv$(newValue);
                                    changed = true;
                                }
                            }
                        } else {
                            MIRParam newValue;
                            // No-ops
                            switch (se.op) {
                                // `foo + 0 == foo`
                                // `foo - 0 == foo`
                                case MIRBinOp::ADD:
                                case MIRBinOp::SUB:
                                    if (se.valR.is_Constant() && se.valR.as_Constant().is_Uint() && se.valR.as_Constant().as_Uint().v == 0) {
                                        newValue = mv$(se.valL);
                                    }
                                    break;
                                // `foo % 1 == 0`
                                case MIRBinOp::MOD:
                                    if (se.valR.is_Constant() && se.valR.as_Constant().is_Uint() && se.valR.as_Constant().as_Uint().v == 1) {
                                        newValue = MIRConstant::make_Uint({U128(0), se.valR.as_Constant().as_Uint().t});
                                    }
                                    break;
                                // `foo / 1 == foo`
                                case MIRBinOp::DIV:
                                    if (se.valR.is_Constant() && se.valR.as_Constant().is_Uint() && se.valR.as_Constant().as_Uint().v == 1) {
                                        newValue = mv$(se.valL);
                                    }
                                    break;
                                // `foo * 0 == 0`
                                // `foo * 1 == foo`
                                // `0 * foo == 0`
                                // `1 * foo == foo`
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
                                DEBUG(state << " " << e->src << " = " << newValue);
                            switch (newValue.tag()) {
                                case MIRParam::TAG_LValue: {
                                    auto& v = newValue.as_LValue();
                                    e->src = mv$(v);
                                    break;
                                }
                                case MIRParam::TAG_Borrow: {
                                    auto& _ = newValue.as_Borrow();
                                    throw "";
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
                                            // Invalid type for Uint literal
                                            replace = false;
                                            break;
                                    }
                                    newValue = MIRConstant::make_Uint({val, ve.t});
                                    break;
                                }
                                case MIRConstant::TAG_Int: {
                                    auto& ve = val.as_Int();
                                    // ! is valid on Int, it inverts bits the same way as an uint
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
                                            // Invalid type for Uint literal
                                            replace = false;
                                            break;
                                    }
                                    newValue = MIRConstant::make_Int({val, ve.t});
                                    break;
                                }
                                case MIRConstant::TAG_Float: {
                                    // Not valid?
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
                                    // Not valid?
                                    break;
                                }
                                case MIRConstant::TAG_Int: {
                                    auto& ve = val.as_Int();
                                    newValue = MIRConstant::make_Int({ H::truncateS(ve.t, -ve.v), ve.t });
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
                                    // Not valid?
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
                                DEBUG(state << " " << e->src << " = " << newValue);
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
                        // NOTE: This disables any checks if the metadata isn't populated.
                        // This avoids issues with cleanup when optimise is run first
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
                    }
                }
            }
            // - If a known temporary is borrowed mutably or mutated somehow, clear its knowledge
            optVisitMirLvalues(stmt, [&knownValues, &knownValuesVar](const MIRLValue& lv, MIRValUsage vu) -> bool {
                if (vu == MIRValUsage::Write) {
                    knownValues.erase(lv);
                    knownValuesVar.erase(lv);
                }
                return false;
            });
            // - Locate `temp = SOME_CONST` and record value
            if (const auto* e = stmt.opt_Assign()) {
                if (e->dst.is_Local()) {
                    // Known constant
                    if (const auto* ce = e->src.opt_Constant()) {
                        knownValues.insert(::std::make_pair(e->dst.clone(), ce->clone()));
                        DEBUG(state << stmt);
                    }
                    // Known variant
                    else if (const auto* ce = e->src.opt_EnumVariant()) {
                        knownValuesVar.insert(::std::make_pair(e->dst.clone(), ce->index));
                        DEBUG(state << stmt);
                    }
                    // Propagate knowledge through Local=Local assignments
                    else if (const auto* ce = e->src.opt_Use()) {
                        if (ce->is_Local()) {
                            auto it1 = knownValues.find(*ce);
                            auto it2 = knownValuesVar.find(*ce);
                            assert(!(it1 != knownValues.end() && it2 != knownValuesVar.end()));
                            if (it1 != knownValues.end()) {
                                knownValues.insert(::std::make_pair(e->dst.clone(), it1->second.clone()));
                                DEBUG(state << stmt);
                            } else if (it2 != knownValuesVar.end()) {
                                knownValuesVar.insert(::std::make_pair(e->dst.clone(), it2->second));
                                DEBUG(state << stmt);
                            } else {
                                // Neither known, don't propagate
                            }
                        }
                    } else {
                        // No need to clear, the visit above this if block handles it.
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
                        DEBUG(state << "Convert " << bb.terminator << " into Goto(" << newBb << ") because variant known to be #" << it->second);
                        bb.terminator = MIRTerminator::make_Goto(newBb);

                        changed = true;
                    }

                }
                break;
                break;
                case MIRTerminator::TAG_If: {
                    auto& te = bb.terminator.as_If();
                    auto it = knownValues.find(te.cond);
                    if (it != knownValues.end()) {
                        if (it->second.is_Const() || it->second.is_Generic()) {
                        } else {
                            MIR_ASSERT(state, it->second.is_Bool(), "Terminator::If with known value not Bool - " << it->second);
                            auto newBb = (it->second.as_Bool().v ? te.bbTrue : te.bbFalse);
                            DEBUG(state << "Convert " << bb.terminator << " into Goto(" << newBb << ") because condition known to be " << it->second);
                            bb.terminator = MIRTerminator::make_Goto(newBb);

                            changed = true;
                        }
                    }

                }
                break;
                break;
                case MIRTerminator::TAG_Call: {
                    auto& te = bb.terminator.as_Call();
                    for (auto& a : te.args) {
                        checkParam(a);
                    }

                }
                break;
            default:
                break;
        }
    }

    // - Remove based on known booleans within a single block
    //  > Eliminates `if false`/`if true` branches
    // TODO: Is this now defunct after the handling of Terminator::If above?
    for (auto& bb : fcn.blocks) {
        auto bbidx = &bb - &fcn.blocks.front();
        if (!bb.terminator.is_If()) {
            continue;
        }
        const auto& te = bb.terminator.as_If();

        // Restrict condition to being a temporary/variable
        if (te.cond.is_Local())
            ;
        else {
            continue;
        }

        auto hasCond = [&](const auto& lv, auto ut) -> bool {
            return lv == te.cond;
        };
        bool valKnown = false;
        bool knownVal;
        for (unsigned int i = bb.statements.size(); i--;) {
            if (bb.statements[i].is_Assign()) {
                const auto& se = bb.statements[i].as_Assign();
                // If the condition was mentioned, don't assume it has the same value
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
            DEBUG("bb" << bbidx << ": Condition known to be " << knownVal);
            bb.terminator = MIRTerminator::make_Goto(knownVal ? te.bbTrue : te.bbFalse);
            changed = true;
        }
    }

    return changed;
}

// --------------------------------------------------------------------
// Split aggregated values that are never used by outer value into inner values
// --------------------------------------------------------------------
// NOTE: This is a generalised version of the old de-tuple pass (and fills part of MIR_Optimise_PropagateKnownValues)
// NOTE: This has a special case rule that disallowes borrows of the first field: Sometimes a borrow of the first
//       field is used as a proxy for the entire struct.
bool MIROptimiseSplitAggregates(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // Find locals that are:
    // - Assigned once
    // - From a constructor
    // - And only ever used via a field access
    // Replace the construction with assignments of `n` locals instead (which can be optimised by further passes)

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

    // 1. Find locals created from constructors (struct/tuple)
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
                }
                // NOTE: Arrays are eligable (as long as they're only accessed using field operator
                else if (auto* sse = se->src.opt_Array()) {
                    if (sse->vals.size() == 0) {
                        continue;
                    }
                }
                // Variants are allowed, they store the variant index for later checking
                else if (auto* sse = se->src.opt_EnumVariant()) {
                    if (sse->vals.size() == 0) {
                        continue;
                    }
                    DEBUG("> BB" << bbIdx << "/" << i << ": POSSIBLE " << stmt);
                    potentials.insert(std::make_pair(se->dst.as_Local(), Potential(bbIdx, i, sse->index)));
                    continue;
                }
                // NOTE: Union variants need special handling in the replacement
                else {
                    continue;
                }

                // Found a potential.
                DEBUG("> BB" << bbIdx << "/" << i << ": POSSIBLE " << stmt);
                potentials.insert(std::make_pair(se->dst.as_Local(), Potential(bbIdx, i)));
            }
        }
    }
    // - Nothing to do? return early
    if (potentials.empty()) {
        return false;
    }

    // 2. Check how the variables are used (allow one write, and no other direct usage)
    // - Removes any potentials that are invalidated.
    optVisitMirLvalues(state, fcn, [&](const MIRLValue& lv, MIRValUsage vu) -> bool {
        if (lv.root.is_Local()) {
            // Is this one of the potentials?
            auto it = potentials.find(lv.root.as_Local());
            if (it != potentials.end()) {
                if (lv.wrappers.empty()) {
                    // NOTE: A single write is allowed (the assignment)
                    // - Any other would be a re-assignent or a drop
                    if (vu == MIRValUsage::Write) {
                        it->second.nWrite += 1;
                    } else {
                        // Direct usage!
                        it->second.isDirectUsed = true;
                    }
                } else if (lv.wrappers.front().is_Field()) {
                    // Field acess: allowed UNLESS it's a borrow of the first field
                    // TODO: Find out what code makes the assumption that `&foo.0` is a good stand-in for `&foo`
                    if (lv.wrappers.front().as_Field() == 0 && vu == MIRValUsage::Borrow) {
                        it->second.isDirectUsed = true;
                    }
                } else if (lv.wrappers.front().is_Downcast()) {
                    // Downcast to a variant other than the variant it was constructed as, don't do anything.
                    // - For enums, this is an error (but here we don't know for sure). For unions it's valid behaviour
                    // A bare downcast uses the complete variant payload, so it cannot be replaced with a field local.
                    if (lv.wrappers.front().as_Downcast() != it->second.variantIdx || lv.wrappers.size() < 2 || !lv.wrappers[1].is_Field()) {
                        it->second.isDirectUsed = true;
                    }
                } else {
                    // Index and deref are disallowed
                    it->second.isDirectUsed = true;
                }

                // If invalidated, delete.
                if (it->second.isDirectUsed || it->second.nWrite > 1) {
                    const auto& stmt = fcn.blocks[it->second.srcBbIdx].statements[it->second.srcStmtIdx];
                    DEBUG(state << ": REMOVE BB" << it->second.srcBbIdx << "/" << it->second.srcStmtIdx << " " << stmt << " from " << lv /*<< " vu=" << vu*/);
                    potentials.erase(it);
                }
            }
        }
        return true;
    });
    // - All potentials removed? Return early
    if (potentials.empty()) {
        return false;
    }

    // 3. Explode sources into locals
    // NOTE: This needs to handle movement of indexes
    for (auto& p : potentials) {
        auto bbIdx = p.second.srcBbIdx;
        auto stmtIdx = p.second.srcStmtIdx;
        state.setCurStmt(bbIdx, stmtIdx);
        auto& block = fcn.blocks[bbIdx];

        DEBUG("- BB" << bbIdx << "/" << stmtIdx << ": " << block.statements[stmtIdx]);
        // Extract the list of values from the existing statement
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

        //for(size_t i = 0; i < block.statements.size(); i ++)

        // Insert new statements as required
        if (offset > 0) {
            block.statements.resize(block.statements.size() + offset);
            // Move all elements [stmt_idx+1 .. ] up by `offset`
            // NOTE: move_backward's third argument is 'past-the-end'
            std::move_backward(block.statements.begin() + stmtIdx + 1, block.statements.end() - offset, block.statements.end());
        }

        // Create new statements (allocating new locals)
        auto newLocalBase = fcn.locals.size();
        fcn.locals.resize(fcn.locals.size() + vals.size());
        p.second.replacements.resize(vals.size());
        for (size_t i = 0; i < vals.size(); i++) {
            // Allocate a new local
            auto newLocal = static_cast<unsigned>(newLocalBase + i);
            HIRTypeRef tmp;
            fcn.locals[newLocal] = state.getParamType(tmp, vals[i]);
            p.second.replacements[i] = newLocal;
            // Set the relevant statement to be an assignment to that new local
            block.statements[stmtIdx + i] = MIRStatement::make_Assign({MIRLValue::newLocal(newLocal), paramToRvalue(mv$(vals[i]))});
            DEBUG("+ BB" << bbIdx << "/" << (stmtIdx + i) << ": " << block.statements[stmtIdx + i]);
        }

        //for(size_t i = 0; i < block.statements.size(); i ++)

        // If this replacement changed the number of statements in this block, update all existing references.
        if (offset > 0) {
            for (auto& otherP : potentials) {
                if (otherP.second.srcBbIdx == bbIdx && otherP.second.srcStmtIdx > stmtIdx) {
                    otherP.second.srcStmtIdx += offset;
                }
            }
        }
    }

    // 4. Replace all usages
    optVisitMirLvaluesMut(state, fcn, [&](MIRLValue& lv, MIRValUsage vu) -> bool {
        if (lv.root.is_Local()) {
            // Is this one of the potentials?
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
                DEBUG(state << " " << lv << " -> " << newLv);
                lv = mv$(newLv);
            }
        }
        return true;
    });

    // If we reach this point, a replacement was done.
    changed = true;
    return true;
}

// --------------------------------------------------------------------
// Replace `tmp = RValue::Use()` where the temp is only used once
// --------------------------------------------------------------------
bool MIROptimisePropagateSingleAssignments(MIRTypeResolve& state, MIRFunction& fcn) {
    bool replacementHappend;
    TRACE_FUNCTION_FR("", replacementHappend);

    // TODO: This requires kowing that doing so has no effect.
    // - Can use little heristics like a Call pointing to an assignment of its RV
    // - Count the read/write count of a variable, if it's 1,1 then this optimisation is correct.
    // - If the count is read=*,write=1 and the write is of an argument, replace with the argument.
    struct ValUse {
        unsigned int read = 0;
        unsigned int write = 0;
        unsigned int borrow = 0;
    };

    struct {
        ::std::vector<ValUse> localUses;

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
    } valUses = {::std::vector<ValUse>(fcn.locals.size())};

    optVisitMirLvalues(state, fcn, [&](const auto& lv, auto ut) {
        valUses.useLvalue(lv, ut);
        return false;
    });

    // --- Eliminate `tmp = Use(...)` (moves lvalues downwards)
    // > Find an assignment `tmp = Use(...)` where the temporary is only written and read once
    // > Locate the usage of this temporary
    //  - Stop on any conditional terminator
    // > Any lvalues in the source lvalue must not be mutated between the source assignment and the usage.
    //  - This includes mutation, borrowing, or moving.
    // > Replace usage with the inner of the original `Use`
    {
        // 1. Assignments (forward propagate)
        //::std::map< ::MIR::LValue::CRef, ::MIR::RValue>    replacements;
        ::std::vector<::std::pair<MIRLValue, MIRRValue>> replacements;
        auto replacementsFind = [&replacements](const MIRLValue::CRef& lv) {
            return ::std::find_if(replacements.begin(), replacements.end(), [&](const auto& e) {
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
                DEBUG(state << stmt);
                // > Assignment
                if (!stmt.is_Assign()) {
                    continue;
                }
                const auto& e = stmt.as_Assign();
                // > Of a temporary from with a RValue::Use
                if (e.dst.is_Local()) {
                    const auto& vu = valUses.localUses[e.dst.as_Local()];
                    DEBUG(" - VU " << e.dst << " R:" << vu.read << " W:" << vu.write << " B:" << vu.borrow);
                    // TODO: Allow write many?
                    // > Where the variable is written once and read once
                    if (!(vu.read == 1 && vu.write == 1 && vu.borrow == 0)) {
                        DEBUG("> Not a single read+write");
                        continue;
                    }
                } else {
                    continue;
                }
                bool onlyOne = false;
                if (e.src.is_Use()) {
                    // Keep the complexity down
                    const auto* srcp = &e.src.as_Use();
                    // If there are deref/index accesses, then only allow one statement
                    // - This is the lazy option, avoids needing to check for invalidation (could be a write through deref)
                    if (::std::any_of(srcp->wrappers.begin(), srcp->wrappers.end(), [](auto& w) {
                        return !w.is_Field() && !w.is_Downcast();
                    })) {
                        DEBUG("Non-field access");
                        onlyOne = true;
                        continue;
                    }
                    // TODO: Why is this limited to locals only?
                    if (!srcp->root.is_Local()) {
                        DEBUG("> Can't replace, not a local root");
                        continue;
                    }

                    if (replacementsFind(*srcp) != replacements.end()) {
                        DEBUG("> Can't replace, source has pending replacement");
                        continue;
                    }
                } else {
                    // Not a use
                    continue;
                }
                bool srcIsLvalue = e.src.is_Use();
                DEBUG("- Locate usage");

                auto isLvalueUsage = [&](const auto& lv, auto) {
                    return lv.root == e.dst.root;
                };

                // Eligable for replacement
                // Find where this value is used
                // - Stop on a conditional block terminator
                // - Stop if any value mentioned in the source is mutated/invalidated
                bool stop = false;
                bool found = false;
                for (unsigned int si2 = stmtIdx + 1; si2 < block.statements.size(); si2++) {
                    state.setCurStmt(&block - &fcn.blocks.front(), si2);
                    const auto& stmt2 = block.statements[si2];
                    DEBUG(state << "[find usage] " << stmt2);

                    // Check for invalidation (done first, to avoid cases where the source is moved into a struct)
                    if (checkInvalidatesLvalue(stmt2, e.src.as_Use(), false)) {
                        stop = true;
                        DEBUG("Source invalidated");
                        break;
                    }

                    // Usage found.
                    if (optVisitMirLvalues(stmt2, isLvalueUsage)) {
                        // If the source isn't a Use, ensure that this is a Use
                        if (!srcIsLvalue) {
                            if (stmt2.is_Assign() && stmt2.as_Assign().src.is_Use()) {
                                // Good
                            } else {
                                // Bad, this has to stay a temporary
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
                        DEBUG("Source invalidated in terminator");
                    }
                }
                if (!stop) {
                    state.setCurStmtTerm(&block - &fcn.blocks.front());
                    DEBUG(state << "[find usage] " << block.terminator);
                    if (srcIsLvalue) {
                        optVisitMirLvalues(block.terminator, [&](const auto& lv, auto vu) {
                            found |= isLvalueUsage(lv, vu);
                            return found;
                        });
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
                            DEBUG("TODO: Chain");
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
                // Schedule a replacement in a future pass
                if (found) {
                    DEBUG("> Schedule replace " << e.dst << " with " << e.src.as_Use());
                    replacements.push_back(::std::make_pair(e.dst.clone(), e.src.clone()));
                } else {
                    DEBUG("- Single-write/read " << e.dst << " not replaced - couldn't find usage");
                }
            } // for(stmt : block.statements)
        }

        DEBUG("replacements = " << replacements);

        // Apply replacements within replacements
        for (;;) {
            unsigned int innerReplacedCount = 0;
            for (auto& r : replacements) {
                optVisitMirLvaluesMut(r.second, [&](MIRLValue& lv, auto vu) {
                    if (vu == MIRValUsage::Read || vu == MIRValUsage::Move) {
                        visitMirLvalueMut(lv, vu, [&](MIRLValue::MRef& lvr, auto vu) {
                            auto it = replacementsFind(lvr);
                            if (it != replacements.end() && it->second.is_Use()) {
                                lvr.replace(it->second.as_Use().clone());
                                innerReplacedCount++;
                            }
                            return false;
                        });
                    }
                    return false;
                });
            }
            if (innerReplacedCount == 0) {
                break;
            }
        }
        DEBUG("replacements = " << replacements);

        // Apply replacements
        unsigned int replaced = 0;
        while (replaced < replacements.size()) {
            auto oldReplaced = replaced;
            auto cb = [&](MIRLValue& lv, auto vu) {
                return visitMirLvalueMut(lv, vu, [&](MIRLValue::MRef& lv, auto vu) {
                    if (vu == MIRValUsage::Read || vu == MIRValUsage::Move) {
                        auto it = replacementsFind(lv);
                        if (it != replacements.end()) {
                            MIR_ASSERT(state, !it->second.isDead(), "Replacement of  " << lv << " fired twice");
                            MIR_ASSERT(state, it->second.is_Use(), "Replacing a lvalue with a rvalue - " << lv << " with " << it->second);
                            auto rval = ::std::move(it->second);
                            DEBUG("> Do replace " << lv << " => " << rval);
                            lv.replace(::std::move(rval.as_Use()));
                            replaced += 1;
                        }
                    }
                    return false;
                });
            };
            for (unsigned int blockIdx = 0; blockIdx < fcn.blocks.size(); blockIdx++) {
                auto& block = fcn.blocks[blockIdx];
                if (block.terminator.isDead()) {
                    continue;
                }
                for (auto& stmt : block.statements) {
                    state.setCurStmt(blockIdx, (&stmt - &block.statements.front()));
                    DEBUG(state << stmt);
                    {
                        optVisitMirLvaluesMut(stmt, cb);
                    }
                }
                state.setCurStmtTerm(blockIdx);
                optVisitMirLvaluesMut(block.terminator, cb);
            }
            MIR_ASSERT(state, replaced > oldReplaced, "Temporary eliminations didn't advance");
        }
        // Remove assignments of replaced values
        for (auto& block : fcn.blocks) {
            for (auto it = block.statements.begin(); it != block.statements.end();) {
                state.setCurStmt(&block - &fcn.blocks.front(), (it - block.statements.begin()));
                // If the statement was an assign of a replaced temporary, remove it.
                auto it2 = replacements.end();
                if (it->is_Assign() && (it2 = replacementsFind(it->as_Assign().dst)) != replacements.end()) {
                    DEBUG(state << "Delete " << *it);
                    it = block.statements.erase(it);
                } else {
                    MIR_ASSERT(state, !(it->is_Assign() && it->as_Assign().src.isDead()), "");
                    ++it;
                }
            }
        }
        replacementHappend = (replaced > 0);
    }
    // --- Eliminate `... = Use(tmp)` (propagate lvalues upwards)
    {
        DEBUG("- Move upwards");
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
                // ^^^  `tmp[1:1] = some_rvalue`

                // Find where it's used
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
                    // `... = Use(to_replace_lval)`

                    // TODO: Ensure that the target isn't borrowed.
                    if (newDstLval.is_Local()) {
                        const auto& vu = valUses.localUses[newDstLval.as_Local()];
                        if (!(vu.read == 1 && vu.write == 1 && vu.borrow == 0)) {
                            break;
                        }
                    } else if (newDstLval.is_Return()) {
                        // Return, can't be borrowed?
                    } else {
                        break;
                    }

                    // Ensure that the target doesn't change in the intervening time.
                    bool wasInvalidated = false;
                    for (auto it3 = it + 1; it3 != it2; it3++) {
                        // Closure returns `true` if the passed lvalue is a component of `new_dst_lval`
                        auto isLvalueInVal = [&](const auto& lv) {
                            // Don't care about indexing?
                            return lv.root == newDstLval.root;
                        };
                        if (optVisitMirLvalues(*it3, [&](const auto& lv, auto) {
                            return isLvalueInVal(lv);
                        })) {
                            wasInvalidated = true;
                            break;
                        }
                    }

                    // Replacement is valid.
                    if (!wasInvalidated) {
                        DEBUG(state << "Replace assignment of " << toReplaceLval << " with " << newDstLval);
                        it->as_Assign().dst = mv$(it2->as_Assign().dst);
                        block.statements.erase(it2);
                        replacementHappend = true;
                        break;
                    }
                }
            }
        }
    }

    // --- Function returns (reverse propagate)
    // > Find `tmp = <function call>` where the temporary is used 1:1
    // > Search the following block for `<anything> = Use(this_tmp)`
    // > Ensure that the target of the above assignment isn't used in the intervening statements
    // > Replace function call result value with target of assignment
    {
        DEBUG("- Returns");
        for (auto& block : fcn.blocks) {
            if (block.terminator.isDead()) {
                continue;
            }

            // If the terminator is a call that writes to a 1:1 value, replace the destination value with the eventual destination (if that value isn't used in the meantime)
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

                // Iterate the target block, looking for where this value is used.
                const MIRLValue* newDst = nullptr;
                auto& blk2 = fcn.blocks.at(e.retBlock);
                for (const auto& stmt : blk2.statements) {
                    // Find `RValue::Use( this_lvalue )`
                    if (stmt.is_Assign() && stmt.as_Assign().src.is_Use() && stmt.as_Assign().src.as_Use() == e.retVal) {
                        newDst = &stmt.as_Assign().dst;
                        break;
                    }
                }

                // Ensure that the new destination value isn't used before assignment
                if (newDst) {
                    auto lvalueImpactsDst = [&](const MIRLValue& lv) -> bool {
                        // Returns true if the two lvalues share a common root
                        // TODO: Could restrict based on the presence of deref/field accesses?
                        // If `lv` is a local AND matches the index in `new_dst`, check for indexing
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
                            DEBUG(state << "- Replace function return " << e.retVal << " with " << *newDst);
                            e.retVal = newDst->clone();
                            // TODO: Invalidate the entry, instead of deleting?
                            it = blk2.statements.erase(it);
                            replacementHappend = true;
                            break;
                        }
                        if (optVisitMirLvalues(stmt, [&](const MIRLValue& lv, MIRValUsage vu) {
                            return lv == *newDst || (vu == MIRValUsage::Write && lvalueImpactsDst(lv));
                        })) {
                            break;
                        }
                    }
                }
            }
        }
    }

    // Locate values that are written, but not read or borrowed
    // - Current implementation requires a single write (to avoid issues with drop)
    // - if T: Drop (or T: !Copy) then the write should become a drop
    {
        DEBUG("- Write-only");
        for (auto& block : fcn.blocks) {
            for (auto it = block.statements.begin(); it != block.statements.end(); ++it) {
                state.setCurStmt(&block - &fcn.blocks.front(), it - block.statements.begin());
                if (const auto& se = it->opt_Assign()) {
                    // Remove No-op assignments (assignment from a lvalue to itself)
                    if (const auto* srcE = se->src.opt_Use()) {
                        if (se->dst == *srcE) {
                            DEBUG(state << se->dst << " set to itself, removing write");
                            it = block.statements.erase(it) - 1;
                            continue;
                        }
                    }

                    // Remove assignments of locals that are never read
                    if (se->dst.is_Local()) {
                        const auto& vu = valUses.localUses[se->dst.as_Local()];
                        if (vu.write == 1 && vu.read == 0 && vu.borrow == 0) {
                            DEBUG(state << se->dst << " only written, removing write");
                            it = block.statements.erase(it) - 1;
                        }
                    }
                }
            }
            // NOTE: Calls can write values, but they also have side-effects
        }
    }

    // TODO: Run special case replacements for when there's `tmp/var = arg` and `rv = tmp/var`

    return replacementHappend;
}

// ----------------------------------------
// Clear all drop flags that are never read
// ----------------------------------------
bool MIROptimiseDeadDropFlags(MIRTypeResolve& state, MIRFunction& fcn) {
    bool removedStatement = false;
    TRACE_FUNCTION_FR("", removedStatement);
    ::std::vector<bool> usedDropFlags(fcn.dropFlags.size());
    {
        ::std::vector<bool> readDropFlags(fcn.dropFlags.size());
        visitBlocks(state, fcn, [&readDropFlags, &usedDropFlags](auto, const MIRBasicBlock& block) {
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
        });
        DEBUG("Un-read drop flags:" << FMT_CB(ss, for (size_t i = 0; i < readDropFlags.size(); i++) if (!readDropFlags[i] && usedDropFlags[i]) ss << " " << i;));
        visitBlocksMut(state, fcn, [&readDropFlags, &removedStatement](auto _id, auto& block) {
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
        });
    }

    // Find any drop flags that are never assigned with a value other than their default, then remove those dead assignments.
    {
        ::std::vector<bool> editedDropFlags(fcn.dropFlags.size());
        visitBlocks(state, fcn, [&editedDropFlags, &fcn](auto, const MIRBasicBlock& block) {
            for (const auto& stmt : block.statements) {
                if (const auto* e = stmt.opt_SetDropFlag()) {
                    if (e->other != ~0u) {
                        // If the drop flag is set based on another, assume it's changed
                        editedDropFlags[e->idx] = true;
                    } else if (e->newVal != fcn.dropFlags[e->idx]) {
                        // If the new value is not the default, it's changed
                        editedDropFlags[e->idx] = true;
                    } else {
                        // Set to the default, doesn't change the 'edited' state
                    }
                }
            }
        });
        DEBUG("Un-edited drop flags:" << FMT_CB(ss, for (size_t i = 0; i < editedDropFlags.size(); i++) if (!editedDropFlags[i] && usedDropFlags[i]) ss << " " << i;));
        visitBlocksMut(state, fcn, [&editedDropFlags, &removedStatement, &fcn](auto _id, auto& block) {
            for (auto it = block.statements.begin(); it != block.statements.end();) {
                // If this is a SetDropFlag and the target flag isn't edited, remove
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
        });
    }

    return removedStatement;
}

// --------------------------------------------------------------------
// Remove unread assignments of locals (and replaced assignments of anything?)
// --------------------------------------------------------------------
bool MIROptimiseDeadAssignments(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // Find any locals that are never read, and delete their assignments.

    // Per-local flag indicating that the particular local is read.
    ::std::vector<bool> readLocals(fcn.locals.size());
    ::std::vector<bool> droppedLocals(fcn.locals.size());
    for (const auto& bb : fcn.blocks) {
        auto cb = [&](const MIRLValue& lv, MIRValUsage vu) {
            if (lv.root.is_Local()) {
                readLocals[lv.root.as_Local()] = true;
            }
            for (const auto& w : lv.wrappers) {
                if (w.is_Index()) {
                    readLocals[w.as_Index()] = true;
                }
            }
            return false;
        };
        for (const auto& stmt : bb.statements) {
            // If the assignment is to a local, then just consider the source (the target is writing to a local)
            if (stmt.is_Assign() && stmt.as_Assign().dst.is_Local()) {
                optVisitMirLvalues(stmt.as_Assign().src, cb);
            }
            // For other statment types (e.g. asm) - record anything
            else {
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

            // Not an assignment, ignore
            if (!(it->is_Assign() && it->as_Assign().dst.is_Local())) {
                ++it;
                continue;
            }
            auto idx = it->as_Assign().dst.as_Local();
            // Local was read, ignore it
            if (readLocals[idx]) {
                ++it;
                continue;
            }
            // If the local was dropped, then ignore IF it's not a borrow (TODO: Only if there's drop glue?)
            if (droppedLocals[idx] && !fcn.locals[idx]->is_Borrow()) {
                ++it;
                continue;
            }
            // Remove the assignment, as it's unused
            DEBUG(state << "Unread assignment, remove - " << *it);
            it = bb.statements.erase(it);
            changed = true;
        }
        if (auto* drop = bb.terminator.opt_Drop(); drop && drop->slot.is_Local()) {
            auto idx = drop->slot.as_Local();
            if (!readLocals[idx] && fcn.locals[idx]->is_Borrow()) {
                auto target = drop->target;
                DEBUG(state << "Drop of unread value, replace with Goto(bb" << target << ")");
                bb.terminator = MIRTerminator::make_Goto(target);
                changed = true;
            }
        }
    }

    // Locate assignments of locals then find the next assignment or read.
    return changed;
}

// --------------------------------------------------------------------
// Eliminate no-operation assignments that may have appeared
// --------------------------------------------------------------------
bool MIROptimiseNoopRemoval(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    HIRTypeRef tmpTy;
    // Remove useless operations
    for (auto& bb : fcn.blocks) {
        // Multi-statement no-ops (round-trip casts, reboorrow+cast)
        for (auto it = bb.statements.begin(); it != bb.statements.end(); ++it) {
            state.setCurStmt(&bb - fcn.blocks.data(), it - bb.statements.begin());
            // `_0 = &mut *foo`, then `_1 = _0 as *mut T` where `foo: *mut T`
            // - Note: Accepts `_0 = &*foo; _1 = _0 as T` where `foo: T`
            if (it->is_Assign() && it->as_Assign().dst.is_Local() && it->as_Assign().src.is_Borrow() && it->as_Assign().src.as_Borrow().val.is_Deref()) {
                const auto& dstLv = it->as_Assign().dst;
                auto srcLv = it->as_Assign().src.as_Borrow().val.cloneUnwrapped();
                // Find the next use of this target lvalue
                for (auto it2 = it + 1; it2 != bb.statements.end(); ++it2) {
                    // If it's a cast back to the original type, then replace with a direct assignment of the original value
                    if (it2->is_Assign() && it2->as_Assign().src.is_Cast() && it2->as_Assign().src.as_Cast().val == dstLv) {
                        const auto& dstTy = it2->as_Assign().src.as_Cast().type;
                        HIRTypeRef tmp;
                        const auto& origTy = state.getLvalueType(tmp, srcLv);
                        if (origTy == dstTy) {
                            DEBUG(state << "Reborrow and cast back - " << *it << " and " << *it2);
                            it2->as_Assign().src = std::move(srcLv);
                            break;
                        }
                    }
                    if (checkInvalidatesLvalue(*it2, srcLv, false)) {
                        break;
                    }
                }
            }

            // `_0 = foo as *const T; _1 = _0 as *mut T` where `foo: *mut T`
            // - Note: Accepts `_0 = foo as *const T; _1 = _0 as U` where `foo: U`
            if (it->is_Assign() && it->as_Assign().dst.is_Local() && it->as_Assign().src.is_Cast() && it->as_Assign().src.as_Cast().type->is_Pointer()) {
                const auto& dstLv = it->as_Assign().dst;
                const auto& srcLv = it->as_Assign().src.as_Cast().val;
                // Find the next use of this target lvalue
                for (auto it2 = it + 1; it2 != bb.statements.end(); ++it2) {
                    // If it's a cast back to the original type, then replace with a direct assignment of the original value
                    if (it2->is_Assign() && it2->as_Assign().src.is_Cast() && it2->as_Assign().src.as_Cast().val == dstLv) {
                        const auto& dstTy = it2->as_Assign().src.as_Cast().type;
                        HIRTypeRef tmp;
                        const auto& origTy = state.getLvalueType(tmp, srcLv);
                        if (origTy == dstTy) {
                            DEBUG(state << "Round-trip pointer cast - " << *it << " and " << *it2);
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

            // Placeholder: Asm block with empty template and no inputs/outputs/flags
            if (*it == MIRStatement::make_Asm({})) {
                DEBUG(state << "Empty ASM placeholder, remove - " << *it);
                it = bb.statements.erase(it);
                changed = true;

                continue;
            }

            // `Value = Use(Value)`
            if (it->is_Assign() && it->as_Assign().src.is_Use() && it->as_Assign().src.as_Use() == it->as_Assign().dst) {
                DEBUG(state << "Useless assignment, remove - " << *it);
                it = bb.statements.erase(it);
                changed = true;

                continue;
            }

            // Unit has a single value, so retaining a local read cannot carry
            // any information.  Canonicalising it also handles compiler-built
            // bodies such as `custom_mir`, where the macro's synthetic RET
            // binding is intentionally left without a Rust assignment.
            if (it->is_Assign() && it->as_Assign().src.is_Use() && state.getLvalueType(tmpTy, it->as_Assign().src.as_Use()) == state.crate.types.unit()) {
                DEBUG(state << "Replace unit local with the canonical value - " << *it);
                it->as_Assign().src = MIRRValue::make_Tuple({});
                changed = true;

                ++it;
                continue;
            }

            // `Value = Borrow(Deref(Value))`
            if (it->is_Assign() && it->as_Assign().src.is_Borrow() && it->as_Assign().src.as_Borrow().val.is_Deref() && it->as_Assign().src.as_Borrow().val.cloneUnwrapped() == it->as_Assign().dst) {
                DEBUG(state << "Useless assignment (v = &*v), remove - " << *it);
                it = bb.statements.erase(it);
                changed = true;

                continue;
            }

            // Cast to the same type
            if (it->is_Assign() && it->as_Assign().src.is_Cast() && it->as_Assign().src.as_Cast().type == state.getLvalueType(tmpTy, it->as_Assign().src.as_Cast().val)) {
                DEBUG(state << "No-op cast, replace with assignment - " << *it);
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
            DEBUG(state << "Drop of Copy type, replace with Goto(bb" << target << ")");
            bb.terminator = MIRTerminator::make_Goto(target);
            changed = true;
        }
    }

    return changed;
}

// --------------------------------------------------------------------
// If the first statement of a block is an assignment from a local, and all sources of that block assign to that local
// - Move the assigment backwards
// --------------------------------------------------------------------
bool MIROptimiseGotoAssign(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // 1. Locate blocks that start with an elligable assignemnt
    // - Target must be "simple" (not a static, no wrappers)
    // - Source can be any lvalue? Restrict to locals for now (static/deref assignment is a side-effect)
    //   > Restrict to single-read locals? Or replace the trigger statement with a reversed copy?
    // 2. Check all source blocks, and see if they assign to that block
    // > Terminator must be: GOTO, or CALL <lv> = ... (with the non-panic arm)
    // 3. If more than half the source blocks assign the source, then move up
    // - Any IF/SWITCH/... terminator blocks the optimisation

    // Precompute per-local read/borrow counts in a single pass over the whole
    // function. The eligibility check below needs the read count of one local
    // per candidate block; scanning every lvalue afresh for each candidate made
    // this pass O(n^2) in function size (dominant cost on large functions).
    // Applying the optimisation never changes another candidate's source count
    // (an eligible source is read exactly once, so no two candidates share one),
    // so a single snapshot stays valid for the whole block loop.
    // Likewise, map each block to its predecessors once instead of rescanning
    // every terminator per candidate. Edges are recorded with the multiplicity
    // `visitTerminatorTarget` reports them, matching the per-candidate scan this
    // replaces. The rewrite below only retargets assignment destinations and
    // call return values, never terminator targets, so this stays valid.
    ::std::vector<::std::vector<unsigned>> blockPreds(fcn.blocks.size());
    for (const auto& srcBb : fcn.blocks) {
        unsigned srcIdx = &srcBb - fcn.blocks.data();
        struct CollectPreds final: public MIRTargetVisitor {
            ::std::vector<::std::vector<unsigned>>& blockPreds;
            unsigned srcIdx;

            CollectPreds(::std::vector<::std::vector<unsigned>>& blockPreds, unsigned srcIdx)
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

    ::std::vector<unsigned> localReads(fcn.locals.size(), 0);
    ::std::vector<unsigned> localBorrows(fcn.locals.size(), 0);
    optVisitMirLvalues(state, fcn, [&](const auto& lv, auto vu) {
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
    });

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
        // Source must be a single-read local (so this assignment can be
        // deleted). Counts come from the whole-function snapshot above.
        unsigned nRead = localReads[src.as_Local()];
        unsigned nBorrow = localBorrows[src.as_Local()];
        if (nRead > 1 || nBorrow > 0) {
            DEBUG(state << "Source " << src << " is read " << nRead << " times and borrowed " << nBorrow);
            continue;
        }
        DEBUG(state << "Eligible assignment (" << stmt << ")");

        // Find source blocks, check terminators/last (predecessors precomputed)
        std::vector<unsigned> sources;
        unsigned numUsed = 0;
        const auto& preds = blockPreds[state.getCurBlock()];
        for (size_t predI = 0; predI < preds.size(); predI++) {
            unsigned bbIdx = preds[predI];
            const auto& srcBb = fcn.blocks[bbIdx];
            // One entry per edge, as the previous per-terminator scan produced.
            sources.push_back(bbIdx);
            // ... but only inspect each source block once.
            if (predI > 0 && preds[predI - 1] == bbIdx) {
                continue;
            }
            {
                switch (srcBb.terminator.tag()) {
                    case MIRTerminator::TAG_Goto: {
                        if (srcBb.statements.empty()) {
                            DEBUG(state << "BB" << bbIdx << " empty");
                        } else if ((srcBb.statements.back().is_Assign() && (srcBb.statements.back().as_Assign().dst == src))) {
                            DEBUG("BB" << bbIdx << "/" << srcBb.statements.size() << " " << srcBb.statements.back());
                            numUsed += 1;
                        } else {
                            DEBUG("BB" << bbIdx << "/" << srcBb.statements.size() << " " << srcBb.statements.back() << " - Doesn't write");
                        }
                        break;
                    }
                    case MIRTerminator::TAG_Call: {
                        auto& e = srcBb.terminator.as_Call();
                        if (e.retBlock != state.getCurBlock()) {
                            DEBUG(state << "BB" << bbIdx << "/TERM " << srcBb.terminator << " - Not return block");
                        } else if (e.retVal != src) {
                            DEBUG(state << "BB" << bbIdx << "/TERM " << srcBb.terminator << " - Doesn't write to source");
                        } else {
                            numUsed += 1;
                        }
                        break;
                    }
break;
                    default:
                        DEBUG(state << "BB" << bbIdx << "/TERM " << srcBb.terminator << " - Wrong terminator type");
                        break;
                }
            }
        }

        // TODO: Allow if one arm doesn't update?
        // - What if a call invalidates the target?
        if (numUsed < sources.size()) {
            DEBUG(state << "- Not all sources set the value");
            continue;
        }

        changed = true;

        // Time to edit.
        // 1. Update all sources
        for (auto bbIdx : sources) {
            auto& srcBb = fcn.blocks[bbIdx];

            if ((srcBb.terminator.is_Call() && (srcBb.terminator.as_Call().retVal == src))) {
                DEBUG("- Source block: BB" << bbIdx << " - term " << srcBb.terminator);
                srcBb.terminator.as_Call().retVal = dst.clone();
            } else if (!srcBb.statements.empty() && (srcBb.statements.back().is_Assign() && (srcBb.statements.back().as_Assign().dst == src))) {
                DEBUG("- Source block: BB" << bbIdx << " - tail " << srcBb.statements.back());
                srcBb.statements.back().as_Assign().dst = dst.clone();
            } else {
                MIR_TODO(state, "Handle copying assignment to source");
            }
            if (!srcBb.statements.empty()) {
                DEBUG("+- BB" << bbIdx << "/" << (srcBb.statements.size() - 1) << " " << srcBb.statements.back());
            }
            DEBUG("+- BB" << bbIdx << "/TERM " << srcBb.terminator);
        }
        // IF the value is `Copy` (i.e. the initial assignment could be expected to survive), then reverse the destination
        // - Can't do this, it's going to cause infinite recursion!
        if (false && state.lvalueIsCopy(dst)) {
            auto d = dst.clone();
            dst = mv$(src);
            src = mv$(d);
            DEBUG(state << "- Updated (" << stmt << ")");
        } else {
            stmt = MIRStatement();
            DEBUG(state << "- Deleted");
        }
    }

    return changed;
}

// --------------------------------------------------------------------
// Find re-borrows of values that aren't otherwise used.
// - Look for `<local> = &[mut] *<local/arg>`
// - Check if the source is only ever used here (and in a drop)
// - If that's the case, replace usage with a move and delete the drop
// TODO: Could allow multiple uses if it's a shared borrow
// --------------------------------------------------------------------
bool MIROptimiseUselessReborrows(MIRTypeResolve& state, MIRFunction& fcn) {
    bool changed = false;
    TRACE_FUNCTION_FR("", changed);

    // TODO: This doesn't work if the assignment happens in a loop (can lead to multiple moves)
    // - Need to have a way of knowing if a block is a loop member

    return changed;
}

// --------------------------------------------------------------------
// Clear all unused blocks
// --------------------------------------------------------------------
bool MIROptimiseGarbageCollectPartial(MIRTypeResolve& state, MIRFunction& fcn) {
    bool rv = false;
    TRACE_FUNCTION_FR("", rv);
    ::std::vector<bool> visited(fcn.blocks.size());
    visitBlocks(state, fcn, [&visited](auto bb, const auto& /*block*/) {
        assert(!visited[bb]);
        visited[bb] = true;
    });
    for (unsigned int i = 0; i < visited.size(); i++) {
        auto& blk = fcn.blocks[i];
        if (blk.terminator.is_Incomplete() && blk.statements.empty()) {
        } else if (visited[i]) {
        } else {
            DEBUG("CLEAR bb" << i);
            blk.statements.clear();
            blk.terminator = MIRTerminator::make_Incomplete({});
            rv = true;
        }
    }
    return rv;
}

// --------------------------------------------------------------------
// Remove all unused temporaries and blocks
// --------------------------------------------------------------------
bool MIROptimiseGarbageCollect(MIRTypeResolve& state, MIRFunction& fcn) {
    ::std::vector<bool> usedLocals(fcn.locals.size());
    ::std::vector<bool> usedDfs(fcn.dropFlags.size());
    ::std::vector<bool> visited(fcn.blocks.size());

    visitBlocks(state, fcn, [&](auto bb, const auto& block) {
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
            }
            //else if( const auto* e = stmt.opt_Drop() )
            //{
            //    //if( e->flag_idx != ~0u )
            //    //    used_dfs.at(e->flag_idx) = true;
            //}
            else if (const auto* e = stmt.opt_Asm()) {
                for (const auto& val : e->outputs) {
                    assignedLval(val.second);
                }
            }
            else if (const auto* e = stmt.opt_Asm2()) {
                for (const auto& p : e->params) {
                    if (p.is_Reg() && p.as_Reg().output) {
                        assignedLval(*p.as_Reg().output);
                    }
                }
            }
            else if (const auto* e = stmt.opt_SetDropFlag()) {
                if (e->other != ~0u) {
                    usedDfs.at(e->other) = true;
                }
                usedDfs.at(e->idx) = true;
            }
            else if (const auto* e = stmt.opt_LoadDropFlag()) {
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
    });

    ::std::vector<unsigned int> localRewriteTable;
    unsigned int nLocals = fcn.locals.size();
    for (unsigned int i = 0, j = 0; i < nLocals; i++) {
        if (!usedLocals[i]) {
            fcn.locals.erase(fcn.locals.begin() + j);
        } else {
            DEBUG("_" << i << " => _" << j);
        }
        localRewriteTable.push_back(usedLocals[i] ? j++ : ~0u);
    }
    DEBUG("Deleted Locals:" << FMT_CB(ss, for (auto run : runs(usedLocals)) if (!usedLocals[run.first]) {
              ss << " " << run.first;
              if (run.second != run.first) {
                  ss << "-" << run.second;
              }
          }));
    ::std::vector<unsigned int> dfRewriteTable;
    unsigned int nDf = fcn.dropFlags.size();
    for (unsigned int i = 0, j = 0; i < nDf; i++) {
        if (!usedDfs[i]) {
            DEBUG("GC df" << i);
            // NOTE: Not erased until after rewriting
        }
        dfRewriteTable.push_back(usedDfs[i] ? j++ : ~0u);
    }

    auto it = fcn.blocks.begin();
    for (unsigned int i = 0; i < visited.size(); i++) {
        if (visited[i]) {
            auto lvalueCb = [&](MIRLValue& lv, auto) {
                if (lv.root.is_Local()) {
                    auto e = lv.root.as_Local();
                    MIR_ASSERT(state, e < localRewriteTable.size(), "Variable out of range - " << lv);
                    // If the table entry for this variable is !0, it wasn't marked as used
                    MIR_ASSERT(state, localRewriteTable.at(e) != ~0u, "LValue " << lv << " incorrectly marked as unused");
                    lv.root = MIRLValue::Storage::newLocal(localRewriteTable.at(e));
                }
                for (auto& w : lv.wrappers) {
                    if (w.is_Index()) {
                        w = MIRLValue::Wrapper::newIndex(localRewriteTable.at(w.as_Index()));
                    }
                }
                return false;
            };
            ::std::vector<bool> toRemoveStatements(it->statements.size());
            for (auto& stmt : it->statements) {
                auto stmtIdx = &stmt - &it->statements.front();
                state.setCurStmt(i, stmtIdx);

                if (stmt == MIRStatement()) {
                    DEBUG(state << "Remove " << stmt << " - Pure default");
                    toRemoveStatements[stmtIdx] = true;
                    continue;
                }

                optVisitMirLvaluesMut(stmt, lvalueCb);
                if (auto* se = stmt.opt_SetDropFlag()) {
                    // Rewrite drop flag indexes OR delete
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
                        DEBUG(state << "Delete ScopeEnd (now empty)");
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

            // Delete all statements flagged in a bitmap for deletion
            assert(it->statements.size() == toRemoveStatements.size());
            auto newEnd = ::std::remove_if(it->statements.begin(), it->statements.end(), [&](const auto& s) {
                size_t stmtIdx = (&s - &it->statements.front());
                return toRemoveStatements[stmtIdx];
            });
            it->statements.erase(newEnd, it->statements.end());
        }
        ++it;
    }

    // Removing a Drop terminator also removes its unwind edge.  Recompute block
    // reachability after all such rewrites, otherwise the detached cleanup
    // subgraph is retained and can be sorted ahead of the real entry block.
    visited.assign(fcn.blocks.size(), false);
    visitBlocks(state, fcn, [&](auto bb, const auto&) {
        visited[bb] = true;
    });

    ::std::vector<unsigned int> blockRewriteTable;
    for (unsigned int i = 0, j = 0; i < fcn.blocks.size(); i++) {
        blockRewriteTable.push_back(visited[i] ? j++ : ~0u);
    }
    for (unsigned int i = 0; i < fcn.blocks.size(); i++) {
        if (!visited[i]) {
            continue;
        }
        struct ApplyRewriteTable final: public MIRTargetVisitorMut {
            const MIRTypeResolve& state;
            const ::std::vector<unsigned int>& blockRewriteTable;

            ApplyRewriteTable(const MIRTypeResolve& state, const ::std::vector<unsigned int>& blockRewriteTable)
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

    auto newBlocksEnd = ::std::remove_if(fcn.blocks.begin(), fcn.blocks.end(), [&](const auto& bb) {
        size_t i = &bb - &fcn.blocks.front();
        if (!visited[i]) {
            DEBUG("GC bb" << i);
        }
        return !visited[i];
    });
    fcn.blocks.erase(newBlocksEnd, fcn.blocks.end());

    // Drop flags use vector<bool> proxy storage, so erase them by original and compacted index.
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

/// Sort basic blocks to approximate program flow (helps when reading MIR)
void MIRSortBlocks(const StaticTraitResolve& resolve, const HIRItemPath& path, MIRFunction& fcn) {
    ::std::vector<bool> visited(fcn.blocks.size());
    ::std::vector<::std::pair<unsigned, unsigned>> depths(fcn.blocks.size());

    struct Todo {
        size_t bbIdx;
        unsigned branchCount;
        unsigned level;
    };

    unsigned int branches = 0;
    ::std::vector<Todo> todo;
    todo.push_back(Todo{0, 0, 0});

    while (!todo.empty()) {
        auto info = todo.back();
        todo.pop_back();
        if (visited[info.bbIdx]) {
            continue;
        }

        visited[info.bbIdx] = true;
        depths[info.bbIdx] = ::std::make_pair(info.branchCount, info.level);
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
                todo.push_back(Todo{te.bbTrue, ++branches, info.level + 1}); todo.push_back(Todo{te.bbFalse, ++branches, info.level + 1});
                break;
            }
            case MIRTerminator::TAG_Switch: {
                auto& te = bb.terminator.as_Switch();
                for (auto dst : te.targets) todo.push_back(Todo{dst, ++branches, info.level + 1}); if (te.validFlag != ~0u) todo.push_back(Todo{te.invalidTarget, ++branches, info.level + 1});
                break;
            }
            case MIRTerminator::TAG_SwitchValue: {
                auto& te = bb.terminator.as_SwitchValue();
                for (auto dst : te.targets) todo.push_back(Todo{dst, ++branches, info.level + 1}); todo.push_back(Todo{te.defTarget, info.branchCount, info.level + 1});
                break;
            }
            case MIRTerminator::TAG_Drop: {
                auto& te = bb.terminator.as_Drop();
                todo.push_back(Todo{te.target, info.branchCount, info.level + 1}); if (te.unwind.is_Cleanup()) {
                    auto& target = te.unwind.as_Cleanup();
                    todo.push_back(Todo{target, ++branches, info.level + 1});
                }
                break;
            }
            case MIRTerminator::TAG_Call: {
                auto& te = bb.terminator.as_Call();
                todo.push_back(Todo{te.retBlock, info.branchCount, info.level + 1}); if (te.unwind.is_Cleanup()) {
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
                if (te.retBlock != ~0u) todo.push_back(Todo{te.retBlock, info.branchCount, info.level + 1}); for (const auto& p : te.params) if (const auto* dst = p.opt_Label()) todo.push_back(Todo{*dst, ++branches, info.level + 1});
                break;
            }
        }
    }

    // Sort a list of block indexes by `depths`
    ::std::vector<size_t> idxes;
    idxes.reserve(fcn.blocks.size());
    for (size_t i = 0; i < fcn.blocks.size(); i++) {
        idxes.push_back(i);
    }
    ::std::sort(idxes.begin(), idxes.end(), [&](auto a, auto b) {
        return depths.at(a) < depths.at(b);
    });

    DEBUG(idxes);

    decltype(fcn.blocks) newBlockList;
    newBlockList.reserve(fcn.blocks.size());
    for (auto idx : idxes) {
        auto fixBbIdx = [&](auto idx) {
            return ::std::find(idxes.begin(), idxes.end(), idx) - idxes.begin();
        };
        newBlockList.push_back(mv$(fcn.blocks[idx]));
        newBlockList.back().statements.shrink_to_fit(); // Save some memory
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
    MIROuterVisitor ov{wb, crate, [optLevel, enableInlining](const auto& res, const auto& p, auto& expr, const auto& args, const auto& ty) {
        //}
        auto& mir = expr.getMirOrErrorMut(Span());
        if (optLevel == 0) {
            MIROptimiseMin(res, p, mir, args, ty);
        } else {
            // The crate driver validates after this optimisation and its final cleanup.
            // Preserve explicitly requested diagnostic checks inside the optimiser.
            MIROptimise(res, p, mir, args, ty, optLevel, enableInlining, /*validate=*/getenv("TRUSTME_MIR_CHECK") != nullptr);
        }
        // Run cleanup to handle now-monomoprhised inlined constants
        MIRCleanup(res, p, mir, args, ty);
    }};
    ov.visitCrate(crate);
}

void MIROptimiseCrateInlining(const WireBoard& wb, const HIRCrate& crate, TransList& list, bool postSave, unsigned optLevel, bool enableInlining) {
    TRACE_FUNCTION;

    ::StaticTraitResolve resolve{wb};

    // If running after HIR has been serialised, we can eliminate calls to `const_eval_select` without
    // impacting constant evaluation in downstream crates
    if (postSave) {
        // Visit every function in the monomorph list and raplce `const_eval_select` calls with calls to the runtime function
        for (auto& fcnEnt : list.functions) {
            auto& hirFcn = *const_cast<HIRFunction*>(fcnEnt.second->ptr);
            MIRFunction* fcnP;
            if (fcnEnt.second->monomorphised.code) {
                DEBUG("Generic: " << fcnEnt.first);
                fcnP = &*fcnEnt.second->monomorphised.code;
            } else if (hirFcn.code.mir) {
                DEBUG("Concrete: " << fcnEnt.first);
                fcnP = &hirFcn.code.getMirOrErrorMut(Span());
            } else {
                // Ignore, this is an external function reference.
                DEBUG("External: " << fcnEnt.first);
                continue;
            }

            auto& fcn = *fcnP;
            for (auto& block : fcn.blocks) {
                if (auto* te = block.terminator.opt_Call()) {
                    if (te->fcn.is_Intrinsic() && te->fcn.as_Intrinsic().name == "const_eval_select") {
                        size_t nArgs = te->fcn.as_Intrinsic().params.types.at(0)->as_Tuple().size();
                        const MIRLValue arg = te->args.at(0).as_LValue().clone();
                        // Note: arg 1 is the constant function
                        const HIRPath& fcnPath = *te->args.at(2).as_Constant().as_Function().p;

                        DEBUG(fcnPath);
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
    } else {
        for (const auto& fcn : list.functions) {
            DEBUG("FCN: " << fcn.first);
        }
    }

    if (!enableInlining) {
        return;
    }

    // rustc level 4 removes analysis limits. Preserve a finite cap for normal
    // level-3 inlining, while level 4+ runs this monotonic pass to its fixed point.
    const size_t maxIterations = optLevel >= 4 ? ::std::numeric_limits<size_t>::max() : 5;
    size_t numIterations = 0;
    bool didInlineOnPass;
    do {
        didInlineOnPass = false;

        for (auto& fcnEnt : list.functions) {
            const auto& path = fcnEnt.first;
            auto& hirFcn = *const_cast<HIRFunction*>(fcnEnt.second->ptr);
            auto& monoFcn = fcnEnt.second->monomorphised;

            ::std::string s = FMT(path);
            HIRItemPath ip(s);

            if (monoFcn.code) {
                didInlineOnPass |= MIROptimiseInline(resolve, ip, *monoFcn.code, monoFcn.argTys, monoFcn.retTy, list, optLevel);

                MIRCleanup(resolve, ip, *monoFcn.code, monoFcn.argTys, monoFcn.retTy);
            } else if (hirFcn.code) {
                auto& mir = hirFcn.code.getMirOrErrorMut(Span());
                bool didOpt = MIROptimiseInline(resolve, ip, mir, hirFcn.args, hirFcn.returnType, list, optLevel);
                mir.transEnumState = MIREnumCachePtr(); // Clear MIR enum cache
                didInlineOnPass |= didOpt;

                MIRCleanup(resolve, ip, mir, hirFcn.args, hirFcn.returnType);
            } else {
                // Extern, no optimisations
            }
        }
        numIterations += 1;
    } while (didInlineOnPass && numIterations < maxIterations);

    if (didInlineOnPass) {
        DEBUG("Stopped inlining after the level-specific maximum of " << maxIterations << " passes");
    }
}
