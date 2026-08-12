#include "mir_helpers.h"

#include "hir_hir.h"
#include "hir_type.h"
#include "mir_mir.h"
#include <algorithm> // ::std::find
#include "trans_target.h"

void ::MIR::TypeResolve::fmtPos(::std::ostream& os, bool includePath /*=false*/) const {
    if (includePath) {
        os << this->mPath << " ";
    }
    os << "BB" << this->bbIdx << "/";
    if (this->stmtIdx == STMT_TERM) {
        os << "TERM";
    } else {
        os << this->stmtIdx;
    }
    os << ": ";
}

void ::MIR::TypeResolve::printMsg(const char* tag, ::std::function<void(::std::ostream& os)> cb) const {
    auto& os = ::std::cerr;
    os << "MIR " << tag << ": ";
    fmtPos(os, true);
    cb(os);
    os << ::std::endl;
    abort();
    //throw CheckFailure {};
}

unsigned int ::MIR::TypeResolve::getCurStmtOfs() const {
    if (this->stmtIdx == STMT_TERM) {
        return fcn.blocks.at(this->bbIdx).statements.size();
    } else {
        return this->stmtIdx;
    }
}

const ::MIR::BasicBlock& ::MIR::TypeResolve::getBlock(::MIR::BasicBlockId id) const {
    MIR_ASSERT(*this, id < fcn.blocks.size(), "Block ID " << id << " out of range");
    return fcn.blocks[id];
}

const ::HIR::TypeData* ::MIR::TypeResolve::getStaticType(::HIR::TypeRef& tmp, const ::HIR::Path& path) const {
    if (path.mData.is_UfcsInherent() && path.mData.as_UfcsInherent().item == "#type_id") {
        tmp = crate.types.unit();
        return tmp;
    }
    MonomorphState ms(crate.types);
    auto v = mResolve.getValue(this->sp, path, ms, /*signature_only*/ true);
    MIR_ASSERT(*this, v.is_Static(), "LValue::Static not a static - " << path << " : " << v.tagStr());
    MIR_ASSERT(*this, v.as_Static(), "LValue::Static is null? - " << path << " : " << v.tagStr());
    if (ms.hasTypes()) {
        tmp = ms.monomorphType(sp, v.as_Static()->mType);
        mResolve.expandAssociatedTypes(this->sp, tmp);
        return tmp;
    } else {
        return v.as_Static()->mType;
    }
}

const ::HIR::TypeData* ::MIR::TypeResolve::getLvalueType(::HIR::TypeRef& tmp, const ::MIR::LValue& val, unsigned wrapperSkipCount /*=0*/) const {
    const ::HIR::TypeData* rv = nullptr;
    TU_MATCHA((val.root), (e), (Return, rv = monomorphedRettype ? monomorphedRettype : retType;), (Argument, MIR_ASSERT(*this, e < mArgs.size(), "Argument " << val << " out of range (" << mArgs.size() << ")"); rv = mArgs.at(e).second;), (Local, MIR_ASSERT(*this, e < fcn.locals.size(), "Local " << val << " out of range (" << fcn.locals.size() << ")"); rv = monomorphedLocals ? monomorphedLocals->at(e) : fcn.locals.at(e);), (Static, rv = getStaticType(tmp, e);))
    if (val.wrappers.size() > 0) {
        assert(wrapperSkipCount <= val.wrappers.size());
        const auto* stopWrapper = val.wrappers.data() + (val.wrappers.size() - wrapperSkipCount);
        for (const auto& w : val.wrappers) {
            if (&w == stopWrapper) {
                break;
            }
            rv = this->getUnwrappedType(tmp, w, rv);
        }
    } else {
        assert(wrapperSkipCount == 0);
    }
    return rv;
}

const ::HIR::TypeData* ::MIR::TypeResolve::getUnwrappedType(::HIR::TypeRef& tmp, const ::MIR::LValue::Wrapper& w, const ::HIR::TypeData* ty) const {
    TU_MATCH_HDRA( (w), {)
    TU_ARMA(Field, fieldIndex) {
        TU_MATCH_HDRA( ((*ty)), {)
        default:
            MIR_BUG(*this, "Field access on unexpected type - " << ty);
                // Array and Slice use LValue::Field when the index is constant and known-good
                TU_ARMA(Array, te) {
                    return te.inner;
                }
                TU_ARMA(Slice, te) {
                    return te.inner;
                }
                TU_ARMA(Tuple, te) {
                    MIR_ASSERT(*this, fieldIndex < te.size(), "Field index out of range in tuple " << fieldIndex << " >= " << te.size());
                    return te[fieldIndex];
                }
                TU_ARMA(Path, te) {
                    // TODO: Cache result (to avoid needing to re-monomorph)
                    if (const auto* tep = te.binding.opt_Struct()) {
                        const auto& str = **tep;
                        auto maybeMonomorph = [&](const auto& ty) {
                            return mResolve.monomorphExpandOpt(sp, tmp, ty, MonomorphStatePtr(crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr));
                        };
                        TU_MATCHA((str.mData), (se), (Unit, MIR_BUG(*this, "Field on unit-like struct - " << ty);), (Tuple, MIR_ASSERT(*this, fieldIndex < se.size(), "Field index out of range in tuple-struct " << te.path); return maybeMonomorph(se[fieldIndex].ent);), (Named, MIR_ASSERT(*this, fieldIndex < se.size(), "Field index out of range in struct " << te.path); return maybeMonomorph(se[fieldIndex].ty);))
                    } else if (const auto* tep = te.binding.opt_Union()) {
                        const auto& unm = **tep;
                        auto maybeMonomorph = [&](const ::HIR::TypeData* t) -> const ::HIR::TypeData* {
                            return mResolve.monomorphExpandOpt(sp, tmp, t, MonomorphStatePtr(crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr));
                        };
                        MIR_ASSERT(*this, fieldIndex < unm.mVariants.size(), "Field index out of range for union");
                        return maybeMonomorph(unm.mVariants.at(fieldIndex).ty);
                    } else {
                        MIR_BUG(*this, "Field access on invalid type - " << ty);
                    }
                }
        }
        }
        TU_ARMA(Deref, _e) {
        TU_MATCH_HDRA( ((*ty)), {)
        default:
            MIR_BUG(*this, "Deref on unexpected type - " << ty);
                TU_ARMA(Path, te) {
                    if (const auto* innerPtr = this->isTypeOwnedBox(ty)) {
                        return innerPtr;
                    } else {
                        MIR_BUG(*this, "Deref on unexpected type - " << ty);
                    }
                }
                TU_ARMA(Pointer, te) {
                    return te.inner;
                }
                TU_ARMA(Borrow, te) {
                    return te.inner;
                }
        }
        }
        TU_ARMA(Index, indexLocal) {
        TU_MATCH_HDRA( ((*ty)), { )
        default:
            MIR_BUG(*this, "Index on unexpected type - " << ty);
                TU_ARMA(Slice, te) {
                    return te.inner;
                }
                TU_ARMA(Array, te) {
                    return te.inner;
                }
        }
        }
        TU_ARMA(Downcast, variantIndex) {
        TU_MATCH_HDRA( ((*ty)), {)
        default:
            MIR_BUG(*this, "Downcast on unexpected type - " << ty);
                TU_ARMA(Path, te) {
                    MIR_ASSERT(*this, te.binding.is_Enum() || te.binding.is_Union(), "Downcast on non-Enum");
                    if (te.binding.is_Enum()) {
                        const auto& enm = *te.binding.as_Enum();
                        MIR_ASSERT(*this, enm.mData.is_Data(), "Downcast on non-data enum - " << ty);
                        const auto& variants = enm.mData.as_Data();
                        MIR_ASSERT(*this, variantIndex < variants.size(), "Variant index out of range for " << ty);
                        const auto& variant = variants[variantIndex];

                        const auto& varTy = variant.type;
                        return mResolve.monomorphExpandOpt(sp, tmp, varTy, MonomorphStatePtr(crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr));
                    } else {
                        const auto& unm = *te.binding.as_Union();
                        MIR_ASSERT(*this, variantIndex < unm.mVariants.size(), "Variant index out of range");
                        const auto& variant = unm.mVariants[variantIndex];
                        const auto& varTy = variant.ty;

                        return mResolve.monomorphExpandOpt(sp, tmp, varTy, MonomorphStatePtr(crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr));
                    }
                }
        }
        }
    }
    throw "";
}

