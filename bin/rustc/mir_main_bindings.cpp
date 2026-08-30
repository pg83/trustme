#include "mir_main_bindings.h"
#include "mir_main_bindings.h"

#include "mir_mir.h"
#include "hir_visitor.h"
#include "mir_operations.h"

using namespace stl;

namespace {
    struct MirDumper {
        ZeroCopyOutput& os;
        unsigned int indentLevel;

        MirDumper(ZeroCopyOutput& os, unsigned int il);

        void dumpMir(const MIRFunction& fcn);

        void fmtVal(ZeroCopyOutput& os, const MIRLValue& lval);

        void fmtVal(ZeroCopyOutput& os, const MIRConstant& e);

        void fmtVal(ZeroCopyOutput& os, const MIRParam& param);

        void fmtVal(ZeroCopyOutput& os, const MIRRValue& rval);

        RepeatLitStr indent() const;

        void incIndent();

        void decIndent();
    };

    struct TreeVisitor: public HIRVisitor {
        ZeroCopyOutput& os;
        unsigned int indentLevel;
        bool shortItemName = false;

        TreeVisitor(HIRTypeInterner& types, ZeroCopyOutput& os);

        void visitTypeImpl(HIRTypeImpl& impl) override;

        virtual void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override;

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override;

        void visitTrait(HIRItemPath p, HIRTrait& item) override;

        void visitFunction(HIRItemPath p, HIRFunction& item) override;

        void visitConstant(HIRItemPath p, HIRConstant& item) override;

        void visitStatic(HIRItemPath p, HIRStatic& item) override;

        RepeatLitStr indent() const;

        void incIndent();

        void decIndent();
    };

    void dumpMir(ZeroCopyOutput& os, unsigned int il, const MIRFunction& fcn) {
        MirDumper md{os, il};
        md.dumpMir(fcn);
    }
}

void MIRDump(ZeroCopyOutput& sink, const HIRCrate& crate) {
    TreeVisitor tv{crate.types, sink};

    tv.visitCrate(const_cast<HIRCrate&>(crate));
}

void MIRDumpFcn(ZeroCopyOutput& sink, const MIRFunction& fcn, unsigned int il) {
    MirDumper md{sink, il};
    md.dumpMir(fcn);
}

MirDumper::MirDumper(ZeroCopyOutput& os, unsigned int il)
    : os(os)
    , indentLevel(il)
{
}

