#include "mir_mir.h"

#include "trans_target.h" // Target_GetPointerBits
#include "hir_encoded_literal.h"
#include "hir_typeck_monomorph.h"

#include <algorithm> // std::min

::std::ostream& operator<<(::std::ostream& os, const MIRConstant& v) {
    switch (v.tag()) {
        case MIRConstant::TAG_Int: {
            auto& e = v.as_Int();
            os << (e.v < 0 ? "-" : "+");
            os << (e.v < 0 ? -e.v : e.v);
            os << " " << e.t;
            break;
        }
        case MIRConstant::TAG_Uint: {
            auto& e = v.as_Uint();
            os << std::hex << "0x" << e.v << std::dec;
            os << " " << e.t;
            break;
        }
        case MIRConstant::TAG_Float: {
            auto& e = v.as_Float();
            os << std::hexfloat << e.v << std::defaultfloat;
            os << " " << e.t;
            break;
        }
        case MIRConstant::TAG_Bool: {
            auto& e = v.as_Bool();
            os << (e.v ? "true" : "false");
            break;
        }
        case MIRConstant::TAG_Bytes: {
            auto& e = v.as_Bytes();
            os << "b\"";
            os << ::std::hex;
            for (auto v : e) {
                if (v == '\\' || v == '"') {
                    os << "\\" << v;
                } else if (' ' <= v && v < 0x7F) {
                    os << v;
                } else if (v < 16) {
                    os << "\\x0" << (unsigned int)v;
                } else {
                    os << "\\x" << ((unsigned int)v & 0xFF);
                }
            }
            os << "\"";
            os << ::std::dec;
            break;
        }
        case MIRConstant::TAG_StaticString: {
            auto& e = v.as_StaticString();
            os << "\"" << FmtEscaped(e) << "\"";
            break;
        }
        case MIRConstant::TAG_Encoded: {
            auto& e = v.as_Encoded();
            os << "encoded(" << e.type << ": " << e.value << ")";
            break;
        }
        case MIRConstant::TAG_Const: {
            auto& e = v.as_Const();
            assert(e.p);
            os << *e.p;
            break;
        }
        case MIRConstant::TAG_Generic: {
            auto& e = v.as_Generic();
            os << e;
            break;
        }
        case MIRConstant::TAG_Function: {
            auto& e = v.as_Function();
            assert(e.p);
            os << "fn " << *e.p;
            break;
        }
        case MIRConstant::TAG_ItemAddr: {
            auto& e = v.as_ItemAddr();
            if (e) {
                os << "&" << *e;
                if (e.offset != U128(0)) {
                    os << "+0x" << std::hex << e.offset << std::dec;
                }
            } else {
                os << "#UNSIZE_PLACEHOLDER"; // A `Const` with `nullptr` is a placeholder for MakeDst `Unsize`
            }
            break;
        }
    }
    return os;
}

::Ordering MIRConstant::ord(const MIRConstant& b) const {
    if (this->tag() != b.tag()) {
        return ::ord(static_cast<unsigned int>(this->tag()), static_cast<unsigned int>(b.tag()));
    }
    switch ((*this).tag()) {
        case MIRConstant::TAG_Int: {
            auto& ae = (*this).as_Int();
            auto& be = b.as_Int();
            if (ae.v != be.v) {
                return ::ord(ae.v, be.v);
            }
            return ::ord((unsigned)ae.t, (unsigned)be.t);
            break;
        }
        case MIRConstant::TAG_Uint: {
            auto& ae = (*this).as_Uint();
            auto& be = b.as_Uint();
            if (ae.v != be.v) {
                return ::ord(ae.v, be.v);
            }
            return ::ord((unsigned)ae.t, (unsigned)be.t);
            break;
        }
        case MIRConstant::TAG_Float: {
            auto& ae = (*this).as_Float();
            auto& be = b.as_Float();
            if (ae.v != be.v) {
                return ae.v > be.v ? OrdGreater : OrdLess;
            }
            return ::ord((unsigned)ae.t, (unsigned)be.t);
            break;
        }
        case MIRConstant::TAG_Bool: {
            auto& ae = (*this).as_Bool();
            auto& be = b.as_Bool();
            return ::ord(ae.v, be.v);
        }
        case MIRConstant::TAG_Bytes: {
            auto& ae = (*this).as_Bytes();
            auto& be = b.as_Bytes();
            return ::ord(ae, be);
        }
        case MIRConstant::TAG_StaticString: {
            auto& ae = (*this).as_StaticString();
            auto& be = b.as_StaticString();
            return ::ord(ae, be);
        }
        case MIRConstant::TAG_Encoded: {
            auto& ae = (*this).as_Encoded();
            auto& be = b.as_Encoded();
            ORD(ae.type, be.type);
            return ae.value.ord(be.value);
            break;
        }
        case MIRConstant::TAG_Const: {
            auto& ae = (*this).as_Const();
            auto& be = b.as_Const();
            return ::ord(*ae.p, *be.p);
        }
        case MIRConstant::TAG_Generic: {
            auto& ae = (*this).as_Generic();
            auto& be = b.as_Generic();
            return ::ord(ae.binding, be.binding);
        }
        case MIRConstant::TAG_Function: {
            auto& ae = (*this).as_Function();
            auto& be = b.as_Function();
            return ::ord(*ae.p, *be.p);
        }
        case MIRConstant::TAG_ItemAddr: {
            auto& ae = (*this).as_ItemAddr();
            auto& be = b.as_ItemAddr();
            ORD(static_cast<bool>(ae), static_cast<bool>(be));
            if (ae) {
                ORD(*ae, *be);
            }
            ORD(ae.offset, be.offset);
            return OrdEqual;
            break;
        }
    }
    UNREACHABLE();
}

void MIRLValue::RefCommon::fmt(::std::ostream& os) const {
    os << lv_->root;
    for (size_t i = 0; i < wrapperCount_; i++) {
        os << lv_->wrappers.at(i);
    }
}

::std::ostream& operator<<(::std::ostream& os, const MIRLValue& x) {
    MIRLValue::CRef(x).fmt(os);
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const MIRLValue::Storage& r) {
    switch (r.tag()) {
        case MIRLValue::Storage::TAG_Return: {
            os << "retval";
            break;
        }
        case MIRLValue::Storage::TAG_Argument: {
            decltype(r.as_Argument()) e = r.as_Argument();
            os << "a" << e;
            break;
        }
        case MIRLValue::Storage::TAG_Local: {
            decltype(r.as_Local()) e = r.as_Local();
            os << "_" << e;
            break;
        }
        case MIRLValue::Storage::TAG_Static: {
            decltype(r.as_Static()) e = r.as_Static();
            os << "(" << e << ")";
            break;
        }
    }
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const MIRLValue::Wrapper& w) {
    switch (w.tag()) {
        case MIRLValue::Wrapper::TAG_Field: {
            decltype(w.as_Field()) e = w.as_Field();
            os << "." << e;
            break;
        }
        case MIRLValue::Wrapper::TAG_Deref: {
            os << "*";
            break;
        }
        case MIRLValue::Wrapper::TAG_Index: {
            decltype(w.as_Index()) e = w.as_Index();
            os << "[_" << e << "]";
            break;
        }
        case MIRLValue::Wrapper::TAG_Downcast: {
            decltype(w.as_Downcast()) e = w.as_Downcast();
            os << "#" << e;
            break;
        }
    }
    return os;
}

