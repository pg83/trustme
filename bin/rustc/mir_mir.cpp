#include "mir_mir.h"
#include <algorithm> // std::min
#include "hir_typeck_monomorph.h"
#include "hir_encoded_literal.h"
#include "trans_target.h" // Target_GetPointerBits

namespace MIR {
    ::std::ostream& operator<<(::std::ostream& os, const Constant& v) {
        TU_MATCHA(
            (v),
            (e),
            (Int, os << (e.v < 0 ? "-" : "+"); os << (e.v < 0 ? -e.v : e.v); os << " " << e.t;),
            (Uint, os << std::hex << "0x" << e.v << std::dec; os << " " << e.t;),
            (Float, os << std::hexfloat << e.v << std::defaultfloat; os << " " << e.t;),
            (Bool, os << (e.v ? "true" : "false");),
            (
                Bytes, os << "b\""; os << ::std::hex; for (auto v : e) {
                    if (v == '\\' || v == '"') {
                        os << "\\" << v;
                    } else if (' ' <= v && v < 0x7F) {
                        os << v;
                    } else if (v < 16) {
                        os << "\\x0" << (unsigned int)v;
                    } else {
                        os << "\\x" << ((unsigned int)v & 0xFF);
                    }
                } os << "\"";
                os << ::std::dec;
            ),
            (StaticString, os << "\"" << FmtEscaped(e) << "\"";),
            (Const, assert(e.p); os << *e.p;),
            (Generic, os << e;),
            (Function, assert(e.p); os << "fn " << *e.p;),
            (ItemAddr, if (e) { os << "&" << *e; if (e.offset != U128(0)) os << "+0x" << std::hex << e.offset << std::dec; } else {
                os << "#UNSIZE_PLACEHOLDER"; // A `Const` with `nullptr` is a placeholder for MakeDst `Unsize`
            })
        )
        return os;
    }

    ::Ordering Constant::ord(const Constant& b) const {
        if (this->tag() != b.tag()) {
            return ::ord(static_cast<unsigned int>(this->tag()), static_cast<unsigned int>(b.tag()));
        }
        TU_MATCHA((*this, b), (ae, be), (Int, if (ae.v != be.v) return ::ord(ae.v, be.v); return ::ord((unsigned)ae.t, (unsigned)be.t);), (Uint, if (ae.v != be.v) return ::ord(ae.v, be.v); return ::ord((unsigned)ae.t, (unsigned)be.t);), (Float, if (ae.v != be.v) return ae.v > be.v ? OrdGreater : OrdLess; return ::ord((unsigned)ae.t, (unsigned)be.t);), (Bool, return ::ord(ae.v, be.v);), (Bytes, return ::ord(ae, be);), (StaticString, return ::ord(ae, be);), (Const, return ::ord(*ae.p, *be.p);), (Generic, return ::ord(ae.binding, be.binding);), (Function, return ::ord(*ae.p, *be.p);), (ItemAddr, ORD(static_cast<bool>(ae), static_cast<bool>(be)); if (ae) ORD(*ae, *be); ORD(ae.offset, be.offset); return OrdEqual;))
        throw "";
    }

    void LValue::RefCommon::fmt(::std::ostream& os) const {
        os << mLv->root;
        for (size_t i = 0; i < wrapperCount; i++) {
            os << mLv->wrappers.at(i);
        }
    }

    ::std::ostream& operator<<(::std::ostream& os, const LValue& x) {
        LValue::CRef(x).fmt(os);
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const LValue::Storage& r) {
        TU_MATCHA((r), (e), (Return, os << "retval";), (Argument, os << "a" << e;), (Local, os << "_" << e;), (Static, os << "(" << e << ")";))
        return os;
    }

    ::std::ostream& operator<<(::std::ostream& os, const LValue::Wrapper& w) {
        TU_MATCHA((w), (e), (Field, os << "." << e;), (Deref, os << "*";), (Index, os << "[_" << e << "]";), (Downcast, os << "#" << e;))
        return os;
    }

    Ordering LValue::Storage::ord(const LValue::Storage& x) const {
        if (x.is_Static()) {
            if (this->is_Static()) {
                return this->as_Static().ord(x.as_Static());
            } else {
                return OrdLess;
            }
        } else {
            if (this->is_Static()) {
                return OrdGreater;
            }
        }

        return ::ord(this->val, x.val);
    }

    Ordering LValue::ord(const LValue& x) const {
        auto rv = root.ord(x.root);
        if (rv != OrdEqual) {
            return rv;
        }
        return ::ord(wrappers, x.wrappers);
    }

    Ordering LValue::RefCommon::ord(const LValue::RefCommon& x) const {
        Ordering rv;
        //TRACE_FUNCTION_FR(FMT_CB(ss, this->fmt(ss); ss << " ? "; x.fmt(ss);), rv);
        rv = mLv->root.ord(x.mLv->root);
        if (rv != OrdEqual) {
            return rv;
        }
        for (size_t i = 0; i < ::std::min(wrapperCount, x.wrapperCount); i++) {
            rv = mLv->wrappers[i].ord(x.mLv->wrappers[i]);
            if (rv != OrdEqual) {
                return rv;
            }
        }
        return (rv = ::ord(wrapperCount, x.wrapperCount));
    }

    ::std::ostream& operator<<(::std::ostream& os, const Param& x) {
        TU_MATCHA((x), (e), (LValue, os << e;), (Borrow, os << "Borrow(" << e.type << ", " << e.val << ")";), (Constant, os << e;))
        return os;
    }

    bool Param::operator==(const Param& x) const {
        if (this->tag() != x.tag()) {
            return false;
        }
        TU_MATCHA((*this, x), (ea, eb), (LValue, return ea == eb;), (Borrow, return ea.type == eb.type && ea.val == eb.val;), (Constant, return ea == eb;))
        throw "";
    }