auto MirDumper::dumpMir(const MIRFunction& fcn) -> void {
    for (size_t i = 0; i < fcn.locals.length(); i++) {
        os << indent() << StringView("let _$") << i << StringView(": ") << fcn.locals[i] << StringView(";\n");
    }
    for (unsigned int i = 0; i < fcn.dropFlags.length(); i++) {
        os << indent() << StringView("let df$") << i << StringView(" = ") << fcn.dropFlags[i] << StringView(";\n");
    }

#define FMT_M(x) FMT_CB(os, this->fmtVal(os, x);)
    for (unsigned int i = 0; i < fcn.blocks.size(); i++) {
        const auto& block = fcn.blocks[i];

        os << indent() << StringView("bb") << i << StringView(": {\n");
        incIndent();
        for (const auto& stmt : block.statements) {
            os << indent();

            switch (stmt.tag()) {
                case MIRStatement::TAG_Assign: {
                    auto& e = stmt.as_Assign();
                    os << FMT_M(e.dst) << StringView(" = ") << FMT_M(e.src) << StringView(";\n");
                    break;
                }
                case MIRStatement::TAG_Asm: {
                    auto& e = stmt.as_Asm();
                    os << StringView("(");
                    for (const auto& v : e.outputs) {
                        os << StringView("\"") << ::FmtEscaped(v.first) << StringView("\"=") << FMT_M(v.second) << StringView(",");
                    }
                    os << StringView(") = asm! \"");
                    os << ::FmtEscaped(e.tpl);
                    os << StringView("\"(");
                    for (const auto& v : e.inputs) {
                        os << StringView("\"") << ::FmtEscaped(v.first) << StringView("\"=") << FMT_M(v.second) << StringView(",");
                    }
                    os << StringView(" : ");
                    for (const auto& v : e.clobbers) {
                        os << StringView("\"") << v << StringView("\",");
                    }
                    os << StringView(")");
                    for (const auto& v : e.flags) {
                        os << StringView(" \"") << v << StringView("\"");
                    }
                    os << StringView(";\n");
                    break;
                }
                case MIRStatement::TAG_Asm2: {
                    auto& e = stmt.as_Asm2();
                    os << StringView("asm2!(");
                    for (const auto& l : e.lines) {
                        l.fmt(os);
                    }
                    for (const auto& p : e.params) {
                        os << StringView(", ");
                        switch (p.tag()) {
                            case MIRAsmParam::TAG_Const: {
                                auto& v = p.as_Const();
                                os << StringView("const ") << v;
                                break;
                            }
                            case MIRAsmParam::TAG_Sym: {
                                auto& v = p.as_Sym();
                                os << StringView("sym ") << v;
                                break;
                            }
                            case MIRAsmParam::TAG_Reg: {
                                auto& v = p.as_Reg();
                                os << StringView("reg ") << v.dir << StringView(" ") << v.spec;
                                if (v.input) {
                                    os << FMT_M(*v.input);
                                } else {
                                    os << StringView("_");
                                }
                                os << StringView(" => ");
                                if (v.output) {
                                    os << FMT_M(*v.output);
                                } else {
                                    os << StringView("_");
                                }
                                break;
                            }
                            case MIRAsmParam::TAG_Label: {
                                auto& v = p.as_Label();
                                os << StringView("label bb") << v;
                                break;
                            }
                        }
                    }
                    if (e.options.any()) {
                        e.options.fmt(os);
                    }
                    os << StringView(")");
                    break;
                }
                case MIRStatement::TAG_SetDropFlag: {
                    auto& e = stmt.as_SetDropFlag();
                    os << StringView("df$") << e.idx << StringView(" = ");
                    if (e.other == ~0u) {
                        os << e.newVal;
                    } else if (!e.newVal) {
                        os << StringView("df$") << e.other;
                    } else {
                        os << StringView("! df$") << e.other;
                    }
                    os << StringView(";\n");
                    break;
                }
                case MIRStatement::TAG_SaveDropFlag: {
                    auto& e = stmt.as_SaveDropFlag();
                    os << StringView("SaveDropFlag(") << FMT_M(e.slot) << StringView(" BIT ") << e.bitIndex << StringView(" = df$") << e.idx << StringView(")");
                    break;
                }
                case MIRStatement::TAG_LoadDropFlag: {
                    auto& e = stmt.as_LoadDropFlag();
                    os << StringView("LoadDropFlag(df$") << e.idx << StringView(" = ") << FMT_M(e.slot) << StringView(" BIT ") << e.bitIndex << StringView(")");
                    break;
                }
                case MIRStatement::TAG_ScopeEnd: {
                    auto& e = stmt.as_ScopeEnd();
                    os << StringView("// Scope End: ");
                    for (auto idx : e.slots) {
                        os << StringView("_$") << idx << StringView(",");
                    }
                    os << StringView("\n");
                    break;
                }
            }
        }

        os << indent();
        auto fmtUnwind = [this](const MIRUnwindAction& action) {
            switch (action.tag()) {
                case MIRUnwindAction::TAG_Continue: {
                    os << StringView("continue");
                    break;
                }
                case MIRUnwindAction::TAG_Cleanup: {
                    auto& ue = action.as_Cleanup();
                    os << StringView("cleanup bb") << ue;
                    break;
                }
                case MIRUnwindAction::TAG_Terminate: {
                    os << StringView("terminate");
                    break;
                }
                case MIRUnwindAction::TAG_Unreachable: {
                    os << StringView("unreachable");
                    break;
                }
            }
        };
        switch (block.terminator.tag()) {
            case MIRTerminator::TAG_Incomplete: {
                os << StringView("INVALID;\n");
                break;
            }
            case MIRTerminator::TAG_Return: {
                os << StringView("return;\n");
                break;
            }
            case MIRTerminator::TAG_UnwindResume: {
                os << StringView("unwind resume;\n");
                break;
            }
            case MIRTerminator::TAG_UnwindTerminate: {
                os << StringView("unwind terminate;\n");
                break;
            }
            case MIRTerminator::TAG_Unreachable: {
                os << StringView("unreachable;\n");
                break;
            }
            case MIRTerminator::TAG_Goto: {
                auto& e = block.terminator.as_Goto();
                os << StringView("goto bb") << e << StringView(";\n");
                break;
            }
            case MIRTerminator::TAG_If: {
                auto& e = block.terminator.as_If();
                os << StringView("if ") << FMT_M(e.cond) << StringView(" { goto bb") << e.bbTrue << StringView("; } else { goto bb") << e.bbFalse << StringView("; }\n");
                break;
            }
            case MIRTerminator::TAG_Switch: {
                auto& e = block.terminator.as_Switch();
                os << StringView("switch ") << FMT_M(e.val) << StringView(" {");
                for (unsigned int j = 0; j < e.targets.length(); j++) {
                    os << j << StringView(" => bb") << e.targets[j] << StringView(", ");
                }
                os << StringView("}\n");
                break;
            }
            case MIRTerminator::TAG_SwitchValue: {
                auto& e = block.terminator.as_SwitchValue();
                os << StringView("switch ") << FMT_M(e.val) << StringView(" {");
                switch (e.values.tag()) {
                    case MIRSwitchValues::TAG_Unsigned: {
                        auto& ve = e.values.as_Unsigned();
                        for (unsigned int j = 0; j < e.targets.length(); j++) {
                            os << ve[j] << StringView(" => bb") << e.targets[j] << StringView(", ");
                        }
                        break;
                    }
                    case MIRSwitchValues::TAG_Signed: {
                        auto& ve = e.values.as_Signed();
                        for (unsigned int j = 0; j < e.targets.length(); j++) {
                            os << (ve[j] >= 0 ? "+" : "") << ve[j] << StringView(" => bb") << e.targets[j] << StringView(", ");
                        }
                        break;
                    }
                    case MIRSwitchValues::TAG_String: {
                        auto& ve = e.values.as_String();
                        for (unsigned int j = 0; j < e.targets.length(); j++) {
                            os << StringView("\"") << FmtEscaped(ve[j]) << StringView("\" => bb") << e.targets[j] << StringView(", ");
                        }
                        break;
                    }
                    case MIRSwitchValues::TAG_ByteString: {
                        auto& ve = e.values.as_ByteString();
                        for (unsigned int j = 0; j < e.targets.length(); j++) {
                            os << StringView("b\"");
                            for (size_t i = 0; i < ve[j].length(); i++) {
                                auto b = ve[j][i];
                                switch (b) {
                                    case '\\':
                                        os << StringView("\\\\");
                                        break;
                                    case '\"':
                                        os << StringView("\\\"");
                                        break;
                                    default:
                                        if (' ' <= b && b < 0x7f) {
                                            os << char(ve[j][i]);
                                        } else {
                                            os << StringView("\\x");
                                            os << StringView("0123456789ABCDEF")[b >> 4];
                                            os << StringView("0123456789ABCDEF")[b & 15];
                                        }
                                        break;
                                }
                            }
                            os << StringView("\" => bb") << e.targets[j] << StringView(", ");
                        }
                        break;
                    }
                }
                os << StringView("_ => bb") << e.defTarget << StringView("}\n");
                break;
            }
            case MIRTerminator::TAG_Drop: {
                auto& e = block.terminator.as_Drop();
                os << StringView("drop(") << FMT_M(e.slot);
                if (e.kind == MIRDropKind::SHALLOW) {
                    os << StringView(" SHALLOW");
                }
                if (e.flagIdx != ~0u) {
                    os << StringView(" IF df$") << e.flagIdx;
                }
                os << StringView(") goto bb") << e.target << StringView(" unwind ");
                fmtUnwind(e.unwind);
                os << StringView("\n");
                break;
            }
            case MIRTerminator::TAG_Call: {
                auto& e = block.terminator.as_Call();
                os << FMT_M(e.retVal) << StringView(" = ");
                switch (e.fcn.tag()) {
                    case MIRCallTarget::TAG_Value: {
                        auto& e2 = e.fcn.as_Value();
                        os << StringView("(") << FMT_M(e2) << StringView(")");
                        break;
                    }
                    case MIRCallTarget::TAG_Path: {
                        auto& e2 = e.fcn.as_Path();
                        os << e2;
                        break;
                    }
                    case MIRCallTarget::TAG_Intrinsic: {
                        auto& e2 = e.fcn.as_Intrinsic();
                        os << StringView("\"") << e2.name << StringView("\"::") << e2.params;
                        break;
                    }
                }
                os << StringView("( ");
                for (const auto& arg : e.args) {
                    os << FMT_M(arg) << StringView(", ");
                }
                os << StringView(") goto bb") << e.retBlock << StringView(" unwind ");
                fmtUnwind(e.unwind);
                os << StringView("\n");
                break;
            }
            case MIRTerminator::TAG_TailCall: {
                auto& e = block.terminator.as_TailCall();
                os << StringView("tailcall ");
                switch (e.fcn.tag()) {
                    case MIRCallTarget::TAG_Value: {
                        auto& e2 = e.fcn.as_Value();
                        os << StringView("(") << FMT_M(e2) << StringView(")");
                        break;
                    }
                    case MIRCallTarget::TAG_Path: {
                        auto& e2 = e.fcn.as_Path();
                        os << e2;
                        break;
                    }
                    case MIRCallTarget::TAG_Intrinsic: {
                        auto& e2 = e.fcn.as_Intrinsic();
                        os << StringView("\"") << e2.name << StringView("\"::") << e2.params;
                        break;
                    }
                }
                os << StringView("( ");
                for (const auto& arg : e.args) {
                    os << FMT_M(arg) << StringView(", ");
                }
                os << StringView(")\n");
                break;
            }
            case MIRTerminator::TAG_Asm2: {
                auto& e = block.terminator.as_Asm2();
                os << StringView("asm2!(");
                for (const auto& l : e.lines) {
                    l.fmt(os);
                }
                os << StringView(") -> ");
                if (e.retBlock != ~0u) {
                    os << StringView("bb") << e.retBlock << StringView(", ");
                }
                for (const auto& p : e.params) {
                    if (const auto* target = p.opt_Label()) {
                        os << StringView("bb") << *target << StringView(", ");
                    }
                }
                os << StringView("\n");
                break;
            }
        }
        decIndent();
        os << indent() << StringView("}\n");

        os.flush();
    }
#undef FMT
}