const ::HIR::TypeData* MIR::TypeResolve::getParamType(::HIR::TypeRef& tmp, const ::MIR::Param& val) const {
    TU_MATCH_HDRA((val), {)
    TU_ARMA(LValue, e) {
            return getLvalueType(tmp, e);
        }
        TU_ARMA(Constant, e) {
            return tmp = getConstType(e);
        }
        TU_ARMA(Borrow, e) {
            ::HIR::TypeRef tmp2;
            return tmp = crate.types.borrow(e.type, getLvalueType(tmp2, e.val));
        }
    }
    throw "";
}

::HIR::TypeRef MIR::TypeResolve::getConstType(const ::MIR::Constant& c) const {
    TU_MATCH_HDRA( (c), {)
    TU_ARMA(Int, e) {
            return crate.types.primitive(e.t);
        }
        TU_ARMA(Uint, e) {
            return crate.types.primitive(e.t);
        }
        TU_ARMA(Float, e) {
            return crate.types.primitive(e.t);
        }
        TU_ARMA(Bool, e) {
            return crate.types.primitive(::HIR::CoreType::Bool);
        }
        TU_ARMA(Bytes, e) {
            return crate.types.borrow(::HIR::BorrowType::Shared, crate.types.array(crate.types.primitive(::HIR::CoreType::U8), e.size()));
        }
        TU_ARMA(StaticString, e) {
            return crate.types.borrow(::HIR::BorrowType::Shared, crate.types.primitive(::HIR::CoreType::Str));
        }
        TU_ARMA(Const, e) {
            MonomorphState p(crate.types);
            auto v = mResolve.getValue(this->sp, *e.p, p, /*signature_only=*/true);
            if (const auto* ve = v.opt_Constant()) {
                const auto& ty = (*ve)->mType;
                if (monomorphiseTypeNeeded(ty)) {
                    auto rv = p.monomorphType(this->sp, ty);
                    mResolve.expandAssociatedTypes(this->sp, rv);
                    return rv;
                } else {
                    return ty;
                }
            } else {
                MIR_BUG(*this, "get_const_type - Not a constant " << *e.p);
            }
        }
        TU_ARMA(Generic, e) {
            return mResolve.getConstParamType(this->sp, e.binding);
        }
        TU_ARMA(Function, e) {
            MonomorphState p(crate.types);
            auto v = mResolve.getValue(this->sp, *e.p, p, /*signature_only=*/true);
        TU_MATCH_HDRA( (v), {)
        default:
            MIR_BUG(*this, "get_const_type - Function points to bad type: " << v.tagStr() << " - " << c);
                TU_ARMA(NotFound, ve) {
                    MIR_BUG(*this, "get_const_type - ItemAddr points to unknown value - " << c);
                }
                TU_ARMA(Function, ve) {
                    return crate.types.intern(::HIR::TypeData::make_NamedFunction({e.p->clone(), ve}));
                }
                TU_ARMA(EnumConstructor, ve) {
                    return crate.types.intern(::HIR::TypeData::make_NamedFunction({e.p->clone(), ::HIR::TypeDataNamedFunctionTy::make_EnumConstructor({ve.e, ve.v})}));
                }
                TU_ARMA(StructConstructor, ve) {
                    return crate.types.intern(::HIR::TypeData::make_NamedFunction({e.p->clone(), ve.s}));
                }
        }
        }
        TU_ARMA(ItemAddr, e) {
            MonomorphState p(crate.types);
            ASSERT_BUG(sp, e, "get_const_type - " << c);
            auto v = mResolve.getValue(this->sp, *e, p, /*signature_only=*/true);
        TU_MATCH_HDRA( (v), {)
        TU_ARMA(NotFound, ve) {
                    MIR_BUG(*this, "get_const_type - ItemAddr points to unknown value - " << c);
                }
                TU_ARMA(NotYetKnown, ve) {
                    if (e->mData.is_UfcsKnown()) {
                        const auto& pe = e->mData.as_UfcsKnown();
                        if (pe.item == "vtable#" && pe.trait.mPath == HIR::SimplePath()) {
                            ::std::vector<HIR::TypeRef> fields;
                            fields.push_back(crate.types.primitive(HIR::CoreType::Usize));
                            fields.push_back(crate.types.primitive(HIR::CoreType::Usize));
                            fields.push_back(crate.types.primitive(HIR::CoreType::Usize));
                            return crate.types.borrow(HIR::BorrowType::Shared, crate.types.tuple(mv$(fields)));
                        }
                    }
                    MIR_BUG(*this, "get_const_type - get_value returned NotYetKnown with signature_only=true");
                }
                TU_ARMA(Constant, ve) {
                    const auto& ty = ve->mType;
                    HIR::TypeRef rv;
                    if (monomorphiseTypeNeeded(ty)) {
                        rv = p.monomorphType(this->sp, ty);
                        mResolve.expandAssociatedTypes(this->sp, rv);
                    } else {
                        rv = ty;
                    }
                    return crate.types.borrow(HIR::BorrowType::Shared, rv);
                }
                TU_ARMA(Static, ve) {
                    const auto& ty = ve->mType;
                    HIR::TypeRef rv;
                    if (monomorphiseTypeNeeded(ty)) {
                        rv = p.monomorphType(this->sp, ty);
                        mResolve.expandAssociatedTypes(this->sp, rv);
                    } else {
                        rv = ty;
                    }
                    return crate.types.borrow(HIR::BorrowType::Shared, rv);
                }
                TU_ARMA(Function, ve) {
                    auto rv = crate.types.function((::HIR::TypeData::Data_NamedFunction{e->clone(), ve}).decay(crate.types, this->sp));
                    mResolve.expandAssociatedTypes(this->sp, rv);
                    return rv;
                }
                TU_ARMA(EnumValue, ve) {
                    MIR_BUG(*this, "get_const_type - ItemAddr points to an enum value - " << c);
                }
                TU_ARMA(EnumConstructor, ve) {
                    auto rv = crate.types.function((::HIR::TypeData::Data_NamedFunction{e->clone(), ::HIR::TypeDataNamedFunctionTy::make_EnumConstructor({ve.e, ve.v})}).decay(crate.types, this->sp));
                    mResolve.expandAssociatedTypes(this->sp, rv);
                    return rv;
                }
                TU_ARMA(StructConstant, ve) {
                    MIR_BUG(*this, c << " pointing to a struct constant");
                }
                TU_ARMA(StructConstructor, ve) {
                    auto rv = crate.types.function((::HIR::TypeData::Data_NamedFunction{e->clone(), ve.s}).decay(crate.types, this->sp));
                    mResolve.expandAssociatedTypes(this->sp, rv);
                    return rv;
                }
        }
        }
    }
    throw "";
}

bool ::MIR::TypeResolve::lvalueIsCopy(const ::MIR::LValue& val) const {
    ::HIR::TypeRef tmp;
    return mResolve.typeIsCopy(this->sp, getLvalueType(tmp, val));
}

const ::HIR::TypeData* ::MIR::TypeResolve::isTypeOwnedBox(const ::HIR::TypeData* ty) const {
    return mResolve.isTypeOwnedBox(ty);
}