Ordering MIRLValue::Storage::ord(const MIRLValue::Storage& x) const {
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

Ordering MIRLValue::ord(const MIRLValue& x) const {
    auto rv = root.ord(x.root);
    if (rv != OrdEqual) {
        return rv;
    }
    return ::ord(wrappers, x.wrappers);
}

Ordering MIRLValue::RefCommon::ord(const MIRLValue::RefCommon& x) const {
    Ordering rv;
    rv = lv_->root.ord(x.lv_->root);
    if (rv != OrdEqual) {
        return rv;
    }
    for (size_t i = 0; i < ::std::min(wrapperCount_, x.wrapperCount_); i++) {
        rv = lv_->wrappers[i].ord(x.lv_->wrappers[i]);
        if (rv != OrdEqual) {
            return rv;
        }
    }
    return (rv = ::ord(wrapperCount_, x.wrapperCount_));
}

::std::ostream& operator<<(::std::ostream& os, const MIRParam& x) {
    switch (x.tag()) {
        case MIRParam::TAG_LValue: {
            auto& e = x.as_LValue();
            os << e;
            break;
        }
        case MIRParam::TAG_Borrow: {
            auto& e = x.as_Borrow();
            os << "Borrow(" << e.type << ", " << e.val << ")";
            break;
        }
        case MIRParam::TAG_Constant: {
            auto& e = x.as_Constant();
            os << e;
            break;
        }
    }
    return os;
}

bool MIRParam::operator==(const MIRParam& x) const {
    if (this->tag() != x.tag()) {
        return false;
    }
    switch ((*this).tag()) {
        case MIRParam::TAG_LValue: {
            auto& ea = (*this).as_LValue();
            auto& eb = x.as_LValue();
            return ea == eb;
        }
        case MIRParam::TAG_Borrow: {
            auto& ea = (*this).as_Borrow();
            auto& eb = x.as_Borrow();
            return ea.type == eb.type && ea.val == eb.val;
        }
        case MIRParam::TAG_Constant: {
            auto& ea = (*this).as_Constant();
            auto& eb = x.as_Constant();
            return ea == eb;
        }
    }
    UNREACHABLE();
}

::std::ostream& operator<<(::std::ostream& os, const MIRRValue& x) {
    switch (x.tag()) {
        case MIRRValue::TAG_Use: {
            auto& e = x.as_Use();
            os << "Use(" << e << ")";
            break;
        }
        case MIRRValue::TAG_Constant: {
            auto& e = x.as_Constant();
            os << "Constant(" << e << ")";
            break;
        }
        case MIRRValue::TAG_SizedArray: {
            auto& e = x.as_SizedArray();
            os << "SizedArray(" << e.val << "; " << e.count << ")";
            break;
        }
        case MIRRValue::TAG_Borrow: {
            auto& e = x.as_Borrow();
            os << "Borrow(" << e.type << ", " << e.val << ")";
            break;
        }
        case MIRRValue::TAG_Cast: {
            auto& e = x.as_Cast();
            os << "Cast(" << e.val << " as " << e.type << ")";
            break;
        }
        case MIRRValue::TAG_BinOp: {
            auto& e = x.as_BinOp();
            os << "BinOp(" << e.valL << " ";
            switch (e.op) {
                case MIRBinOp::ADD:
                    os << "ADD";
                    break;
                case MIRBinOp::SUB:
                    os << "SUB";
                    break;
                case MIRBinOp::MUL:
                    os << "MUL";
                    break;
                case MIRBinOp::DIV:
                    os << "DIV";
                    break;
                case MIRBinOp::MOD:
                    os << "MOD";
                    break;
                case MIRBinOp::ADD_OV:
                    os << "ADD_OV";
                    break;
                case MIRBinOp::SUB_OV:
                    os << "SUB_OV";
                    break;
                case MIRBinOp::MUL_OV:
                    os << "MUL_OV";
                    break;
                case MIRBinOp::DIV_OV:
                    os << "DIV_OV";
                    break;

                case MIRBinOp::BIT_OR:
                    os << "BIT_OR";
                    break;
                case MIRBinOp::BIT_AND:
                    os << "BIT_AND";
                    break;
                case MIRBinOp::BIT_XOR:
                    os << "BIT_XOR";
                    break;
                case MIRBinOp::BIT_SHL:
                    os << "BIT_SHL";
                    break;
                case MIRBinOp::BIT_SHR:
                    os << "BIT_SHR";
                    break;

                case MIRBinOp::EQ:
                    os << "EQ";
                    break;
                case MIRBinOp::NE:
                    os << "NE";
                    break;
                case MIRBinOp::GT:
                    os << "GT";
                    break;
                case MIRBinOp::GE:
                    os << "GE";
                    break;
                case MIRBinOp::LT:
                    os << "LT";
                    break;
                case MIRBinOp::LE:
                    os << "LE";
                    break;
            }
            os << " " << e.valR << ")";
            break;
        }
        case MIRRValue::TAG_UniOp: {
            auto& e = x.as_UniOp();
            os << "UniOp(" << e.val << " ";
            switch (e.op) {
                case MIRUniOp::INV:
                    os << "INV";
                    break;
                case MIRUniOp::NEG:
                    os << "NEG";
                    break;
            }
            os << ")";
            break;
        }
        case MIRRValue::TAG_DstMeta: {
            auto& e = x.as_DstMeta();
            os << "DstMeta(" << e.val << ")";
            break;
        }
        case MIRRValue::TAG_DstPtr: {
            auto& e = x.as_DstPtr();
            os << "DstPtr(" << e.val << ")";
            break;
        }
        case MIRRValue::TAG_MakeDst: {
            auto& e = x.as_MakeDst();
            os << "MakeDst(" << e.ptrVal << ", " << e.metaVal << ")";
            break;
        }
        case MIRRValue::TAG_Tuple: {
            auto& e = x.as_Tuple();
            os << "Tuple(" << e.vals << ")";
            break;
        }
        case MIRRValue::TAG_Array: {
            auto& e = x.as_Array();
            os << "Array(" << e.vals << ")";
            break;
        }
        case MIRRValue::TAG_UnionVariant: {
            auto& e = x.as_UnionVariant();
            os << "UnionVariant(" << e.path << " #" << e.index << ", " << e.val << ")";
            break;
        }
        case MIRRValue::TAG_EnumVariant: {
            auto& e = x.as_EnumVariant();
            os << "Variant(" << e.path << " #" << e.index << ", {" << e.vals << "})";
            break;
        }
        case MIRRValue::TAG_Struct: {
            auto& e = x.as_Struct();
            os << "Struct(" << e.path << ", {" << e.vals << "})";
            break;
        }
    }
    return os;
}

bool operator==(const MIRRValue& a, const MIRRValue& b) {
    if (a.tag() != b.tag()) {
        return false;
    }
    switch (a.tag()) {
        case MIRRValue::TAG_Use: {
            auto& are = a.as_Use();
            auto& bre = b.as_Use();
            return are == bre;
        }
        case MIRRValue::TAG_Constant: {
            auto& are = a.as_Constant();
            auto& bre = b.as_Constant();
            return are == bre;
        }
        case MIRRValue::TAG_SizedArray: {
            auto& are = a.as_SizedArray();
            auto& bre = b.as_SizedArray();
            if (are.val != bre.val) {
                return false;
            }
            if (are.count != bre.count) {
                return false;
            }
            return true;
            break;
        }
        case MIRRValue::TAG_Borrow: {
            auto& are = a.as_Borrow();
            auto& bre = b.as_Borrow();
            if (are.type != bre.type) {
                return false;
            }
            if (are.val != bre.val) {
                return false;
            }
            return true;
            break;
        }
        case MIRRValue::TAG_Cast: {
            auto& are = a.as_Cast();
            auto& bre = b.as_Cast();
            if (are.type != bre.type) {
                return false;
            }
            if (are.val != bre.val) {
                return false;
            }
            return true;
            break;
        }
        case MIRRValue::TAG_BinOp: {
            auto& are = a.as_BinOp();
            auto& bre = b.as_BinOp();
            if (are.valL != bre.valL) {
                return false;
            }
            if (are.op != bre.op) {
                return false;
            }
            if (are.valR != bre.valR) {
                return false;
            }
            return true;
            break;
        }
        case MIRRValue::TAG_UniOp: {
            auto& are = a.as_UniOp();
            auto& bre = b.as_UniOp();
            if (are.op != bre.op) {
                return false;
            }
            if (are.val != bre.val) {
                return false;
            }
            return true;
            break;
        }
        case MIRRValue::TAG_DstPtr: {
            auto& are = a.as_DstPtr();
            auto& bre = b.as_DstPtr();
            return are.val == bre.val;
        }
        case MIRRValue::TAG_DstMeta: {
            auto& are = a.as_DstMeta();
            auto& bre = b.as_DstMeta();
            return are.val == bre.val;
        }
        case MIRRValue::TAG_MakeDst: {
            auto& are = a.as_MakeDst();
            auto& bre = b.as_MakeDst();
            if (are.metaVal != bre.metaVal) {
                return false;
            }
            if (are.ptrVal != bre.ptrVal) {
                return false;
            }
            return true;
            break;
        }
        case MIRRValue::TAG_Tuple: {
            auto& are = a.as_Tuple();
            auto& bre = b.as_Tuple();
            return are.vals == bre.vals;
        }
        case MIRRValue::TAG_Array: {
            auto& are = a.as_Array();
            auto& bre = b.as_Array();
            return are.vals == bre.vals;
        }
        case MIRRValue::TAG_UnionVariant: {
            auto& are = a.as_UnionVariant();
            auto& bre = b.as_UnionVariant();
            if (are.path != bre.path) {
                return false;
            }
            if (are.index != bre.index) {
                return false;
            }
            return are.val == bre.val;
            break;
        }
        case MIRRValue::TAG_EnumVariant: {
            auto& are = a.as_EnumVariant();
            auto& bre = b.as_EnumVariant();
            if (are.path != bre.path) {
                return false;
            }
            if (are.index != bre.index) {
                return false;
            }
            return are.vals == bre.vals;
            break;
        }
        case MIRRValue::TAG_Struct: {
            auto& are = a.as_Struct();
            auto& bre = b.as_Struct();
            if (are.path != bre.path) {
                return false;
            }
            return are.vals == bre.vals;
            break;
        }
    }
    UNREACHABLE();
}

::std::ostream& operator<<(::std::ostream& os, const MIRTerminator& x) {
    auto fmtUnwind = [&os](const MIRUnwindAction& action) {
        switch (action.tag()) {
            case MIRUnwindAction::TAG_Continue: {
                os << "continue";
                break;
            }
            case MIRUnwindAction::TAG_Cleanup: {
                auto& ue = action.as_Cleanup();
                os << "cleanup bb" << ue;
                break;
            }
            case MIRUnwindAction::TAG_Terminate: {
                os << "terminate";
                break;
            }
            case MIRUnwindAction::TAG_Unreachable: {
                os << "unreachable";
                break;
            }
        }
    };
    switch (x.tag()) {
        case MIRTerminator::TAG_Incomplete: {
            os << "Invalid";
            break;
        }
        case MIRTerminator::TAG_Return: {
            os << "Return";
            break;
        }
        case MIRTerminator::TAG_UnwindResume: {
            os << "UnwindResume";
            break;
        }
        case MIRTerminator::TAG_UnwindTerminate: {
            os << "UnwindTerminate";
            break;
        }
        case MIRTerminator::TAG_Unreachable: {
            os << "Unreachable";
            break;
        }
        case MIRTerminator::TAG_Goto: {
            auto& e = x.as_Goto();
            os << "Goto(" << e << ")";
            break;
        }
        case MIRTerminator::TAG_If: {
            auto& e = x.as_If();
            os << "If( " << e.cond << " : " << e.bbTrue << ", " << e.bbFalse << ")";
            break;
        }
        case MIRTerminator::TAG_Switch: {
            auto& e = x.as_Switch();
            os << "Switch( ";
            if (e.validFlag != ~0u) {
                os << "IF df$" << e.validFlag << " ELSE bb" << e.invalidTarget << ", ";
            }
            os << e.val << " : ";
            for (unsigned int j = 0; j < e.targets.size(); j++) {
                os << j << " => bb" << e.targets[j] << ", ";
            }
            os << ")";
            break;
        }
        case MIRTerminator::TAG_SwitchValue: {
            auto& e = x.as_SwitchValue();
            os << "SwitchValue( " << e.val << " : ";
            switch (e.values.tag()) {
                case MIRSwitchValues::TAG_Unsigned: {
                    auto& ve = e.values.as_Unsigned();
                    for (unsigned int j = 0; j < e.targets.size(); j++) {
                        os << ve[j] << " => bb" << e.targets[j] << ", ";
                    }
                    break;
                }
                case MIRSwitchValues::TAG_Signed: {
                    auto& ve = e.values.as_Signed();
                    for (unsigned int j = 0; j < e.targets.size(); j++) {
                        os << (ve[j] >= 0 ? "+" : "") << ve[j] << " => bb" << e.targets[j] << ", ";
                    }
                    break;
                }
                case MIRSwitchValues::TAG_String: {
                    auto& ve = e.values.as_String();
                    for (unsigned int j = 0; j < e.targets.size(); j++) {
                        os << "\"" << ve[j] << "\" => bb" << e.targets[j] << ", ";
                    }
                    break;
                }
                case MIRSwitchValues::TAG_ByteString: {
                    auto& ve = e.values.as_ByteString();
                    for (unsigned int j = 0; j < e.targets.size(); j++) {
                        os << "b\"" << ve[j] << "\" => bb" << e.targets[j] << ", ";
                    }
                    break;
                }
            }
            os << "else bb" << e.defTarget << ")";
            break;
        }
        case MIRTerminator::TAG_Drop: {
            auto& e = x.as_Drop();
            os << "Drop(" << e.slot;
            if (e.kind == MIRDropKind::SHALLOW) {
                os << " SHALLOW";
            }
            if (e.flagIdx != ~0u) {
                os << " IF df$" << e.flagIdx;
            }
            os << ") -> bb" << e.target << " unwind ";
            fmtUnwind(e.unwind);
            break;
        }
        case MIRTerminator::TAG_Call: {
            auto& e = x.as_Call();
            os << "Call( " << e.retVal << " = ";
            switch (e.fcn.tag()) {
                case MIRCallTarget::TAG_Value: {
                    auto& e2 = e.fcn.as_Value();
                    os << "(" << e2 << ")";
                    break;
                }
                case MIRCallTarget::TAG_Path: {
                    auto& e2 = e.fcn.as_Path();
                    os << e2;
                    break;
                }
                case MIRCallTarget::TAG_Intrinsic: {
                    auto& e2 = e.fcn.as_Intrinsic();
                    os << "\"" << e2.name << "\"::" << e2.params;
                    break;
                }
            }
            os << "( ";
            for (const auto& arg : e.args) {
                os << arg << ", ";
            }
            os << "), bb" << e.retBlock << ", ";
            fmtUnwind(e.unwind);
            os << ")";
            break;
        }
        case MIRTerminator::TAG_TailCall: {
            auto& e = x.as_TailCall();
            os << "TailCall( ";
            switch (e.fcn.tag()) {
                case MIRCallTarget::TAG_Value: {
                    auto& e2 = e.fcn.as_Value();
                    os << "(" << e2 << ")";
                    break;
                }
                case MIRCallTarget::TAG_Path: {
                    auto& e2 = e.fcn.as_Path();
                    os << e2;
                    break;
                }
                case MIRCallTarget::TAG_Intrinsic: {
                    auto& e2 = e.fcn.as_Intrinsic();
                    os << "\"" << e2.name << "\"::" << e2.params;
                    break;
                }
            }
            os << "( ";
            for (const auto& arg : e.args) {
                os << arg << ", ";
            }
            os << ") )";
            break;
        }
        case MIRTerminator::TAG_Asm2: {
            auto& e = x.as_Asm2();
            os << "asm!(...) -> ";
            if (e.retBlock != ~0u) {
                os << "bb" << e.retBlock << ", ";
            }
            for (const auto& p : e.params) {
                if (const auto* bb = p.opt_Label()) {
                    os << "label bb" << *bb << ", ";
                }
            }
            break;
        }
    }

    return os;
}

bool operator==(const MIRTerminator& a, const MIRTerminator& b) {
    if (a.tag() != b.tag()) {
        return false;
    }
    auto unwindEqual = [](const MIRUnwindAction& lhs, const MIRUnwindAction& rhs) {
        if (lhs.tag() != rhs.tag()) {
            return false;
        }
        switch (lhs.tag()) {
            case MIRUnwindAction::TAG_Continue: {
                return true;
            }
            case MIRUnwindAction::TAG_Cleanup: {
                auto& le = lhs.as_Cleanup();
                auto& re = rhs.as_Cleanup();
                return le == re;
            }
            case MIRUnwindAction::TAG_Terminate: {
                return true;
            }
            case MIRUnwindAction::TAG_Unreachable: {
                return true;
            }
        }
        return false;
    };
    switch (a.tag()) {
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
            auto& ae = a.as_Goto();
            auto& be = b.as_Goto();
            if (ae != be) {
                return false;
            }
            break;
        }
        case MIRTerminator::TAG_If: {
            auto& ae = a.as_If();
            auto& be = b.as_If();
            if (ae.cond != be.cond) {
                return false;
            }
            if (ae.bbTrue != be.bbTrue) {
                return false;
            }
            if (ae.bbFalse != be.bbFalse) {
                return false;
            }
            break;
        }
        case MIRTerminator::TAG_Switch: {
            auto& ae = a.as_Switch();
            auto& be = b.as_Switch();
            if (ae.val != be.val) {
                return false;
            }
            if (ae.targets != be.targets) {
                return false;
            }
            if (ae.validFlag != be.validFlag) {
                return false;
            }
            if (ae.invalidTarget != be.invalidTarget) {
                return false;
            }
            break;
        }
        case MIRTerminator::TAG_SwitchValue: {
            auto& ae = a.as_SwitchValue();
            auto& be = b.as_SwitchValue();
            if (ae.val != be.val) {
                return false;
            }
            if (ae.targets != be.targets) {
                return false;
            }
            if (ae.defTarget != be.defTarget) {
                return false;
            }
            if (ae.values != be.values) {
                return false;
            }
            break;
        }
        case MIRTerminator::TAG_Drop: {
            auto& ae = a.as_Drop();
            auto& be = b.as_Drop();
            if (ae.kind != be.kind || ae.slot != be.slot || ae.flagIdx != be.flagIdx || ae.target != be.target || !unwindEqual(ae.unwind, be.unwind)) {
                return false;
            }
            break;
        }
        case MIRTerminator::TAG_Call: {
            auto& ae = a.as_Call();
            auto& be = b.as_Call();
            if (ae.retVal != be.retVal) {
                return false;
            }
            if (ae.fcn.tag() != be.fcn.tag()) {
                return false;
            }
            switch (ae.fcn.tag()) {
                case MIRCallTarget::TAG_Value: {
                    auto& afe = ae.fcn.as_Value();
                    auto& bfe = be.fcn.as_Value();
                    if (afe != bfe) {
                        return false;
                    }
                    break;
                }
                case MIRCallTarget::TAG_Path: {
                    auto& afe = ae.fcn.as_Path();
                    auto& bfe = be.fcn.as_Path();
                    if (afe != bfe) {
                        return false;
                    }
                    break;
                }
                case MIRCallTarget::TAG_Intrinsic: {
                    auto& afe = ae.fcn.as_Intrinsic();
                    auto& bfe = be.fcn.as_Intrinsic();
                    if (afe.name != bfe.name) {
                        return false;
                    }
                    if (afe.params != bfe.params) {
                        return false;
                    }
                    break;
                }
            }
            if (ae.args != be.args) {
                return false;
            }
            if (ae.retBlock != be.retBlock) {
                return false;
            }
            if (!unwindEqual(ae.unwind, be.unwind)) {
                return false;
            }
            if (ae.source != be.source) {
                return false;
            }
            if (ae.tracksCaller != be.tracksCaller) {
                return false;
            }
            break;
        }
        case MIRTerminator::TAG_TailCall: {
            auto& ae = a.as_TailCall();
            auto& be = b.as_TailCall();
            if (ae.fcn.tag() != be.fcn.tag()) {
                return false;
            }
            switch (ae.fcn.tag()) {
                case MIRCallTarget::TAG_Value: {
                    auto& afe = ae.fcn.as_Value();
                    auto& bfe = be.fcn.as_Value();
                    if (afe != bfe) {
                        return false;
                    }
                    break;
                }
                case MIRCallTarget::TAG_Path: {
                    auto& afe = ae.fcn.as_Path();
                    auto& bfe = be.fcn.as_Path();
                    if (afe != bfe) {
                        return false;
                    }
                    break;
                }
                case MIRCallTarget::TAG_Intrinsic: {
                    auto& afe = ae.fcn.as_Intrinsic();
                    auto& bfe = be.fcn.as_Intrinsic();
                    if (afe.name != bfe.name) {
                        return false;
                    }
                    if (afe.params != bfe.params) {
                        return false;
                    }
                    break;
                }
            }
            if (ae.args != be.args) {
                return false;
            }
            if (ae.source != be.source) {
                return false;
            }
            if (ae.tracksCaller != be.tracksCaller) {
                return false;
            }
            break;
        }
        case MIRTerminator::TAG_Asm2: {
            auto& ae = a.as_Asm2();
            auto& be = b.as_Asm2();
            if (ae.options != be.options || ae.lines != be.lines || ae.params != be.params || ae.retBlock != be.retBlock) {
                return false;
            }
            break;
        }
    }
    return true;
}