auto MirDumper::fmtVal(ZeroCopyOutput& os, const MIRLValue& lval) -> void {
    os << lval;
}

auto MirDumper::fmtVal(ZeroCopyOutput& os, const MIRConstant& e) -> void {
    os << e;
}

auto MirDumper::fmtVal(ZeroCopyOutput& os, const MIRParam& param) -> void {
    switch (param.tag()) {
        case MIRParam::TAG_LValue: {
            auto& e = param.as_LValue();
            fmtVal(os, e);
            break;
        }
        case MIRParam::TAG_Borrow: {
            auto& e = param.as_Borrow();
            os << StringView("&");
            switch (e.type) {
                case HIRBorrowType::Shared:
                    break;
                case HIRBorrowType::Unique:
                    os << StringView("mut ");
                    break;
                case HIRBorrowType::Owned:
                    os << StringView("move ");
                    break;
            }
            os << StringView("(");
            fmtVal(os, e.val);
            os << StringView(")");
            break;
        }
        case MIRParam::TAG_Constant: {
            auto& e = param.as_Constant();
            fmtVal(os, e);
            break;
        }
    }
}

auto MirDumper::fmtVal(ZeroCopyOutput& os, const MIRRValue& rval) -> void {
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
            os << StringView("[");
            fmtVal(os, e.val);
            os << StringView(";") << e.count << StringView("]");
            break;
        }
        case MIRRValue::TAG_Borrow: {
            auto& e = rval.as_Borrow();
            os << StringView("&");
            switch (e.type) {
                case HIRBorrowType::Shared:
                    break;
                case HIRBorrowType::Unique:
                    os << StringView("mut ");
                    break;
                case HIRBorrowType::Owned:
                    os << StringView("move ");
                    break;
            }
            os << StringView("(");
            fmtVal(os, e.val);
            os << StringView(")");
            break;
        }
        case MIRRValue::TAG_Cast: {
            auto& e = rval.as_Cast();
            os << StringView("(");
            fmtVal(os, e.val);
            os << StringView(") as ") << e.type;
            break;
        }
        case MIRRValue::TAG_BinOp: {
            auto& e = rval.as_BinOp();
            switch (e.op) {
                case MIRBinOp::ADD:
                    os << StringView("ADD");
                    break;
                case MIRBinOp::SUB:
                    os << StringView("SUB");
                    break;
                case MIRBinOp::MUL:
                    os << StringView("MUL");
                    break;
                case MIRBinOp::DIV:
                    os << StringView("DIV");
                    break;
                case MIRBinOp::MOD:
                    os << StringView("MOD");
                    break;
                case MIRBinOp::ADD_OV:
                    os << StringView("ADD_OV");
                    break;
                case MIRBinOp::SUB_OV:
                    os << StringView("SUB_OV");
                    break;
                case MIRBinOp::MUL_OV:
                    os << StringView("MUL_OV");
                    break;
                case MIRBinOp::DIV_OV:
                    os << StringView("DIV_OV");
                    break;

                case MIRBinOp::BIT_OR:
                    os << StringView("BIT_OR");
                    break;
                case MIRBinOp::BIT_AND:
                    os << StringView("BIT_AND");
                    break;
                case MIRBinOp::BIT_XOR:
                    os << StringView("BIT_XOR");
                    break;

                case MIRBinOp::BIT_SHR:
                    os << StringView("BIT_SHR");
                    break;
                case MIRBinOp::BIT_SHL:
                    os << StringView("BIT_SHL");
                    break;

                case MIRBinOp::EQ:
                    os << StringView("EQ");
                    break;
                case MIRBinOp::NE:
                    os << StringView("NE");
                    break;
                case MIRBinOp::GT:
                    os << StringView("GT");
                    break;
                case MIRBinOp::GE:
                    os << StringView("GE");
                    break;
                case MIRBinOp::LT:
                    os << StringView("LT");
                    break;
                case MIRBinOp::LE:
                    os << StringView("LE");
                    break;
            }
            os << StringView("(");
            fmtVal(os, e.valL);
            os << StringView(", ");
            fmtVal(os, e.valR);
            os << StringView(")");
            break;
        }
        case MIRRValue::TAG_UniOp: {
            auto& e = rval.as_UniOp();
            switch (e.op) {
                case MIRUniOp::INV:
                    os << StringView("INV");
                    break;
                case MIRUniOp::NEG:
                    os << StringView("NEG");
                    break;
            }
            os << StringView("(");
            fmtVal(os, e.val);
            os << StringView(")");
            break;
        }
        case MIRRValue::TAG_DstMeta: {
            auto& e = rval.as_DstMeta();
            os << StringView("META(");
            fmtVal(os, e.val);
            os << StringView(")");
            break;
        }
        case MIRRValue::TAG_DstPtr: {
            auto& e = rval.as_DstPtr();
            os << StringView("PTR(");
            fmtVal(os, e.val);
            os << StringView(")");
            break;
        }
        case MIRRValue::TAG_MakeDst: {
            auto& e = rval.as_MakeDst();
            os << StringView("DST(");
            fmtVal(os, e.ptrVal);
            os << StringView(", ");
            fmtVal(os, e.metaVal);
            os << StringView(")");
            break;
        }
        case MIRRValue::TAG_Tuple: {
            auto& e = rval.as_Tuple();
            os << StringView("(");
            for (const auto& v : e.vals) {
                fmtVal(os, v);
                os << StringView(", ");
            }
            os << StringView(")");
            break;
        }
        case MIRRValue::TAG_Array: {
            auto& e = rval.as_Array();
            os << StringView("[");
            for (const auto& v : e.vals) {
                fmtVal(os, v);
                os << StringView(", ");
            }
            os << StringView("]");
            break;
        }
        case MIRRValue::TAG_UnionVariant: {
            auto& e = rval.as_UnionVariant();
            os << e.path << StringView(" #") << e.index << StringView(" (");
            fmtVal(os, e.val);
            os << StringView(")");
            break;
        }
        case MIRRValue::TAG_EnumVariant: {
            auto& e = rval.as_EnumVariant();
            os << e.path << StringView(" #") << e.index << StringView(" { ");
            for (const auto& v : e.vals) {
                fmtVal(os, v);
                os << StringView(", ");
            }
            os << StringView("}");
            break;
        }
        case MIRRValue::TAG_Struct: {
            auto& e = rval.as_Struct();
            os << e.path << StringView(" { ");
            for (const auto& v : e.vals) {
                fmtVal(os, v);
                os << StringView(", ");
            }
            os << StringView("}");
            break;
        }
    }
}