size_t MIR::TypeResolve::intrinsicOffsetOf(const ::HIR::TypeData* ty, const ::std::vector<MIR::Param>& values) const {
    const auto* curTy = ty;
    size_t baseOfs = 0;
    for (size_t i = 0; i < values.size(); i++) {
        MIR_ASSERT(*this, values[i].is_Constant(), "Arguments to `offset_of` must be constants");
        size_t idx = 0;
        TU_MATCH_HDRA( (values[i].as_Constant()), { )
        default:
            MIR_TODO(*this, "offset_of: field " << values[i]);
            TU_ARMA(Int, fieldIdx) {
                MIR_ASSERT(*this, fieldIdx.v.isI64() && fieldIdx.v >= S128(0), "Invalid tuple field index " << fieldIdx.v);
                idx = static_cast<size_t>(fieldIdx.v.truncateI64());
            }
            TU_ARMA(Uint, fieldIdx) {
                MIR_ASSERT(*this, fieldIdx.v.isU64() && fieldIdx.v <= U128(SIZE_MAX), "Invalid tuple field index " << fieldIdx.v);
                idx = static_cast<size_t>(fieldIdx.v.truncateU64());
            }
            TU_ARMA(StaticString, fieldName) {
                char* end = nullptr;
                auto numericIdx = ::std::strtoul(fieldName.c_str(), &end, 10);
                if (end != fieldName.c_str() && *end == '\0') {
                    MIR_ASSERT(*this, numericIdx <= SIZE_MAX, "Invalid tuple field index " << fieldName);
                    idx = static_cast<size_t>(numericIdx);
                } else if (const auto* tyPath = curTy->opt_Path()) {
                    if (const auto* bep = tyPath->binding.opt_Struct()) {
                        const auto& str = **bep;
                    TU_MATCH_HDRA((str.mData), {)
                    TU_ARMA(Named, fields) {
                                idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                                    return x.name == fieldName;
                                }) - fields.begin();
                            }
                            TU_ARMA(Tuple, fields) {
                                MIR_BUG(*this, "Named field on tuple struct: " << curTy << " ." << fieldName);
                            }
                            TU_ARMA(Unit, _) {
                                MIR_BUG(*this, "Empty struct: " << curTy << " ." << fieldName);
                            }
                    }
                    } else if (const auto* bep = tyPath->binding.opt_Union()) {
                        const auto& unm = **bep;
                        const auto& fields = unm.mVariants;
                        idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                            return x.name == fieldName;
                        }) - fields.begin();
                    } else if (const auto* bep = tyPath->binding.opt_Enum()) {
                        const auto& enm = **bep;
                        MIR_ASSERT(*this, enm.mData.is_Data(), "Non-Data enum: " << curTy << " ." << fieldName);
                        const auto& fields = enm.mData.as_Data();
                        idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                            return x.name == fieldName;
                        }) - fields.begin();
                    } else {
                        MIR_TODO(*this, "offset_of: named field/variant - " << fieldName);
                    }
                } else {
                    MIR_TODO(*this, "offset_of: named field/variant - " << fieldName);
                }
            }
        }
        auto* repr = TargetGetTypeRepr(this->sp, mResolve, curTy);
        if(!repr) {
            MIR_BUG(*this, "Calling `offset_of!` on type with non-defined repr: " << curTy);
        }
        MIR_ASSERT(*this, idx < repr->fields.size(), "Field index " << idx << " out of range for " << curTy);
        curTy = repr->fields[idx].ty;
        baseOfs += repr->fields[idx].offset;
    }
    return baseOfs;
}

std::string MIR::TypeResolve::intrinsicTypeName(const ::HIR::TypeData* ty) const {
    if (ty->is_Path() && ty->as_Path().path.mData.is_Generic()) {
        auto p = ty->as_Path().path.mData.as_Generic().clone();
        p.mParams.mLifetimes.resize(0);
        return FMT(p);
    }
    return FMT(ty);
}

using namespace MIR::visit;

namespace MIR {

    namespace visit {
        struct LValueCbVisitor: public Visitor {
            ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb;

            LValueCbVisitor(::std::function<bool(const ::MIR::LValue&, ValUsage)> cb)
                : cb(std::move(cb))
            {
            }

            bool visitLvalue(const ::MIR::LValue& lv, ValUsage u) override {
                if (cb(lv, u)) {
                    return true;
                }
                return Visitor::visitLvalue(lv, u);
            }
        };

        bool visitMirLvalue(const ::MIR::LValue& lv, ValUsage u, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
            LValueCbVisitor v{mv$(cb)};
            return v.visitLvalue(lv, u);
        }

        bool visitMirLvalue(const ::MIR::Param& p, ValUsage u, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
            LValueCbVisitor v{mv$(cb)};
            return v.visitParam(p, u);
        }

        bool visitMirLvalues(const ::MIR::RValue& rval, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
            LValueCbVisitor v{mv$(cb)};
            return v.visitRvalue(rval);
        }

        bool visitMirLvalues(const ::MIR::Statement& stmt, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
            LValueCbVisitor v{mv$(cb)};
            return v.visitStmt(stmt);
        }

        bool visitMirLvalues(const ::MIR::Terminator& term, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
            LValueCbVisitor v{mv$(cb)};
            return v.visitTerminator(term);
        }

        /*
    void visit_mir_lvalues_mut(::MIR::TypeResolve& state, ::MIR::Function& fcn, ::std::function<bool(::MIR::LValue& , ValUsage)> cb)
    {
        for(unsigned int block_idx = 0; block_idx < fcn.blocks.size(); block_idx ++)
        {
            auto& block = fcn.blocks[block_idx];
            for(auto& stmt : block.statements)
            {
                state.set_cur_stmt(block_idx, (&stmt - &block.statements.front()));
                visit_mir_lvalues_mut(stmt, cb);
            }
            if( block.terminator.tag() == ::MIR::Terminator::TAGDEAD )
                continue ;
            state.set_cur_stmt_term(block_idx);
            visit_mir_lvalues_mut(block.terminator, cb);
        }
    }
    void visit_mir_lvalues(::MIR::TypeResolve& state, const ::MIR::Function& fcn, ::std::function<bool(const ::MIR::LValue& , ValUsage)> cb)
    {
        visit_mir_lvalues_mut(state, const_cast<::MIR::Function&>(fcn), [&](auto& lv, auto im){ return cb(lv, im); });
    }
    */

        void visitTerminatorTargetMut(::MIR::Terminator& term, ::std::function<void(::MIR::BasicBlockId&)> cb) {
            struct TermCbVisitorMut: public VisitorMut {
                ::std::function<void(::MIR::BasicBlockId&)> cb;

                bool visitBlockId(::MIR::BasicBlockId& x) override {
                    cb(x);
                    return false;
                }
            } v;

            v.cb = std::move(cb);
            v.visitTerminator(term);
        }

        void visitTerminatorTarget(const ::MIR::Terminator& term, ::std::function<void(const ::MIR::BasicBlockId&)> cb) {
            visitTerminatorTargetMut(const_cast<::MIR::Terminator&>(term), cb);
        }
    } // namespace visit
} // namespace MIR

// --------------------------------------------------------------------
// MIR_Helper_GetLifetimes
// --------------------------------------------------------------------
namespace {
    struct ValueLifetime {
        /// Bitmap of locations where the variable is valid
        ::std::vector<bool> stmtBitmap;

        ValueLifetime(size_t stmtCount)
            : stmtBitmap(stmtCount)
        {
        }

        void fill(const ::std::vector<size_t>& block_offsets, size_t bb, size_t firstStmt, size_t lastStmt) {
            size_t limit = block_offsets[bb + 1] - block_offsets[bb] - 1;
            DEBUG("bb" << bb << " : " << firstStmt << "--" << lastStmt);
            assert(firstStmt <= limit);
            assert(lastStmt <= limit);
            for (size_t stmt = firstStmt; stmt <= lastStmt; stmt++) {
                stmtBitmap[block_offsets[bb] + stmt] = true;
            }
        }

        void dumpDebug(const char* suffix, unsigned i, const ::std::vector<size_t>& block_offsets) {
            ::std::string name = FMT(suffix << "$" << i);
            while (name.size() < 3 + 1 + 3) {
                name += " ";
            }
            DEBUG(name << " : " << FMT_CB(os, for (unsigned int j = 0; j < this->stmtBitmap.size(); j++) {
                      if (j != 0 && ::std::find(block_offsets.begin(), block_offsets.end(), j) != block_offsets.end()) {
                          os << "|";
                      }
                      os << (this->stmtBitmap[j] ? "X" : " ");
                  }));
        }
    };
}

#if 1 // Alternate algorithm
void MIRHelperGetLifetimesDetermineValueLifetime(::MIR::TypeResolve& state, const ::MIR::Function& fcn, size_t bbIdx, size_t stmtIdx, const ::MIR::LValue& lv, const ::std::vector<size_t>& block_offsets, const ::std::vector<bool>& useBitmap, ValueLifetime& vl);

// ----------
// TODO: Improved algorithm
//
// 1. Locate loops (such that a block can be checked for if it's part of a loop, relative to another block)
//  - This can also be used to determine if one bb is before another
// 2. Locate assignment operations (and inline assembly outputs) of locals
// 3. Run forwards until:
// - a jump to a visited block (inner loop)
// - a jump before the first known usage
// - a jump after the last known usage
// - an asignment of the value
// - a use-by-move

