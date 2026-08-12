#include "mir_main_bindings.h"

#include "mir_main_bindings.h"
#include "hir_visitor.h"
#include "mir_mir.h"
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

        void dumpMir(const ::MIR::Function& fcn) {
            for (size_t i = 0; i < fcn.locals.size(); i++) {
                os << indent() << "let _$" << i << ": " << fcn.locals[i] << ";\n";
            }
            for (unsigned int i = 0; i < fcn.dropFlags.size(); i++) {
                os << indent() << "let df$" << i << " = " << fcn.dropFlags[i] << ";\n";
            }

#define FMT_M(x) FMT_CB(os, this->fmtVal(os, x);)
            for (unsigned int i = 0; i < fcn.blocks.size(); i++) {
                const auto& block = fcn.blocks[i];
                //DEBUG("BB" << i);

                os << indent() << "bb" << i << ": {\n";
                incIndent();
                for (const auto& stmt : block.statements) {
                    os << indent();

                    TU_MATCH_HDRA( (stmt), {)
                    TU_ARMA(Assign, e) {
                            //DEBUG("- Assign " << e.dst << " = " << e.src);
                            os << FMT_M(e.dst) << " = " << FMT_M(e.src) << ";\n";
                        }
                        TU_ARMA(Asm, e) {
                            //DEBUG("- Asm");
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
                auto fmtUnwind = [this](const ::MIR::UnwindAction& action) {
                    TU_MATCHA((action), (ue),
                        (Continue, os << "continue";),
                        (Cleanup, os << "cleanup bb" << ue;),
                        (Terminate, os << "terminate";),
                        (Unreachable, os << "unreachable";)
                    )
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
                    (Drop, os << "drop(" << FMT_M(e.slot); if (e.kind == ::MIR::eDropKind::SHALLOW) os << " SHALLOW"; if (e.flagIdx != ~0u) os << " IF df$" << e.flagIdx; os << ") goto bb" << e.target << " unwind "; fmtUnwind(e.unwind); os << "\n";),
                    (Call, os << FMT_M(e.retVal) << " = "; TU_MATCHA((e.fcn), (e2), (Value, os << "(" << FMT_M(e2) << ")";), (Path, os << e2;), (Intrinsic, os << "\"" << e2.name << "\"::" << e2.params;)) os << "( "; for (const auto& arg : e.args) os << FMT_M(arg) << ", "; os << ") goto bb" << e.retBlock << " unwind "; fmtUnwind(e.unwind); os << "\n";)
                )
                decIndent();
                os << indent() << "}\n";

                os.flush();
            }
#undef FMT
        }

        void fmtVal(::std::ostream& os, const ::MIR::LValue& lval) {
            os << lval;
        }

        void fmtVal(::std::ostream& os, const ::MIR::Constant& e) {
            os << e;
        }

        void fmtVal(::std::ostream& os, const ::MIR::Param& param) {
            TU_MATCHA(
                (param),
                (e),
                (LValue, fmtVal(os, e);),
                (
                    Borrow, os << "&"; switch (e.type) {
                        case ::HIR::BorrowType::Shared:
                            break;
                        case ::HIR::BorrowType::Unique:
                            os << "mut ";
                            break;
                        case ::HIR::BorrowType::Owned:
                            os << "move ";
                            break;
                    } os << "(";
                    fmtVal(os, e.val);
                    os << ")";
                ),
                (Constant, fmtVal(os, e);)
            )
        }

        void fmtVal(::std::ostream& os, const ::MIR::RValue& rval) {
            TU_MATCHA(
                (rval),
                (e),
                (Use, fmtVal(os, e);),
                (Constant, fmtVal(os, e);),
                (SizedArray, os << "["; fmtVal(os, e.val); os << ";" << e.count << "]";),
                (
                    Borrow, os << "&";
                    //os << e.region;
                    switch (e.type) {
                        case ::HIR::BorrowType::Shared:
                            break;
                        case ::HIR::BorrowType::Unique:
                            os << "mut ";
                            break;
                        case ::HIR::BorrowType::Owned:
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
                         //case ::MIR::eBinOp::MOD_OV: os << "MOD_OV"; break;

                     case ::MIR::eBinOp::BIT_OR:
                         os << "BIT_OR";
                         break;
                     case ::MIR::eBinOp::BIT_AND:
                         os << "BIT_AND";
                         break;
                     case ::MIR::eBinOp::BIT_XOR:
                         os << "BIT_XOR";
                         break;

                     case ::MIR::eBinOp::BIT_SHR:
                         os << "BIT_SHR";
                         break;
                     case ::MIR::eBinOp::BIT_SHL:
                         os << "BIT_SHL";
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
                 } os << "(";
                 fmtVal(os, e.val_l);
                 os << ", ";
                 fmtVal(os, e.val_r);
                 os << ")";),
                (UniOp,
                 switch (e.op) {
                     case ::MIR::eUniOp::INV:
                         os << "INV";
                         break;
                     case ::MIR::eUniOp::NEG:
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

    void dumpMir(::std::ostream& os, unsigned int il, const ::MIR::Function& fcn) {
        MirDumper md{os, il};
        md.dumpMir(fcn);
    }

    class TreeVisitor: public ::HIR::Visitor {
        ::std::ostream& os;
        unsigned int indentLevel;
        bool shortItemName = false;

    public:
        TreeVisitor(::HIR::TypeInterner& types, ::std::ostream& os)
            : ::HIR::Visitor(nullptr, types)
            , os(os)
            , indentLevel(0)
        {
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            shortItemName = true;

            os << indent() << "impl" << impl.mParams.fmtArgs() << " " << impl.mType << "\n";
            if (!impl.mParams.bounds.empty()) {
                os << indent() << " " << impl.mParams.fmtBounds() << "\n";
            }
            os << indent() << "{\n";
            incIndent();
            ::HIR::Visitor::visit_type_impl(impl);
            decIndent();
            os << indent() << "}\n";

            shortItemName = false;
        }

        virtual void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            shortItemName = true;

            os << indent() << "impl" << impl.mParams.fmtArgs() << " " << trait_path << impl.traitArgs << " for " << impl.mType << "\n";
            if (!impl.mParams.bounds.empty()) {
                os << indent() << " " << impl.mParams.fmtBounds() << "\n";
            }
            os << indent() << "{\n";
            incIndent();
            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
            decIndent();
            os << indent() << "}\n";

            shortItemName = false;
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            shortItemName = true;

            os << indent() << "impl" << impl.mParams.fmtArgs() << " " << (impl.isPositive ? "" : "!") << trait_path << impl.traitArgs << " for " << impl.mType << "\n";
            if (!impl.mParams.bounds.empty()) {
                os << indent() << " " << impl.mParams.fmtBounds() << "\n";
            }
            os << indent() << "{ }\n";

            shortItemName = false;
        }

        // - Type Items
        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            shortItemName = true;

            os << indent() << "trait " << p << item.mParams.fmtArgs() << "\n";
            if (!item.mParams.bounds.empty()) {
                os << indent() << " " << item.mParams.fmtBounds() << "\n";
            }
            os << indent() << "{\n";
            incIndent();
            ::HIR::Visitor::visit_trait(p, item);
            decIndent();
            os << indent() << "}\n";

            shortItemName = false;
        }

        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            os << indent();
            if (item.isConst) {
                os << "const ";
            }
            if (item.unsafe) {
                os << "unsafe ";
            }
            if (item.mAbi != ABI_RUST) {
                os << "extern \"" << item.mAbi << "\" ";
            }
            os << "fn ";
            if (shortItemName) {
                os << p.getName();
            } else {
                os << p;
            }
            os << item.mParams.fmtArgs() << "(";
            for (unsigned int i = 0; i < item.mArgs.size(); i++) {
                if (i == 0 && item.mArgs[i].first.mBindings.size() > 0 && item.mArgs[i].first.mBindings[0].mName == "self") {
                    os << "self=";
                }
                os << "arg$" << i << ": " << item.mArgs[i].second << ", ";
            }
            os << ") -> " << item.returnType << "\n";
            if (!item.mParams.bounds.empty()) {
                os << indent() << " " << item.mParams.fmtBounds() << "\n";
            }

            if (item.mCode) {
                os << indent() << "{\n";
                incIndent();
                dumpMir(os, indentLevel, item.mCode.getMirOrError(Span()));
                decIndent();
                os << indent() << "}\n";
            } else {
                os << indent() << "  ;\n";
            }
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            os << indent();
            os << "const ";
            if (shortItemName) {
                os << p.getName();
            } else {
                os << p;
            }
            os << ": " << item.mType;
            if (item.mValue) {
                incIndent();
                os << " = {\n";
                incIndent();
                dumpMir(os, indentLevel, item.mValue.getMirOrError(Span()));
                decIndent();
                os << indent() << "} /* = " << item.valueRes << "*/;\n";
                decIndent();
            } else {
                os << ";\n";
            }
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            os << indent();
            os << "static ";
            if (shortItemName) {
                os << p.getName();
            } else {
                os << p;
            }
            os << ": " << item.mType;
            if (item.mValue) {
                incIndent();
                os << " = {\n";
                incIndent();
                dumpMir(os, indentLevel, item.mValue.getMirOrError(Span()));
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

void MIRDump(::std::ostream& sink, const ::HIR::Crate& crate) {
    TreeVisitor tv{crate.types, sink};

    tv.visit_crate(const_cast<::HIR::Crate&>(crate));
}

void MIRDumpFcn(::std::ostream& sink, const ::MIR::Function& fcn, unsigned int il) {
    MirDumper md{sink, il};
    md.dumpMir(fcn);
}