auto MirDumper::indent() const -> RepeatLitStr {
    return RepeatLitStr{"   ", static_cast<int>(indentLevel)};
}

auto MirDumper::incIndent() -> void {
    indentLevel++;
}

auto MirDumper::decIndent() -> void {
    indentLevel--;
}

TreeVisitor::TreeVisitor(HIRTypeInterner& types, ZeroCopyOutput& os)
    : HIRVisitor(nullptr, types)
    , os(os)
    , indentLevel(0)
{
}

auto TreeVisitor::visitTypeImpl(HIRTypeImpl& impl) -> void {
    shortItemName = true;

    os << indent() << StringView("impl") << impl.params.fmtArgs() << StringView(" ") << impl.type << StringView("\n");
    if (!impl.params.bounds.empty()) {
        os << indent() << StringView(" ") << impl.params.fmtBounds() << StringView("\n");
    }
    os << indent() << StringView("{\n");
    incIndent();
    HIRVisitor::visitTypeImpl(impl);
    decIndent();
    os << indent() << StringView("}\n");

    shortItemName = false;
}

auto TreeVisitor::visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) -> void {
    shortItemName = true;

    os << indent() << StringView("impl") << impl.params.fmtArgs() << StringView(" ") << traitPath << impl.traitArgs << StringView(" for ") << impl.type << StringView("\n");
    if (!impl.params.bounds.empty()) {
        os << indent() << StringView(" ") << impl.params.fmtBounds() << StringView("\n");
    }
    os << indent() << StringView("{\n");
    incIndent();
    HIRVisitor::visitTraitImpl(traitPath, impl);
    decIndent();
    os << indent() << StringView("}\n");

    shortItemName = false;
}