    ::std::ostream& operator<<(::std::ostream& os, const RValue& x) {
        TU_MATCHA(
            (x),
            (e),
            (Use, os << "Use(" << e << ")";),
            (Constant, os << "Constant(" << e << ")";),
            (SizedArray, os << "SizedArray(" << e.val << "; " << e.count << ")";),
            (Borrow, os << "Borrow(" << e.type << ", " << e.val << ")";),
            (Cast, os << "Cast(" << e.val << " as " << e.type << ")";),
            (
                BinOp, os << "BinOp(" << e.val_l << " "; switch (e.op) {
                    case ::MIR::eBinOp::ADD:
                        os << "ADD";
                        break;
                    case ::MIR::eBinOp::SUB:
                        os << "SUB";
                        break;
                    case ::MIR::eBinOp::MUL:
                        os << "MUL";
                        break;
                    case ::MIR::eBinOp::DIV:
                        os << "DIV";
                        break;
                    case ::MIR::eBinOp::MOD:
                        os << "MOD";
                        break;
                    case ::MIR::eBinOp::ADD_OV:
                        os << "ADD_OV";
                        break;
                    case ::MIR::eBinOp::SUB_OV:
                        os << "SUB_OV";
                        break;
                    case ::MIR::eBinOp::MUL_OV:
                        os << "MUL_OV";
                        break;
                    case ::MIR::eBinOp::DIV_OV:
                        os << "DIV_OV";
                        break;

                    case ::MIR::eBinOp::BIT_OR:
                        os << "BIT_OR";
                        break;
                    case ::MIR::eBinOp::BIT_AND:
                        os << "BIT_AND";
                        break;
                    case ::MIR::eBinOp::BIT_XOR:
                        os << "BIT_XOR";
                        break;
                    case ::MIR::eBinOp::BIT_SHL:
                        os << "BIT_SHL";
                        break;
                    case ::MIR::eBinOp::BIT_SHR:
                        os << "BIT_SHR";
                        break;

                    case ::MIR::eBinOp::EQ:
                        os << "EQ";
                        break;
                    case ::MIR::eBinOp::NE:
                        os << "NE";
                        break;
                    case ::MIR::eBinOp::GT:
                        os << "GT";
                        break;
                    case ::MIR::eBinOp::GE:
                        os << "GE";
                        break;
                    case ::MIR::eBinOp::LT:
                        os << "LT";
                        break;
                    case ::MIR::eBinOp::LE:
                        os << "LE";
                        break;
                } os << " "
                     << e.val_r << ")";
            ),
            (
                UniOp, os << "UniOp(" << e.val << " "; switch (e.op) {
                    case ::MIR::eUniOp::INV:
                        os << "INV";
                        break;
                    case ::MIR::eUniOp::NEG:
                        os << "NEG";
                        break;
                } os << ")";
            ),
            (DstMeta, os << "DstMeta(" << e.val << ")";),
            (DstPtr, os << "DstPtr(" << e.val << ")";),
            (MakeDst, os << "MakeDst(" << e.ptrVal << ", " << e.metaVal << ")";),
            (Tuple, os << "Tuple(" << e.vals << ")";),
            (Array, os << "Array(" << e.vals << ")";),
            (UnionVariant, os << "UnionVariant(" << e.path << " #" << e.index << ", " << e.val << ")";),
            (EnumVariant, os << "Variant(" << e.path << " #" << e.index << ", {" << e.vals << "})";),
            (Struct, os << "Struct(" << e.path << ", {" << e.vals << "})";)
        )
        return os;
    }

    bool operator==(const RValue& a, const RValue& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        TU_MATCHA((a, b), (are, bre), (Use, return are == bre;), (Constant, return are == bre;), (SizedArray, if (are.val != bre.val) return false; if (are.count != bre.count) return false; return true;), (Borrow, if (are.type != bre.type) return false; if (are.val != bre.val) return false; return true;), (Cast, if (are.type != bre.type) return false; if (are.val != bre.val) return false; return true;), (BinOp, if (are.val_l != bre.val_l) return false; if (are.op != bre.op) return false; if (are.val_r != bre.val_r) return false; return true;), (UniOp, if (are.op != bre.op) return false; if (are.val != bre.val) return false; return true;), (DstPtr, return are.val == bre.val;), (DstMeta, return are.val == bre.val;), (MakeDst, if (are.metaVal != bre.metaVal) return false; if (are.ptrVal != bre.ptrVal) return false; return true;), (Tuple, return are.vals == bre.vals;), (Array, return are.vals == bre.vals;), (UnionVariant, if (are.path != bre.path) return false; if (are.index != bre.index) return false; return are.val == bre.val;), (EnumVariant, if (are.path != bre.path) return false; if (are.index != bre.index) return false; return are.vals == bre.vals;), (Struct, if (are.path != bre.path) return false; return are.vals == bre.vals;))
        throw "";
    }

    ::std::ostream& operator<<(::std::ostream& os, const Terminator& x) {
        auto fmtUnwind = [&os](const UnwindAction& action) {
            TU_MATCHA((action), (ue),
                (Continue, os << "continue";),
                (Cleanup, os << "cleanup bb" << ue;),
                (Terminate, os << "terminate";),
                (Unreachable, os << "unreachable";)
            )
        };
        TU_MATCHA((x), (e),
            (Incomplete, os << "Invalid";),
            (Return, os << "Return";),
            (UnwindResume, os << "UnwindResume";),
            (UnwindTerminate, os << "UnwindTerminate";),
            (Unreachable, os << "Unreachable";),
            (Goto, os << "Goto(" << e << ")";),
            (If, os << "If( " << e.cond << " : " << e.bbTrue << ", " << e.bbFalse << ")";),
            (Switch, os << "Switch( "; if (e.valid_flag != ~0u) os << "IF df$" << e.valid_flag << " ELSE bb" << e.invalidTarget << ", "; os << e.val << " : "; for (unsigned int j = 0; j < e.targets.size(); j++) os << j << " => bb" << e.targets[j] << ", "; os << ")";),
            (SwitchValue, os << "SwitchValue( " << e.val << " : "; TU_MATCHA((e.values), (ve), (Unsigned, for (unsigned int j = 0; j < e.targets.size(); j++) os << ve[j] << " => bb" << e.targets[j] << ", ";), (Signed, for (unsigned int j = 0; j < e.targets.size(); j++) os << (ve[j] >= 0 ? "+" : "") << ve[j] << " => bb" << e.targets[j] << ", ";), (String, for (unsigned int j = 0; j < e.targets.size(); j++) os << "\"" << ve[j] << "\" => bb" << e.targets[j] << ", ";), (ByteString, for (unsigned int j = 0; j < e.targets.size(); j++) os << "b\"" << ve[j] << "\" => bb" << e.targets[j] << ", ";)) os << "else bb" << e.defTarget << ")";),
            (Drop, os << "Drop(" << e.slot; if (e.kind == eDropKind::SHALLOW) os << " SHALLOW"; if (e.flagIdx != ~0u) os << " IF df$" << e.flagIdx; os << ") -> bb" << e.target << " unwind "; fmtUnwind(e.unwind);),
            (Call, os << "Call( " << e.retVal << " = "; TU_MATCHA((e.fcn), (e2), (Value, os << "(" << e2 << ")";), (Path, os << e2;), (Intrinsic, os << "\"" << e2.name << "\"::" << e2.params;)) os << "( "; for (const auto& arg : e.args) os << arg << ", "; os << "), bb" << e.retBlock << ", "; fmtUnwind(e.unwind); os << ")";)
        )

        return os;
    }