bool operator==(const MIRAsmParam& a, const MIRAsmParam& b) {
    if (a.tag() != b.tag()) {
        return false;
    }
    switch (a.tag()) {
        case MIRAsmParam::TAG_Const: {
            auto& ae = a.as_Const();
            auto& be = b.as_Const();
            return ae == be;
        }
        case MIRAsmParam::TAG_Sym: {
            auto& ae = a.as_Sym();
            auto& be = b.as_Sym();
            return ae == be;
        }
        case MIRAsmParam::TAG_Reg: {
            auto& ae = a.as_Reg();
            auto& be = b.as_Reg();
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
            break;
        }
        case MIRAsmParam::TAG_Label: {
            auto& ae = a.as_Label();
            auto& be = b.as_Label();
            return ae == be;
        }
    }
    return true;
}

::std::ostream& operator<<(::std::ostream& os, const MIRStatement& x) {
    switch (x.tag()) {
        case MIRStatement::TAG_Assign: {
            auto& e = x.as_Assign();
            os << e.dst << " = " << e.src;
            break;
        }
        case MIRStatement::TAG_Asm: {
            auto& e = x.as_Asm();
            os << "(";
            for (const auto& spec : e.outputs) {
                os << "\"" << spec.first << "\" : " << spec.second << ", ";
            }
            os << ") = llvm_asm!(\"" << FmtEscaped(e.tpl) << "\", input=( ";
            for (const auto& spec : e.inputs) {
                os << "\"" << spec.first << "\" : " << spec.second << ", ";
            }
            os << "), clobbers=[" << e.clobbers << "], flags=[" << e.flags << "])";
            break;
        }
        case MIRStatement::TAG_Asm2: {
            auto& e = x.as_Asm2();
            os << "asm!(";
            for (const auto& l : e.lines) {
                if (&l != &e.lines.front()) {
                    os << " ";
                }
                l.fmt(os);
            }
            for (const auto& p : e.params) {
                os << ", ";
                switch (p.tag()) {
                    case MIRAsmParam::TAG_Const: {
                        auto& v = p.as_Const();
                        os << "const " << v;
                        break;
                    }
                    case MIRAsmParam::TAG_Sym: {
                        auto& v = p.as_Sym();
                        os << "sym " << v;
                        break;
                    }
                    case MIRAsmParam::TAG_Reg: {
                        auto& v = p.as_Reg();
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
                        break;
                    }
                    case MIRAsmParam::TAG_Label: {
                        auto& v = p.as_Label();
                        os << "label bb" << v;
                        break;
                    }
                }
            }
            if (e.options.any()) {
                os << ", ";
                e.options.fmt(os);
            }
            os << ")";
            break;
        }
        case MIRStatement::TAG_SetDropFlag: {
            auto& e = x.as_SetDropFlag();
            os << "df$" << e.idx << " = ";
            if (e.other == ~0u) {
                os << e.newVal;
            } else {
                os << (e.newVal ? "!" : "") << "df$" << e.other;
            }
            break;
        }
        case MIRStatement::TAG_SaveDropFlag: {
            os << "SaveDropFlag()";
            break;
        }
        case MIRStatement::TAG_LoadDropFlag: {
            os << "LoadDropFlag()";
            break;
        }
        case MIRStatement::TAG_ScopeEnd: {
            auto& e = x.as_ScopeEnd();
            os << "ScopeEnd(";
            for (auto idx : e.slots) {
                os << "_$" << idx << ",";
            }
            os << ")";
            break;
        }
    }
    return os;
}