auto TreeVisitor::visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) -> void {
    shortItemName = true;

    os << indent() << StringView("impl") << impl.params.fmtArgs() << StringView(" ") << (impl.isPositive ? "" : "!") << traitPath << impl.traitArgs << StringView(" for ") << impl.type << StringView("\n");
    if (!impl.params.bounds.empty()) {
        os << indent() << StringView(" ") << impl.params.fmtBounds() << StringView("\n");
    }
    os << indent() << StringView("{ }\n");

    shortItemName = false;
}

auto TreeVisitor::visitTrait(HIRItemPath p, HIRTrait& item) -> void {
    shortItemName = true;

    os << indent() << StringView("trait ") << p << item.params.fmtArgs() << StringView("\n");
    if (!item.params.bounds.empty()) {
        os << indent() << StringView(" ") << item.params.fmtBounds() << StringView("\n");
    }
    os << indent() << StringView("{\n");
    incIndent();
    HIRVisitor::visitTrait(p, item);
    decIndent();
    os << indent() << StringView("}\n");

    shortItemName = false;
}

auto TreeVisitor::visitFunction(HIRItemPath p, HIRFunction& item) -> void {
    os << indent();
    if (item.isConst) {
        os << StringView("const ");
    }
    if (item.unsafe) {
        os << StringView("unsafe ");
    }
    if (item.abi != ABI_RUST) {
        os << StringView("extern \"") << item.abi << StringView("\" ");
    }
    os << StringView("fn ");
    if (shortItemName) {
        os << p.getName();
    } else {
        os << p;
    }
    os << item.params.fmtArgs() << StringView("(");
    for (unsigned int i = 0; i < item.args.size(); i++) {
        if (i == 0 && item.args[i].first.bindings.size() > 0 && item.args[i].first.bindings[0].name == "self") {
            os << StringView("self=");
        }
        os << StringView("arg$") << i << StringView(": ") << item.args[i].second << StringView(", ");
    }
    os << StringView(") -> ") << item.returnType << StringView("\n");
    if (!item.params.bounds.empty()) {
        os << indent() << StringView(" ") << item.params.fmtBounds() << StringView("\n");
    }

    if (item.code) {
        os << indent() << StringView("{\n");
        incIndent();
        dumpMir(os, indentLevel, item.code.getMirOrError(Span()));
        decIndent();
        os << indent() << StringView("}\n");
    } else {
        os << indent() << StringView("  ;\n");
    }
}