    bool operator==(const Terminator& a, const Terminator& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        auto unwind_equal = [](const UnwindAction& lhs, const UnwindAction& rhs) {
            if (lhs.tag() != rhs.tag()) return false;
            TU_MATCHA((lhs, rhs), (le, re), (Continue, return true;), (Cleanup, return le == re;), (Terminate, return true;), (Unreachable, return true;))
            return false;
        };
        TU_MATCHA((a, b), (ae, be), (Incomplete, ), (Return, ), (UnwindResume, ), (UnwindTerminate, ), (Unreachable, ), (Goto, if (ae != be) return false;), (If, if (ae.cond != be.cond) return false; if (ae.bbTrue != be.bbTrue) return false; if (ae.bbFalse != be.bbFalse) return false;), (Switch, if (ae.val != be.val) return false; if (ae.targets != be.targets) return false; if (ae.valid_flag != be.valid_flag) return false; if (ae.invalidTarget != be.invalidTarget) return false;), (SwitchValue, if (ae.val != be.val) return false; if (ae.targets != be.targets) return false; if (ae.defTarget != be.defTarget) return false; if (ae.values != be.values) return false;), (Drop, if (ae.kind != be.kind || ae.slot != be.slot || ae.flagIdx != be.flagIdx || ae.target != be.target || !unwind_equal(ae.unwind, be.unwind)) return false;), (Call, if (ae.retVal != be.retVal) return false; if (ae.fcn.tag() != be.fcn.tag()) return false; TU_MATCHA((ae.fcn, be.fcn), (afe, bfe), (Value, if (afe != bfe) return false;), (Path, if (afe != bfe) return false;), (Intrinsic, if (afe.name != bfe.name) return false; if (afe.params != bfe.params) return false;)) if (ae.args != be.args) return false; if (ae.retBlock != be.retBlock) return false; if (!unwind_equal(ae.unwind, be.unwind)) return false;))
        return true;
    }

    bool operator==(const AsmParam& a, const AsmParam& b) {
        if (a.tag() != b.tag()) {
            return false;
        }
        TU_MATCH_HDRA( (a,b), {)
        TU_ARMA(Const, ae, be) {
                return ae == be;
            }
            TU_ARMA(Sym, ae, be) {
                return ae == be;
            }
            TU_ARMA(Reg, ae, be) {
                if (ae.dir != be.dir) {
                    return false;
                }
                if (ae.spec != be.spec) {
                    return false;
                }
                if (!!ae.input != !!be.input) {
                    return false;
                }
                if (ae.input && *ae.input != *be.input) {
                    return false;
                }
                if (!!ae.output != !!be.output) {
                    return false;
                }
                if (ae.output && *ae.output != *be.output) {
                    return false;
                }
            }
        }
        return true;
    }

    ::std::ostream& operator<<(::std::ostream& os, const Statement& x) {
        TU_MATCH_HDRA( (x), {)
        TU_ARMA(Assign, e) {
                os << e.dst << " = " << e.src;
            }
            TU_ARMA(Asm, e) {
                os << "(";
                for (const auto& spec : e.outputs) {
                    os << "\"" << spec.first << "\" : " << spec.second << ", ";
                }
                os << ") = llvm_asm!(\"" << FmtEscaped(e.tpl) << "\", input=( ";
                for (const auto& spec : e.inputs) {
                    os << "\"" << spec.first << "\" : " << spec.second << ", ";
                }
                os << "), clobbers=[" << e.clobbers << "], flags=[" << e.flags << "])";
            }
            TU_ARMA(Asm2, e) {
                os << "asm!(";
                for (const auto& l : e.lines) {
                    if (&l != &e.lines.front()) {
                        os << " ";
                    }
                    l.fmt(os);
                }
                for (const auto& p : e.params) {
                    os << ", ";
                TU_MATCH_HDRA( (p), { )
                TU_ARMA(Const, v) {
                            os << "const " << v;
                        }
                        TU_ARMA(Sym, v) {
                            os << "sym " << v;
                        }
                        TU_ARMA(Reg, v) {
                            os << "reg " << v.dir << " " << v.spec << " ";
                            if (v.input) {
                                os << *v.input;
                            } else {
                                os << "_";
                            }
                            os << " => ";
                            if (v.output) {
                                os << *v.output;
                            } else {
                                os << "_";
                            }
                        }
                }
                }
                if (e.options.any()) {
                    os << ", ";
                    e.options.fmt(os);
                }
                os << ")";
            }
            TU_ARMA(SetDropFlag, e) {
                os << "df$" << e.idx << " = ";
                if (e.other == ~0u) {
                    os << e.newVal;
                } else {
                    os << (e.newVal ? "!" : "") << "df$" << e.other;
                }
            }
            TU_ARMA(SaveDropFlag, e) {
                os << "SaveDropFlag()";
            }
            TU_ARMA(LoadDropFlag, e) {
                os << "LoadDropFlag()";
            }
            TU_ARMA(ScopeEnd, e) {
                os << "ScopeEnd(";
                for (auto idx : e.slots) {
                    os << "_$" << idx << ",";
                }
                os << ")";
            }
        }
        return os;
    }

    bool operator==(const Statement& a, const Statement& b) {
        if (a.tag() != b.tag()) {
            return false;
        }

        TU_MATCH_HDRA( (a,b), {)
        TU_ARMA(Assign, ae,be) {
                return ae.dst == be.dst && ae.src == be.src;
            }
            TU_ARMA(Asm, ae, be) {
                return ae.outputs == be.outputs && ae.inputs == be.inputs && ae.clobbers == be.clobbers && ae.flags == be.flags;
            }
            TU_ARMA(Asm2, ae, be) {
                return ae.lines == be.lines && ae.options == be.options && ae.params == be.params;
            }
            TU_ARMA(SetDropFlag, ae, be) {
                return ae.idx == be.idx && ae.other == be.other && ae.newVal == be.newVal;
            }
            TU_ARMA(SaveDropFlag, ae, be) {
                return ae.idx == be.idx && ae.slot == be.slot && ae.bitIndex == be.bitIndex;
            }
            TU_ARMA(LoadDropFlag, ae, be) {
                return ae.idx == be.idx && ae.slot == be.slot && ae.bitIndex == be.bitIndex;
            }
            TU_ARMA(ScopeEnd, ae, be) {
                return ae.slots == be.slots;
            }
        }
        throw "";
    }
}