bool operator==(const MIRStatement& a, const MIRStatement& b) {
    if (a.tag() != b.tag()) {
        return false;
    }

    switch (a.tag()) {
        case MIRStatement::TAG_Assign: {
            auto& ae = a.as_Assign();
            auto& be = b.as_Assign();
            return ae.dst == be.dst && ae.src == be.src;
        }
        case MIRStatement::TAG_Asm: {
            auto& ae = a.as_Asm();
            auto& be = b.as_Asm();
            return ae.outputs == be.outputs && ae.inputs == be.inputs && ae.clobbers == be.clobbers && ae.flags == be.flags;
        }
        case MIRStatement::TAG_Asm2: {
            auto& ae = a.as_Asm2();
            auto& be = b.as_Asm2();
            return ae.lines == be.lines && ae.options == be.options && ae.params == be.params;
        }
        case MIRStatement::TAG_SetDropFlag: {
            auto& ae = a.as_SetDropFlag();
            auto& be = b.as_SetDropFlag();
            return ae.idx == be.idx && ae.other == be.other && ae.newVal == be.newVal;
        }
        case MIRStatement::TAG_SaveDropFlag: {
            auto& ae = a.as_SaveDropFlag();
            auto& be = b.as_SaveDropFlag();
            return ae.idx == be.idx && ae.slot == be.slot && ae.bitIndex == be.bitIndex;
        }
        case MIRStatement::TAG_LoadDropFlag: {
            auto& ae = a.as_LoadDropFlag();
            auto& be = b.as_LoadDropFlag();
            return ae.idx == be.idx && ae.slot == be.slot && ae.bitIndex == be.bitIndex;
        }
        case MIRStatement::TAG_ScopeEnd: {
            auto& ae = a.as_ScopeEnd();
            auto& be = b.as_ScopeEnd();
            return ae.slots == be.slots;
        }
    }
    UNREACHABLE();
}

MIRLValue::Storage MIRLValue::Storage::clone() const {
    if (is_Static()) {
        return newStatic(as_Static().clone());
    } else {
        return Storage(this->val);
    }
}

MIRConstant MIRConstant::clone() const {
    switch ((*this).tag()) {
        case MIRConstant::TAG_Int: {
            auto& e2 = (*this).as_Int();
            return MIRConstant(e2);
        }
        case MIRConstant::TAG_Uint: {
            auto& e2 = (*this).as_Uint();
            return MIRConstant(e2);
        }
        case MIRConstant::TAG_Float: {
            auto& e2 = (*this).as_Float();
            return MIRConstant(e2);
        }
        case MIRConstant::TAG_Bool: {
            auto& e2 = (*this).as_Bool();
            return MIRConstant(e2);
        }
        case MIRConstant::TAG_Bytes: {
            auto& e2 = (*this).as_Bytes();
            return MIRConstant(e2);
        }
        case MIRConstant::TAG_StaticString: {
            auto& e2 = (*this).as_StaticString();
            return MIRConstant(e2);
        }
        case MIRConstant::TAG_Encoded: {
            auto& e2 = (*this).as_Encoded();
            return MIRConstant::make_Encoded({e2.type, e2.value.clone()});
        }
        case MIRConstant::TAG_Const: {
            auto& e2 = (*this).as_Const();
            return MIRConstant::make_Const({box$(e2.p->clone())});
        }
        case MIRConstant::TAG_Generic: {
            auto& e2 = (*this).as_Generic();
            return MIRConstant(e2);
        }
        case MIRConstant::TAG_Function: {
            auto& e2 = (*this).as_Function();
            return MIRConstant::make_Function({box$(e2.p->clone())});
        }
        case MIRConstant::TAG_ItemAddr: {
            auto& e2 = (*this).as_ItemAddr();
            return MIRConstant::make_ItemAddr(e2.clone());
        }
    }
    UNREACHABLE();
}

MIRParam MIRParam::clone() const {
    switch ((*this).tag()) {
        case MIRParam::TAG_LValue: {
            auto& e = (*this).as_LValue();
            return e.clone();
        }
        case MIRParam::TAG_Borrow: {
            auto& e = (*this).as_Borrow();
            return MIRParam::make_Borrow({e.type, e.val.clone()});
        }
        case MIRParam::TAG_Constant: {
            auto& e = (*this).as_Constant();
            return e.clone();
        }
    }
    UNREACHABLE();
}