auto TreeVisitor::visitConstant(HIRItemPath p, HIRConstant& item) -> void {
    os << indent();
    os << StringView("const ");
    if (shortItemName) {
        os << p.getName();
    } else {
        os << p;
    }
    os << StringView(": ") << item.type;
    if (item.value) {
        incIndent();
        os << StringView(" = {\n");
        incIndent();
        dumpMir(os, indentLevel, item.value.getMirOrError(Span()));
        decIndent();
        os << indent() << StringView("} /* = ") << item.valueRes << StringView("*/;\n");
        decIndent();
    } else {
        os << StringView(";\n");
    }
}

auto TreeVisitor::visitStatic(HIRItemPath p, HIRStatic& item) -> void {
    os << indent();
    os << StringView("static ");
    if (shortItemName) {
        os << p.getName();
    } else {
        os << p;
    }
    os << StringView(": ") << item.type;
    if (item.value) {
        incIndent();
        os << StringView(" = {\n");
        incIndent();
        dumpMir(os, indentLevel, item.value.getMirOrError(Span()));
        decIndent();
        os << indent() << StringView("} /* = ") << item.valueRes << StringView("*/;\n");
        decIndent();
    } else {
        os << StringView(";\n");
    }
}

auto TreeVisitor::indent() const -> RepeatLitStr {
    return RepeatLitStr{"   ", static_cast<int>(indentLevel)};
}

auto TreeVisitor::incIndent() -> void {
    indentLevel++;
}

auto TreeVisitor::decIndent() -> void {
    indentLevel--;
}