::MIR::LValue::Storage MIR::LValue::Storage::clone() const {
    if (is_Static()) {
        return newStatic(as_Static().clone());
    } else {
        return Storage(this->val);
    }
}

::MIR::Constant MIR::Constant::clone() const {
    TU_MATCHA((*this), (e2), (Int, return ::MIR::Constant(e2);), (Uint, return ::MIR::Constant(e2);), (Float, return ::MIR::Constant(e2);), (Bool, return ::MIR::Constant(e2);), (Bytes, return ::MIR::Constant(e2);), (StaticString, return ::MIR::Constant(e2);), (Const, return ::MIR::Constant::make_Const({box$(e2.p->clone())});), (Generic, return ::MIR::Constant(e2);), (Function, return ::MIR::Constant::make_Function({box$(e2.p->clone())});), (ItemAddr, return ::MIR::Constant::make_ItemAddr(e2.clone());))
    throw "";
}

::MIR::Param MIR::Param::clone() const {
    TU_MATCHA((*this), (e), (LValue, return e.clone();), (Borrow, return ::MIR::Param::make_Borrow({e.type, e.val.clone()});), (Constant, return e.clone();))
    throw "";
}

::MIR::RValue MIR::RValue::clone() const {
    TU_MATCHA(
        (*this),
        (e),
        (Use, return ::MIR::RValue(e.clone());),
        (Constant, return e.clone();),
        (SizedArray, return ::MIR::RValue::make_SizedArray({e.val.clone(), e.count.clone()});),
        (Borrow, return ::MIR::RValue::make_Borrow({e.type, e.isRaw, e.val.clone()});),
        (Cast, return ::MIR::RValue::make_Cast({e.val.clone(), e.type});),
        (BinOp, return ::MIR::RValue::make_BinOp({e.val_l.clone(), e.op, e.val_r.clone()});),
        (UniOp, return ::MIR::RValue::make_UniOp({e.val.clone(), e.op});),
        (DstMeta, return ::MIR::RValue::make_DstMeta({e.val.clone()});),
        (DstPtr, return ::MIR::RValue::make_DstPtr({e.val.clone()});),
        // Construct a DST pointer from a thin pointer and metadata
        (MakeDst, return ::MIR::RValue::make_MakeDst({e.ptrVal.clone(), e.metaVal.clone()});),
        (Tuple, decltype(e.vals) ret; ret.reserve(e.vals.size()); for (const auto& v : e.vals) ret.push_back(v.clone()); return ::MIR::RValue::make_Tuple({mv$(ret)});),
        // Array literal
        (Array, decltype(e.vals) ret; ret.reserve(e.vals.size()); for (const auto& v : e.vals) ret.push_back(v.clone()); return ::MIR::RValue::make_Array({mv$(ret)});),
        // Create a new instance of a union
        (UnionVariant, return ::MIR::RValue::make_UnionVariant({e.path.clone(), e.index, e.val.clone()});),
        // Create a new instance of an enum
        (EnumVariant, decltype(e.vals) ret; ret.reserve(e.vals.size()); for (const auto& v : e.vals) ret.push_back(v.clone()); return ::MIR::RValue::make_EnumVariant({e.path.clone(), e.index, mv$(ret)});),
        // Create a new instance of a struct
        (Struct, decltype(e.vals) ret; ret.reserve(e.vals.size()); for (const auto& v : e.vals) ret.push_back(v.clone()); return ::MIR::RValue::make_Struct({e.path.clone(), mv$(ret)});)
    )
    throw "";
}

::MIR::SwitchValues MIR::SwitchValues::clone() const {
    TU_MATCHA((*this), (ve), (Unsigned, return ve;), (Signed, return ve;), (String, return ve;), (ByteString, return ve;))
    throw "";
}

bool MIR::SwitchValues::operator==(const SwitchValues& x) const {
    if (this->tag() != x.tag()) {
        return false;
    }
    TU_MATCHA(((*this), x), (ave, bve), (Unsigned, if (ave != bve) return false;), (Signed, if (ave != bve) return false;), (String, if (ave != bve) return false;), (ByteString, if (ave != bve) return false;))
    return true;
}

const HIR::TypeData* MIR::Cloner::value_generic_type(HIR::GenericRef ce) const {
    TODO(sp, "`value_generic_type` not implemented, shouldn't be called unless `monomorpiser` has been overridden");
}

MIR::Cloner::Cloner(const Span& sp, HIR::TypeInterner& types)
    : nop(new MonomorphiserNop(types))
    , sp(sp)
{
}

MIR::Cloner::~Cloner() = default;

const Monomorphiser& MIR::Cloner::monomorphiser() const {
    return *nop;
}

::HIR::TypeRef MIR::Cloner::monomorph(const ::HIR::TypeData* ty) const {
    TRACE_FUNCTION_F(ty);
    auto rv = monomorphiser().monomorphType(sp, ty);
    if (auto* r = resolve()) {
        r->expandAssociatedTypes(sp, rv);
    }
    return rv;
}

::HIR::GenericPath MIR::Cloner::monomorph(const ::HIR::GenericPath& ty) const {
    TRACE_FUNCTION_F(ty);
    auto rv = monomorphiser().monomorphGenericpath(sp, ty, false);
    if (const auto* r = resolve()) {
        r->evaluatePathParams(sp, rv.mParams);
        for (auto& arg : rv.mParams.types) {
            r->expandAssociatedTypes(sp, arg);
        }
    }
    return rv;
}

::HIR::Path MIR::Cloner::monomorph(const ::HIR::Path& ty) const {
    TRACE_FUNCTION_F(ty);
    auto rv = monomorphiser().monomorphPath(sp, ty, false);
    if (const auto* r = resolve()) {
        TU_MATCH(
            ::HIR::Path::Data,
            (rv.mData),
            (e2),
            (Generic, r->evaluatePathParams(sp, e2.mParams); for (auto& arg : e2.mParams.types) r->expandAssociatedTypes(sp, arg);),
            (UfcsInherent, r->expandAssociatedTypes(sp, e2.type); r->evaluatePathParams(sp, e2.params); r->evaluatePathParams(sp, e2.impl_params); for (auto& arg : e2.params.types) r->expandAssociatedTypes(sp, arg);
             // TODO: impl params too?
             for (auto& arg : e2.impl_params.types) r->expandAssociatedTypes(sp, arg);),
            (UfcsKnown, r->expandAssociatedTypes(sp, e2.type); r->evaluatePathParams(sp, e2.trait.mParams); r->evaluatePathParams(sp, e2.params); for (auto& arg : e2.trait.mParams.types) r->expandAssociatedTypes(sp, arg); for (auto& arg : e2.params.types) r->expandAssociatedTypes(sp, arg);),
            (UfcsUnknown, BUG(sp, "Encountered UfcsUnknown");)
        )
    }
    return rv;
}