MIRRValue MIRRValue::clone() const {
    switch ((*this).tag()) {
        case MIRRValue::TAG_Use: {
            auto& e = (*this).as_Use();
            return MIRRValue(e.clone());
        }
        case MIRRValue::TAG_Constant: {
            auto& e = (*this).as_Constant();
            return e.clone();
        }
        case MIRRValue::TAG_SizedArray: {
            auto& e = (*this).as_SizedArray();
            return MIRRValue::make_SizedArray({e.val.clone(), e.count.clone()});
        }
        case MIRRValue::TAG_Borrow: {
            auto& e = (*this).as_Borrow();
            return MIRRValue::make_Borrow({e.type, e.isRaw, e.val.clone()});
        }
        case MIRRValue::TAG_Cast: {
            auto& e = (*this).as_Cast();
            return MIRRValue::make_Cast({e.val.clone(), e.type});
        }
        case MIRRValue::TAG_BinOp: {
            auto& e = (*this).as_BinOp();
            return MIRRValue::make_BinOp({e.valL.clone(), e.op, e.valR.clone()});
        }
        case MIRRValue::TAG_UniOp: {
            auto& e = (*this).as_UniOp();
            return MIRRValue::make_UniOp({e.val.clone(), e.op});
        }
        case MIRRValue::TAG_DstMeta: {
            auto& e = (*this).as_DstMeta();
            return MIRRValue::make_DstMeta({e.val.clone()});
        }
        case MIRRValue::TAG_DstPtr: {
            auto& e = (*this).as_DstPtr();
            return MIRRValue::make_DstPtr({e.val.clone()});
        }
        case MIRRValue::TAG_MakeDst: {
            auto& e = (*this).as_MakeDst();
            return MIRRValue::make_MakeDst({e.ptrVal.clone(), e.metaVal.clone()});
        }
        case MIRRValue::TAG_Tuple: {
            auto& e = (*this).as_Tuple();
            decltype(e.vals) ret;
            ret.reserve(e.vals.size());
            for (const auto& v : e.vals) {
                ret.push_back(v.clone());
            }
            return MIRRValue::make_Tuple({mv$(ret)});
            break;
        }
        case MIRRValue::TAG_Array: {
            auto& e = (*this).as_Array();
            decltype(e.vals) ret;
            ret.reserve(e.vals.size());
            for (const auto& v : e.vals) {
                ret.push_back(v.clone());
            }
            return MIRRValue::make_Array({mv$(ret)});
            break;
        }
        case MIRRValue::TAG_UnionVariant: {
            auto& e = (*this).as_UnionVariant();
            return MIRRValue::make_UnionVariant({e.path.clone(), e.index, e.val.clone()});
        }
        case MIRRValue::TAG_EnumVariant: {
            auto& e = (*this).as_EnumVariant();
            decltype(e.vals) ret;
            ret.reserve(e.vals.size());
            for (const auto& v : e.vals) {
                ret.push_back(v.clone());
            }
            return MIRRValue::make_EnumVariant({e.path.clone(), e.index, mv$(ret)});
            break;
        }
        case MIRRValue::TAG_Struct: {
            auto& e = (*this).as_Struct();
            decltype(e.vals) ret;
            ret.reserve(e.vals.size());
            for (const auto& v : e.vals) {
                ret.push_back(v.clone());
            }
            return MIRRValue::make_Struct({e.path.clone(), mv$(ret)});
            break;
        }
    }
    UNREACHABLE();
}

MIRSwitchValues MIRSwitchValues::clone() const {
    switch ((*this).tag()) {
        case MIRSwitchValues::TAG_Unsigned: {
            auto& ve = (*this).as_Unsigned();
            return ve;
        }
        case MIRSwitchValues::TAG_Signed: {
            auto& ve = (*this).as_Signed();
            return ve;
        }
        case MIRSwitchValues::TAG_String: {
            auto& ve = (*this).as_String();
            return ve;
        }
        case MIRSwitchValues::TAG_ByteString: {
            auto& ve = (*this).as_ByteString();
            return ve;
        }
    }
    UNREACHABLE();
}

bool MIRSwitchValues::operator==(const MIRSwitchValues& x) const {
    if (this->tag() != x.tag()) {
        return false;
    }
    switch ((*this).tag()) {
        case MIRSwitchValues::TAG_Unsigned: {
            auto& ave = (*this).as_Unsigned();
            auto& bve = x.as_Unsigned();
            if (ave != bve) {
                return false;
            }
            break;
        }
        case MIRSwitchValues::TAG_Signed: {
            auto& ave = (*this).as_Signed();
            auto& bve = x.as_Signed();
            if (ave != bve) {
                return false;
            }
            break;
        }
        case MIRSwitchValues::TAG_String: {
            auto& ave = (*this).as_String();
            auto& bve = x.as_String();
            if (ave != bve) {
                return false;
            }
            break;
        }
        case MIRSwitchValues::TAG_ByteString: {
            auto& ave = (*this).as_ByteString();
            auto& bve = x.as_ByteString();
            if (ave != bve) {
                return false;
            }
            break;
        }
    }
    return true;
}

const HIRTypeData* MIRCloner::valueGenericType(HIRGenericRef ce) const {
    TODO(sp, "`value_generic_type` not implemented, shouldn't be called unless `monomorpiser` has been overridden");
}

MIRCloner::MIRCloner(const Span& sp, HIRTypeInterner& types)
    : nop(new MonomorphiserNop(types))
    , sp(sp)
{
}

MIRCloner::~MIRCloner() = default;

const Monomorphiser& MIRCloner::monomorphiser() const {
    return *nop;
}

HIRTypeRef MIRCloner::monomorph(const HIRTypeData* ty) const {
    auto rv = monomorphiser().monomorphType(sp, ty);
    if (auto* r = resolve()) {
        r->expandAssociatedTypes(sp, rv);
    }
    return rv;
}

HIRGenericPath MIRCloner::monomorph(const HIRGenericPath& ty) const {
    auto rv = monomorphiser().monomorphGenericpath(sp, ty, false);
    if (const auto* r = resolve()) {
        r->evaluatePathParams(sp, rv.params);
        for (auto& arg : rv.params.types) {
            r->expandAssociatedTypes(sp, arg);
        }
    }
    return rv;
}

HIRPath MIRCloner::monomorph(const HIRPath& ty) const {
    auto rv = monomorphiser().monomorphPath(sp, ty, false);
    if (const auto* r = resolve()) {
        switch (rv.data.tag()) {
            case HIRPath::Data::TAG_Generic: {
                auto& e2 = rv.data.as_Generic();
                r->evaluatePathParams(sp, e2.params);
                for (auto& arg : e2.params.types) {
                    r->expandAssociatedTypes(sp, arg);
                }
                break;
            }
            case HIRPath::Data::TAG_UfcsInherent: {
                auto& e2 = rv.data.as_UfcsInherent();
                r->expandAssociatedTypes(sp, e2.type);
                r->evaluatePathParams(sp, e2.params);
                r->evaluatePathParams(sp, e2.implParams);
                for (auto& arg : e2.params.types) {
                    r->expandAssociatedTypes(sp, arg);
                }
                // TODO: impl params too?
                for (auto& arg : e2.implParams.types) {
                    r->expandAssociatedTypes(sp, arg);
                }
                break;
            }
            case HIRPath::Data::TAG_UfcsKnown: {
                auto& e2 = rv.data.as_UfcsKnown();
                r->expandAssociatedTypes(sp, e2.type);
                r->evaluatePathParams(sp, e2.trait.params);
                r->evaluatePathParams(sp, e2.params);
                for (auto& arg : e2.trait.params.types) {
                    r->expandAssociatedTypes(sp, arg);
                }
                for (auto& arg : e2.params.types) {
                    r->expandAssociatedTypes(sp, arg);
                }
                break;
            }
            case HIRPath::Data::TAG_UfcsUnknown: {
                BUG(sp, "Encountered UfcsUnknown");
                break;
            }
        }
    }
    return rv;
}

HIRPathParams MIRCloner::monomorph(const HIRPathParams& ty) const {
    auto rv = monomorphiser().monomorphPathParams(sp, ty, false);
    if (const auto* r = resolve()) {
        r->evaluatePathParams(sp, rv);
        for (auto& arg : rv.types) {
            r->expandAssociatedTypes(sp, arg);
        }
    }
    return rv;
}

::std::vector<MIRAsmParam> MIRCloner::cloneAsmParams(const ::std::vector<MIRAsmParam>& params) const {
    ::std::vector<MIRAsmParam> rv;
    for (const auto& p : params) {
        switch (p.tag()) {
            case MIRAsmParam::TAG_Const: {
                auto& v = p.as_Const();
                rv.push_back(this->cloneConstant(v));
                break;
            }
            case MIRAsmParam::TAG_Sym: {
                auto& v = p.as_Sym();
                rv.push_back(this->monomorph(v));
                break;
            }
            case MIRAsmParam::TAG_Reg: {
                auto& v = p.as_Reg();
                rv.push_back(MIRAsmParam::make_Reg({v.dir, v.spec.clone(), v.input ? box$(this->cloneParam(*v.input)) : std::unique_ptr<MIRParam>(), v.output ? box$(this->cloneLval(*v.output)) : std::unique_ptr<MIRLValue>()}));
                break;
            }
            case MIRAsmParam::TAG_Label: {
                auto& v = p.as_Label();
                rv.push_back(MIRAsmParam::make_Label(mapBbIdx(v)));
                break;
            }
        }
    }
    return rv;
}