::MIR::ValueLifetimes MIRHelperGetLifetimes(::MIR::TypeResolve& state, const ::MIR::Function& fcn, bool dumpDebug, const ::std::vector<bool>* mask /*=nullptr*/) {
    TRACE_FUNCTION_F(state);

    size_t statementCount = 0;
    ::std::vector<size_t> block_offsets;
    block_offsets.reserve(fcn.blocks.size());
    for (const auto& bb : fcn.blocks) {
        block_offsets.push_back(statementCount);
        statementCount += bb.statements.size() + 1; // +1 for the terminator
    }
    block_offsets.push_back(statementCount); // Store the final limit for later code to use.

    ::std::vector<ValueLifetime> slotLifetimes(fcn.locals.size(), ValueLifetime(statementCount));

    // - Enumerate all read positions for each slot
    std::vector<std::vector<bool>> slotReadBitmaps(fcn.locals.size());
    {
        for (auto& b : slotReadBitmaps) {
            b.resize(statementCount);
        }
        size_t pos = 0;
        auto useCb = [&](const ::MIR::LValue& tlv, ValUsage vu) {
            if (tlv.root.is_Local()) {
                if (vu != ValUsage::Write) {
                    slotReadBitmaps[tlv.root.as_Local()][pos] = true;
                }
            }
            for (const auto& w : tlv.wrappers) {
                if (w.is_Index()) {
                    slotReadBitmaps[w.as_Index()][pos] = true;
                }
            }
            return false;
        };
        for (const auto& bb : fcn.blocks) {
            for (const auto& stmt : bb.statements) {
                visitMirLvalues(stmt, useCb);
                pos++;
            }
            visitMirLvalues(bb.terminator, useCb);
            pos++;
        }
    }

    // Enumerate direct assignments of variables (linear iteration of BB list)
    for (size_t bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
        auto assignedLvalue = [&](size_t bbIdx, size_t stmtIdx, const ::MIR::LValue& lv) {
            // NOTE: Fills the first statement after running, just to ensure that any assigned value has _a_ lifetime
            if (lv.is_Local()) {
                auto de = lv.root.as_Local();
                if (!mask || mask->at(de)) {
                    MIRHelperGetLifetimesDetermineValueLifetime(state, fcn, bbIdx, stmtIdx, lv, block_offsets, slotReadBitmaps[de], slotLifetimes[de]);
                    slotLifetimes[de].fill(block_offsets, bbIdx, stmtIdx, stmtIdx);
                }
            }
        };

        const auto& bb = fcn.blocks[bbIdx];
        for (size_t stmtIdx = 0; stmtIdx < bb.statements.size(); stmtIdx++) {
            state.setCurStmt(bbIdx, stmtIdx);
            const auto& stmt = bb.statements[stmtIdx];
            if (const auto* se = stmt.opt_Assign()) {
                // For assigned variables, determine how long that value will live
                assignedLvalue(bbIdx, stmtIdx + 1, se->dst);
            } else if (const auto* se = stmt.opt_Asm()) {
                for (const auto& e : se->outputs) {
                    assignedLvalue(bbIdx, stmtIdx + 1, e.second);
                }
            }
        }
        state.setCurStmtTerm(bbIdx);

        // Only Call can assign a value
        TU_IFLET(::MIR::Terminator, bb.terminator, Call, te, assignedLvalue(te.retBlock, 0, te.retVal);)
    }

    // Dump out variable lifetimes.
    if (dumpDebug) {
        for (size_t i = 0; i < slotLifetimes.size(); i++) {
            slotLifetimes[i].dumpDebug("_", i, block_offsets);
        }
    }

    ::MIR::ValueLifetimes rv;
    rv.blockOffsets = mv$(block_offsets);
    rv.slots.reserve(slotLifetimes.size());
    for (auto& lft : slotLifetimes) {
        rv.slots.push_back(::MIR::ValueLifetime(mv$(lft.stmtBitmap)));
    }
    return rv;
}