::HIR::PathParams MIR::Cloner::monomorph(const ::HIR::PathParams& ty) const {
    TRACE_FUNCTION_F(ty);
    auto rv = monomorphiser().monomorphPathParams(sp, ty, false);
    if (const auto* r = resolve()) {
        r->evaluatePathParams(sp, rv);
        for (auto& arg : rv.types) {
            r->expandAssociatedTypes(sp, arg);
        }
    }
    return rv;
}

::std::vector<MIR::AsmParam> MIR::Cloner::cloneAsmParams(const ::std::vector<MIR::AsmParam>& params) const {
    ::std::vector<MIR::AsmParam> rv;
    for (const auto& p : params) {
        TU_MATCH_HDRA((p), {)
        TU_ARMA(Const, v)
            rv.push_back( this->cloneConstant(v) );
            TU_ARMA(Sym, v)
            rv.push_back(this->monomorph(v));
            TU_ARMA(Reg, v)
            rv.push_back(::MIR::AsmParam::make_Reg({v.dir, v.spec.clone(), v.input ? box$(this->cloneParam(*v.input)) : std::unique_ptr<MIR::Param>(), v.output ? box$(this->cloneLval(*v.output)) : std::unique_ptr<MIR::LValue>()}));
        }
    }
    return rv;
}

::MIR::Statement MIR::Cloner::cloneStmt(const ::MIR::Statement& src) const {
    TU_MATCH_HDRA( (src), { )
    TU_ARMA(Assign, se) {
            return ::MIR::Statement::make_Assign({this->cloneLval(se.dst), this->cloneRval(se.src)});
        }
        TU_ARMA(Asm, se) {
            return ::MIR::Statement::make_Asm({se.tpl, this->cloneNameLvalVec(se.outputs), this->cloneNameLvalVec(se.inputs), se.clobbers, se.flags});
        }
        TU_ARMA(Asm2, se) {
            return ::MIR::Statement::make_Asm2({se.options, se.lines, this->cloneAsmParams(se.params)});
        }
        TU_ARMA(SetDropFlag, se) {
            return ::MIR::Statement::make_SetDropFlag({mapDropFlag(se.idx), se.newVal, se.other == ~0u ? ~0u : mapDropFlag(se.other)});
        }
        TU_ARMA(SaveDropFlag, se) {
            TODO(Span(), "clone_bb SaveDropFlag");
        }
        TU_ARMA(LoadDropFlag, se) {
            TODO(Span(), "clone_bb LoadDropFlag");
        }
        TU_ARMA(ScopeEnd, se) {
            ::MIR::Statement::Data_ScopeEnd newSe;
            newSe.slots.reserve(se.slots.size());
            for (auto idx : se.slots) {
                newSe.slots.push_back(mapLocal(idx));
            }
            return ::MIR::Statement(mv$(newSe));
        }
    }
    throw "";
}

::MIR::Terminator MIR::Cloner::cloneTerm(const ::MIR::Terminator& src) const {
    TU_MATCH_HDRA( (src), { )
    TU_ARMA(Incomplete, se) {
            return ::MIR::Terminator::make_Incomplete({});
        }
        TU_ARMA(Return, se) {
            return ::MIR::Terminator::make_Return({});
        }
        TU_ARMA(UnwindResume, se) {
            return ::MIR::Terminator::make_UnwindResume({});
        }
        TU_ARMA(UnwindTerminate, se) {
            return ::MIR::Terminator::make_UnwindTerminate({});
        }
        TU_ARMA(Unreachable, se) {
            return ::MIR::Terminator::make_Unreachable({});
        }
        TU_ARMA(Goto, se) {
            return ::MIR::Terminator::make_Goto(mapBbIdx(se));
        }
        TU_ARMA(If, se) {
            return ::MIR::Terminator::make_If({this->cloneLval(se.cond), mapBbIdx(se.bbTrue), mapBbIdx(se.bbFalse)});
        }
        TU_ARMA(Switch, se) {
            ::std::vector<::MIR::BasicBlockId> arms;
            arms.reserve(se.targets.size());
            for (const auto& bbi : se.targets) {
                arms.push_back(mapBbIdx(bbi));
            }
            return ::MIR::Terminator::make_Switch({this->cloneLval(se.val), mv$(arms), se.valid_flag == ~0u ? ~0u : mapDropFlag(se.valid_flag), se.invalidTarget == ~0u ? ~0u : mapBbIdx(se.invalidTarget)});
        }
        TU_ARMA(SwitchValue, se) {
            ::std::vector<::MIR::BasicBlockId> arms;
            arms.reserve(se.targets.size());
            for (const auto& bbi : se.targets) {
                arms.push_back(mapBbIdx(bbi));
            }
            return ::MIR::Terminator::make_SwitchValue({this->cloneLval(se.val), mapBbIdx(se.defTarget), mv$(arms), se.values.clone()});
        }
        TU_ARMA(Drop, se) {
            UnwindAction unwind;
            TU_MATCHA((se.unwind), (ue), (Continue, unwind = UnwindAction::make_Continue({});), (Cleanup, unwind = UnwindAction::make_Cleanup(mapBbIdx(ue));), (Terminate, unwind = UnwindAction::make_Terminate({});), (Unreachable, unwind = UnwindAction::make_Unreachable({});))
            return ::MIR::Terminator::make_Drop({se.kind, this->cloneLval(se.slot), se.flagIdx == ~0u ? ~0u : mapDropFlag(se.flagIdx), mapBbIdx(se.target), mv$(unwind)});
        }
        TU_ARMA(Call, se) {
            ::MIR::CallTarget tgt;
            TU_MATCHA((se.fcn), (ste), (Value, tgt = ::MIR::CallTarget::make_Value(this->cloneLval(ste));), (Path, tgt = ::MIR::CallTarget::make_Path(this->monomorph(ste));), (Intrinsic, tgt = ::MIR::CallTarget::make_Intrinsic({ste.name, this->monomorph(ste.params)});))
            UnwindAction unwind;
            TU_MATCHA((se.unwind), (ue), (Continue, unwind = UnwindAction::make_Continue({});), (Cleanup, unwind = UnwindAction::make_Cleanup(mapBbIdx(ue));), (Terminate, unwind = UnwindAction::make_Terminate({});), (Unreachable, unwind = UnwindAction::make_Unreachable({});))
            return ::MIR::Terminator::make_Call({mapBbIdx(se.retBlock), mv$(unwind), this->cloneLval(se.retVal), mv$(tgt), this->cloneParamVec(se.args)});
        }
    }
    throw "";
}

