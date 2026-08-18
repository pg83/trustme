#include "mir_main_bindings.h"
#include "mir_main_bindings.h"

#include "mir_mir.h"
#include "hir_visitor.h"
#include "mir_operations.h"

#include <iomanip>

namespace {

    class MirDumper {
        ::std::ostream& os;
        unsigned int indentLevel;

    public:
        MirDumper(::std::ostream& os, unsigned int il)
            : os(os)
            , indentLevel(il)
        {
        }

        void dumpMir(const MIRFunction& fcn) {
            for (size_t i = 0; i < fcn.locals.size(); i++) {
                os << indent() << "let _$" << i << ": " << fcn.locals[i] << ";\n";
            }
            for (unsigned int i = 0; i < fcn.dropFlags.size(); i++) {
                os << indent() << "let df$" << i << " = " << fcn.dropFlags[i] << ";\n";
            }

#define FMT_M(x) FMT_CB(os, this->fmtVal(os, x);)
            for (unsigned int i = 0; i < fcn.blocks.size(); i++) {
                const auto& block = fcn.blocks[i];

                os << indent() << "bb" << i << ": {\n";
                incIndent();
                for (const auto& stmt : block.statements) {
                    os << indent();

                    TU_MATCH_HDRA( (stmt), {)
                    TU_ARMA(Assign, e) {
                            os << FMT_M(e.dst) << " = " << FMT_M(e.src) << ";\n";
                        }
                        TU_ARMA(Asm, e) {
                            os << "(";
                            for (const auto& v : e.outputs) {
                                os << "\"" << ::FmtEscaped(v.first) << "\"=" << FMT_M(v.second) << ",";
                            }
                            os << ") = asm! \"";
                            os << ::FmtEscaped(e.tpl);
                            os << "\"(";
                            for (const auto& v : e.inputs) {
                                os << "\"" << ::FmtEscaped(v.first) << "\"=" << FMT_M(v.second) << ",";
                            }
                            os << " : ";
                            for (const auto& v : e.clobbers) {
                                os << "\"" << v << "\",";
                            }
                            os << ")";
                            for (const auto& v : e.flags) {
                                os << " \"" << v << "\"";
                            }
                            os << ";\n";
                        }
                        TU_ARMA(Asm2, e) {
                            os << "asm2!(";
                            for (const auto& l : e.lines) {
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
                                        os << "reg " << v.dir << " " << v.spec;
                                        if (v.input) {
                                            os << FMT_M(*v.input);
                                        } else {
                                            os << "_";
                                        }
                                        os << " => ";
                                        if (v.output) {
                                            os << FMT_M(*v.output);
                                        } else {
                                            os << "_";
                                        }
                                    }
                                    TU_ARMA(Label, v) {
                                        os << "label bb" << v;
                                    }
                            }
                            }
                            if (e.options.any()) {
                                e.options.fmt(os);
                            }
                            os << ")";
                        }
                        TU_ARMA(SetDropFlag, e) {
                            os << "df$" << e.idx << " = ";
                            if (e.other == ~0u) {
                                os << e.newVal;
                            } else if (!e.newVal) {
                                os << "df$" << e.other;
                            } else {
                                os << "! df$" << e.other;
                            }
                            os << ";\n";
                        }
                        TU_ARMA(SaveDropFlag, e) {
                            os << "SaveDropFlag(" << FMT_M(e.slot) << " BIT " << e.bitIndex << " = df$" << e.idx << ")";
                        }
                        TU_ARMA(LoadDropFlag, e) {
                            os << "LoadDropFlag(df$" << e.idx << " = " << FMT_M(e.slot) << " BIT " << e.bitIndex << ")";
                        }
                        TU_ARMA(ScopeEnd, e) {
                            os << "// Scope End: ";
                            for (auto idx : e.slots) {
                                os << "_$" << idx << ",";
                            }
                            os << "\n";
                        }
                    }
                }