void MIRHelperGetLifetimesDetermineValueLifetime(
    ::MIR::TypeResolve& mir_res,
    const ::MIR::Function& fcn,
    size_t bbIdx,
    size_t stmtIdx, // First statement in which the value is valid (after the assignment)
    const ::MIR::LValue& lv,
    const ::std::vector<size_t>& block_offsets,
    const ::std::vector<bool>& useBitmap,
    ValueLifetime& vl
) {
    TRACE_FUNCTION_F(mir_res << lv << " assigned");

    // Walk the BB tree until:
    // - Loopback
    // - Assignment
    // - Drop

    struct State {
        const ::std::vector<size_t>& blockOffsets;
        ValueLifetime& outVl;

        ::std::vector<unsigned int> bbHistory;
        size_t lastReadOfs; // Statement index
        bool isBorrowed;

        State(const ::std::vector<size_t>& block_offsets, ValueLifetime& vl, size_t init_bb_idx, size_t init_stmt_idx)
            : blockOffsets(block_offsets)
            , outVl(vl)
            , bbHistory()
            , lastReadOfs(init_stmt_idx)
            , isBorrowed(false)
        {
            bbHistory.push_back(init_bb_idx);
        }

        State(State&& x)
            : blockOffsets(x.blockOffsets)
            , outVl(x.outVl)
            , bbHistory(mv$(x.bbHistory))
            , lastReadOfs(x.lastReadOfs)
            , isBorrowed(x.isBorrowed)
        {
        }

        State& operator=(State&& x) {
            this->bbHistory = mv$(x.bbHistory);
            this->lastReadOfs = x.lastReadOfs;
            this->isBorrowed = x.isBorrowed;
            return *this;
        }

        State clone() const {
            State rv{blockOffsets, outVl, 0, lastReadOfs};
            rv.bbHistory = bbHistory;
            rv.isBorrowed = isBorrowed;
            return rv;
        }

        // Returns true if the variable has been borrowed
        bool is_borrowed() const {
            return this->isBorrowed;
        }

        void markBorrowed(size_t stmtIdx) {
            if (!isBorrowed) {
                isBorrowed = false;
                this->fillTo(stmtIdx);
            }
            isBorrowed = true;
        }

        void markRead(size_t stmtIdx) {
            if (!isBorrowed) {
                this->fillTo(stmtIdx);
            } else {
                isBorrowed = false;
                this->fillTo(stmtIdx);
                isBorrowed = true;
            }
        }

        void fmt(::std::ostream& os) const {
            os << "BB" << bbHistory.front() << "/" << lastReadOfs << "--";
            os << "[" << bbHistory << "]";
        }

        void finalise(size_t stmtIdx) {
            if (isBorrowed) {
                isBorrowed = false;
                this->fillTo(stmtIdx);
                isBorrowed = true;
            }
        }

    private:
        void fillTo(size_t stmtIdx) {
            TRACE_FUNCTION_F(FMT_CB(ss, this->fmt(ss);));
            assert(!isBorrowed);
            assert(bbHistory.size() > 0);
            if (bbHistory.size() == 1) {
                // only one block
                outVl.fill(blockOffsets, bbHistory[0], lastReadOfs, stmtIdx);
            } else {
                // First block.
                auto init_bb_idx = bbHistory[0];
                auto limit0 = blockOffsets[init_bb_idx + 1] - blockOffsets[init_bb_idx] - 1;
                outVl.fill(blockOffsets, init_bb_idx, lastReadOfs, limit0);

                // Middle blocks
                for (size_t i = 1; i < bbHistory.size() - 1; i++) {
                    size_t bbIdx = bbHistory[i];
                    assert(bbIdx + 1 < blockOffsets.size());
                    size_t limit = blockOffsets[bbIdx + 1] - blockOffsets[bbIdx] - 1;
                    outVl.fill(blockOffsets, bbIdx, 0, limit);
                }

                // Last block
                auto bbIdx = bbHistory.back();
                outVl.fill(blockOffsets, bbIdx, 0, stmtIdx);
            }

            lastReadOfs = stmtIdx;

            auto cur = this->bbHistory.back();
            this->bbHistory.clear();
            this->bbHistory.push_back(cur);
        }
    };

    struct Runner {
        ::MIR::TypeResolve& mirRes;
        const ::MIR::Function& fcn;
        size_t initBbIdx;
        size_t initStmtIdx;
        const ::MIR::LValue& mLv;
        const ::std::vector<size_t>& blockOffsets;
        ValueLifetime& mLifetimes;
        bool isCopy;

        ::std::vector<bool> visitedStatements;

        ::std::vector<::std::pair<size_t, State>> statesToDo;

        Runner(::MIR::TypeResolve& mir_res, const ::MIR::Function& fcn, size_t init_bb_idx, size_t init_stmt_idx, const ::MIR::LValue& lv, const ::std::vector<size_t>& block_offsets, ValueLifetime& vl)
            : mirRes(mir_res)
            , fcn(fcn)
            , initBbIdx(init_bb_idx)
            , initStmtIdx(init_stmt_idx)
            , mLv(lv)
            , blockOffsets(block_offsets)
            , mLifetimes(vl)
            ,

            visitedStatements(mLifetimes.stmtBitmap.size())
        {
            ::HIR::TypeRef tmp;
            isCopy = mirRes.mResolve.typeIsCopy(mir_res.sp, mirRes.getLvalueType(tmp, lv));
        }

        void runBlock(size_t bbIdx, size_t stmtIdx, State state) {
            const auto& bb = fcn.blocks.at(bbIdx);
            assert(stmtIdx <= bb.statements.size());

            bool wasMoved = false;
            bool wasUpdated = false;
            auto visitCb = [&](const auto& lv, auto vu) {
                if (lv.root == mLv.root) {
                    switch (vu) {
                        case ValUsage::Read:
                            DEBUG(mirRes << "Used");
                            state.markRead(stmtIdx);
                            wasUpdated = true;
                            break;
                        case ValUsage::Move:
                            if (lv.wrappers.size() == mLv.wrappers.size()) {
                                DEBUG(mirRes << (isCopy ? "Read" : "Moved"));
                                state.markRead(stmtIdx);
                                wasMoved = !isCopy;
                            } else {
                                DEBUG(mirRes << "Used (partial)");
                                state.markRead(stmtIdx);
                                wasUpdated = true;
                            }
                            break;
                        case ValUsage::Borrow:
                            DEBUG(mirRes << "Borrowed");
                            state.markBorrowed(stmtIdx);
                            wasUpdated = true;
                            break;
                        case ValUsage::Write:
                            // Don't care
                            break;
                    }
                }
                for (const auto& w : lv.wrappers) {
                    if (w.is_Index() && mLv.is_Local() && w.as_Index() == mLv.as_Local()) {
                        DEBUG(mirRes << "Index used");
                        state.markRead(stmtIdx);
                        wasUpdated = true;
                    }
                }
                return false;
            };

            for (; stmtIdx < bb.statements.size(); stmtIdx++) {
                const auto& stmt = bb.statements[stmtIdx];
                mirRes.setCurStmt(bbIdx, stmtIdx);
                visitedStatements[blockOffsets.at(bbIdx) + stmtIdx] = true;

                // Visit and see if the value is read (setting the read flag or end depending on if the value is Copy)
                wasUpdated = false;
                visitMirLvalues(stmt, visitCb);
                if (wasUpdated || wasMoved) {
                    DEBUG(mirRes << stmt);
                }

                if (wasMoved) {
                    // Moved: Update read position and apply
                    DEBUG(mirRes << "Moved, return");
                    state.markRead(stmtIdx);
                    state.finalise(stmtIdx);
                    return;
                }

                TU_MATCH_HDRA( (stmt), {)
                TU_ARMA(Assign, se) {
                        if (se.dst == mLv) {
                            DEBUG(mirRes << "- Assigned to, return");
                            // Value assigned, just apply
                            state.finalise(stmtIdx);
                            return;
                        }
                    }
                    TU_ARMA(Asm, se) {
                        //
                        for (const auto& e : se.outputs) {
                            if (e.second == mLv) {
                                // Assigned, just apply
                                DEBUG(mirRes << "- Assigned (asm!), return");
                                state.finalise(stmtIdx);
                                return;
                            }
                        }
                    }
                    TU_ARMA(Asm2, se) {
                        for (const auto& p : se.params) {
                        TU_MATCH_HDRA( (p), {)
                        TU_ARMA(Const, v) {
                                }
                                TU_ARMA(Sym, v) {
                                }
                                TU_ARMA(Reg, v) {
                                    if (v.output) {
                                        if (*v.output == mLv) {
                                            // Assigned, just apply
                                            DEBUG(mirRes << "- Assigned (asm!), return");
                                            state.finalise(stmtIdx);
                                            return;
                                        }
                                    }
                                }
                        }
                        }
                    }
                    TU_ARMA(SetDropFlag, se) {
                        // Ignore
                    }
                    TU_ARMA(SaveDropFlag, se) {
                        // Ignore
                    }
                    TU_ARMA(LoadDropFlag, se) {
                        // Ignore
                    }
                    TU_ARMA(ScopeEnd, se) {
                        // Ignore
                    }
                }
            }
            mirRes.setCurStmtTerm(bbIdx);
            visitedStatements[blockOffsets.at(bbIdx) + stmtIdx] = true;

            wasUpdated = false;
            visitMirLvalues(bb.terminator, visitCb);
            DEBUG(mirRes << bb.terminator << (wasUpdated ? " (used)" : ""));

            if (wasMoved) {
                // Moved: Update read position and apply
                DEBUG(mirRes << "- Moved, return");
                state.markRead(stmtIdx);
                state.finalise(stmtIdx);
                return;
            }

            // Terminator
            TU_MATCH_HDRA( (bb.terminator), {)
            TU_ARMA(Incomplete, te) {
                    // TODO: Isn't this a bug?
                    DEBUG(mirRes << "Incomplete");
                    state.finalise(stmtIdx);
                }
                TU_ARMA(Return, te) {
                    DEBUG(mirRes << "Return");
                    state.finalise(stmtIdx);
                }
                TU_ARMA(UnwindResume, te) {
                    DEBUG(mirRes << "UnwindResume");
                    state.finalise(stmtIdx);
                }
                TU_ARMA(UnwindTerminate, te) {
                    DEBUG(mirRes << "UnwindTerminate");
                    state.finalise(stmtIdx);
                }
                TU_ARMA(Unreachable, te) {
                    DEBUG(mirRes << "Unreachable");
                    state.finalise(stmtIdx);
                }
                TU_ARMA(Goto, te) {
                    statesToDo.push_back(::std::make_pair(te, mv$(state)));
                }
                TU_ARMA(If, te) {
                    statesToDo.push_back(::std::make_pair(te.bbTrue, state.clone()));
                    statesToDo.push_back(::std::make_pair(te.bbFalse, mv$(state)));
                }
                TU_ARMA(Switch, te) {
                    for (size_t i = 0; i < te.targets.size(); i++) {
                        statesToDo.push_back(::std::make_pair(te.targets[i], state.clone()));
                    }
                    if (te.validFlag != ~0u) {
                        statesToDo.push_back(::std::make_pair(te.invalidTarget, mv$(state)));
                    }
                }
                TU_ARMA(SwitchValue, te) {
                    for (size_t i = 0; i < te.targets.size(); i++) {
                        statesToDo.push_back(::std::make_pair(te.targets[i], state.clone()));
                    }
                    statesToDo.push_back(::std::make_pair(te.defTarget, mv$(state)));
                }
                TU_ARMA(Drop, te) {
                    if (te.slot == mLv) {
                        DEBUG(mirRes << "Dropped, return");
                        state.markRead(stmtIdx);
                        state.finalise(stmtIdx);
                        return;
                    }
                    TU_IFLET(::MIR::UnwindAction, te.unwind, Cleanup, target,
                        statesToDo.push_back(::std::make_pair(target, state.clone()));
                    )
                    statesToDo.push_back(::std::make_pair(te.target, mv$(state)));
                }
                TU_ARMA(Call, te) {
                    if (te.retVal == mLv) {
                        DEBUG(mirRes << "Assigned (Call), return");
                        // Value assigned, just apply
                        state.finalise(stmtIdx);
                        return;
                    }
                    TU_IFLET(::MIR::UnwindAction, te.unwind, Cleanup, target,
                        statesToDo.push_back(::std::make_pair(target, state.clone()));
                    )
                    statesToDo.push_back(::std::make_pair(te.retBlock, mv$(state)));
                }
            }
        }
    };

    Runner runner(mir_res, fcn, bbIdx, stmtIdx, lv, block_offsets, vl);
    ::std::vector<::std::pair<size_t, State>> postCheckList;

    // TODO: Have a bitmap of visited statements. If a visted statement is hit, stop the current state
    // - Use the same rules as loopback.

    // Fill the first statement, to ensure that there is at least one bit set.
    runner.runBlock(bbIdx, stmtIdx, State(block_offsets, vl, bbIdx, stmtIdx));

    while (!runner.statesToDo.empty()) {
        auto bbIdx = runner.statesToDo.back().first;
        auto state = mv$(runner.statesToDo.back().second);
        runner.statesToDo.pop_back();

        DEBUG("state.bb_history=[" << state.bbHistory << "], -> BB" << bbIdx);
        state.bbHistory.push_back(bbIdx);

        if (runner.visitedStatements.at(block_offsets.at(bbIdx) + 0)) {
            if (vl.stmtBitmap.at(block_offsets.at(bbIdx) + 0)) {
                DEBUG("Looped (to already valid)");
                state.markRead(0);
                state.finalise(0);
                continue;
            } else if (state.is_borrowed()) {
                DEBUG("Looped (borrowed)");
                state.markRead(0);
                state.finalise(0);
                continue;
            } else {
                // Put this state elsewhere and check if the variable is known valid at that point.
                DEBUG("Looped (after last read), push for later");
                postCheckList.push_back(::std::make_pair(bbIdx, mv$(state)));
                continue;
            }
        }


        // Special case for when doing multiple runs on the same output
        if (vl.stmtBitmap.at(block_offsets.at(bbIdx) + 0)) {
            DEBUG("Already valid in BB" << bbIdx);
            state.markRead(0);
            state.finalise(0);
            continue;
        }

        runner.runBlock(bbIdx, 0, mv$(state));
    }

    // Iterate while there are items in the post_check list
    while (!postCheckList.empty()) {
        bool change = false;
        for (auto it = postCheckList.begin(); it != postCheckList.end();) {
            auto bbIdx = it->first;
            auto& state = it->second;
            // If the target of this loopback is valid, then the entire route to the loopback must have been valid
            if (vl.stmtBitmap.at(block_offsets.at(bbIdx) + 0)) {
                change = true;
                DEBUG("Looped (now valid)");
                state.markRead(0);
                state.finalise(0);

                it = postCheckList.erase(it);
            } else {
                ++it;
            }
        }
        // Keep going while changes happen
        if (!change) {
            break;
        }
    }
}