::std::vector<::std::pair<::std::string, ::MIR::LValue>> MIR::Cloner::cloneNameLvalVec(const ::std::vector<::std::pair<::std::string, ::MIR::LValue>>& src) const {
    ::std::vector<::std::pair<::std::string, ::MIR::LValue>> rv;
    rv.reserve(src.size());
    for (const auto& e : src) {
        rv.push_back(::std::make_pair(e.first, this->cloneLval(e.second)));
    }
    return rv;
}

::std::vector<::MIR::LValue> MIR::Cloner::cloneLvalVec(const ::std::vector<::MIR::LValue>& src) const {
    ::std::vector<::MIR::LValue> rv;
    rv.reserve(src.size());
    for (const auto& lv : src) {
        rv.push_back(this->cloneLval(lv));
    }
    return rv;
}

::std::vector<::MIR::Param> MIR::Cloner::cloneParamVec(const ::std::vector<::MIR::Param>& src) const {
    ::std::vector<::MIR::Param> rv;
    rv.reserve(src.size());
    for (const auto& lv : src) {
        rv.push_back(this->cloneParam(lv));
    }
    return rv;
}

::MIR::LValue MIR::Cloner::cloneLval(const ::MIR::LValue& src) const {
    auto wrappers = src.wrappers;
    for (auto& w : wrappers) {
        if (w.is_Index()) {
            w = ::MIR::LValue::Wrapper::newIndex(mapLocal(w.as_Index()));
        }
    }
    TU_MATCH_HDRA( (src.root), {)
    TU_ARMA(Return, se) {
            return ::MIR::LValue(::MIR::LValue::Storage::newReturn(), mv$(wrappers));
        }
        TU_ARMA(Argument, se) {
            return ::MIR::LValue(::MIR::LValue::Storage::newArgument(se), mv$(wrappers));
        }
        TU_ARMA(Local, se) {
            return ::MIR::LValue(::MIR::LValue::Storage::newLocal(this->mapLocal(se)), mv$(wrappers));
        }
        TU_ARMA(Static, se) {
            return ::MIR::LValue(::MIR::LValue::Storage::newStatic(this->monomorph(se)), mv$(wrappers));
        }
    }
    throw "";
}

::MIR::Constant MIR::Cloner::cloneConstant(const ::MIR::Constant& src) const {
    TU_MATCH_HDRA( (src), {)
    TU_ARMA(Int  , ce) return ::MIR::Constant(ce);
        TU_ARMA(Uint, ce) return ::MIR::Constant(ce);
        TU_ARMA(Float, ce) return ::MIR::Constant(ce);
        TU_ARMA(Bool, ce) return ::MIR::Constant(ce);
        TU_ARMA(Bytes, ce) return ::MIR::Constant(ce);
        TU_ARMA(StaticString, ce) return ::MIR::Constant(ce);
        TU_ARMA(Const, ce) {
            return ::MIR::Constant::make_Const({box$(this->monomorph(*ce.p))});
        }
        TU_ARMA(Generic, ce) {
            auto val = monomorphiser().getValue(sp, ce);
            if (const auto* r = resolve()) {
                r->evaluateConstGeneric(sp, val);
            }
        TU_MATCH_HDRA( (val), {)
        default:
            TODO(sp, "Monomorphise MIR generic constant " << ce << " = " << val);
                TU_ARMA(Generic, ve) {
                    return ve;
                }
                TU_ARMA(Evaluated, ve) {
                    const auto& ty = this->value_generic_type(ce);
                    auto v = EncodedLiteralSlice(*ve);
                    ASSERT_BUG(sp, ty->is_Primitive(), "Handle non-primitive const generic: " << ty);
                    // TODO: This is duplicated in `mir/from_hir_match.cpp` - De-duplicate?
                    switch (ty->as_Primitive()) {
                        case ::HIR::CoreType::Bool:
                            return ::MIR::Constant::make_Bool({v.readUint(1) != 0});
                        case ::HIR::CoreType::U8:
                        case ::HIR::CoreType::U16:
                        case ::HIR::CoreType::U32:
                        case ::HIR::CoreType::U64:
                        case ::HIR::CoreType::U128:
                            return ::MIR::Constant::make_Uint({v.readUint(ve->bytes.size()), ty->as_Primitive()});
                        case ::HIR::CoreType::Usize:
                            return ::MIR::Constant::make_Uint({v.readUint(TargetGetPointerBits() / 8), ty->as_Primitive()});
                        case ::HIR::CoreType::I8:
                        case ::HIR::CoreType::I16:
                        case ::HIR::CoreType::I32:
                        case ::HIR::CoreType::I64:
                        case ::HIR::CoreType::I128:
                            return ::MIR::Constant::make_Int({v.readSint(ve->bytes.size()), ty->as_Primitive()});
                        case ::HIR::CoreType::Isize:
                            return ::MIR::Constant::make_Int({v.readSint(TargetGetPointerBits() / 8), ty->as_Primitive()});
                        case ::HIR::CoreType::F16:
                        case ::HIR::CoreType::F32:
                        case ::HIR::CoreType::F64:
                        case ::HIR::CoreType::F128:
                            return ::MIR::Constant::make_Float({v.readFloat(ve->bytes.size()), ty->as_Primitive()});
                        case ::HIR::CoreType::Char:
                            return ::MIR::Constant::make_Uint({v.readUint(4), ty->as_Primitive()});
                        case ::HIR::CoreType::Str:
                            BUG(sp, "`str` const generic");
                    }
                }
        }
        }
        TU_ARMA(Function, ce) {
            return ::MIR::Constant::make_Function({box$(this->monomorph(*ce.p))});
        }
        TU_ARMA(ItemAddr, ce) {
            if (!ce) {
                return ::MIR::Constant::make_ItemAddr({});
            }
            return ::MIR::Constant::make_ItemAddr({box$(this->monomorph(*ce)), ce.offset});
        }
    }
    throw "";
}

::MIR::Param MIR::Cloner::cloneParam(const ::MIR::Param& src) const {
    TU_MATCHA((src), (se), (LValue, return cloneLval(se);), (Borrow, return ::MIR::Param::make_Borrow({se.type, this->cloneLval(se.val)});), (Constant, return cloneConstant(se);))
    throw "";
}