                os << indent();
                auto fmtUnwind = [this](const MIRUnwindAction& action) {
                    switch (action.tag()) {
                        case MIRUnwindAction::TAG_Continue: {
                            auto& ue = action.as_Continue();
                            (void)ue;
                            os << "continue";
                            break;
                        }
                        case MIRUnwindAction::TAG_Cleanup: {
                            auto& ue = action.as_Cleanup();
                            os << "cleanup bb" << ue;
                            break;
                        }
                        case MIRUnwindAction::TAG_Terminate: {
                            auto& ue = action.as_Terminate();
                            (void)ue;
                            os << "terminate";
                            break;
                        }
                        case MIRUnwindAction::TAG_Unreachable: {
                            auto& ue = action.as_Unreachable();
                            (void)ue;
                            os << "unreachable";
                            break;
                        }
                    }
                };
                switch (block.terminator.tag()) {
                    case MIRTerminator::TAG_Incomplete: {
                        auto& e = block.terminator.as_Incomplete();
                        (void)e;
                        os << "INVALID;\n";
                        break;
                    }
                    case MIRTerminator::TAG_Return: {
                        auto& e = block.terminator.as_Return();
                        (void)e;
                        os << "return;\n";
                        break;
                    }
                    case MIRTerminator::TAG_UnwindResume: {
                        auto& e = block.terminator.as_UnwindResume();
                        (void)e;
                        os << "unwind resume;\n";
                        break;
                    }
                    case MIRTerminator::TAG_UnwindTerminate: {
                        auto& e = block.terminator.as_UnwindTerminate();
                        (void)e;
                        os << "unwind terminate;\n";
                        break;
                    }
                    case MIRTerminator::TAG_Unreachable: {
                        auto& e = block.terminator.as_Unreachable();
                        (void)e;
                        os << "unreachable;\n";
                        break;
                    }
                    case MIRTerminator::TAG_Goto: {
                        auto& e = block.terminator.as_Goto();
                        os << "goto bb" << e << ";\n";
                        break;
                    }
                    case MIRTerminator::TAG_If: {
                        auto& e = block.terminator.as_If();
                        os << "if " << FMT_M(e.cond) << " { goto bb" << e.bbTrue << "; } else { goto bb" << e.bbFalse << "; }\n";
                        break;
                    }
                    case MIRTerminator::TAG_Switch: {
                        auto& e = block.terminator.as_Switch();
                        os << "switch " << FMT_M(e.val) << " {"; for (unsigned int j = 0; j < e.targets.size(); j++) os << j << " => bb" << e.targets[j] << ", "; os << "}\n";
                        break;
                    }
                    case MIRTerminator::TAG_SwitchValue: {
                        auto& e = block.terminator.as_SwitchValue();
                        os << "switch " << FMT_M(e.val) << " {"; switch (e.values.tag()) {
                            case MIRSwitchValues::TAG_Unsigned: {
                                auto& ve = e.values.as_Unsigned();
                                for (unsigned int j = 0; j < e.targets.size(); j++) os << ve[j] << " => bb" << e.targets[j] << ", ";
                                break;
                            }
                            case MIRSwitchValues::TAG_Signed: {
                                auto& ve = e.values.as_Signed();
                                for (unsigned int j = 0; j < e.targets.size(); j++) os << (ve[j] >= 0 ? "+" : "") << ve[j] << " => bb" << e.targets[j] << ", ";
                                break;
                            }
                            case MIRSwitchValues::TAG_String: {
                                auto& ve = e.values.as_String();
                                for (unsigned int j = 0; j < e.targets.size(); j++) os << "\"" << FmtEscaped(ve[j]) << "\" => bb" << e.targets[j] << ", ";
                                break;
                            }
                            case MIRSwitchValues::TAG_ByteString: {
                                auto& ve = e.values.as_ByteString();
                                for (unsigned int j = 0; j < e.targets.size(); j++) {
                                    os << "b\"";
                                    for (size_t i = 0; i < ve[j].size(); i++) {
                                        auto b = ve[j][i];
                                        switch (b) {
                                            case '\\':
                                                os << "\\\\";
                                                break;
                                            case '\"':
                                                os << "\\\"";
                                                break;
                                            default:
                                                if (' ' <= b && b < 0x7f) {
                                                    os << char(ve[j][i]);
                                                } else {
                                                    os << "\\x";
                                                    os << "0123456789ABCDEF"[b >> 4];
                                                    os << "0123456789ABCDEF"[b & 15];
                                                }
                                                break;
                                        }
                                    }
                                    os << "\" => bb" << e.targets[j] << ", ";
                                }
                                break;
                            }
                        } os
                        << "_ => bb" << e.defTarget << "}\n";
                        break;
                    }
                    case MIRTerminator::TAG_Drop: {
                        auto& e = block.terminator.as_Drop();
                        os << "drop(" << FMT_M(e.slot); if (e.kind == MIRDropKind::SHALLOW) os << " SHALLOW"; if (e.flagIdx != ~0u) os << " IF df$" << e.flagIdx; os << ") goto bb" << e.target << " unwind "; fmtUnwind(e.unwind); os << "\n";
                        break;
                    }
                    case MIRTerminator::TAG_Call: {
                        auto& e = block.terminator.as_Call();
                        os << FMT_M(e.retVal) << " = "; switch (e.fcn.tag()) {
                            case MIRCallTarget::TAG_Value: {
                                auto& e2 = e.fcn.as_Value();
                                os << "(" << FMT_M(e2) << ")";
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
                        } os << "( "; for (const auto& arg : e.args) os << FMT_M(arg) << ", "; os << ") goto bb" << e.retBlock << " unwind "; fmtUnwind(e.unwind); os << "\n";
                        break;
                    }
                    case MIRTerminator::TAG_TailCall: {
                        auto& e = block.terminator.as_TailCall();
                        os << "tailcall "; switch (e.fcn.tag()) {
                            case MIRCallTarget::TAG_Value: {
                                auto& e2 = e.fcn.as_Value();
                                os << "(" << FMT_M(e2) << ")";
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
                        } os << "( "; for (const auto& arg : e.args) os << FMT_M(arg) << ", "; os << ")\n";
                        break;
                    }
                    case MIRTerminator::TAG_Asm2: {
                        auto& e = block.terminator.as_Asm2();
                        os << "asm2!("; for (const auto& l : e.lines) l.fmt(os); os << ") -> "; if (e.retBlock != ~0u) os << "bb" << e.retBlock << ", "; for (const auto& p : e.params) if (const auto* target = p.opt_Label()) os << "bb" << *target << ", "; os << "\n";
                        break;
                    }
                }
                decIndent();
                os << indent() << "}\n";

                os.flush();
            }
#undef FMT
        }