MIRStatement MIRCloner::cloneStmt(const MIRStatement& src) const {
    switch (src.tag()) {
        case MIRStatement::TAG_Assign: {
            auto& se = src.as_Assign();
            return MIRStatement::make_Assign({this->cloneLval(se.dst), this->cloneRval(se.src)});
        }
        case MIRStatement::TAG_Asm: {
            auto& se = src.as_Asm();
            return MIRStatement::make_Asm({se.tpl, this->cloneNameLvalVec(se.outputs), this->cloneNameLvalVec(se.inputs), se.clobbers, se.flags});
        }
        case MIRStatement::TAG_Asm2: {
            auto& se = src.as_Asm2();
            return MIRStatement::make_Asm2({se.options, se.lines, this->cloneAsmParams(se.params)});
        }
        case MIRStatement::TAG_SetDropFlag: {
            auto& se = src.as_SetDropFlag();
            return MIRStatement::make_SetDropFlag({mapDropFlag(se.idx), se.newVal, se.other == ~0u ? ~0u : mapDropFlag(se.other)});
        }
        case MIRStatement::TAG_SaveDropFlag: {
            TODO(Span(), "clone_bb SaveDropFlag");
            break;
        }
        case MIRStatement::TAG_LoadDropFlag: {
            TODO(Span(), "clone_bb LoadDropFlag");
            break;
        }
        case MIRStatement::TAG_ScopeEnd: {
            auto& se = src.as_ScopeEnd();
            MIRStatement::Data_ScopeEnd newSe;
            newSe.slots.reserve(se.slots.size());
            for (auto idx : se.slots) {
                newSe.slots.push_back(mapLocal(idx));
            }
            return MIRStatement(mv$(newSe));
        }
    }
    UNREACHABLE();
}

MIRTerminator MIRCloner::cloneTerm(const MIRTerminator& src) const {
    switch (src.tag()) {
        case MIRTerminator::TAG_Incomplete: {
            return MIRTerminator::make_Incomplete({});
        }
        case MIRTerminator::TAG_Return: {
            return MIRTerminator::make_Return({});
        }
        case MIRTerminator::TAG_UnwindResume: {
            return MIRTerminator::make_UnwindResume({});
        }
        case MIRTerminator::TAG_UnwindTerminate: {
            return MIRTerminator::make_UnwindTerminate({});
        }
        case MIRTerminator::TAG_Unreachable: {
            return MIRTerminator::make_Unreachable({});
        }
        case MIRTerminator::TAG_Goto: {
            auto& se = src.as_Goto();
            return MIRTerminator::make_Goto(mapBbIdx(se));
        }
        case MIRTerminator::TAG_If: {
            auto& se = src.as_If();
            return MIRTerminator::make_If({this->cloneLval(se.cond), mapBbIdx(se.bbTrue), mapBbIdx(se.bbFalse)});
        }
        case MIRTerminator::TAG_Switch: {
            auto& se = src.as_Switch();
            ::std::vector<MIRBasicBlockId> arms;
            arms.reserve(se.targets.size());
            for (const auto& bbi : se.targets) {
                arms.push_back(mapBbIdx(bbi));
            }
            return MIRTerminator::make_Switch({this->cloneLval(se.val), mv$(arms), se.validFlag == ~0u ? ~0u : mapDropFlag(se.validFlag), se.invalidTarget == ~0u ? ~0u : mapBbIdx(se.invalidTarget)});
        }
        case MIRTerminator::TAG_SwitchValue: {
            auto& se = src.as_SwitchValue();
            ::std::vector<MIRBasicBlockId> arms;
            arms.reserve(se.targets.size());
            for (const auto& bbi : se.targets) {
                arms.push_back(mapBbIdx(bbi));
            }
            return MIRTerminator::make_SwitchValue({this->cloneLval(se.val), mapBbIdx(se.defTarget), mv$(arms), se.values.clone()});
        }
        case MIRTerminator::TAG_Drop: {
            auto& se = src.as_Drop();
            MIRUnwindAction unwind;
            switch (se.unwind.tag()) {
                case MIRUnwindAction::TAG_Continue: {
                    unwind = MIRUnwindAction::make_Continue({});
                    break;
                }
                case MIRUnwindAction::TAG_Cleanup: {
                    auto& ue = se.unwind.as_Cleanup();
                    unwind = MIRUnwindAction::make_Cleanup(mapBbIdx(ue));
                    break;
                }
                case MIRUnwindAction::TAG_Terminate: {
                    unwind = MIRUnwindAction::make_Terminate({});
                    break;
                }
                case MIRUnwindAction::TAG_Unreachable: {
                    unwind = MIRUnwindAction::make_Unreachable({});
                    break;
                }
            }
            return MIRTerminator::make_Drop({se.kind, this->cloneLval(se.slot), se.flagIdx == ~0u ? ~0u : mapDropFlag(se.flagIdx), mapBbIdx(se.target), mv$(unwind)});
        }
        case MIRTerminator::TAG_Call: {
            auto& se = src.as_Call();
            MIRCallTarget tgt;
            switch (se.fcn.tag()) {
                case MIRCallTarget::TAG_Value: {
                    auto& ste = se.fcn.as_Value();
                    tgt = MIRCallTarget::make_Value(this->cloneLval(ste));
                    break;
                }
                case MIRCallTarget::TAG_Path: {
                    auto& ste = se.fcn.as_Path();
                    tgt = MIRCallTarget::make_Path(this->monomorph(ste));
                    break;
                }
                case MIRCallTarget::TAG_Intrinsic: {
                    auto& ste = se.fcn.as_Intrinsic();
                    tgt = MIRCallTarget::make_Intrinsic({ste.name, this->monomorph(ste.params)});
                    break;
                }
            }
            MIRUnwindAction unwind;
            switch (se.unwind.tag()) {
                case MIRUnwindAction::TAG_Continue: {
                    unwind = MIRUnwindAction::make_Continue({});
                    break;
                }
                case MIRUnwindAction::TAG_Cleanup: {
                    auto& ue = se.unwind.as_Cleanup();
                    unwind = MIRUnwindAction::make_Cleanup(mapBbIdx(ue));
                    break;
                }
                case MIRUnwindAction::TAG_Terminate: {
                    unwind = MIRUnwindAction::make_Terminate({});
                    break;
                }
                case MIRUnwindAction::TAG_Unreachable: {
                    unwind = MIRUnwindAction::make_Unreachable({});
                    break;
                }
            }
            return MIRTerminator::make_Call({mapBbIdx(se.retBlock), mv$(unwind), this->cloneLval(se.retVal), mv$(tgt), this->cloneParamVec(se.args), se.source, se.tracksCaller});
        }
        case MIRTerminator::TAG_TailCall: {
            auto& se = src.as_TailCall();
            MIRCallTarget tgt;
            switch (se.fcn.tag()) {
                case MIRCallTarget::TAG_Value: {
                    auto& ste = se.fcn.as_Value();
                    tgt = MIRCallTarget::make_Value(this->cloneLval(ste));
                    break;
                }
                case MIRCallTarget::TAG_Path: {
                    auto& ste = se.fcn.as_Path();
                    tgt = MIRCallTarget::make_Path(this->monomorph(ste));
                    break;
                }
                case MIRCallTarget::TAG_Intrinsic: {
                    auto& ste = se.fcn.as_Intrinsic();
                    tgt = MIRCallTarget::make_Intrinsic({ste.name, this->monomorph(ste.params)});
                    break;
                }
            }
            return MIRTerminator::make_TailCall({mv$(tgt), this->cloneParamVec(se.args), se.source, se.tracksCaller});
        }
        case MIRTerminator::TAG_Asm2: {
            auto& se = src.as_Asm2();
            return MIRTerminator::make_Asm2({se.options, se.lines, this->cloneAsmParams(se.params), se.retBlock == ~0u ? ~0u : mapBbIdx(se.retBlock)});
        }
    }
    UNREACHABLE();
}

::std::vector<::std::pair<::std::string, MIRLValue>> MIRCloner::cloneNameLvalVec(const ::std::vector<::std::pair<::std::string, MIRLValue>>& src) const {
    ::std::vector<::std::pair<::std::string, MIRLValue>> rv;
    rv.reserve(src.size());
    for (const auto& e : src) {
        rv.push_back(::std::make_pair(e.first, this->cloneLval(e.second)));
    }
    return rv;
}

::std::vector<MIRLValue> MIRCloner::cloneLvalVec(const ::std::vector<MIRLValue>& src) const {
    ::std::vector<MIRLValue> rv;
    rv.reserve(src.size());
    for (const auto& lv : src) {
        rv.push_back(this->cloneLval(lv));
    }
    return rv;
}

::std::vector<MIRParam> MIRCloner::cloneParamVec(const ::std::vector<MIRParam>& src) const {
    ::std::vector<MIRParam> rv;
    rv.reserve(src.size());
    for (const auto& lv : src) {
        rv.push_back(this->cloneParam(lv));
    }
    return rv;
}

MIRLValue MIRCloner::cloneLval(const MIRLValue& src) const {
    auto wrappers = src.wrappers;
    for (auto& w : wrappers) {
        if (w.is_Index()) {
            w = MIRLValue::Wrapper::newIndex(mapLocal(w.as_Index()));
        }
    }
    switch (src.root.tag()) {
        case MIRLValue::Storage::TAG_Return: {
            return MIRLValue(MIRLValue::Storage::newReturn(), mv$(wrappers));
        }
        case MIRLValue::Storage::TAG_Argument: {
            decltype(src.root.as_Argument()) se = src.root.as_Argument();
            return MIRLValue(MIRLValue::Storage::newArgument(se), mv$(wrappers));
        }
        case MIRLValue::Storage::TAG_Local: {
            decltype(src.root.as_Local()) se = src.root.as_Local();
            return MIRLValue(MIRLValue::Storage::newLocal(this->mapLocal(se)), mv$(wrappers));
        }
        case MIRLValue::Storage::TAG_Static: {
            decltype(src.root.as_Static()) se = src.root.as_Static();
            return MIRLValue(MIRLValue::Storage::newStatic(this->monomorph(se)), mv$(wrappers));
        }
    }
    UNREACHABLE();
}