::MIR::RValue MIR::Cloner::cloneRval(const ::MIR::RValue& src) const {
    TU_MATCH_HDRA( (src), {)
    TU_ARMA(Use, se) {
            //if( const auto* ae = se.opt_Argument() )
            //    if( const auto* e = this->te.args.at(ae->idx).opt_Constant() )
            //        return e->clone();
            return ::MIR::RValue(this->cloneLval(se));
        }
        TU_ARMA(Constant, se) {
            return this->cloneConstant(se);
        }
        TU_ARMA(SizedArray, se) {
            auto count = monomorphiser().monomorphArraysize(sp, se.count);
            if (const auto* resolver = resolve()) {
                resolver->evaluateArraySize(sp, count);
            }
            return ::MIR::RValue::make_SizedArray({this->cloneParam(se.val), std::move(count)});
        }
        TU_ARMA(Borrow, se) {
            return ::MIR::RValue::make_Borrow({se.type, se.isRaw, this->cloneLval(se.val)});
        }
        TU_ARMA(Cast, se) {
            return ::MIR::RValue::make_Cast({this->cloneLval(se.val), this->monomorph(se.type)});
        }
        TU_ARMA(BinOp, se) {
            return ::MIR::RValue::make_BinOp({this->cloneParam(se.val_l), se.op, this->cloneParam(se.val_r)});
        }
        TU_ARMA(UniOp, se) {
            return ::MIR::RValue::make_UniOp({this->cloneLval(se.val), se.op});
        }
        TU_ARMA(DstMeta, se) {
            return ::MIR::RValue::make_DstMeta({this->cloneLval(se.val)});
        }
        TU_ARMA(DstPtr, se) {
            return ::MIR::RValue::make_DstPtr({this->cloneLval(se.val)});
        }
        TU_ARMA(MakeDst, se) {
            return ::MIR::RValue::make_MakeDst({this->cloneParam(se.ptrVal), this->cloneParam(se.metaVal)});
        }
        TU_ARMA(Tuple, se) {
            return ::MIR::RValue::make_Tuple({this->cloneParamVec(se.vals)});
        }
        TU_ARMA(Array, se) {
            return ::MIR::RValue::make_Array({this->cloneParamVec(se.vals)});
        }
        TU_ARMA(UnionVariant, se) {
            return ::MIR::RValue::make_UnionVariant({this->monomorph(se.path), se.index, this->cloneParam(se.val)});
        }
        TU_ARMA(EnumVariant, se) {
            return ::MIR::RValue::make_EnumVariant({this->monomorph(se.path), se.index, this->cloneParamVec(se.vals)});
        }
        TU_ARMA(Struct, se) {
            return ::MIR::RValue::make_Struct({this->monomorph(se.path), this->cloneParamVec(se.vals)});
        }
    }
    throw "";
}

