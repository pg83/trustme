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
    if (this->stmt_idx == STMT_TERM) {
        os << "TERM";
    } else {
        os << this->stmt_idx;
    }
    os << ": ";
}

void ::MIR::TypeResolve::print_msg(const char* tag, ::std::function<void(::std::ostream& os)> cb) const {
    auto& os = ::std::cerr;
    os << "MIR " << tag << ": ";
    fmtPos(os, true);
    cb(os);
    os << ::std::endl;
    abort();
    //throw CheckFailure {};
}

unsigned int ::MIR::TypeResolve::getCurStmtOfs() const {
    if (this->stmt_idx == STMT_TERM) {
        return fcn.blocks.at(this->bbIdx).statements.size();
    } else {
        return this->stmt_idx;
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
    MIR_ASSERT(*this, v.is_Static(), "LValue::Static not a static - " << path << " : " << v.tag_str());
    MIR_ASSERT(*this, v.as_Static(), "LValue::Static is null? - " << path << " : " << v.tag_str());
    if (ms.hasTypes()) {
        tmp = ms.monomorph_type(sp, v.as_Static()->mType);
        mResolve.expandAssociatedTypes(this->sp, tmp);
        return tmp;
    } else {
        return v.as_Static()->mType;
    }
}

const ::HIR::TypeData* ::MIR::TypeResolve::getLvalueType(::HIR::TypeRef& tmp, const ::MIR::LValue& val, unsigned wrapper_skip_count /*=0*/) const {
    const ::HIR::TypeData* rv = nullptr;
    TU_MATCHA((val.root), (e), (Return, rv = monomorphedRettype ? monomorphedRettype : retType;), (Argument, MIR_ASSERT(*this, e < mArgs.size(), "Argument " << val << " out of range (" << mArgs.size() << ")"); rv = mArgs.at(e).second;), (Local, MIR_ASSERT(*this, e < fcn.locals.size(), "Local " << val << " out of range (" << fcn.locals.size() << ")"); rv = monomorphedLocals ? monomorphedLocals->at(e) : fcn.locals.at(e);), (Static, rv = getStaticType(tmp, e);))
    if (val.wrappers.size() > 0) {
        assert(wrapper_skip_count <= val.wrappers.size());
        const auto* stop_wrapper = val.wrappers.data() + (val.wrappers.size() - wrapper_skip_count);
        for (const auto& w : val.wrappers) {
            if (&w == stop_wrapper) {
                break;
            }
            rv = this->getUnwrappedType(tmp, w, rv);
        }
    } else {
        assert(wrapper_skip_count == 0);
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
                        auto maybe_monomorph = [&](const auto& ty) {
                            return mResolve.monomorph_expand_opt(sp, tmp, ty, MonomorphStatePtr(crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr));
                        };
                        TU_MATCHA((str.mData), (se), (Unit, MIR_BUG(*this, "Field on unit-like struct - " << ty);), (Tuple, MIR_ASSERT(*this, fieldIndex < se.size(), "Field index out of range in tuple-struct " << te.path); return maybe_monomorph(se[fieldIndex].ent);), (Named, MIR_ASSERT(*this, fieldIndex < se.size(), "Field index out of range in struct " << te.path); return maybe_monomorph(se[fieldIndex].ty);))
                    } else if (const auto* tep = te.binding.opt_Union()) {
                        const auto& unm = **tep;
                        auto maybe_monomorph = [&](const ::HIR::TypeData* t) -> const ::HIR::TypeData* {
                            return mResolve.monomorph_expand_opt(sp, tmp, t, MonomorphStatePtr(crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr));
                        };
                        MIR_ASSERT(*this, fieldIndex < unm.mVariants.size(), "Field index out of range for union");
                        return maybe_monomorph(unm.mVariants.at(fieldIndex).ty);
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
        TU_ARMA(Downcast, variant_index) {
        TU_MATCH_HDRA( ((*ty)), {)
        default:
            MIR_BUG(*this, "Downcast on unexpected type - " << ty);
                TU_ARMA(Path, te) {
                    MIR_ASSERT(*this, te.binding.is_Enum() || te.binding.is_Union(), "Downcast on non-Enum");
                    if (te.binding.is_Enum()) {
                        const auto& enm = *te.binding.as_Enum();
                        MIR_ASSERT(*this, enm.mData.is_Data(), "Downcast on non-data enum - " << ty);
                        const auto& variants = enm.mData.as_Data();
                        MIR_ASSERT(*this, variant_index < variants.size(), "Variant index out of range for " << ty);
                        const auto& variant = variants[variant_index];

                        const auto& var_ty = variant.type;
                        return mResolve.monomorph_expand_opt(sp, tmp, var_ty, MonomorphStatePtr(crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr));
                    } else {
                        const auto& unm = *te.binding.as_Union();
                        MIR_ASSERT(*this, variant_index < unm.mVariants.size(), "Variant index out of range");
                        const auto& variant = unm.mVariants[variant_index];
                        const auto& var_ty = variant.ty;

                        return mResolve.monomorph_expand_opt(sp, tmp, var_ty, MonomorphStatePtr(crate.types, nullptr, &te.path.mData.as_Generic().mParams, nullptr));
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
                if (monomorphise_type_needed(ty)) {
                    auto rv = p.monomorph_type(this->sp, ty);
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
            MIR_BUG(*this, "get_const_type - Function points to bad type: " << v.tag_str() << " - " << c);
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
                    if (monomorphise_type_needed(ty)) {
                        rv = p.monomorph_type(this->sp, ty);
                        mResolve.expandAssociatedTypes(this->sp, rv);
                    } else {
                        rv = ty;
                    }
                    return crate.types.borrow(HIR::BorrowType::Shared, rv);
                }
                TU_ARMA(Static, ve) {
                    const auto& ty = ve->mType;
                    HIR::TypeRef rv;
                    if (monomorphise_type_needed(ty)) {
                        rv = p.monomorph_type(this->sp, ty);
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
    return mResolve.type_is_copy(this->sp, getLvalueType(tmp, val));
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
                idx = static_cast<size_t>(fieldIdx.v.truncate_i64());
            }
            TU_ARMA(Uint, fieldIdx) {
                MIR_ASSERT(*this, fieldIdx.v.isU64() && fieldIdx.v <= U128(SIZE_MAX), "Invalid tuple field index " << fieldIdx.v);
                idx = static_cast<size_t>(fieldIdx.v.truncate_u64());
            }
            TU_ARMA(StaticString, fieldName) {
                char* end = nullptr;
                auto numeric_idx = ::std::strtoul(fieldName.c_str(), &end, 10);
                if (end != fieldName.c_str() && *end == '\0') {
                    MIR_ASSERT(*this, numeric_idx <= SIZE_MAX, "Invalid tuple field index " << fieldName);
                    idx = static_cast<size_t>(numeric_idx);
                } else if (const auto* ty_path = curTy->opt_Path()) {
                    if (const auto* bep = ty_path->binding.opt_Struct()) {
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
                    } else if (const auto* bep = ty_path->binding.opt_Union()) {
                        const auto& unm = **bep;
                        const auto& fields = unm.mVariants;
                        idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                            return x.name == fieldName;
                        }) - fields.begin();
                    } else if (const auto* bep = ty_path->binding.opt_Enum()) {
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

            bool visit_lvalue(const ::MIR::LValue& lv, ValUsage u) override {
                if (cb(lv, u)) {
                    return true;
                }
                return Visitor::visit_lvalue(lv, u);
            }
        };

        bool visit_mir_lvalue(const ::MIR::LValue& lv, ValUsage u, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
            LValueCbVisitor v{mv$(cb)};
            return v.visit_lvalue(lv, u);
        }

        bool visit_mir_lvalue(const ::MIR::Param& p, ValUsage u, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
            LValueCbVisitor v{mv$(cb)};
            return v.visit_param(p, u);
        }

        bool visit_mir_lvalues(const ::MIR::RValue& rval, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
            LValueCbVisitor v{mv$(cb)};
            return v.visit_rvalue(rval);
        }

        bool visit_mir_lvalues(const ::MIR::Statement& stmt, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
            LValueCbVisitor v{mv$(cb)};
            return v.visit_stmt(stmt);
        }

        bool visit_mir_lvalues(const ::MIR::Terminator& term, ::std::function<bool(const ::MIR::LValue&, ValUsage)> cb) {
            LValueCbVisitor v{mv$(cb)};
            return v.visit_terminator(term);
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

        void visit_terminator_target_mut(::MIR::Terminator& term, ::std::function<void(::MIR::BasicBlockId&)> cb) {
            struct TermCbVisitorMut: public VisitorMut {
                ::std::function<void(::MIR::BasicBlockId&)> cb;

                bool visit_block_id(::MIR::BasicBlockId& x) override {
                    cb(x);
                    return false;
                }
            } v;

            v.cb = std::move(cb);
            v.visit_terminator(term);
        }

        void visit_terminator_target(const ::MIR::Terminator& term, ::std::function<void(const ::MIR::BasicBlockId&)> cb) {
            visit_terminator_target_mut(const_cast<::MIR::Terminator&>(term), cb);
        }
    } // namespace visit
} // namespace MIR

// --------------------------------------------------------------------
// MIR_Helper_GetLifetimes
// --------------------------------------------------------------------
namespace {
    struct ValueLifetime {
        /// Bitmap of locations where the variable is valid
        ::std::vector<bool> stmt_bitmap;

        ValueLifetime(size_t stmt_count)
            : stmt_bitmap(stmt_count)
        {
        }

        void fill(const ::std::vector<size_t>& block_offsets, size_t bb, size_t firstStmt, size_t lastStmt) {
            size_t limit = block_offsets[bb + 1] - block_offsets[bb] - 1;
            DEBUG("bb" << bb << " : " << firstStmt << "--" << lastStmt);
            assert(firstStmt <= limit);
            assert(lastStmt <= limit);
            for (size_t stmt = firstStmt; stmt <= lastStmt; stmt++) {
                stmt_bitmap[block_offsets[bb] + stmt] = true;
            }
        }

        void dumpDebug(const char* suffix, unsigned i, const ::std::vector<size_t>& block_offsets) {
            ::std::string name = FMT(suffix << "$" << i);
            while (name.size() < 3 + 1 + 3) {
                name += " ";
            }
            DEBUG(name << " : " << FMT_CB(os, for (unsigned int j = 0; j < this->stmt_bitmap.size(); j++) {
                      if (j != 0 && ::std::find(block_offsets.begin(), block_offsets.end(), j) != block_offsets.end()) {
                          os << "|";
                      }
                      os << (this->stmt_bitmap[j] ? "X" : " ");
                  }));
        }
    };
}

#if 1 // Alternate algorithm
void MIRHelperGetLifetimesDetermineValueLifetime(::MIR::TypeResolve& state, const ::MIR::Function& fcn, size_t bbIdx, size_t stmt_idx, const ::MIR::LValue& lv, const ::std::vector<size_t>& block_offsets, const ::std::vector<bool>& use_bitmap, ValueLifetime& vl);

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

    size_t statement_count = 0;
    ::std::vector<size_t> block_offsets;
    block_offsets.reserve(fcn.blocks.size());
    for (const auto& bb : fcn.blocks) {
        block_offsets.push_back(statement_count);
        statement_count += bb.statements.size() + 1; // +1 for the terminator
    }
    block_offsets.push_back(statement_count); // Store the final limit for later code to use.

    ::std::vector<ValueLifetime> slot_lifetimes(fcn.locals.size(), ValueLifetime(statement_count));

    // - Enumerate all read positions for each slot
    std::vector<std::vector<bool>> slot_read_bitmaps(fcn.locals.size());
    {
        for (auto& b : slot_read_bitmaps) {
            b.resize(statement_count);
        }
        size_t pos = 0;
        auto use_cb = [&](const ::MIR::LValue& tlv, ValUsage vu) {
            if (tlv.root.is_Local()) {
                if (vu != ValUsage::Write) {
                    slot_read_bitmaps[tlv.root.as_Local()][pos] = true;
                }
            }
            for (const auto& w : tlv.wrappers) {
                if (w.is_Index()) {
                    slot_read_bitmaps[w.as_Index()][pos] = true;
                }
            }
            return false;
        };
        for (const auto& bb : fcn.blocks) {
            for (const auto& stmt : bb.statements) {
                visit_mir_lvalues(stmt, use_cb);
                pos++;
            }
            visit_mir_lvalues(bb.terminator, use_cb);
            pos++;
        }
    }

    // Enumerate direct assignments of variables (linear iteration of BB list)
    for (size_t bbIdx = 0; bbIdx < fcn.blocks.size(); bbIdx++) {
        auto assignedLvalue = [&](size_t bbIdx, size_t stmt_idx, const ::MIR::LValue& lv) {
            // NOTE: Fills the first statement after running, just to ensure that any assigned value has _a_ lifetime
            if (lv.is_Local()) {
                auto de = lv.root.as_Local();
                if (!mask || mask->at(de)) {
                    MIRHelperGetLifetimesDetermineValueLifetime(state, fcn, bbIdx, stmt_idx, lv, block_offsets, slot_read_bitmaps[de], slot_lifetimes[de]);
                    slot_lifetimes[de].fill(block_offsets, bbIdx, stmt_idx, stmt_idx);
                }
            }
        };

        const auto& bb = fcn.blocks[bbIdx];
        for (size_t stmt_idx = 0; stmt_idx < bb.statements.size(); stmt_idx++) {
            state.set_cur_stmt(bbIdx, stmt_idx);
            const auto& stmt = bb.statements[stmt_idx];
            if (const auto* se = stmt.opt_Assign()) {
                // For assigned variables, determine how long that value will live
                assignedLvalue(bbIdx, stmt_idx + 1, se->dst);
            } else if (const auto* se = stmt.opt_Asm()) {
                for (const auto& e : se->outputs) {
                    assignedLvalue(bbIdx, stmt_idx + 1, e.second);
                }
            }
        }
        state.set_cur_stmt_term(bbIdx);

        // Only Call can assign a value
        TU_IFLET(::MIR::Terminator, bb.terminator, Call, te, assignedLvalue(te.ret_block, 0, te.ret_val);)
    }

    // Dump out variable lifetimes.
    if (dumpDebug) {
        for (size_t i = 0; i < slot_lifetimes.size(); i++) {
            slot_lifetimes[i].dumpDebug("_", i, block_offsets);
        }
    }

    ::MIR::ValueLifetimes rv;
    rv.blockOffsets = mv$(block_offsets);
    rv.slots.reserve(slot_lifetimes.size());
    for (auto& lft : slot_lifetimes) {
        rv.slots.push_back(::MIR::ValueLifetime(mv$(lft.stmt_bitmap)));
    }
    return rv;
}

void MIRHelperGetLifetimesDetermineValueLifetime(
    ::MIR::TypeResolve& mir_res,
    const ::MIR::Function& fcn,
    size_t bbIdx,
    size_t stmt_idx, // First statement in which the value is valid (after the assignment)
    const ::MIR::LValue& lv,
    const ::std::vector<size_t>& block_offsets,
    const ::std::vector<bool>& use_bitmap,
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

        void markBorrowed(size_t stmt_idx) {
            if (!isBorrowed) {
                isBorrowed = false;
                this->fillTo(stmt_idx);
            }
            isBorrowed = true;
        }

        void markRead(size_t stmt_idx) {
            if (!isBorrowed) {
                this->fillTo(stmt_idx);
            } else {
                isBorrowed = false;
                this->fillTo(stmt_idx);
                isBorrowed = true;
            }
        }

        void fmt(::std::ostream& os) const {
            os << "BB" << bbHistory.front() << "/" << lastReadOfs << "--";
            os << "[" << bbHistory << "]";
        }

        void finalise(size_t stmt_idx) {
            if (isBorrowed) {
                isBorrowed = false;
                this->fillTo(stmt_idx);
                isBorrowed = true;
            }
        }

    private:
        void fillTo(size_t stmt_idx) {
            TRACE_FUNCTION_F(FMT_CB(ss, this->fmt(ss);));
            assert(!isBorrowed);
            assert(bbHistory.size() > 0);
            if (bbHistory.size() == 1) {
                // only one block
                outVl.fill(blockOffsets, bbHistory[0], lastReadOfs, stmt_idx);
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
                outVl.fill(blockOffsets, bbIdx, 0, stmt_idx);
            }

            lastReadOfs = stmt_idx;

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

            visitedStatements(mLifetimes.stmt_bitmap.size())
        {
            ::HIR::TypeRef tmp;
            isCopy = mirRes.mResolve.type_is_copy(mir_res.sp, mirRes.getLvalueType(tmp, lv));
        }

        void run_block(size_t bbIdx, size_t stmt_idx, State state) {
            const auto& bb = fcn.blocks.at(bbIdx);
            assert(stmt_idx <= bb.statements.size());

            bool was_moved = false;
            bool was_updated = false;
            auto visit_cb = [&](const auto& lv, auto vu) {
                if (lv.root == mLv.root) {
                    switch (vu) {
                        case ValUsage::Read:
                            DEBUG(mirRes << "Used");
                            state.markRead(stmt_idx);
                            was_updated = true;
                            break;
                        case ValUsage::Move:
                            if (lv.wrappers.size() == mLv.wrappers.size()) {
                                DEBUG(mirRes << (isCopy ? "Read" : "Moved"));
                                state.markRead(stmt_idx);
                                was_moved = !isCopy;
                            } else {
                                DEBUG(mirRes << "Used (partial)");
                                state.markRead(stmt_idx);
                                was_updated = true;
                            }
                            break;
                        case ValUsage::Borrow:
                            DEBUG(mirRes << "Borrowed");
                            state.markBorrowed(stmt_idx);
                            was_updated = true;
                            break;
                        case ValUsage::Write:
                            // Don't care
                            break;
                    }
                }
                for (const auto& w : lv.wrappers) {
                    if (w.is_Index() && mLv.is_Local() && w.as_Index() == mLv.as_Local()) {
                        DEBUG(mirRes << "Index used");
                        state.markRead(stmt_idx);
                        was_updated = true;
                    }
                }
                return false;
            };

            for (; stmt_idx < bb.statements.size(); stmt_idx++) {
                const auto& stmt = bb.statements[stmt_idx];
                mirRes.set_cur_stmt(bbIdx, stmt_idx);
                visitedStatements[blockOffsets.at(bbIdx) + stmt_idx] = true;

                // Visit and see if the value is read (setting the read flag or end depending on if the value is Copy)
                was_updated = false;
                visit_mir_lvalues(stmt, visit_cb);
                if (was_updated || was_moved) {
                    DEBUG(mirRes << stmt);
                }

                if (was_moved) {
                    // Moved: Update read position and apply
                    DEBUG(mirRes << "Moved, return");
                    state.markRead(stmt_idx);
                    state.finalise(stmt_idx);
                    return;
                }

                TU_MATCH_HDRA( (stmt), {)
                TU_ARMA(Assign, se) {
                        if (se.dst == mLv) {
                            DEBUG(mirRes << "- Assigned to, return");
                            // Value assigned, just apply
                            state.finalise(stmt_idx);
                            return;
                        }
                    }
                    TU_ARMA(Asm, se) {
                        //
                        for (const auto& e : se.outputs) {
                            if (e.second == mLv) {
                                // Assigned, just apply
                                DEBUG(mirRes << "- Assigned (asm!), return");
                                state.finalise(stmt_idx);
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
                                            state.finalise(stmt_idx);
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
            mirRes.set_cur_stmt_term(bbIdx);
            visitedStatements[blockOffsets.at(bbIdx) + stmt_idx] = true;

            was_updated = false;
            visit_mir_lvalues(bb.terminator, visit_cb);
            DEBUG(mirRes << bb.terminator << (was_updated ? " (used)" : ""));

            if (was_moved) {
                // Moved: Update read position and apply
                DEBUG(mirRes << "- Moved, return");
                state.markRead(stmt_idx);
                state.finalise(stmt_idx);
                return;
            }

            // Terminator
            TU_MATCH_HDRA( (bb.terminator), {)
            TU_ARMA(Incomplete, te) {
                    // TODO: Isn't this a bug?
                    DEBUG(mirRes << "Incomplete");
                    state.finalise(stmt_idx);
                }
                TU_ARMA(Return, te) {
                    DEBUG(mirRes << "Return");
                    state.finalise(stmt_idx);
                }
                TU_ARMA(UnwindResume, te) {
                    DEBUG(mirRes << "UnwindResume");
                    state.finalise(stmt_idx);
                }
                TU_ARMA(UnwindTerminate, te) {
                    DEBUG(mirRes << "UnwindTerminate");
                    state.finalise(stmt_idx);
                }
                TU_ARMA(Unreachable, te) {
                    DEBUG(mirRes << "Unreachable");
                    state.finalise(stmt_idx);
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
                    if (te.valid_flag != ~0u) {
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
                        state.markRead(stmt_idx);
                        state.finalise(stmt_idx);
                        return;
                    }
                    TU_IFLET(::MIR::UnwindAction, te.unwind, Cleanup, target,
                        statesToDo.push_back(::std::make_pair(target, state.clone()));
                    )
                    statesToDo.push_back(::std::make_pair(te.target, mv$(state)));
                }
                TU_ARMA(Call, te) {
                    if (te.ret_val == mLv) {
                        DEBUG(mirRes << "Assigned (Call), return");
                        // Value assigned, just apply
                        state.finalise(stmt_idx);
                        return;
                    }
                    TU_IFLET(::MIR::UnwindAction, te.unwind, Cleanup, target,
                        statesToDo.push_back(::std::make_pair(target, state.clone()));
                    )
                    statesToDo.push_back(::std::make_pair(te.ret_block, mv$(state)));
                }
            }
        }
    };

    Runner runner(mir_res, fcn, bbIdx, stmt_idx, lv, block_offsets, vl);
    ::std::vector<::std::pair<size_t, State>> post_check_list;

    // TODO: Have a bitmap of visited statements. If a visted statement is hit, stop the current state
    // - Use the same rules as loopback.

    // Fill the first statement, to ensure that there is at least one bit set.
    runner.run_block(bbIdx, stmt_idx, State(block_offsets, vl, bbIdx, stmt_idx));

    while (!runner.statesToDo.empty()) {
        auto bbIdx = runner.statesToDo.back().first;
        auto state = mv$(runner.statesToDo.back().second);
        runner.statesToDo.pop_back();

        DEBUG("state.bb_history=[" << state.bbHistory << "], -> BB" << bbIdx);
        state.bbHistory.push_back(bbIdx);

        if (runner.visitedStatements.at(block_offsets.at(bbIdx) + 0)) {
            if (vl.stmt_bitmap.at(block_offsets.at(bbIdx) + 0)) {
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
                post_check_list.push_back(::std::make_pair(bbIdx, mv$(state)));
                continue;
            }
        }


        // Special case for when doing multiple runs on the same output
        if (vl.stmt_bitmap.at(block_offsets.at(bbIdx) + 0)) {
            DEBUG("Already valid in BB" << bbIdx);
            state.markRead(0);
            state.finalise(0);
            continue;
        }

        runner.run_block(bbIdx, 0, mv$(state));
    }

    // Iterate while there are items in the post_check list
    while (!post_check_list.empty()) {
        bool change = false;
        for (auto it = post_check_list.begin(); it != post_check_list.end();) {
            auto bbIdx = it->first;
            auto& state = it->second;
            // If the target of this loopback is valid, then the entire route to the loopback must have been valid
            if (vl.stmt_bitmap.at(block_offsets.at(bbIdx) + 0)) {
                change = true;
                DEBUG("Looped (now valid)");
                state.markRead(0);
                state.finalise(0);

                it = post_check_list.erase(it);
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
        size_t path_index = 0; // index into the block path.
        unsigned int stmt_idx = 0;

        bool operator==(const Position& x) const {
            return path_index == x.path_index && stmt_idx == x.stmt_idx;
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
        ::std::vector<ProtoLifetime> tmp_ends;
        ::std::vector<ProtoLifetime> var_ends;

        State(const ::MIR::Function& fcn)
            : tmp_ends(fcn.temporaries.size(), ProtoLifetime())
            , var_ends(fcn.named_variables.size(), ProtoLifetime())
        {
        }

        State clone() const {
            auto rv = *this;
            rv.index = ++NEXT_INDEX;
            return rv;
        }
    };

    NEXT_INDEX = 0;

    size_t statement_count = 0;
    ::std::vector<size_t> block_offsets;
    block_offsets.reserve(fcn.blocks.size());
    for (const auto& bb : fcn.blocks) {
        block_offsets.push_back(statement_count);
        statement_count += bb.statements.size() + 1; // +1 for the terminator
    }

    ::std::vector<ValueLifetime> temporary_lifetimes(fcn.temporaries.size(), ValueLifetime(statement_count));
    ::std::vector<ValueLifetime> variable_lifetimes(fcn.named_variables.size(), ValueLifetime(statement_count));

    struct BlockSeenLifetimes {
        bool hasState = false;
        const ::std::vector<size_t>& block_offsets;
        ::std::vector<::std::vector<unsigned int>> tmp;
        ::std::vector<::std::vector<unsigned int>> var;

        BlockSeenLifetimes(const ::std::vector<size_t>& block_offsets, const ::MIR::Function& fcn)
            : block_offsets(block_offsets)
            , tmp(fcn.temporaries.size())
            , var(fcn.named_variables.size())
        {
        }

        bool has_state() const {
            return hasState;
        }

        bool try_merge(const State& val_state) const {
            // TODO: This logic isn't quite correct. Just becase a value's existing end is already marked as valid,
            // doesn't mean that we have no new information.
            // - Wait, doesn't it?
            auto try_merge_lft = [&](const ProtoLifetime& lft, const ::std::vector<unsigned int>& seen) -> bool {
                if (lft.is_empty()) {
                    return false;
                }
                // TODO: What should be done for borrow flagged values
                if (lft.is_borrowed()) {
                    return false;
                }
                auto endIdx = block_offsets.at(val_state.blockPath.at(lft.end.path_index)) + lft.end.stmt_idx;

                auto it = ::std::find(seen.begin(), seen.end(), endIdx);
                return (it == seen.end());
            };
            for (size_t i = 0; i < val_state.tmp_ends.size(); i++) {
                if (try_merge_lft(val_state.tmp_ends[i], this->tmp[i])) {
                    return true;
                }
            }
            for (size_t i = 0; i < val_state.var_ends.size(); i++) {
                if (try_merge_lft(val_state.var_ends[i], this->var[i])) {
                    return true;
                }
            }
            return false;
        }

        bool merge(const State& val_state) {
            bool rv = false;
            auto merge_lft = [&](const ProtoLifetime& lft, ::std::vector<unsigned int>& seen) -> bool {
                if (lft.is_empty()) {
                    return false;
                }
                // TODO: What should be done for borrow flagged values
                if (lft.end == Position{~0u, ~0u}) {
                    return false;
                }
                auto endIdx = block_offsets.at(val_state.blockPath.at(lft.end.path_index)) + lft.end.stmt_idx;

                auto it = ::std::find(seen.begin(), seen.end(), endIdx);
                if (it == seen.end()) {
                    seen.push_back(endIdx);
                    return true;
                } else {
                    return false;
                }
            };
            for (size_t i = 0; i < val_state.tmp_ends.size(); i++) {
                rv |= merge_lft(val_state.tmp_ends[i], this->tmp[i]);
            }
            for (size_t i = 0; i < val_state.var_ends.size(); i++) {
                rv |= merge_lft(val_state.var_ends[i], this->var[i]);
            }
            hasState = true;
            return rv;
        }
    };

    ::std::vector<BlockSeenLifetimes> blockSeenLifetimes(fcn.blocks.size(), BlockSeenLifetimes(block_offsets, fcn));

    State initState(fcn);

    ::std::vector<::std::pair<unsigned int, State>> todo_queue;
    todo_queue.push_back(::std::make_pair(0, mv$(initState)));

    while (!todo_queue.empty()) {
        auto bbIdx = todo_queue.back().first;
        auto val_state = mv$(todo_queue.back().second);
        todo_queue.pop_back();
        state.set_cur_stmt(bbIdx, 0);

        // Fill alive time in the bitmap
        // TODO: Maybe also store the range (as a sequence of {block,start,end})
        auto addLifetimeS = [&](State& val_state, const ::MIR::LValue& lv, const Position& start, const Position& end) {
            assert(start.path_index <= end.path_index);
            assert(start.path_index < end.path_index || start.stmt_idx <= end.stmt_idx);
            if (start.path_index == end.path_index && start.stmt_idx == end.stmt_idx) {
                return;
            }
            DEBUG("[add_lifetime] " << lv << " (" << start.path_index << "," << start.stmt_idx << ") -- (" << end.path_index << "," << end.stmt_idx << ")");
            ValueLifetime* lft;
            if (const auto* e = lv.opt_Temporary()) {
                lft = &temporary_lifetimes[e->idx];
            } else if (const auto* e = lv.opt_Variable()) {
                lft = &variable_lifetimes[*e];
            } else {
                MIR_TODO(state, "[add_lifetime] " << lv);
                return;
            }

            // Fill lifetime map for this temporary in the indicated range
            bool didSet = false;
            unsigned int j = start.stmt_idx;
            unsigned int i = start.path_index;
            while (i <= end.path_index && i < val_state.blockPath.size()) {
                auto bbIdx = val_state.blockPath.at(i);
                const auto& bb = fcn.blocks[bbIdx];
                MIR_ASSERT(state, j <= bb.statements.size(), "");
                MIR_ASSERT(state, bbIdx < block_offsets.size(), "");

                auto blockBase = block_offsets.at(bbIdx);
                auto idx = blockBase + j;
                if (!lft->stmt_bitmap.at(idx)) {
                    lft->stmt_bitmap[idx] = true;
                    didSet = true;
                }

                if (i == end.path_index && j == (end.stmt_idx != ~0u ? end.stmt_idx : bb.statements.size())) {
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
                DEBUG("[add_lifetime] " << lv << " (" << start.path_index << "," << start.stmt_idx << ") -- (" << end.path_index << "," << end.stmt_idx << ") - New information");
                val_state.curChangeIdx += 1;
            }
        };
        auto addLifetime = [&](const ::MIR::LValue& lv, const Position& start, const Position& end) {
            addLifetimeS(val_state, lv, start, end);
        };

        auto applyState = [&](State& state) {
            // Apply all changes in this state, just in case there was new information
            for (unsigned i = 0; i < fcn.temporaries.size(); i++) {
                addLifetimeS(state, ::MIR::LValue::make_Temporary({i}), state.tmp_ends[i].start, state.tmp_ends[i].end);
            }
            for (unsigned i = 0; i < fcn.named_variables.size(); i++) {
                addLifetimeS(state, ::MIR::LValue::make_Variable({i}), state.var_ends[i].start, state.var_ends[i].end);
            }
        };
        auto addToVisit = [&](unsigned int new_bb_idx, State new_state) {
            auto& bbMemoryEnt = blockSeenLifetimes[new_bb_idx];
            if (!bbMemoryEnt.has_state()) {
                // No recorded state, needs to be visited
                DEBUG(state << " state" << new_state.index << " -> bb" << new_bb_idx << " (no existing state)");
            } else if (bbMemoryEnt.try_merge(new_state)) {
                // This state has new information, needs to be visited
                DEBUG(state << " state" << new_state.index << " -> bb" << new_bb_idx << " (new info)");
            } else {
                // Skip
                // TODO: Acquire from the target block the actual end of any active lifetimes, then apply them.
                DEBUG(state << " state" << new_state.index << " -> bb" << new_bb_idx << " - No new state, no push");
                // - For all variables currently active, check if they're valid in the first statement of the target block.
                // - If so, mark as valid at the end of the current block
                auto bmIdx = block_offsets[new_bb_idx];
                Position cur_pos;
                cur_pos.path_index = val_state.blockPath.size() - 1;
                cur_pos.stmt_idx = fcn.blocks[bbIdx].statements.size();
                for (unsigned i = 0; i < fcn.temporaries.size(); i++) {
                    if (!new_state.tmp_ends[i].is_empty() && temporary_lifetimes[i].stmt_bitmap[bmIdx]) {
                        DEBUG("- tmp$" << i << " - Active in target, assume active");
                        new_state.tmp_ends[i].end = cur_pos;
                    }
                }
                for (unsigned i = 0; i < fcn.named_variables.size(); i++) {
                    if (!new_state.var_ends[i].is_empty() && variable_lifetimes[i].stmt_bitmap[bmIdx]) {
                        DEBUG("- var$" << i << " - Active in target, assume active");
                        new_state.var_ends[i].end = cur_pos;
                    }
                }
                // - Apply whatever state was still active
                applyState(new_state);
                return;
            }
            todo_queue.push_back(::std::make_pair(new_bb_idx, mv$(new_state)));
        };

        // Compare this state to a composite list of lifetimes seen in this block
        // - Just compares the end of each proto lifetime
        {
            auto& bbMemoryEnt = blockSeenLifetimes[bbIdx];
            bool hadState = bbMemoryEnt.has_state();
            bool hasNew = bbMemoryEnt.merge(val_state);

            if (!hasNew && hadState) {
                DEBUG(state << " state" << val_state.index << " - No new entry state");
                applyState(val_state);

                continue;
            }
        }

        // Check if this state has visited this block before, and if anything changed since last time
        {
            auto it = ::std::find(val_state.blockPath.rbegin(), val_state.blockPath.rend(), bbIdx);
            if (it != val_state.blockPath.rend()) {
                auto idx = &*it - &val_state.blockPath.front();
                if (val_state.blockChangeIdx[idx] == val_state.curChangeIdx) {
                    DEBUG(state << " " << val_state.index << " Loop and no change");
                    continue;
                } else {
                    assert(val_state.blockChangeIdx[idx] < val_state.curChangeIdx);
                    DEBUG(state << " " << val_state.index << " --- Loop, " << val_state.curChangeIdx - val_state.blockChangeIdx[idx] << " changes");
                }
            } else {
                DEBUG(state << " " << val_state.index << " ---");
            }
            val_state.blockPath.push_back(bbIdx);
            val_state.blockChangeIdx.push_back(val_state.curChangeIdx);
        }

        Position cur_pos;
        cur_pos.path_index = val_state.blockPath.size() - 1;
        cur_pos.stmt_idx = 0;
        auto lvalueRead = [&](const ::MIR::LValue& lv) {
            ProtoLifetime* slot;
            if (const auto* e = lv.opt_Temporary()) {
                slot = &val_state.tmp_ends.at(e->idx);
            } else if (const auto* e = lv.opt_Variable()) {
                slot = &val_state.var_ends.at(*e);
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
                slot = &val_state.tmp_ends.at(e->idx);
            } else if (const auto* e = lv.opt_Variable()) {
                slot = &val_state.var_ends.at(*e);
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
                slot = &val_state.tmp_ends.at(e->idx);
            } else if (const auto* e = lv.opt_Variable()) {
                slot = &val_state.var_ends.at(*e);
            } else {
                return;
            }
            // TODO: Flag this value as currently being borrowed (a flag that never clears)
            slot->end = Position{~0u, ~0u};
        };
        auto visit_lval_cb = [&](const auto& lv, ValUsage vu) -> bool {
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
            auto stmt_idx = &stmt - &fcn.blocks[bbIdx].statements.front();
            cur_pos.stmt_idx = stmt_idx;
            state.set_cur_stmt(bbIdx, stmt_idx);
            DEBUG(state << " " << stmt);

            if (const auto* e = stmt.opt_Drop()) {
                visit_mir_lvalues(stmt, [&](const auto& lv, ValUsage vu) -> bool {
                    if (vu == ValUsage::Read) {
                        lvalueRead(lv);
                    }
                    return false;
                });
                lvalueRead(e->slot);
                lvalueSet(e->slot);
            } else {
                visit_mir_lvalues(stmt, visit_lval_cb);
            }
        }
        cur_pos.stmt_idx = fcn.blocks[bbIdx].statements.size();

        state.set_cur_stmt_term(bbIdx);
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
             applyState(val_state);),
            (UnwindResume, applyState(val_state);),
            (UnwindTerminate, applyState(val_state);),
            (Unreachable, applyState(val_state);),
            (Goto, addToVisit(e, mv$(val_state));),
            (If, visit_mir_lvalue(e.cond, ValUsage::Read, visit_lval_cb);

             // Push blocks
             addToVisit(e.bb0, val_state.clone());
             addToVisit(e.bb1, mv$(val_state));),
            (
                Switch, visit_mir_lvalue(e.val, ValUsage::Read, visit_lval_cb); ::std::set<unsigned int> tgts; for (const auto& tgt : e.targets) tgts.insert(tgt);

                for (const auto& tgt : tgts) {
                    auto vs = (tgt == *tgts.rbegin() ? mv$(val_state) : val_state.clone());
                    addToVisit(tgt, mv$(vs));
                }
            ),
            (Drop, visit_mir_lvalue(e.slot, ValUsage::Move, visit_lval_cb); TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target, addToVisit(target, val_state.clone());) addToVisit(e.target, mv$(val_state));),
            (Call, if (const auto* f = e.fcn.opt_Value()) visit_mir_lvalue(*f, ValUsage::Read, visit_lval_cb); for (const auto& arg : e.args) if (const auto* e = arg.opt_LValue()) visit_mir_lvalue(*e, ValUsage::Read, visit_lval_cb);

             // Push blocks (with return valid only in one)
             TU_IFLET(::MIR::UnwindAction, e.unwind, Cleanup, target, addToVisit(target, val_state.clone());)

             // TODO: If the function returns !, don't follow the ret_block
             lvalueSet(e.ret_val);
             addToVisit(e.ret_block, mv$(val_state));)
        )
    }

    // Dump out variable lifetimes.
    if (dumpDebug) {
        for (unsigned int i = 0; i < temporary_lifetimes.size(); i++) {
            temporary_lifetimes[i].dumpDebug("tmp", i, block_offsets);
        }
        for (unsigned int i = 0; i < variable_lifetimes.size(); i++) {
            variable_lifetimes[i].dumpDebug("var", i, block_offsets);
        }
    }

    // Move lifetime bitmaps into the variable for the below code
    ::MIR::ValueLifetimes rv;
    rv.blockOffsets = mv$(block_offsets);
    rv.temporaries.reserve(temporary_lifetimes.size());
    for (auto& lft : temporary_lifetimes) {
        rv.temporaries.push_back(::MIR::ValueLifetime(mv$(lft.stmt_bitmap)));
    }
    rv.variables.reserve(variable_lifetimes.size());
    for (auto& lft : variable_lifetimes) {
        rv.variables.push_back(::MIR::ValueLifetime(mv$(lft.stmt_bitmap)));
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
void TypeResolve::set_cur_stmt(const ::MIR::BasicBlock& bb, const ::MIR::Statement& stmt) {
    assert(&stmt >= &bb.statements.front());
    assert(&stmt <= &bb.statements.back());
    this->set_cur_stmt(bb, &stmt - bb.statements.data());
}
void TypeResolve::set_cur_stmt(const ::MIR::BasicBlock& bb, unsigned int stmt_idx) {
    assert(&bb >= &fcn.blocks.front());
    assert(&bb <= &fcn.blocks.back());
    this->set_cur_stmt(&bb - fcn.blocks.data(), stmt_idx);
}
void TypeResolve::set_cur_stmt(unsigned int bbIdx, unsigned int stmt_idx) {
    this->bbIdx = bbIdx;
    this->stmt_idx = stmt_idx;
}
void TypeResolve::set_cur_stmt_term(const ::MIR::BasicBlock& bb) {
    assert(&bb >= &fcn.blocks.front());
    assert(&bb <= &fcn.blocks.back());
    this->set_cur_stmt_term(&bb - fcn.blocks.data());
}
void TypeResolve::set_cur_stmt_term(unsigned int bbIdx) {
    this->bbIdx = bbIdx;
    this->stmt_idx = STMT_TERM;
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

bool Visitor::visit_lvalue(const ::MIR::LValue& lv, ValUsage u) {
    if (lv.root.is_Static()) {
        visit_path(lv.root.as_Static());
    }

    for (auto& w : lv.wrappers) {
        if (w.is_Index()) {
            if (visit_lvalue(LValue::newLocal(w.as_Index()), ValUsage::Read)) {
                return true;
            }
        }
    }
    return false;
}
bool VisitorMut::visit_lvalue(::MIR::LValue& lv, ValUsage u) {
    if (lv.root.is_Static()) {
        visit_path(lv.root.as_Static());
    }
    for (auto& w : lv.wrappers) {
        if (w.is_Index()) {
            auto lv = LValue::newLocal(w.as_Index());
            bool rv = visit_lvalue(lv, ValUsage::Read);
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
