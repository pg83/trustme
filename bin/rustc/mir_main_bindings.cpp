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
                    TU_MATCHA((action), (ue), (Continue, os << "continue";), (Cleanup, os << "cleanup bb" << ue;), (Terminate, os << "terminate";), (Unreachable, os << "unreachable";))
                };
                TU_MATCHA(
                    (block.terminator),
                    (e),
                    (Incomplete, os << "INVALID;\n";),
                    (Return, os << "return;\n";),
                    (UnwindResume, os << "unwind resume;\n";),
                    (UnwindTerminate, os << "unwind terminate;\n";),
                    (Unreachable, os << "unreachable;\n";),
                    (Goto, os << "goto bb" << e << ";\n";),
                    (If, os << "if " << FMT_M(e.cond) << " { goto bb" << e.bbTrue << "; } else { goto bb" << e.bbFalse << "; }\n";),
                    (Switch, os << "switch " << FMT_M(e.val) << " {"; for (unsigned int j = 0; j < e.targets.size(); j++) os << j << " => bb" << e.targets[j] << ", "; os << "}\n";),
                    (SwitchValue, os << "switch " << FMT_M(e.val) << " {"; TU_MATCHA(
                                                                               (e.values),
                                                                               (ve),
                                                                               (Unsigned, for (unsigned int j = 0; j < e.targets.size(); j++) os << ve[j] << " => bb" << e.targets[j] << ", ";),
                                                                               (Signed, for (unsigned int j = 0; j < e.targets.size(); j++) os << (ve[j] >= 0 ? "+" : "") << ve[j] << " => bb" << e.targets[j] << ", ";),
                                                                               (String, for (unsigned int j = 0; j < e.targets.size(); j++) os << "\"" << FmtEscaped(ve[j]) << "\" => bb" << e.targets[j] << ", ";),
                                                                               (ByteString,
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
                                                                                })
                                                                           ) os
                                                                           << "_ => bb" << e.defTarget << "}\n";),
                    (Drop, os << "drop(" << FMT_M(e.slot); if (e.kind == MIRDropKind::SHALLOW) os << " SHALLOW"; if (e.flagIdx != ~0u) os << " IF df$" << e.flagIdx; os << ") goto bb" << e.target << " unwind "; fmtUnwind(e.unwind); os << "\n";),
                    (Call, os << FMT_M(e.retVal) << " = "; TU_MATCHA((e.fcn), (e2), (Value, os << "(" << FMT_M(e2) << ")";), (Path, os << e2;), (Intrinsic, os << "\"" << e2.name << "\"::" << e2.params;)) os << "( "; for (const auto& arg : e.args) os << FMT_M(arg) << ", "; os << ") goto bb" << e.retBlock << " unwind "; fmtUnwind(e.unwind); os << "\n";),
                    (TailCall, os << "tailcall "; TU_MATCHA((e.fcn), (e2), (Value, os << "(" << FMT_M(e2) << ")";), (Path, os << e2;), (Intrinsic, os << "\"" << e2.name << "\"::" << e2.params;)) os << "( "; for (const auto& arg : e.args) os << FMT_M(arg) << ", "; os << ")\n";),
                    (Asm2, os << "asm2!("; for (const auto& l : e.lines) l.fmt(os); os << ") -> "; if (e.retBlock != ~0u) os << "bb" << e.retBlock << ", "; for (const auto& p : e.params) if (const auto* target = p.opt_Label()) os << "bb" << *target << ", "; os << "\n";)
                )
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
            TU_MATCHA(
                (param),
                (e),
                (LValue, fmtVal(os, e);),
                (
                    Borrow, os << "&"; switch (e.type) {
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
                ),
                (Constant, fmtVal(os, e);)
            )
        }

        void fmtVal(::std::ostream& os, const MIRRValue& rval) {
            TU_MATCHA(
                (rval),
                (e),
                (Use, fmtVal(os, e);),
                (Constant, fmtVal(os, e);),
                (SizedArray, os << "["; fmtVal(os, e.val); os << ";" << e.count << "]";),
                (
                    Borrow, os << "&";
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
                ),
                (Cast, os << "("; fmtVal(os, e.val); os << ") as " << e.type;),
                (BinOp,
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
                 os << ")";),
                (UniOp,
                 switch (e.op) {
                     case MIRUniOp::INV:
                         os << "INV";
                         break;
                     case MIRUniOp::NEG:
                         os << "NEG";
                         break;
                 } os << "(";
                 fmtVal(os, e.val);
                 os << ")";),
                (DstMeta, os << "META("; fmtVal(os, e.val); os << ")";),
                (DstPtr, os << "PTR("; fmtVal(os, e.val); os << ")";),
                (MakeDst, os << "DST("; fmtVal(os, e.ptrVal); os << ", "; fmtVal(os, e.metaVal); os << ")";),
                (
                    Tuple, os << "("; for (const auto& v : e.vals) {
                        fmtVal(os, v);
                        os << ", ";
                    } os << ")";
                ),
                (
                    Array, os << "["; for (const auto& v : e.vals) {
                        fmtVal(os, v);
                        os << ", ";
                    } os << "]";
                ),
                (UnionVariant, os << e.path << " #" << e.index << " ("; fmtVal(os, e.val); os << ")";),
                (
                    EnumVariant, os << e.path << " #" << e.index << " { "; for (const auto& v : e.vals) {
                        fmtVal(os, v);
                        os << ", ";
                    } os << "}";
                ),
                (Struct, os << e.path << " { "; for (const auto& v : e.vals) {
                    fmtVal(os, v);
                    os << ", ";
                } os << "}";)
            )
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