#else

::MIR::ValueLifetimes MIRHelperGetLifetimes(::MIR::TypeResolve& state, const ::MIR::Function& fcn, bool dumpDebug) {
    TRACE_FUNCTION_F(state);

    // New algorithm notes:
    // ---
    // The lifetime of a value starts when it is written, and ends the last time it is read
    // - When a variable is read, end any existing lifetime and start a new one.
    // - When the value is read, update the end of its lifetime.
    // ---
    // A lifetime is a range in the call graph (with a start and end, including list of blocks)
    // - Representation: Bitmap with a bit per statement.
    // - Record the current block path in general state, along with known active lifetimes

    // TODO: If a value is borrowed, assume it lives forevermore
    // - Ideally there would be borrow tracking to determine its actual required lifetime.
    // - NOTE: This doesn't impact the borrows themselves, just the borrowee

    // TODO: Add a statement type StorageDead (or similar?) that indicates the point where a values scope ends

    // Scan through all possible paths in the graph (with loopback detection using a memory of the path)
    // - If a loop is detected, determine if there were changes to the lifetime set during that pass
    //  > Changes are noticed by recording in the state structure when it triggers a change in the lifetime
    //    map.
    struct Position {
        size_t pathIndex = 0; // index into the block path.
        unsigned int stmtIdx = 0;

        bool operator==(const Position& x) const {
            return pathIndex == x.pathIndex && stmtIdx == x.stmtIdx;
        }
    };

    struct ProtoLifetime {
        Position start;
        Position end;

        bool is_empty() const {
            return start == end;
        }

        bool is_borrowed() const {
            return this->end == Position{~0u, ~0u};
        }
    };

    static unsigned NEXT_INDEX = 0;

    struct State {
        unsigned int index = 0;
        ::std::vector<unsigned int> blockPath;
        ::std::vector<unsigned int> blockChangeIdx;
        unsigned int curChangeIdx = 0;

        // if read, update. If set, save and update
        ::std::vector<ProtoLifetime> tmpEnds;
        ::std::vector<ProtoLifetime> varEnds;

        State(const ::MIR::Function& fcn)
            : tmpEnds(fcn.temporaries.size(), ProtoLifetime())
            , varEnds(fcn.namedVariables.size(), ProtoLifetime())
        {
        }

        State clone() const {
            auto rv = *this;
            rv.index = ++NEXT_INDEX;
            return rv;
        }
    };

    NEXT_INDEX = 0;

    size_t statementCount = 0;
    ::std::vector<size_t> block_offsets;
    block_offsets.reserve(fcn.blocks.size());
    for (const auto& bb : fcn.blocks) {
        block_offsets.push_back(statementCount);
        statementCount += bb.statements.size() + 1; // +1 for the terminator
    }

    ::std::vector<ValueLifetime> temporaryLifetimes(fcn.temporaries.size(), ValueLifetime(statementCount));
    ::std::vector<ValueLifetime> variableLifetimes(fcn.namedVariables.size(), ValueLifetime(statementCount));

    struct BlockSeenLifetimes {
        bool hasState = false;
        const ::std::vector<size_t>& block_offsets;
        ::std::vector<::std::vector<unsigned int>> tmp;
        ::std::vector<::std::vector<unsigned int>> var;

        BlockSeenLifetimes(const ::std::vector<size_t>& block_offsets, const ::MIR::Function& fcn)
            : block_offsets(block_offsets)
            , tmp(fcn.temporaries.size())
            , var(fcn.namedVariables.size())
        {
        }

        bool has_state() const {
            return hasState;
        }

        bool tryMerge(const State& valState) const {
            // TODO: This logic isn't quite correct. Just becase a value's existing end is already marked as valid,
            // doesn't mean that we have no new information.
            // - Wait, doesn't it?
            auto tryMergeLft = [&](const ProtoLifetime& lft, const ::std::vector<unsigned int>& seen) -> bool {
                if (lft.is_empty()) {
                    return false;
                }
                // TODO: What should be done for borrow flagged values
                if (lft.is_borrowed()) {
                    return false;
                }
                auto endIdx = block_offsets.at(valState.blockPath.at(lft.end.pathIndex)) + lft.end.stmtIdx;

                auto it = ::std::find(seen.begin(), seen.end(), endIdx);
                return (it == seen.end());
            };
            for (size_t i = 0; i < valState.tmpEnds.size(); i++) {
                if (tryMergeLft(valState.tmpEnds[i], this->tmp[i])) {
                    return true;
                }
            }
            for (size_t i = 0; i < valState.varEnds.size(); i++) {
                if (tryMergeLft(valState.varEnds[i], this->var[i])) {
                    return true;
                }
            }
            return false;
        }

        bool merge(const State& valState) {
            bool rv = false;
            auto mergeLft = [&](const ProtoLifetime& lft, ::std::vector<unsigned int>& seen) -> bool {
                if (lft.is_empty()) {
                    return false;
                }
                // TODO: What should be done for borrow flagged values
                if (lft.end == Position{~0u, ~0u}) {
                    return false;
                }
                auto endIdx = block_offsets.at(valState.blockPath.at(lft.end.pathIndex)) + lft.end.stmtIdx;

                auto it = ::std::find(seen.begin(), seen.end(), endIdx);
                if (it == seen.end()) {
                    seen.push_back(endIdx);
                    return true;
                } else {
                    return false;
                }
            };
            for (size_t i = 0; i < valState.tmpEnds.size(); i++) {
                rv |= mergeLft(valState.tmpEnds[i], this->tmp[i]);
            }
            for (size_t i = 0; i < valState.varEnds.size(); i++) {
                rv |= mergeLft(valState.varEnds[i], this->var[i]);
            }
            hasState = true;
            return rv;
        }
    };

    ::std::vector<BlockSeenLifetimes> blockSeenLifetimes(fcn.blocks.size(), BlockSeenLifetimes(block_offsets, fcn));

    State initState(fcn);

    ::std::vector<::std::pair<unsigned int, State>> todoQueue;
    todoQueue.push_back(::std::make_pair(0, mv$(initState)));

    while (!todoQueue.empty()) {
        auto bbIdx = todoQueue.back().first;
        auto valState = mv$(todoQueue.back().second);
        todoQueue.pop_back();
        state.setCurStmt(bbIdx, 0);

        // Fill alive time in the bitmap
        // TODO: Maybe also store the range (as a sequence of {block,start,end})
        auto addLifetimeS = [&](State& valState, const ::MIR::LValue& lv, const Position& start, const Position& end) {
            assert(start.pathIndex <= end.pathIndex);
            assert(start.pathIndex < end.pathIndex || start.stmtIdx <= end.stmtIdx);
            if (start.pathIndex == end.pathIndex && start.stmtIdx == end.stmtIdx) {
                return;
            }
            DEBUG("[add_lifetime] " << lv << " (" << start.pathIndex << "," << start.stmtIdx << ") -- (" << end.pathIndex << "," << end.stmtIdx << ")");
            ValueLifetime* lft;
            if (const auto* e = lv.opt_Temporary()) {
                lft = &temporaryLifetimes[e->idx];
            } else if (const auto* e = lv.opt_Variable()) {
                lft = &variableLifetimes[*e];
            } else {
                MIR_TODO(state, "[add_lifetime] " << lv);
                return;
            }

            // Fill lifetime map for this temporary in the indicated range
            bool didSet = false;
            unsigned int j = start.stmtIdx;
            unsigned int i = start.pathIndex;
            while (i <= end.pathIndex && i < valState.blockPath.size()) {
                auto bbIdx = valState.blockPath.at(i);
                const auto& bb = fcn.blocks[bbIdx];
                MIR_ASSERT(state, j <= bb.statements.size(), "");
                MIR_ASSERT(state, bbIdx < block_offsets.size(), "");

                auto blockBase = block_offsets.at(bbIdx);
                auto idx = blockBase + j;
                if (!lft->stmtBitmap.at(idx)) {
                    lft->stmtBitmap[idx] = true;
                    didSet = true;
                }

                if (i == end.pathIndex && j == (end.stmtIdx != ~0u ? end.stmtIdx : bb.statements.size())) {
                    break;
                }

                // If the current index is the terminator (one after the size)
                if (j == bb.statements.size()) {
                    j = 0;
                    i++;
                } else {
                    j++;
                }
            }

            // - If the above set a new bit, increment `val_state.cur_change_idx`
            if (didSet) {
                DEBUG("[add_lifetime] " << lv << " (" << start.pathIndex << "," << start.stmtIdx << ") -- (" << end.pathIndex << "," << end.stmtIdx << ") - New information");
                valState.curChangeIdx += 1;
            }
        };
        auto addLifetime = [&](const ::MIR::LValue& lv, const Position& start, const Position& end) {
            addLifetimeS(valState, lv, start, end);
        };

        auto applyState = [&](State& state) {
            // Apply all changes in this state, just in case there was new information
            for (unsigned i = 0; i < fcn.temporaries.size(); i++) {
                addLifetimeS(state, ::MIR::LValue::make_Temporary({i}), state.tmpEnds[i].start, state.tmpEnds[i].end);
            }
            for (unsigned i = 0; i < fcn.namedVariables.size(); i++) {
                addLifetimeS(state, ::MIR::LValue::make_Variable({i}), state.varEnds[i].start, state.varEnds[i].end);
            }
        };
        auto addToVisit = [&](unsigned int newBbIdx, State newState) {
            auto& bbMemoryEnt = blockSeenLifetimes[newBbIdx];
            if (!bbMemoryEnt.has_state()) {
                // No recorded state, needs to be visited
                DEBUG(state << " state" << newState.index << " -> bb" << newBbIdx << " (no existing state)");
            } else if (bbMemoryEnt.tryMerge(newState)) {
                // This state has new information, needs to be visited
                DEBUG(state << " state" << newState.index << " -> bb" << newBbIdx << " (new info)");
            } else {
                // Skip
                // TODO: Acquire from the target block the actual end of any active lifetimes, then apply them.
                DEBUG(state << " state" << newState.index << " -> bb" << newBbIdx << " - No new state, no push");
                // - For all variables currently active, check if they're valid in the first statement of the target block.
                // - If so, mark as valid at the end of the current block
                auto bmIdx = block_offsets[newBbIdx];
                Position cur_pos;
                cur_pos.pathIndex = valState.blockPath.size() - 1;
                cur_pos.stmtIdx = fcn.blocks[bbIdx].statements.size();
                for (unsigned i = 0; i < fcn.temporaries.size(); i++) {
                    if (!newState.tmpEnds[i].is_empty() && temporaryLifetimes[i].stmtBitmap[bmIdx]) {
                        DEBUG("- tmp$" << i << " - Active in target, assume active");
                        newState.tmpEnds[i].end = cur_pos;
                    }
                }
                for (unsigned i = 0; i < fcn.namedVariables.size(); i++) {
                    if (!newState.varEnds[i].is_empty() && variableLifetimes[i].stmtBitmap[bmIdx]) {
                        DEBUG("- var$" << i << " - Active in target, assume active");
                        newState.varEnds[i].end = cur_pos;
                    }
                }
                // - Apply whatever state was still active
                applyState(newState);
                return;
            }
            todoQueue.push_back(::std::make_pair(newBbIdx, mv$(newState)));
        };

        // Compare this state to a composite list of lifetimes seen in this block
        // - Just compares the end of each proto lifetime
        {
            auto& bbMemoryEnt = blockSeenLifetimes[bbIdx];
            bool hadState = bbMemoryEnt.has_state();
            bool hasNew = bbMemoryEnt.merge(valState);

            if (!hasNew && hadState) {
                DEBUG(state << " state" << valState.index << " - No new entry state");
                applyState(valState);

                continue;
            }
        }

        // Check if this state has visited this block before, and if anything changed since last time
        {
            auto it = ::std::find(valState.blockPath.rbegin(), valState.blockPath.rend(), bbIdx);
            if (it != valState.blockPath.rend()) {
                auto idx = &*it - &valState.blockPath.front();
                if (valState.blockChangeIdx[idx] == valState.curChangeIdx) {
                    DEBUG(state << " " << valState.index << " Loop and no change");
                    continue;
                } else {
                    assert(valState.blockChangeIdx[idx] < valState.curChangeIdx);
                    DEBUG(state << " " << valState.index << " --- Loop, " << valState.curChangeIdx - valState.blockChangeIdx[idx] << " changes");
                }
            } else {
                DEBUG(state << " " << valState.index << " ---");
            }
            valState.blockPath.push_back(bbIdx);
            valState.blockChangeIdx.push_back(valState.curChangeIdx);
        }

        Position cur_pos;
        cur_pos.pathIndex = valState.blockPath.size() - 1;
        cur_pos.stmtIdx = 0;
        auto lvalueRead = [&](const ::MIR::LValue& lv) {
            ProtoLifetime* slot;
            if (const auto* e = lv.opt_Temporary()) {
                slot = &valState.tmpEnds.at(e->idx);
            } else if (const auto* e = lv.opt_Variable()) {
                slot = &valState.varEnds.at(*e);
            } else {
                return;
            }
            // Update the last read location
            //DEBUG("Update END " << lv << " to " << cur_pos);
            slot->end = cur_pos;
        };
        auto lvalueSet = [&](const ::MIR::LValue& lv) {
            ProtoLifetime* slot;
            if (const auto* e = lv.opt_Temporary()) {
                slot = &valState.tmpEnds.at(e->idx);
            } else if (const auto* e = lv.opt_Variable()) {
                slot = &valState.varEnds.at(*e);
            } else {
                return;
            }
            // End whatever value was originally there, and insert this new one
            slot->end = cur_pos;
            addLifetime(lv, slot->start, slot->end);
            slot->start = cur_pos;
        };
        auto lvalueBorrow = [&](const ::MIR::LValue& lv) {
            ProtoLifetime* slot;
            if (const auto* e = lv.opt_Temporary()) {
                slot = &valState.tmpEnds.at(e->idx);
            } else if (const auto* e = lv.opt_Variable()) {
                slot = &valState.varEnds.at(*e);
            } else {
                return;
            }
            // TODO: Flag this value as currently being borrowed (a flag that never clears)
            slot->end = Position{~0u, ~0u};
        };
        auto visitLvalCb = [&](const auto& lv, ValUsage vu) -> bool {
            if (vu == ValUsage::Read) {
                lvalueRead(lv);
            }
            if (vu == ValUsage::Borrow) {
                lvalueBorrow(lv);
            }
            if (vu == ValUsage::Write) {
                lvalueSet(lv);
            }
            return false;
        };

        // Run statements
        for (const auto& stmt : fcn.blocks[bbIdx].statements) {
            auto stmtIdx = &stmt - &fcn.blocks[bbIdx].statements.front();
            cur_pos.stmtIdx = stmtIdx;
            state.setCurStmt(bbIdx, stmtIdx);
            DEBUG(state << " " << stmt);

            if (const auto* e = stmt.opt_Drop()) {
                visitMirLvalues(stmt, [&](const auto& lv, ValUsage vu) -> bool {
                    if (vu == ValUsage::Read) {
                        lvalueRead(lv);
                    }
                    return false;
                });
                lvalueRead(e->slot);
                lvalueSet(e->slot);
            } else {
                visitMirLvalues(stmt, visitLvalCb);
            }
        }
        cur_pos.stmtIdx = fcn.blocks[bbIdx].statements.size();

        state.setCurStmtTerm(bbIdx);
        DEBUG(state << "TERM " << fcn.blocks[bbIdx].terminator);
        TU_MATCH(
            ::MIR::Terminator,
            (fcn.blocks[bbIdx].terminator),
            (e),
            (
                Incomplete,
                // Should be impossible here.
            ),
            (Return,
             // End all active lifetimes at their previous location.
             applyState(valState);),
            (UnwindResume, applyState(valState);),
            (UnwindTerminate, applyState(valState);),
            (Unreachable, applyState(valState);),
            (Goto, addToVisit(e, mv$(valState));),
            (If, visitMirLvalue(e.cond, ValUsage::Read, visitLvalCb);

             // Push blocks
             addToVisit(e.bb0, valState.clone());
             addToVisit(e.bb1, mv$(valState));),
            (
                Switch, visitMirLvalue(e.val, ValUsage::Read, visitLvalCb); ::std::set<unsigned int> tgts; for (const auto& tgt : e.targets) tgts.insert(tgt);

                for (const auto& tgt : tgts) {
                    auto vs = (tgt == *tgts.rbegin() ? mv$(valState) : valState.clone());
                    addToVisit(tgt, mv$(vs));
                }
            ),
            (Drop, visitMirLvalue(e.slot, ValUsage::Move, visitLvalCb); TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target, addToVisit(target, valState.clone());) addToVisit(e.target, mv$(valState));),
            (Call, if (const auto* f = e.fcn.opt_Value()) visitMirLvalue(*f, ValUsage::Read, visitLvalCb); for (const auto& arg : e.args) if (const auto* e = arg.opt_LValue()) visitMirLvalue(*e, ValUsage::Read, visitLvalCb);

             // Push blocks (with return valid only in one)
             TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target, addToVisit(target, valState.clone());)

             // TODO: If the function returns !, don't follow the ret_block
             lvalueSet(e.retVal);
             addToVisit(e.retBlock, mv$(valState));)
        )
    }

    // Dump out variable lifetimes.
    if (dumpDebug) {
        for (unsigned int i = 0; i < temporaryLifetimes.size(); i++) {
            temporaryLifetimes[i].dumpDebug("tmp", i, block_offsets);
        }
        for (unsigned int i = 0; i < variableLifetimes.size(); i++) {
            variableLifetimes[i].dumpDebug("var", i, block_offsets);
        }
    }

    // Move lifetime bitmaps into the variable for the below code
    ::MIR::ValueLifetimes rv;
    rv.blockOffsets = mv$(block_offsets);
    rv.temporaries.reserve(temporaryLifetimes.size());
    for (auto& lft : temporaryLifetimes) {
        rv.temporaries.push_back(::MIR::ValueLifetime(mv$(lft.stmtBitmap)));
    }
    rv.variables.reserve(variableLifetimes.size());
    for (auto& lft : variableLifetimes) {
        rv.variables.push_back(::MIR::ValueLifetime(mv$(lft.stmtBitmap)));
    }

    return rv;
}
#endif