MIRConstant MIRCloner::cloneConstant(const MIRConstant& src) const {
    switch (src.tag()) {
        case MIRConstant::TAG_Int: {
            auto& ce = src.as_Int();
            return MIRConstant(ce);
        }
        case MIRConstant::TAG_Uint: {
            auto& ce = src.as_Uint();
            return MIRConstant(ce);
        }
        case MIRConstant::TAG_Float: {
            auto& ce = src.as_Float();
            return MIRConstant(ce);
        }
        case MIRConstant::TAG_Bool: {
            auto& ce = src.as_Bool();
            return MIRConstant(ce);
        }
        case MIRConstant::TAG_Bytes: {
            auto& ce = src.as_Bytes();
            return MIRConstant(ce);
        }
        case MIRConstant::TAG_StaticString: {
            auto& ce = src.as_StaticString();
            return MIRConstant(ce);
        }
        case MIRConstant::TAG_Encoded: {
            auto& ce = src.as_Encoded();
            return MIRConstant::make_Encoded({this->monomorph(ce.type), ce.value.clone()});
        }
        case MIRConstant::TAG_Const: {
            auto& ce = src.as_Const();
            return MIRConstant::make_Const({box$(this->monomorph(*ce.p))});
        }
        case MIRConstant::TAG_Generic: {
            auto& ce = src.as_Generic();
            auto val = monomorphiser().getValue(sp, ce);
            if (const auto* r = resolve()) {
                r->evaluateConstGeneric(sp, val);
            }
            switch (val.tag()) {
                default:
                    TODO(sp, "Monomorphise MIR generic constant " << ce << " = " << val);
                case HIRConstGeneric::TAG_Generic: {
                    auto& ve = val.as_Generic();
                    return ve;
                }
                case HIRConstGeneric::TAG_Evaluated: {
                    auto& ve = val.as_Evaluated();
                    const auto ty = this->monomorph(this->valueGenericType(ce));
                    auto v = EncodedLiteralSlice(*ve);
                    if (!ty->is_Primitive()) {
                        return MIRConstant::make_Encoded({ty, ve->clone()});
                    }
                    // TODO: This is duplicated in `mir/from_hir_match.cpp` - De-duplicate?
                    switch (ty->as_Primitive()) {
                        case HIRCoreType::Bool:
                            return MIRConstant::make_Bool({v.readUint(1) != 0});
                        case HIRCoreType::U8:
                        case HIRCoreType::U16:
                        case HIRCoreType::U32:
                        case HIRCoreType::U64:
                        case HIRCoreType::U128:
                            return MIRConstant::make_Uint({v.readUint(ve->bytes.size()), ty->as_Primitive()});
                        case HIRCoreType::Usize:
                            return MIRConstant::make_Uint({v.readUint(TargetGetPointerBits() / 8), ty->as_Primitive()});
                        case HIRCoreType::I8:
                        case HIRCoreType::I16:
                        case HIRCoreType::I32:
                        case HIRCoreType::I64:
                        case HIRCoreType::I128:
                            return MIRConstant::make_Int({v.readSint(ve->bytes.size()), ty->as_Primitive()});
                        case HIRCoreType::Isize:
                            return MIRConstant::make_Int({v.readSint(TargetGetPointerBits() / 8), ty->as_Primitive()});
                        case HIRCoreType::F16:
                        case HIRCoreType::F32:
                        case HIRCoreType::F64:
                        case HIRCoreType::F128:
                            return MIRConstant::make_Float({v.readFloat(ve->bytes.size()), ty->as_Primitive()});
                        case HIRCoreType::Char:
                            return MIRConstant::make_Uint({v.readUint(4), ty->as_Primitive()});
                        case HIRCoreType::Str:
                            BUG(sp, "`str` const generic");
                    }
                    break;
                }
            }
            break;
        }
        case MIRConstant::TAG_Function: {
            auto& ce = src.as_Function();
            return MIRConstant::make_Function({box$(this->monomorph(*ce.p))});
        }
        case MIRConstant::TAG_ItemAddr: {
            auto& ce = src.as_ItemAddr();
            if (!ce) {
                return MIRConstant::make_ItemAddr({});
            }
            return MIRConstant::make_ItemAddr({box$(this->monomorph(*ce)), ce.offset});
        }
    }
    UNREACHABLE();
}

MIRParam MIRCloner::cloneParam(const MIRParam& src) const {
    switch (src.tag()) {
        case MIRParam::TAG_LValue: {
            auto& se = src.as_LValue();
            return cloneLval(se);
        }
        case MIRParam::TAG_Borrow: {
            auto& se = src.as_Borrow();
            return MIRParam::make_Borrow({se.type, this->cloneLval(se.val)});
        }
        case MIRParam::TAG_Constant: {
            auto& se = src.as_Constant();
            return cloneConstant(se);
        }
    }
    UNREACHABLE();
}

MIRRValue MIRCloner::cloneRval(const MIRRValue& src) const {
    switch (src.tag()) {
        case MIRRValue::TAG_Use: {
            auto& se = src.as_Use();
            return MIRRValue(this->cloneLval(se));
        }
        case MIRRValue::TAG_Constant: {
            auto& se = src.as_Constant();
            return this->cloneConstant(se);
        }
        case MIRRValue::TAG_SizedArray: {
            auto& se = src.as_SizedArray();
            auto count = monomorphiser().monomorphArraysize(sp, se.count);
            if (const auto* resolver = resolve()) {
                resolver->evaluateArraySize(sp, count);
            }
            return MIRRValue::make_SizedArray({this->cloneParam(se.val), std::move(count)});
        }
        case MIRRValue::TAG_Borrow: {
            auto& se = src.as_Borrow();
            return MIRRValue::make_Borrow({se.type, se.isRaw, this->cloneLval(se.val)});
        }
        case MIRRValue::TAG_Cast: {
            auto& se = src.as_Cast();
            return MIRRValue::make_Cast({this->cloneLval(se.val), this->monomorph(se.type)});
        }
        case MIRRValue::TAG_BinOp: {
            auto& se = src.as_BinOp();
            return MIRRValue::make_BinOp({this->cloneParam(se.valL), se.op, this->cloneParam(se.valR)});
        }
        case MIRRValue::TAG_UniOp: {
            auto& se = src.as_UniOp();
            return MIRRValue::make_UniOp({this->cloneLval(se.val), se.op});
        }
        case MIRRValue::TAG_DstMeta: {
            auto& se = src.as_DstMeta();
            return MIRRValue::make_DstMeta({this->cloneLval(se.val)});
        }
        case MIRRValue::TAG_DstPtr: {
            auto& se = src.as_DstPtr();
            return MIRRValue::make_DstPtr({this->cloneLval(se.val)});
        }
        case MIRRValue::TAG_MakeDst: {
            auto& se = src.as_MakeDst();
            return MIRRValue::make_MakeDst({this->cloneParam(se.ptrVal), this->cloneParam(se.metaVal)});
        }
        case MIRRValue::TAG_Tuple: {
            auto& se = src.as_Tuple();
            return MIRRValue::make_Tuple({this->cloneParamVec(se.vals)});
        }
        case MIRRValue::TAG_Array: {
            auto& se = src.as_Array();
            return MIRRValue::make_Array({this->cloneParamVec(se.vals)});
        }
        case MIRRValue::TAG_UnionVariant: {
            auto& se = src.as_UnionVariant();
            return MIRRValue::make_UnionVariant({this->monomorph(se.path), se.index, this->cloneParam(se.val)});
        }
        case MIRRValue::TAG_EnumVariant: {
            auto& se = src.as_EnumVariant();
            return MIRRValue::make_EnumVariant({this->monomorph(se.path), se.index, this->cloneParamVec(se.vals)});
        }
        case MIRRValue::TAG_Struct: {
            auto& se = src.as_Struct();
            return MIRRValue::make_Struct({this->monomorph(se.path), this->cloneParamVec(se.vals)});
        }
    }
    UNREACHABLE();
}

MIRLValue::Storage::Storage(uintptr_t v)
    : val(v)
{
}

MIRLValue::Storage::Storage(Storage&& x)
    : val(x.val)
{
    x.val = 0;
}

MIRLValue::Storage& MIRLValue::Storage::operator=(Storage&& x) {
    this->~Storage();
    this->val = x.val;
    x.val = 0;
    return *this;
}

MIRLValue::Storage::~Storage() {
    if (is_Static()) {
        delete reinterpret_cast<HIRPath*>(val & ~3ull);
        val = 0;
    }
}

MIRLValue::Storage MIRLValue::Storage::newArgument(unsigned idx) {
    assert(idx < MAX_ARG);
    return Storage((idx + 1) << 2);
}

MIRLValue::Storage MIRLValue::Storage::newLocal(unsigned idx) {
    assert(idx <= MAX_ARG);
    return Storage((idx << 2) | 1);
}