        void fmtVal(::std::ostream& os, const MIRLValue& lval) {
            os << lval;
        }

        void fmtVal(::std::ostream& os, const MIRConstant& e) {
            os << e;
        }

        void fmtVal(::std::ostream& os, const MIRParam& param) {
            switch (param.tag()) {
                case MIRParam::TAG_LValue: {
                    auto& e = param.as_LValue();
                    fmtVal(os, e);
                    break;
                }
                case MIRParam::TAG_Borrow: {
                    auto& e = param.as_Borrow();
                    os << "&"; switch (e.type) {
                        case HIRBorrowType::Shared:
                            break;
                        case HIRBorrowType::Unique:
                            os << "mut ";
                            break;
                        case HIRBorrowType::Owned:
                            os << "move ";
                            break;
                    } os << "(";
                    fmtVal(os, e.val);
                    os << ")";
                    break;
                }
                case MIRParam::TAG_Constant: {
                    auto& e = param.as_Constant();
                    fmtVal(os, e);
                    break;
                }
            }
        }

        void fmtVal(::std::ostream& os, const MIRRValue& rval) {
            switch (rval.tag()) {
                case MIRRValue::TAG_Use: {
                    auto& e = rval.as_Use();
                    fmtVal(os, e);
                    break;
                }
                case MIRRValue::TAG_Constant: {
                    auto& e = rval.as_Constant();
                    fmtVal(os, e);
                    break;
                }
                case MIRRValue::TAG_SizedArray: {
                    auto& e = rval.as_SizedArray();
                    os << "["; fmtVal(os, e.val); os << ";" << e.count << "]";
                    break;
                }
                case MIRRValue::TAG_Borrow: {
                    auto& e = rval.as_Borrow();
                    os << "&";
                    switch (e.type) {
                        case HIRBorrowType::Shared:
                            break;
                        case HIRBorrowType::Unique:
                            os << "mut ";
                            break;
                        case HIRBorrowType::Owned:
                            os << "move ";
                            break;
                    } os
                    << "(";
                    fmtVal(os, e.val);
                    os << ")";
                    break;
                }
                case MIRRValue::TAG_Cast: {
                    auto& e = rval.as_Cast();
                    os << "("; fmtVal(os, e.val); os << ") as " << e.type;
                    break;
                }
                case MIRRValue::TAG_BinOp: {
                    auto& e = rval.as_BinOp();
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

                        case MIRBinOp::BIT_SHR:
                            os << "BIT_SHR";
                            break;
                        case MIRBinOp::BIT_SHL:
                            os << "BIT_SHL";
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
                    } os << "(";
                    fmtVal(os, e.valL);
                    os << ", ";
                    fmtVal(os, e.valR);
                    os << ")";
                    break;
                }
                case MIRRValue::TAG_UniOp: {
                    auto& e = rval.as_UniOp();
                    switch (e.op) {
                        case MIRUniOp::INV:
                            os << "INV";
                            break;
                        case MIRUniOp::NEG:
                            os << "NEG";
                            break;
                    } os << "(";
                    fmtVal(os, e.val);
                    os << ")";
                    break;
                }
                case MIRRValue::TAG_DstMeta: {
                    auto& e = rval.as_DstMeta();
                    os << "META("; fmtVal(os, e.val); os << ")";
                    break;
                }
                case MIRRValue::TAG_DstPtr: {
                    auto& e = rval.as_DstPtr();
                    os << "PTR("; fmtVal(os, e.val); os << ")";
                    break;
                }
                case MIRRValue::TAG_MakeDst: {
                    auto& e = rval.as_MakeDst();
                    os << "DST("; fmtVal(os, e.ptrVal); os << ", "; fmtVal(os, e.metaVal); os << ")";
                    break;
                }
                case MIRRValue::TAG_Tuple: {
                    auto& e = rval.as_Tuple();
                    os << "("; for (const auto& v : e.vals) {
                        fmtVal(os, v);
                        os << ", ";
                    } os << ")";
                    break;
                }
                case MIRRValue::TAG_Array: {
                    auto& e = rval.as_Array();
                    os << "["; for (const auto& v : e.vals) {
                        fmtVal(os, v);
                        os << ", ";
                    } os << "]";
                    break;
                }
                case MIRRValue::TAG_UnionVariant: {
                    auto& e = rval.as_UnionVariant();
                    os << e.path << " #" << e.index << " ("; fmtVal(os, e.val); os << ")";
                    break;
                }
                case MIRRValue::TAG_EnumVariant: {
                    auto& e = rval.as_EnumVariant();
                    os << e.path << " #" << e.index << " { "; for (const auto& v : e.vals) {
                        fmtVal(os, v);
                        os << ", ";
                    } os << "}";
                    break;
                }
                case MIRRValue::TAG_Struct: {
                    auto& e = rval.as_Struct();
                    os << e.path << " { "; for (const auto& v : e.vals) {
                        fmtVal(os, v);
                        os << ", ";
                    } os << "}";
                    break;
                }
            }
        }