namespace MIR {

TypeResolve::TypeResolve(const Span& sp, const ::StaticTraitResolve& resolve, ::FmtLambda path, const ::HIR::TypeData* ret_type, const argsT& args, const ::MIR::Function& fcn)
    : sp(sp)
    , mResolve(resolve)
    , crate(resolve.crate)
    , mPath(path)
    , retType(ret_type)
    , mArgs(args)
    , fcn(fcn)
    , monomorphedRettype(nullptr)
    , monomorphedLocals(nullptr) {
    if (crate.mLangItems.count("owned_box") > 0) {
        mLangBox = &crate.mLangItems.at("owned_box");
    }
}
void TypeResolve::setCurStmt(const ::MIR::BasicBlock& bb, const ::MIR::Statement& stmt) {
    assert(&stmt >= &bb.statements.front());
    assert(&stmt <= &bb.statements.back());
    this->setCurStmt(bb, &stmt - bb.statements.data());
}
void TypeResolve::setCurStmt(const ::MIR::BasicBlock& bb, unsigned int stmtIdx) {
    assert(&bb >= &fcn.blocks.front());
    assert(&bb <= &fcn.blocks.back());
    this->setCurStmt(&bb - fcn.blocks.data(), stmtIdx);
}
void TypeResolve::setCurStmt(unsigned int bbIdx, unsigned int stmtIdx) {
    this->bbIdx = bbIdx;
    this->stmtIdx = stmtIdx;
}
void TypeResolve::setCurStmtTerm(const ::MIR::BasicBlock& bb) {
    assert(&bb >= &fcn.blocks.front());
    assert(&bb <= &fcn.blocks.back());
    this->setCurStmtTerm(&bb - fcn.blocks.data());
}
void TypeResolve::setCurStmtTerm(unsigned int bbIdx) {
    this->bbIdx = bbIdx;
    this->stmtIdx = STMT_TERM;
}
ValueLifetime::ValueLifetime(::std::vector<bool> stmts)
    : statements(mv$(stmts)) {
}
// true if this value is used at any point
bool ValueLifetime::isUsed() const {
    for (auto v : statements) {
        if (v) {
            return true;
        }
    }
    return false;
}
bool ValueLifetime::overlaps(const ValueLifetime& x) const {
    assert(statements.size() == x.statements.size());
    for (unsigned int i = 0; i < statements.size(); i++) {
        if (statements[i] && x.statements[i]) {
            return true;
        }
    }
    return false;
}
void ValueLifetime::unify(const ValueLifetime& x) {
    assert(statements.size() == x.statements.size());
    for (unsigned int i = 0; i < statements.size(); i++) {
        if (x.statements[i]) {
            statements[i] = true;
        }
    }
}
}

namespace MIR { namespace visit {

bool Visitor::visitLvalue(const ::MIR::LValue& lv, ValUsage u) {
    if (lv.root.is_Static()) {
        visitPath(lv.root.as_Static());
    }

    for (auto& w : lv.wrappers) {
        if (w.is_Index()) {
            if (visitLvalue(LValue::newLocal(w.as_Index()), ValUsage::Read)) {
                return true;
            }
        }
    }
    return false;
}
bool VisitorMut::visitLvalue(::MIR::LValue& lv, ValUsage u) {
    if (lv.root.is_Static()) {
        visitPath(lv.root.as_Static());
    }
    for (auto& w : lv.wrappers) {
        if (w.is_Index()) {
            auto lv = LValue::newLocal(w.as_Index());
            bool rv = visitLvalue(lv, ValUsage::Read);
            ASSERT_BUG(Span(), lv.is_Local(), "visit_lvalue on Index mutated the index to a non-local");
            w = ::MIR::LValue::Wrapper::newIndex(lv.as_Local());
            if (rv) {
                return true;
            }
        }
    }
    return false;
}
}}

namespace MIR {

::std::ostream& operator<<(::std::ostream& os, const TypeResolve& x) {
    x.fmtPos(os);
    return os;
}
}