namespace MIR {

LValue::Storage::Storage(uintptr_t v)
    : val(v) {
}
LValue::Storage::Storage(Storage&& x)
    : val(x.val) {
    x.val = 0;
}
LValue::Storage& LValue::Storage::operator=(Storage&& x) {
    this->~Storage();
    this->val = x.val;
    x.val = 0;
    return *this;
}
LValue::Storage::~Storage() {
    if (is_Static()) {
        delete reinterpret_cast<::HIR::Path*>(val & ~3ull);
        val = 0;
    }
}
LValue::Storage LValue::Storage::newArgument(unsigned idx) {
    assert(idx < MAX_ARG);
    return Storage((idx + 1) << 2);
}
LValue::Storage LValue::Storage::newLocal(unsigned idx) {
    assert(idx <= MAX_ARG);
    return Storage((idx << 2) | 1);
}
LValue::Storage LValue::Storage::newStatic(::HIR::Path p) {
    ::HIR::Path* ptr = new ::HIR::Path(::std::move(p));
    return Storage(reinterpret_cast<uintptr_t>(ptr) | 2);
}
uintptr_t LValue::Storage::getInner() const {
    assert(!is_Static());
    return val;
}
LValue::Storage LValue::Storage::fromInner(uintptr_t v) {
    assert((v & 3) < 2);
    return Storage(v);
}
LValue::Storage::Tag LValue::Storage::tag() const {
    if (val == 0) {
        return TAG_Return;
    }
    return static_cast<Tag>(val & 3);
}
char LValue::Storage::as_Return() const {
    assert(is_Return());
    return 0;
}
unsigned LValue::Storage::as_Argument() const {
    assert(is_Argument());
    return static_cast<unsigned>((val >> 2) - 1);
}
unsigned LValue::Storage::as_Local() const {
    assert(is_Local());
    return static_cast<unsigned>(val >> 2);
}
const ::HIR::Path& LValue::Storage::as_Static() const {
    assert(is_Static());
    return *reinterpret_cast<const ::HIR::Path*>(val & ~3llu);
}
::HIR::Path& LValue::Storage::as_Static() {
    assert(is_Static());
    return *reinterpret_cast<::HIR::Path*>(val & ~3llu);
}
LValue::Wrapper::Wrapper(uint32_t v)
    : val(v) {
}
LValue::Wrapper LValue::Wrapper::newIndex(unsigned idx) {
    if (idx == ~0u) {
        idx = Storage::MAX_ARG;
    }
    return Wrapper((idx << 2) | 3);
}
char LValue::Wrapper::as_Deref() const {
    assert(is_Deref());
    return 0;
}
unsigned LValue::Wrapper::as_Field() const {
    assert(is_Field());
    return (val >> 2);
}
unsigned LValue::Wrapper::as_Downcast() const {
    assert(is_Downcast());
    return (val >> 2);
}
// TODO: Should this return a LValue?
unsigned LValue::Wrapper::as_Index() const {
    assert(is_Index());
    unsigned rv = (val >> 2);
    return rv;
}
void LValue::Wrapper::incField() {
    assert(is_Field());
    *this = Wrapper::newField(as_Field() + 1);
}
void LValue::Wrapper::incDowncast() {
    assert(is_Downcast());
    *this = Wrapper::newDowncast(as_Downcast() + 1);
}
LValue::LValue()
    : root(Storage::newReturn()) {
}
LValue::LValue(Storage root, ::std::vector<Wrapper> wrappers)
    : root(::std::move(root))
    , wrappers(::std::move(wrappers)) {
}
LValue LValue::newDeref(LValue lv) {
    lv.wrappers.push_back(Wrapper::newDeref());
    return lv;
}
LValue LValue::newField(LValue lv, unsigned idx) {
    lv.wrappers.push_back(Wrapper::newField(idx));
    return lv;
}
LValue LValue::newDowncast(LValue lv, unsigned idx) {
    lv.wrappers.push_back(Wrapper::newDowncast(idx));
    return lv;
}
LValue LValue::newIndex(LValue lv, unsigned localIdx) {
    lv.wrappers.push_back(Wrapper::newIndex(localIdx));
    return lv;
}
unsigned LValue::as_Local() const {
    assert(wrappers.empty());
    return root.as_Local();
}
unsigned LValue::as_Field() const {
    assert(!wrappers.empty());
    return wrappers.back().as_Field();
}
void LValue::incField() {
    assert(wrappers.size() > 0);
    wrappers.back().incField();
}
void LValue::incDowncast() {
    assert(wrappers.size() > 0);
    wrappers.back().incDowncast();
}
LValue LValue::cloneWrapped(::std::vector<Wrapper> wrappers) const {
    if (this->wrappers.empty()) {
        return LValue(root.clone(), ::std::move(wrappers));
    } else {
        return cloneWrapped(wrappers.begin(), wrappers.end());
    }
}
LValue LValue::cloneUnwrapped(unsigned count) const {
    assert(count > 0);
    assert(count <= wrappers.size());
    return LValue(root.clone(), ::std::vector<Wrapper>(wrappers.begin(), wrappers.end() - count));
}
// Returns true if one lvalue is a subset of the other
// - Equivalent to `a.is_subset_of(b) || b.is_subset_of(a)` (but more efficient)
bool LValue::isEitherSubset(const LValue& other) const {
    if (!(root == other.root)) {
        return false;
    }
    if (other.wrappers.size() > wrappers.size()) {
        return ::std::equal(wrappers.begin(), wrappers.end(), other.wrappers.begin());
    } else {
        return ::std::equal(other.wrappers.begin(), other.wrappers.end(), wrappers.begin());
    }
}
LValue::RefCommon::RefCommon(const LValue& lv, size_t wrapper_count)
    : mLv(&lv)
    , wrapperCount(wrapper_count) {
    assert(wrapper_count <= lv.wrappers.size());
}
/// Unwrap one level, returning false if already at the root
bool LValue::RefCommon::try_unwrap() {
    if (wrapperCount == 0) {
        return false;
    } else {
        wrapperCount--;
        return true;
    }
}
LValue::RefCommon::Tag LValue::RefCommon::tag() const {
    if (wrapperCount == 0) {
        switch (mLv->root.tag()) {
            case Storage::TAGDEAD:
                return TAGDEAD;
            case Storage::TAG_Return:
                return TAG_Return;
            case Storage::TAG_Argument:
                return TAG_Argument;
            case Storage::TAG_Local:
                return TAG_Local;
            case Storage::TAG_Static:
                return TAG_Static;
        }
    } else {
        switch (mLv->wrappers[wrapperCount - 1].tag()) {
            case Wrapper::TAGDEAD:
                return TAGDEAD;
            case Wrapper::TAG_Deref:
                return TAG_Deref;
            case Wrapper::TAG_Field:
                return TAG_Field;
            case Wrapper::TAG_Downcast:
                return TAG_Downcast;
            case Wrapper::TAG_Index:
                return TAG_Index;
        }
    }
    return TAGDEAD;
}
unsigned LValue::RefCommon::as_Local() const {
    assert(is_Local());
    return mLv->root.as_Local();
}
char LValue::RefCommon::as_Return() const {
    assert(is_Return());
    return mLv->root.as_Return();
}
unsigned LValue::RefCommon::as_Argument() const {
    assert(is_Argument());
    return mLv->root.as_Argument();
}
const HIR::Path& LValue::RefCommon::as_Static() const {
    assert(is_Static());
    return mLv->root.as_Static();
}
char LValue::RefCommon::as_Deref() const {
    assert(is_Deref());
    return mLv->wrappers[wrapperCount - 1].as_Deref();
}
unsigned LValue::RefCommon::as_Field() const {
    assert(is_Field());
    return mLv->wrappers[wrapperCount - 1].as_Field();
}
unsigned LValue::RefCommon::as_Downcast() const {
    assert(is_Downcast());
    return mLv->wrappers[wrapperCount - 1].as_Downcast();
}
unsigned LValue::RefCommon::as_Index() const {
    assert(is_Index());
    return mLv->wrappers[wrapperCount - 1].as_Index();
}
LValue::CRef::CRef(const LValue& lv)
    : RefCommon(lv, lv.wrappers.size()) {
}
LValue::CRef::CRef(const LValue& lv, size_t wc)
    : RefCommon(lv, wc) {
}
/// Unwrap one level
const LValue::CRef LValue::CRef::innerRef() const {
    assert(wrapperCount > 0);
    auto rv = *this;
    rv.wrapperCount--;
    return rv;
}
LValue::MRef::MRef(LValue& lv)
    : RefCommon(lv, lv.wrappers.size()) {
}
LValue::MRef LValue::MRef::innerRef() {
    assert(wrapperCount > 0);
    auto rv = *this;
    rv.wrapperCount--;
    return rv;
}
void LValue::MRef::replace(LValue x) {
    auto& mutLv = const_cast<LValue&>(*mLv);
    // Shortcut: No wrappers on source/destination (just assign the slot/root)
    if (wrapperCount == 0 && x.wrappers.empty()) {
        mutLv.root = ::std::move(x.root);
        return;
    }
    // If there's wrappers on this value (assigning over inner portion)
    if (wrapperCount < mLv->wrappers.size()) {
        // Add those wrappers to the end of the new value
        x.wrappers.insert(x.wrappers.end(), mLv->wrappers.begin() + wrapperCount, mLv->wrappers.end());
    }
    // Overwrite
    mutLv = ::std::move(x);
}
ItemAddress::ItemAddress(::std::unique_ptr<::HIR::Path> p, U128 offset)
    : p(::std::move(p))
    , offset(offset) {
}
EnumCachePtr::EnumCachePtr(const EnumCache* p)
    : p(p) {
}
EnumCachePtr::EnumCachePtr(EnumCachePtr&& x)
    : p(x.p) {
    x.p = nullptr;
}
EnumCachePtr& EnumCachePtr::operator=(EnumCachePtr&& x) {
    this->~EnumCachePtr();
    p = x.p;
    x.p = nullptr;
    return *this;
}
}

namespace MIR {

::std::ostream& operator<<(::std::ostream& os, const LValue::CRef& x) {
    x.fmt(os);
    return os;
}
::std::ostream& operator<<(::std::ostream& os, const LValue::MRef& x) {
    x.fmt(os);
    return os;
}
}