MIRLValue::Storage MIRLValue::Storage::newStatic(HIRPath p) {
    HIRPath* ptr = new HIRPath(::std::move(p));
    return Storage(reinterpret_cast<uintptr_t>(ptr) | 2);
}

uintptr_t MIRLValue::Storage::getInner() const {
    assert(!is_Static());
    return val;
}

MIRLValue::Storage MIRLValue::Storage::fromInner(uintptr_t v) {
    assert((v & 3) < 2);
    return Storage(v);
}

MIRLValue::Storage::Tag MIRLValue::Storage::tag() const {
    if (val == 0) {
        return TAG_Return;
    }
    return static_cast<Tag>(val & 3);
}

char MIRLValue::Storage::as_Return() const {
    assert(is_Return());
    return 0;
}

unsigned MIRLValue::Storage::as_Argument() const {
    assert(is_Argument());
    return static_cast<unsigned>((val >> 2) - 1);
}

unsigned MIRLValue::Storage::as_Local() const {
    assert(is_Local());
    return static_cast<unsigned>(val >> 2);
}

const HIRPath& MIRLValue::Storage::as_Static() const {
    assert(is_Static());
    return *reinterpret_cast<const HIRPath*>(val & ~3llu);
}

HIRPath& MIRLValue::Storage::as_Static() {
    assert(is_Static());
    return *reinterpret_cast<HIRPath*>(val & ~3llu);
}

MIRLValue::Wrapper::Wrapper(u32 v)
    : val(v)
{
}

MIRLValue::Wrapper MIRLValue::Wrapper::newIndex(unsigned idx) {
    if (idx == ~0u) {
        idx = Storage::MAX_ARG;
    }
    return Wrapper((idx << 2) | 3);
}

char MIRLValue::Wrapper::as_Deref() const {
    assert(is_Deref());
    return 0;
}

unsigned MIRLValue::Wrapper::as_Field() const {
    assert(is_Field());
    return (val >> 2);
}

unsigned MIRLValue::Wrapper::as_Downcast() const {
    assert(is_Downcast());
    return (val >> 2);
}

// TODO: Should this return a LValue?
unsigned MIRLValue::Wrapper::as_Index() const {
    assert(is_Index());
    unsigned rv = (val >> 2);
    return rv;
}

void MIRLValue::Wrapper::incField() {
    assert(is_Field());
    *this = Wrapper::newField(as_Field() + 1);
}

void MIRLValue::Wrapper::incDowncast() {
    assert(is_Downcast());
    *this = Wrapper::newDowncast(as_Downcast() + 1);
}

MIRLValue::MIRLValue()
    : root(Storage::newReturn())
{
}

MIRLValue::MIRLValue(Storage root, ::std::vector<Wrapper> wrappers)
    : root(::std::move(root))
    , wrappers(::std::move(wrappers))
{
}

MIRLValue MIRLValue::newDeref(MIRLValue lv) {
    lv.wrappers.push_back(Wrapper::newDeref());
    return lv;
}

MIRLValue MIRLValue::newField(MIRLValue lv, unsigned idx) {
    lv.wrappers.push_back(Wrapper::newField(idx));
    return lv;
}

MIRLValue MIRLValue::newDowncast(MIRLValue lv, unsigned idx) {
    lv.wrappers.push_back(Wrapper::newDowncast(idx));
    return lv;
}

MIRLValue MIRLValue::newIndex(MIRLValue lv, unsigned localIdx) {
    lv.wrappers.push_back(Wrapper::newIndex(localIdx));
    return lv;
}

unsigned MIRLValue::as_Local() const {
    assert(wrappers.empty());
    return root.as_Local();
}

unsigned MIRLValue::as_Field() const {
    assert(!wrappers.empty());
    return wrappers.back().as_Field();
}

void MIRLValue::incField() {
    assert(wrappers.size() > 0);
    wrappers.back().incField();
}

void MIRLValue::incDowncast() {
    assert(wrappers.size() > 0);
    wrappers.back().incDowncast();
}

MIRLValue MIRLValue::cloneWrapped(::std::vector<Wrapper> wrappers) const {
    if (this->wrappers.empty()) {
        return MIRLValue(root.clone(), ::std::move(wrappers));
    } else {
        return cloneWrapped(wrappers.begin(), wrappers.end());
    }
}

MIRLValue MIRLValue::cloneUnwrapped(unsigned count) const {
    assert(count > 0);
    assert(count <= wrappers.size());
    return MIRLValue(root.clone(), ::std::vector<Wrapper>(wrappers.begin(), wrappers.end() - count));
}

bool MIRLValue::isEitherSubset(const MIRLValue& other) const {
    if (!(root == other.root)) {
        return false;
    }
    if (other.wrappers.size() > wrappers.size()) {
        return ::std::equal(wrappers.begin(), wrappers.end(), other.wrappers.begin());
    } else {
        return ::std::equal(other.wrappers.begin(), other.wrappers.end(), wrappers.begin());
    }
}

MIRLValue::RefCommon::RefCommon(const MIRLValue& lv, size_t wrapperCount)
    : lv_(&lv)
    , wrapperCount_(wrapperCount)
{
    assert(wrapperCount <= lv.wrappers.size());
}

bool MIRLValue::RefCommon::tryUnwrap() {
    if (wrapperCount_ == 0) {
        return false;
    } else {
        wrapperCount_--;
        return true;
    }
}

MIRLValue::RefCommon::Tag MIRLValue::RefCommon::tag() const {
    if (wrapperCount_ == 0) {
        switch (lv_->root.tag()) {
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
        switch (lv_->wrappers[wrapperCount_ - 1].tag()) {
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
    UNREACHABLE();
}

unsigned MIRLValue::RefCommon::as_Local() const {
    assert(is_Local());
    return lv_->root.as_Local();
}

char MIRLValue::RefCommon::as_Return() const {
    assert(is_Return());
    return lv_->root.as_Return();
}

unsigned MIRLValue::RefCommon::as_Argument() const {
    assert(is_Argument());
    return lv_->root.as_Argument();
}

const HIRPath& MIRLValue::RefCommon::as_Static() const {
    assert(is_Static());
    return lv_->root.as_Static();
}

char MIRLValue::RefCommon::as_Deref() const {
    assert(is_Deref());
    return lv_->wrappers[wrapperCount_ - 1].as_Deref();
}

unsigned MIRLValue::RefCommon::as_Field() const {
    assert(is_Field());
    return lv_->wrappers[wrapperCount_ - 1].as_Field();
}

unsigned MIRLValue::RefCommon::as_Downcast() const {
    assert(is_Downcast());
    return lv_->wrappers[wrapperCount_ - 1].as_Downcast();
}

unsigned MIRLValue::RefCommon::as_Index() const {
    assert(is_Index());
    return lv_->wrappers[wrapperCount_ - 1].as_Index();
}

MIRLValue::CRef::CRef(const MIRLValue& lv)
    : RefCommon(lv, lv.wrappers.size())
{
}

MIRLValue::CRef::CRef(const MIRLValue& lv, size_t wc)
    : RefCommon(lv, wc)
{
}

const MIRLValue::CRef MIRLValue::CRef::innerRef() const {
    assert(wrapperCount_ > 0);
    auto rv = *this;
    rv.wrapperCount_--;
    return rv;
}

MIRLValue::MRef::MRef(MIRLValue& lv)
    : RefCommon(lv, lv.wrappers.size())
{
}

MIRLValue::MRef MIRLValue::MRef::innerRef() {
    assert(wrapperCount_ > 0);
    auto rv = *this;
    rv.wrapperCount_--;
    return rv;
}

void MIRLValue::MRef::replace(MIRLValue x) {
    auto& mutLv = const_cast<MIRLValue&>(*lv_);
    if (wrapperCount_ == 0 && x.wrappers.empty()) {
        mutLv.root = ::std::move(x.root);
        return;
    }
    if (wrapperCount_ < lv_->wrappers.size()) {
        x.wrappers.insert(x.wrappers.end(), lv_->wrappers.begin() + wrapperCount_, lv_->wrappers.end());
    }
    mutLv = ::std::move(x);
}

ItemAddress::ItemAddress(::std::unique_ptr<HIRPath> p, U128 offset)
    : p(::std::move(p))
    , offset(offset)
{
}

MIREnumCachePtr::MIREnumCachePtr(const MIREnumCache* p)
    : p(p)
{
}

MIREnumCachePtr::MIREnumCachePtr(MIREnumCachePtr&& x)
    : p(x.p)
{
    x.p = nullptr;
}

MIREnumCachePtr& MIREnumCachePtr::operator=(MIREnumCachePtr&& x) {
    this->~MIREnumCachePtr();
    p = x.p;
    x.p = nullptr;
    return *this;
}

::std::ostream& operator<<(::std::ostream& os, const MIRLValue::CRef& x) {
    x.fmt(os);
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const MIRLValue::MRef& x) {
    x.fmt(os);
    return os;
}

MIRBasicBlockId MIRCloner::mapBbIdx(MIRBasicBlockId idx) const {
    return idx;
}

unsigned MIRCloner::mapLocal(unsigned f) const {
    return f;
}

unsigned MIRCloner::mapDropFlag(unsigned f) const {
    return f;
}

const StaticTraitResolve* MIRCloner::resolve() const {
    return nullptr;
}