    private:
        RepeatLitStr indent() const {
            return RepeatLitStr{"   ", static_cast<int>(indentLevel)};
        }

        void incIndent() {
            indentLevel++;
        }

        void decIndent() {
            indentLevel--;
        }
    };

    void dumpMir(::std::ostream& os, unsigned int il, const MIRFunction& fcn) {
        MirDumper md{os, il};
        md.dumpMir(fcn);
    }

    class TreeVisitor: public HIRVisitor {
        ::std::ostream& os;
        unsigned int indentLevel;
        bool shortItemName = false;

    public:
        TreeVisitor(HIRTypeInterner& types, ::std::ostream& os)
            : HIRVisitor(nullptr, types)
            , os(os)
            , indentLevel(0)
        {
        }

        void visitTypeImpl(HIRTypeImpl& impl) override {
            shortItemName = true;

            os << indent() << "impl" << impl.params.fmtArgs() << " " << impl.type << "\n";
            if (!impl.params.bounds.empty()) {
                os << indent() << " " << impl.params.fmtBounds() << "\n";
            }
            os << indent() << "{\n";
            incIndent();
            HIRVisitor::visitTypeImpl(impl);
            decIndent();
            os << indent() << "}\n";

            shortItemName = false;
        }

        virtual void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            shortItemName = true;

            os << indent() << "impl" << impl.params.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.type << "\n";
            if (!impl.params.bounds.empty()) {
                os << indent() << " " << impl.params.fmtBounds() << "\n";
            }
            os << indent() << "{\n";
            incIndent();
            HIRVisitor::visitTraitImpl(traitPath, impl);
            decIndent();
            os << indent() << "}\n";

            shortItemName = false;
        }

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override {
            shortItemName = true;

            os << indent() << "impl" << impl.params.fmtArgs() << " " << (impl.isPositive ? "" : "!") << traitPath << impl.traitArgs << " for " << impl.type << "\n";
            if (!impl.params.bounds.empty()) {
                os << indent() << " " << impl.params.fmtBounds() << "\n";
            }
            os << indent() << "{ }\n";

            shortItemName = false;
        }

        // - Type Items
        void visitTrait(HIRItemPath p, HIRTrait& item) override {
            shortItemName = true;

            os << indent() << "trait " << p << item.params.fmtArgs() << "\n";
            if (!item.params.bounds.empty()) {
                os << indent() << " " << item.params.fmtBounds() << "\n";
            }
            os << indent() << "{\n";
            incIndent();
            HIRVisitor::visitTrait(p, item);
            decIndent();
            os << indent() << "}\n";

            shortItemName = false;
        }

        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            os << indent();
            if (item.isConst) {
                os << "const ";
            }
            if (item.unsafe) {
                os << "unsafe ";
            }
            if (item.abi != ABI_RUST) {
                os << "extern \"" << item.abi << "\" ";
            }
            os << "fn ";
            if (shortItemName) {
                os << p.getName();
            } else {
                os << p;
            }
            os << item.params.fmtArgs() << "(";
            for (unsigned int i = 0; i < item.args.size(); i++) {
                if (i == 0 && item.args[i].first.bindings.size() > 0 && item.args[i].first.bindings[0].name == "self") {
                    os << "self=";
                }
                os << "arg$" << i << ": " << item.args[i].second << ", ";
            }
            os << ") -> " << item.returnType << "\n";
            if (!item.params.bounds.empty()) {
                os << indent() << " " << item.params.fmtBounds() << "\n";
            }

            if (item.code) {
                os << indent() << "{\n";
                incIndent();
                dumpMir(os, indentLevel, item.code.getMirOrError(Span()));
                decIndent();
                os << indent() << "}\n";
            } else {
                os << indent() << "  ;\n";
            }
        }

        void visitConstant(HIRItemPath p, HIRConstant& item) override {
            os << indent();
            os << "const ";
            if (shortItemName) {
                os << p.getName();
            } else {
                os << p;
            }
            os << ": " << item.type;
            if (item.value) {
                incIndent();
                os << " = {\n";
                incIndent();
                dumpMir(os, indentLevel, item.value.getMirOrError(Span()));
                decIndent();
                os << indent() << "} /* = " << item.valueRes << "*/;\n";
                decIndent();
            } else {
                os << ";\n";
            }
        }

        void visitStatic(HIRItemPath p, HIRStatic& item) override {
            os << indent();
            os << "static ";
            if (shortItemName) {
                os << p.getName();
            } else {
                os << p;
            }
            os << ": " << item.type;
            if (item.value) {
                incIndent();
                os << " = {\n";
                incIndent();
                dumpMir(os, indentLevel, item.value.getMirOrError(Span()));
                decIndent();
                os << indent() << "} /* = " << item.valueRes << "*/;\n";
                decIndent();
            } else {
                os << ";\n";
            }
        }

    private:
        RepeatLitStr indent() const {
            return RepeatLitStr{"   ", static_cast<int>(indentLevel)};
        }

        void incIndent() {
            indentLevel++;
        }

        void decIndent() {
            indentLevel--;
        }
    };
}

void MIRDump(::std::ostream& sink, const HIRCrate& crate) {
    TreeVisitor tv{crate.types, sink};

    tv.visitCrate(const_cast<HIRCrate&>(crate));
}

void MIRDumpFcn(::std::ostream& sink, const MIRFunction& fcn, unsigned int il) {
    MirDumper md{sink, il};
    md.dumpMir(fcn);
}
