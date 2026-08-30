#include "ast_dump.h"

#include "ast_ast.h"
#include "hir_hir.h" // ABI_RUST - TODO: Move elsewhere?
#include "ast_expr.h"
#include "ast_crate.h"
#include "output_file.h"

#include <limits>
#include <fstream>
#include <string_view>

using namespace stl;

namespace {
    template <typename... T>
    bool isAny(ASTExprNode& node) {
        return (... || (cast<T>(&node) != nullptr));
    }

    struct RustPrinter: public ASTNodeVisitor {
        ZeroCopyOutput& os;
        int indentLevel;
        bool exprRoot;

        RustPrinter(ZeroCopyOutput& os);

        void handleModule(const ASTModule& mod);
        void handleStruct(const ASTStruct& s);
        void handleEnum(const ASTEnum& s);
        void handleTrait(const ASTTrait& s);

        void handleFunction(const ASTVisibility& vis, const RcString& name, const ASTFunction& f);
        void handleStatic(const ASTVisibility& vis, const RcString& name, const ASTStatic& s);

        virtual bool isConst() const override;

        virtual void visit(ASTExprNodeBlock& n) override;

        virtual void visit(ASTExprNodeAsyncBlock& n) override;

        virtual void visit(ASTExprNodeGeneratorBlock& n) override;

        virtual void visit(ASTExprNodeTry& n) override;

        void dumpToken(const Token& t);

        void dumpTokentree(const TokenTree& tt);

        virtual void visit(ASTExprNodeMacro& n) override;

        virtual void visit(ASTExprNodeAsm& n) override;

        virtual void visit(ASTExprNodeAsm2& n) override;

        virtual void visit(ASTExprNodeFlow& n) override;

        virtual void visit(ASTExprNodeLetBinding& n) override;

        virtual void visit(ASTExprNodeAssign& n) override;

        virtual void visit(ASTExprNodeCallPath& n) override;

        virtual void visit(ASTExprNodeCallMethod& n) override;

        virtual void visit(ASTExprNodeCallObject& n) override;

        virtual void visit(ASTExprNodeLoop& n) override;

        virtual void visit(ASTExprNodeFor& n) override;

        void visitIfletConditions(std::vector<ASTIfLetCondition>& conds);

        void visit(ASTExprNodeWhile& n) override;

        virtual void visit(ASTExprNodeMatch& n) override;

        virtual void visit(ASTExprNodeIf& n) override;

        virtual void visit(ASTExprNodeClosure& n) override;

        virtual void visit(ASTExprNodeWildcardPattern& n) override;

        virtual void visit(ASTExprNodeInteger& n) override;

        virtual void visit(ASTExprNodeFloat& n) override;

        virtual void visit(ASTExprNodeBool& n) override;

        virtual void visit(ASTExprNodeString& n) override;

        virtual void visit(ASTExprNodeByteString& n) override;

        virtual void visit(ASTExprNodeCString& n) override;

        virtual void visit(ASTExprNodeSuffixedLiteral& n) override;

        virtual void visit(ASTExprNodeStructLiteral& n) override;

        virtual void visit(ASTExprNodeStructLiteralPattern& n) override;

        virtual void visit(ASTExprNodeArray& n) override;

        virtual void visit(ASTExprNodeTuple& n) override;

        virtual void visit(ASTExprNodeNamedValue& n) override;

        virtual void visit(ASTExprNodeField& n) override;

        virtual void visit(ASTExprNodeIndex& n) override;

        virtual void visit(ASTExprNodeDeref& n) override;

        virtual void visit(ASTExprNodeCast& n) override;

        virtual void visit(ASTExprNodeTypeAnnotation& n) override;

        virtual void visit(ASTExprNodeBinOp& n) override;

        virtual void visit(ASTExprNodeUniOp& n) override;

        virtual void visit(ASTExprNodeMacroDefinition& n) override;

        void parenWrap(ASTExprNode* node);

        template <typename... T>
        void visitWithParensIf(ASTExprNode* node);

        void printAttrs(const ASTAttributeList& attrs);
        void printParams(const ASTGenericParams& params);
        void printBounds(const ASTGenericParams& params);
        void printPatternTuple(const ASTPattern::TuplePat& v, bool isRefutable);
        void printPattern(const ASTPattern& p, bool isRefutable);
        void printType(ASTType* t);

        void incIndent();
        RepeatLitStr indent();
        void decIndent();
    };
}

void RustPrinter::printAttrs(const ASTAttributeList& attrs) {
    for (const auto& a : attrs.items) {
        os << indent() << StringView("#[") << a << StringView("]\n");
    }
}

void RustPrinter::handleModule(const ASTModule& mod) {
    bool needNl = true;

    for (const auto& ip : mod.items) {
        const auto& i = *ip;
        if (!i.data.is_Use()) {
            continue;
        }
        const auto& iData = i.data.as_Use();
        if (iData.entries.empty()) {
            continue;
        }
        os << indent() << i.vis << StringView("use ");
        if (iData.entries.size() > 1) {
            os << StringView("{");
        }
        for (const auto& ent : iData.entries) {
            if (&ent != &iData.entries.front()) {
                os << StringView(", ");
            }
            os << ent.path;
            if (ent.name == "") {
                os << StringView("::*");
            } else if (ent.path.nodes().size() > 0 && ent.name != ent.path.nodes().back().name()) {
                os << StringView(" as ") << ent.name;
            } else {
            }
        }
        if (iData.entries.size() > 1) {
            os << StringView("}");
        }
        os << StringView(";\n");
    }
    needNl = true;

    for (const auto& ip : mod.items) {
        const auto& item = *ip;
        if (!item.data.is_Crate()) {
            continue;
        }
        const auto& e = item.data.as_Crate();

        printAttrs(item.attrs);
        os << indent() << StringView("extern crate \"") << e.name << StringView("\" as ") << item.name << StringView(";\n");
    }

    for (const auto& ip : mod.items) {
        const auto& item = *ip;
        if (!item.data.is_ExternBlock()) {
            continue;
        }
        const auto& e = item.data.as_ExternBlock();

        printAttrs(item.attrs);
        os << indent() << StringView("extern \"") << e.abi() << StringView("\" {}\n");
    }

    for (const auto& ip : mod.items) {
        const auto& item = *ip;
        if (!item.data.is_Module()) {
            continue;
        }
        const auto& e = item.data.as_Module();

        os << StringView("\n");
        os << indent() << item.vis << StringView("mod ") << item.name << StringView("\n");
        os << indent() << StringView("{\n");
        incIndent();
        handleModule(e);
        decIndent();
        os << indent() << StringView("}\n");
        os << StringView("\n");
    }

    for (const auto& ip : mod.items) {
        const auto& item = *ip;
        if (!item.data.is_Type()) {
            continue;
        }
        const auto& e = item.data.as_Type();

        if (needNl) {
            os << StringView("\n");
            needNl = false;
        }
        printAttrs(item.attrs);
        os << indent() << item.vis << StringView("type ") << item.name;
        printParams(e.params());
        os << StringView(" = ") << e.type();
        printBounds(e.params());
        os << StringView(";\n");
    }
    needNl = true;

    for (const auto& ip : mod.items) {
        const auto& item = *ip;
        if (!item.data.is_Struct()) {
            continue;
        }
        const auto& e = item.data.as_Struct();

        os << StringView("\n");
        printAttrs(item.attrs);
        os << indent() << item.vis << StringView("struct ") << item.name;
        handleStruct(e);
    }

    for (const auto& ip : mod.items) {
        const auto& item = *ip;
        if (!item.data.is_Enum()) {
            continue;
        }
        const auto& e = item.data.as_Enum();

        os << StringView("\n");
        printAttrs(item.attrs);
        os << indent() << item.vis << StringView("enum ") << item.name;
        handleEnum(e);
    }

    for (const auto& ip : mod.items) {
        const auto& item = *ip;
        if (!item.data.is_Trait()) {
            continue;
        }
        const auto& e = item.data.as_Trait();

        os << StringView("\n");
        printAttrs(item.attrs);
        os << indent() << item.vis << StringView("trait ") << item.name;
        handleTrait(e);
    }

    for (const auto& ip : mod.items) {
        const auto& item = *ip;
        if (!item.data.is_Static()) {
            continue;
        }
        const auto& e = item.data.as_Static();

        if (needNl) {
            os << StringView("\n");
            needNl = false;
        }
        printAttrs(item.attrs);
        handleStatic(item.vis, item.name, e);
    }

    for (const auto& ip : mod.items) {
        const auto& item = *ip;
        if (!item.data.is_Function()) {
            continue;
        }
        const auto& e = item.data.as_Function();

        os << StringView("\n");
        printAttrs(item.attrs);
        handleFunction(item.vis, item.name, e);
    }

    for (const auto& ip : mod.items) {
        const auto& item = *ip;
        if (!item.data.is_Impl()) {
            continue;
        }
        const auto& i = item.data.as_Impl();

        os << StringView("\n");
        os << indent() << StringView("impl");
        if (i.def().isConst()) {
            os << StringView(" const");
        }
        printParams(i.def().params());
        if (i.def().trait().ent != ASTPath()) {
            os << StringView(" ") << i.def().trait().ent << StringView(" for");
        }
        os << StringView(" ") << i.def().type() << StringView("\n");

        printBounds(i.def().params());
        os << indent() << StringView("{\n");
        incIndent();
        for (const auto& it : i.items()) {
            switch ((*it.data).tag()) {
                case ASTItem::TAG_None: {
                    break;
                }
                case ASTItem::TAG_MacroInv: {
                    // TODO: Dump macro invocations
                    break;
                }
                case ASTItem::TAG_Static: {
                    auto& e = (*it.data).as_Static();
                    handleStatic(it.vis, it.name, e);
                    break;
                }
                case ASTItem::TAG_Type: {
                    auto& e = (*it.data).as_Type();
                    os << indent() << StringView("type ") << it.name << StringView(" = ") << e.type() << StringView(";\n");
                    break;
                }
                case ASTItem::TAG_Function: {
                    auto& e = (*it.data).as_Function();
                    handleFunction(it.vis, it.name, e);
                    break;
                }
                default: {
                    BUG(Span(), StringView("Unexpected item type in impl block - ") << it.data->tagStr());
                }
            }
        }
        decIndent();
        os << indent() << StringView("}\n");
    }
}

void RustPrinter::printParams(const ASTGenericParams& params) {
    if (!params.params.empty()) {
        bool isFirst = true;
        os << StringView("<");
        for (const auto& p : params.params) {
            if (!isFirst) {
                os << StringView(", ");
            }
            {
                auto& tuMatch = p;
                switch (tuMatch.tag()) {
                    case GenericParam::TAG_None: {
                        os << StringView("/*-*/");
                        break;
                    }
                    case GenericParam::TAG_Lifetime: {
                        auto& p = tuMatch.as_Lifetime();
                        os << p;
                        break;
                    }
                    case GenericParam::TAG_Type: {
                        auto& p = tuMatch.as_Type();
                        os << p.attrs();
                        os << p.name();
                        if (!p.getDefault()->isWildcard()) {
                            os << StringView(" = ") << p.getDefault();
                        }
                        break;
                    }
                    case GenericParam::TAG_Value: {
                        auto& p = tuMatch.as_Value();
                        os << p.attrs();
                        os << StringView("const ") << p.name() << StringView(": ") << p.type();
                        break;
                    }
                }
            }
            isFirst = false;
        }
        os << StringView(">");
    }
}

void RustPrinter::printBounds(const ASTGenericParams& params) {
    if (!params.bounds.empty()) {
        incIndent();
        bool isFirst = true;

        for (const auto& b : params.bounds) {
            if (b.is_None()) {
                os << StringView("/*-*/");
                continue;
            }
            if (!isFirst) {
                os << StringView(",\n");
            } else {
                os << indent() << StringView("where\n");
            }
            isFirst = false;

            os << indent();
            switch (b.tag()) {
                case ASTGenericBound::TAG_None: {
                    os << StringView("/*-*/");
                    break;
                }
                case ASTGenericBound::TAG_Lifetime: {
                    auto& ent = b.as_Lifetime();
                    os << ent.test << StringView(": ") << ent.bound;
                    break;
                }
                case ASTGenericBound::TAG_TypeLifetime: {
                    auto& ent = b.as_TypeLifetime();
                    os << ent.type << StringView(": ") << ent.bound;
                    break;
                }
                case ASTGenericBound::TAG_IsTrait: {
                    auto& ent = b.as_IsTrait();
                    os << ent.outerHrbs << ent.type << StringView(": ");
                    if (ent.constness == ASTBoundConstness::Always) {
                        os << StringView("const ");
                    } else if (ent.constness == ASTBoundConstness::Maybe) {
                        os << StringView("[const] ");
                    }
                    os << ent.innerHrbs << ent.trait;
                    break;
                }
                case ASTGenericBound::TAG_MaybeTrait: {
                    auto& ent = b.as_MaybeTrait();
                    os << ent.type << StringView(": ?") << ent.trait;
                    break;
                }
                case ASTGenericBound::TAG_NotTrait: {
                    auto& ent = b.as_NotTrait();
                    os << ent.type << StringView(": !") << ent.trait;
                    break;
                }
                case ASTGenericBound::TAG_Equality: {
                    auto& ent = b.as_Equality();
                    os << ent.type << StringView(": =") << ent.replacement;
                    break;
                }
            }
        }
        os << StringView("\n");

        decIndent();
    }
}

void RustPrinter::printPatternTuple(const ASTPattern::TuplePat& v, bool isRefutable) {
    for (const auto& sp : v.start) {
        printPattern(sp, isRefutable);
        os << StringView(", ");
    }
    if (v.hasWildcard) {
        os << StringView(".., ");
        for (const auto& sp : v.end) {
            printPattern(sp, isRefutable);
            os << StringView(", ");
        }
    }
}

void RustPrinter::printPattern(const ASTPattern& p, bool isRefutable) {
    for (const auto& pb : p.bindings()) {
        if (pb.isMutable) {
            os << StringView("mut ");
        }
        switch (pb.type) {
            case ASTPatternBinding::Type::MOVE:
                break;
            case ASTPatternBinding::Type::REF:
                os << StringView("ref ");
                break;
            case ASTPatternBinding::Type::MUTREF:
                os << StringView("ref mut ");
                break;
        }
        os << pb.name << StringView("/*") << pb.slot << StringView("*/");
        if (!isRefutable && p.bindings().size() == 1 && p.data().is_Any()) {
            return;
        }
        os << StringView(" @ ");
    }
    switch (p.data().tag()) {
        case ASTPattern::Data::TAG_Any: {
            os << StringView("_");
            break;
        }
        case ASTPattern::Data::TAG_Never: {
            os << StringView("!");
            break;
        }
        case ASTPattern::Data::TAG_MaybeBind: {
            auto& v = p.data().as_MaybeBind();
            os << v.name << StringView(" /*?*/");
            break;
        }
        case ASTPattern::Data::TAG_Macro: {
            auto& v = p.data().as_Macro();
            os << *v.inv;
            break;
        }
        case ASTPattern::Data::TAG_Box: {
            {
                const auto& v = p.data().as_Box();
                os << StringView("box ");
                printPattern(*v.sub, isRefutable);
            }
            break;
        }
        case ASTPattern::Data::TAG_Deref: {
            auto& v = p.data().as_Deref();
            {
                os << StringView("deref!(");
                printPattern(*v.sub, isRefutable);
                os << StringView(")");
            }
            break;
        }
        case ASTPattern::Data::TAG_Ref: {
            {
                const auto& v = p.data().as_Ref();
                if (v.mut) {
                    os << StringView("&mut ");
                } else {
                    os << StringView("& ");
                }
                os << StringView("(");
                printPattern(*v.sub, isRefutable);
                os << StringView(")");
            }
            break;
        }
        case ASTPattern::Data::TAG_Guard: {
            const auto& v = p.data().as_Guard();
            os << StringView("(");
            printPattern(*v.sub, isRefutable);
            os << StringView(" if ");
            os << *v.cond;
            os << StringView(")");
            break;
        }
        case ASTPattern::Data::TAG_Value: {
            auto& v = p.data().as_Value();
            os << v.start;
            if (!v.end.is_Invalid()) {
                os << StringView(" ..= ") << v.end;
            }
            break;
        }
        case ASTPattern::Data::TAG_ValueLeftInc: {
            auto& v = p.data().as_ValueLeftInc();
            os << v.start << StringView(" .. ") << v.end;
            break;
        }
        case ASTPattern::Data::TAG_StructTuple: {
            auto& v = p.data().as_StructTuple();
            os << v.path << StringView("(");
            this->printPatternTuple(v.tupPat, isRefutable);
            os << StringView(")");
            break;
        }
        case ASTPattern::Data::TAG_Struct: {
            {
                const auto& v = p.data().as_Struct();
                os << v.path << StringView("{");
                for (const auto& sp : v.subPatterns) {
                    os << sp.name << StringView(": ");
                    printPattern(sp.pat, isRefutable);
                    os << StringView(",");
                }
                if (!v.isExhaustive) {
                    os << StringView("..");
                }
                os << StringView("}");
            }
            break;
        }
        case ASTPattern::Data::TAG_Tuple: {
            auto& v = p.data().as_Tuple();
            os << StringView("(");
            this->printPatternTuple(v, isRefutable);
            os << StringView(")");
            break;
        }
        case ASTPattern::Data::TAG_Slice: {
            auto& v = p.data().as_Slice();
            os << StringView("[");
            for (const auto& sp : v.subPats) {
                printPattern(sp, isRefutable);
                os << StringView(", ");
            }
            os << StringView("]");
            break;
        }
        case ASTPattern::Data::TAG_SplitSlice: {
            auto& v = p.data().as_SplitSlice();
            os << StringView("[");
            bool needsComma = false;
            for (const auto& sp : v.leading) {
                printPattern(sp, isRefutable);
                os << StringView(", ");
            }

            if (v.extraBind.isValid()) {
                const auto& b = v.extraBind;
                if (b.isMutable) {
                    os << StringView("mut ");
                }
                switch (b.type) {
                    case ASTPatternBinding::Type::MOVE:
                        break;
                    case ASTPatternBinding::Type::REF:
                        os << StringView("ref ");
                        break;
                    case ASTPatternBinding::Type::MUTREF:
                        os << StringView("ref mut ");
                        break;
                }
                os << b.name << StringView("/*") << b.slot << StringView("*/");
            }
            os << StringView("..");
            needsComma = true;

            if (v.trailing.size()) {
                if (needsComma) {
                    os << StringView(", ");
                }
                for (const auto& sp : v.trailing) {
                    printPattern(sp, isRefutable);
                    os << StringView(", ");
                }
            }
            os << StringView("]");
            break;
        }
        case ASTPattern::Data::TAG_Or: {
            auto& v = p.data().as_Or();
            os << StringView("(");
            for (const auto& e : v) {
                os << (&e == &v.front() ? "" : " | ");
                printPattern(e, isRefutable);
            }
            os << StringView(")");
            break;
        }
    }
}

void RustPrinter::printType(ASTType* t) {
    os << t;
}

void RustPrinter::handleStruct(const ASTStruct& s) {
    printParams(s.params());

    switch (s.data.tag()) {
        case ASTStructData::TAG_Unit: {
            os << StringView(" /* unit-like */\n");
            printBounds(s.params());
            os << indent() << StringView(";\n");
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& e = s.data.as_Tuple();
            os << StringView("(");
            for (const auto& i : e.ents) {
                os << i.vis << i.type << StringView(", ");
            }
            os << StringView(")\n");
            printBounds(s.params());
            os << indent() << StringView(";\n");
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& e = s.data.as_Struct();
            os << StringView("\n");
            printBounds(s.params());

            os << indent() << StringView("{\n");
            incIndent();
            for (const auto& i : e.ents) {
                os << indent() << i.vis << i.name << StringView(": ") << i.type->printPretty() << StringView(",\n");
            }
            decIndent();
            os << indent() << StringView("}\n");
            break;
        }
    }
    os << StringView("\n");
}

void RustPrinter::handleEnum(const ASTEnum& s) {
    printParams(s.params());
    os << StringView("\n");
    printBounds(s.params());

    os << indent() << StringView("{\n");
    incIndent();
    unsigned int idx = 0;
    for (const auto& i : s.variants()) {
        os << indent() << StringView("/*") << idx << StringView("*/") << i.name;
        switch (i.data.tag()) {
            case ASTEnumVariantData::TAG_Unit: {
                break;
            }
            case ASTEnumVariantData::TAG_Tuple: {
                auto& e = i.data.as_Tuple();
                os << StringView("(");
                for (const auto& t : e.items) {
                    os << t.type->printPretty() << StringView(", ");
                }
                os << StringView(")");
                break;
            }
            case ASTEnumVariantData::TAG_Struct: {
                auto& e = i.data.as_Struct();
                os << StringView("{\n");
                incIndent();
                for (const auto& i : e.fields) {
                    os << indent() << i.name << StringView(": ") << i.type->printPretty() << StringView(",\n");
                }
                decIndent();
                os << indent() << StringView("}");
                break;
            }
        }
        if (i.discriminantValue) {
            os << StringView(" = ") << *i.discriminantValue;
        }
        os << StringView(",\n");
        idx++;
    }
    decIndent();
    os << indent() << StringView("}\n");
    os << StringView("\n");
}

void RustPrinter::handleTrait(const ASTTrait& s) {
    printParams(s.params());
    {
        char c = ':';
        for (const auto& lft : s.lifetimes()) {
            os << StringView(" ") << c << StringView(" ") << lft.ent;
            c = '+';
        }
        for (const auto& t : s.supertraits()) {
            os << StringView(" ") << c << StringView(" ") << t.ent.hrbs << *t.ent.path;
            c = '+';
        }
    }
    os << StringView("\n");
    printBounds(s.params());

    os << indent() << StringView("{\n");
    incIndent();

    for (const auto& i : s.items()) {
        switch (i.data.tag()) {
            case ASTItem::TAG_Type: {
                os << indent() << StringView("type ") << i.name << StringView(";\n");
                break;
            }
            case ASTItem::TAG_Static: {
                auto& e = i.data.as_Static();
                handleStatic(ASTVisibility::makeBarePrivate(), i.name, e);
                break;
            }
            case ASTItem::TAG_Function: {
                auto& e = i.data.as_Function();
                handleFunction(ASTVisibility::makeBarePrivate(), i.name, e);
                break;
            }
            default: {
                break;
            }
        }
    }

    decIndent();
    os << indent() << StringView("}\n");
    os << StringView("\n");
}

void RustPrinter::handleStatic(const ASTVisibility& vis, const RcString& name, const ASTStatic& s) {
    os << indent() << vis;
    switch (s.sClass()) {
        case ASTStatic::CONST:
            os << StringView("const ");
            break;
        case ASTStatic::STATIC:
            os << StringView("static ");
            break;
        case ASTStatic::MUT:
            os << StringView("static mut ");
            break;
    }
    os << name;
    printParams(s.params());
    os << StringView(": ") << s.type();
    if (s.value()) {
        os << StringView(" = ");
        s.value()->visit(*this);
    }
    if (!s.params().bounds.empty()) {
        os << StringView("\n");
        printBounds(s.params());
        os << indent();
    }
    os << StringView(";\n");
}

void RustPrinter::handleFunction(const ASTVisibility& vis, const RcString& name, const ASTFunction& f) {
    os << indent();
    os << vis;
    if (f.isConst()) {
        os << StringView("const ");
    }
    if (f.isUnsafe()) {
        os << StringView("unsafe ");
    }
    if (f.isAsync()) {
        os << StringView("async ");
    }
    if (f.isGen()) {
        os << StringView("gen ");
    }
    if (f.abi() != ABI_RUST) {
        os << StringView("extern \"") << f.abi() << StringView("\" ");
    }
    os << StringView("fn ") << name;
    printParams(f.params());
    os << StringView("(");
    bool isFirst = true;
    for (size_t i = 0; i < f.args().size(); i++) {
        const auto& a = f.args()[i];
        if (!isFirst) {
            os << StringView(", ");
        }
        printAttrs(a.attrs);
        printPattern(a.pat, false);
        if (f.hasNamedVariadic() && i + 1 == f.args().size()) {
            os << StringView(": ...");
        } else {
            os << StringView(": ") << a.ty->printPretty();
        }
        isFirst = false;
    }
    if (f.isVariadic() && !f.hasNamedVariadic()) {
        if (!isFirst) {
            os << StringView(", ");
        }
        os << StringView("...");
    }
    os << StringView(")");
    if (!f.rettype()->isUnit()) {
        os << StringView(" -> ") << f.rettype()->printPretty();
    }

    if (f.code()) {
        os << StringView("\n");
        printBounds(f.params());

        os << indent();
        f.code()->visit(*this);
        os << StringView("\n");
    } else {
        printBounds(f.params());
        os << StringView(";\n");
    }
}

void RustPrinter::incIndent() {
    indentLevel++;
}

RepeatLitStr RustPrinter::indent() {
    return RepeatLitStr{"    ", indentLevel};
}

void RustPrinter::decIndent() {
    indentLevel--;
}

void DumpRust(const char* filename, const ASTCrate& crate) {
    auto& os = *outputFile(*crate.pool, filename);
    RustPrinter printer(os);
    printer.handleModule(crate.rootModule());
    os.finish();
}

void DumpASTNode(ZeroCopyOutput& os, const ASTExprNode& node) {
    RustPrinter printer(os);
    const_cast<ASTExprNode&>(node).visit(printer);
}

RustPrinter::RustPrinter(ZeroCopyOutput& os)
    : os(os)
    , indentLevel(0)
    , exprRoot(false)
{
}

template <typename... T>
void RustPrinter::visitWithParensIf(ASTExprNode* node) {
    if (isAny<T...>(*node)) {
        parenWrap(node);
    } else {
        ASTNodeVisitor::visit(node);
    }
}

auto RustPrinter::isConst() const -> bool {
    return true;
}

auto RustPrinter::visit(ASTExprNodeBlock& n) -> void {
    switch (n.blockType) {
        case ASTExprNodeBlock::Type::Bare:
            break;
        case ASTExprNodeBlock::Type::Unsafe:
            os << StringView("unsafe ");
            break;
        case ASTExprNodeBlock::Type::Const:
            os << StringView("const ");
            break;
    }
    if (n.label.name != RcString()) {
        os << StringView("'") << n.label << StringView(": ");
    }
    os << StringView("{");
    incIndent();
    if (n.localMod) {
        os << StringView("\n");
        os << indent() << StringView("// ANON: ") << n.localMod->path() << StringView("\n");
        handleModule(*n.localMod);
    }
    for (auto& child : n.nodes) {
        os << StringView("\n");
        if (child.node) {
            this->printAttrs(child.node->attrs());
        }
        os << indent();
        exprRoot = true;
        if (!child.node) {
            os << StringView("/* nil */");
        } else {
            ASTNodeVisitor::visit(child.node);
        }
        if (child.hasSemicolon) {
            os << StringView(";");
        }
    }
    os << StringView("\n");
    decIndent();
    os << indent() << StringView("}");
}

auto RustPrinter::visit(ASTExprNodeAsyncBlock& n) -> void {
    os << StringView("async ");
    if (n.isMove) {
        os << StringView("move ");
    }
    if (n.isUse) {
        os << StringView("use ");
    }
    ASTNodeVisitor::visit(n.inner);
}

auto RustPrinter::visit(ASTExprNodeGeneratorBlock& n) -> void {
    os << StringView("gen ");
    if (n.isMove) {
        os << StringView("move ");
    }
    ASTNodeVisitor::visit(n.inner);
}

auto RustPrinter::visit(ASTExprNodeTry& n) -> void {
    os << StringView("try ");
    ASTNodeVisitor::visit(n.inner);
}

auto RustPrinter::dumpToken(const Token& t) -> void {
    os << t.toStr() << StringView(" ");
}

auto RustPrinter::dumpTokentree(const TokenTree& tt) -> void {
    if (tt.isToken()) {
        dumpToken(tt.tok());
    } else {
        for (size_t i = 0; i < tt.size(); i++) {
            dumpTokentree(tt[i]);
        }
    }
}

auto RustPrinter::visit(ASTExprNodeMacro& n) -> void {
    exprRoot = false;
    os << n.path << StringView("!");
    if (n.ident != "") {
        os << StringView(" ");
        os << n.ident;
    }
    os << StringView(n.isBraced ? "{" : "(");
    dumpTokentree(n.tokens);
    os << StringView(n.isBraced ? "}" : ")");
}

auto RustPrinter::visit(ASTExprNodeAsm& n) -> void {
    os << StringView("asm!( \"") << n.text << StringView("\"");
    os << StringView(" :");
    for (auto& v : n.output) {
        os << StringView(" \"") << v.name << StringView("\" (");
        ASTNodeVisitor::visit(v.value);
        os << StringView("),");
    }
    os << StringView(" :");
    for (auto& v : n.input) {
        os << StringView(" \"") << v.name << StringView("\" (");
        ASTNodeVisitor::visit(v.value);
        os << StringView("),");
    }
    os << StringView(" :");
    for (const auto& v : n.clobbers) {
        os << StringView(" \"") << v << StringView("\",");
    }
    os << StringView(" :");
    for (const auto& v : n.flags) {
        os << StringView(" \"") << v << StringView("\",");
    }
    os << StringView(" )");
}

auto RustPrinter::visit(ASTExprNodeAsm2& n) -> void {
    os << StringView("asm!( ");
    for (const auto& l : n.lines) {
        l.fmt(os);
        os << StringView(", ");
    }
    for (auto& p : n.params) {
        switch (p.tag()) {
            case ASTAsmParam::TAG_Const: {
                auto& e = p.as_Const();
                os << StringView("const ");
                ASTNodeVisitor::visit(e);
                break;
            }
            case ASTAsmParam::TAG_Sym: {
                auto& e = p.as_Sym();
                os << StringView("sym ") << e;
                break;
            }
            case ASTAsmParam::TAG_Label: {
                auto& e = p.as_Label();
                os << StringView("label ");
                ASTNodeVisitor::visit(e.code);
                break;
            }
            case ASTAsmParam::TAG_RegSingle: {
                auto& e = p.as_RegSingle();
                os << e.dir << StringView("(") << e.spec << StringView(") ");
                ASTNodeVisitor::visit(e.val);
                break;
            }
            case ASTAsmParam::TAG_Reg: {
                auto& e = p.as_Reg();
                os << e.dir << StringView("(") << e.spec << StringView(") ");
                if (e.valIn) {
                    ASTNodeVisitor::visit(e.valIn);
                    if (e.valOut) {
                        os << StringView(" => ");
                    }
                }
                if (e.valOut) {
                    ASTNodeVisitor::visit(e.valOut);
                }
                break;
            }
        }
        os << StringView(", ");
    }
    if (n.options.any()) {
        n.options.fmt(os);
    }
    os << StringView(")");
}

auto RustPrinter::visit(ASTExprNodeFlow& n) -> void {
    exprRoot = false;
    switch (n.type) {
        case ASTExprNodeFlow::RETURN:
            os << StringView("return ");
            break;
        case ASTExprNodeFlow::TAILCALL:
            os << StringView("become ");
            break;
        case ASTExprNodeFlow::YIELD:
            os << StringView("yield ");
            break;
        case ASTExprNodeFlow::BREAK:
            os << StringView("break ");
            break;
        case ASTExprNodeFlow::CONTINUE:
            os << StringView("continue ");
            break;
        case ASTExprNodeFlow::YEET:
            os << StringView("do yeet ");
            break;
    }
    if (n.target.name != "") {
        os << StringView("'") << n.target << StringView(" ");
    }
    ASTNodeVisitor::visit(n.value);
}

auto RustPrinter::visit(ASTExprNodeLetBinding& n) -> void {
    exprRoot = false;
    os << StringView("let ");
    printPattern(n.pat, false);
    os << StringView(": ");
    printType(n.type);
    if (n.value) {
        os << StringView(" = ");
        ASTNodeVisitor::visit(n.value);
    }
    if (n.elseNode) {
        os << StringView(" else ");
        ASTNodeVisitor::visit(n.elseNode);
    }
    os << StringView(";");
}

auto RustPrinter::visit(ASTExprNodeAssign& n) -> void {
    exprRoot = false;
    ASTNodeVisitor::visit(n.slot);
    switch (n.op) {
        case ASTExprNodeAssign::NONE:
            os << StringView("  = ");
            break;
        case ASTExprNodeAssign::ADD:
            os << StringView(" += ");
            break;
        case ASTExprNodeAssign::SUB:
            os << StringView(" -= ");
            break;
        case ASTExprNodeAssign::MUL:
            os << StringView(" *= ");
            break;
        case ASTExprNodeAssign::DIV:
            os << StringView(" /= ");
            break;
        case ASTExprNodeAssign::MOD:
            os << StringView(" %= ");
            break;
        case ASTExprNodeAssign::AND:
            os << StringView(" &= ");
            break;
        case ASTExprNodeAssign::OR:
            os << StringView(" |= ");
            break;
        case ASTExprNodeAssign::XOR:
            os << StringView(" ^= ");
            break;
        case ASTExprNodeAssign::SHR:
            os << StringView(" >>= ");
            break;
        case ASTExprNodeAssign::SHL:
            os << StringView(" <<= ");
            break;
    }
    ASTNodeVisitor::visit(n.value);
}

auto RustPrinter::visit(ASTExprNodeCallPath& n) -> void {
    exprRoot = false;
    os << n.path;
    os << StringView("(");
    bool isFirst = true;
    for (auto& arg : n.args) {
        if (isFirst) {
            isFirst = false;
        } else {
            os << StringView(", ");
        }
        ASTNodeVisitor::visit(arg);
    }
    os << StringView(")");
}

auto RustPrinter::visit(ASTExprNodeCallMethod& n) -> void {
    exprRoot = false;
    visitWithParensIf<ASTExprNodeDeref, ASTExprNodeUniOp, ASTExprNodeCast, ASTExprNodeBinOp, ASTExprNodeAssign, ASTExprNodeMatch, ASTExprNodeIf>(n.val);
    os << StringView(".") << n.method;
    os << StringView("(");
    bool isFirst = true;
    for (auto& arg : n.args) {
        if (isFirst) {
            isFirst = false;
        } else {
            os << StringView(", ");
        }
        ASTNodeVisitor::visit(arg);
    }
    os << StringView(")");
}

auto RustPrinter::visit(ASTExprNodeCallObject& n) -> void {
    exprRoot = false;
    os << StringView("(");
    ASTNodeVisitor::visit(n.val);
    os << StringView(")(");
    bool isFirst = true;
    for (auto& arg : n.args) {
        if (isFirst) {
            isFirst = false;
        } else {
            os << StringView(", ");
        }
        ASTNodeVisitor::visit(arg);
    }
    os << StringView(")");
}

auto RustPrinter::visit(ASTExprNodeLoop& n) -> void {
    bool exprRoot = exprRoot;
    exprRoot = false;

    if (n.label.name != "") {
        os << StringView("'") << n.label << StringView(": ");
    }

    os << StringView("loop");

    if (exprRoot) {
        os << StringView("\n");
        os << indent();
    } else {
        os << StringView(" ");
    }

    ASTNodeVisitor::visit(n.code);
}

auto RustPrinter::visit(ASTExprNodeFor& n) -> void {
    bool exprRoot = exprRoot;
    exprRoot = false;

    if (n.label.name != "") {
        os << StringView("'") << n.label << StringView(": ");
    }
    os << StringView("for ");
    printPattern(n.pattern, true);
    os << StringView(" in ");
    ASTNodeVisitor::visit(n.value);

    if (exprRoot) {
        os << StringView("\n");
        os << indent();
    } else {
        os << StringView(" ");
    }

    ASTNodeVisitor::visit(n.code);
}

auto RustPrinter::visitIfletConditions(std::vector<ASTIfLetCondition>& conds) -> void {
    for (size_t i = 0; i < conds.size(); i++) {
        if (i != 0) {
            os << StringView(" && ");
        }
        if (conds[i].optPat) {
            os << StringView("let ");
            printPattern(*conds[i].optPat, true);
            os << StringView(" = ");
        }
        os << StringView("(");
        ASTNodeVisitor::visit(conds[i].value);
        os << StringView(")");
    }
}

auto RustPrinter::visit(ASTExprNodeWhile& n) -> void {
    bool exprRoot = exprRoot;
    exprRoot = false;

    if (n.label.name != "") {
        os << StringView("'") << n.label << StringView(": ");
    }

    os << StringView("while ");
    visitIfletConditions(n.conditions);
    if (exprRoot) {
        os << StringView("\n");
        os << indent();
    } else {
        os << StringView(" ");
    }

    ASTNodeVisitor::visit(n.code);
}

auto RustPrinter::visit(ASTExprNodeMatch& n) -> void {
    bool exprRoot = exprRoot;
    exprRoot = false;
    os << StringView("match ");
    ASTNodeVisitor::visit(n.val);

    if (exprRoot) {
        os << StringView("\n");
        os << indent() << StringView("{\n");
    } else {
        os << StringView(" {\n");
        incIndent();
    }

    for (auto& arm : n.arms) {
        os << indent();
        bool isFirst = true;
        for (const auto& pat : arm.patterns) {
            if (!isFirst) {
                os << StringView("|");
            }
            isFirst = false;
            printPattern(pat, true);
        }
        if (!arm.guard.empty()) {
            os << StringView(" if ");
            visitIfletConditions(arm.guard);
        }
        os << StringView(" => ");
        incIndent();
        ASTNodeVisitor::visit(arm.code);
        decIndent();
        os << StringView(",\n");
    }

    if (exprRoot) {
        os << indent() << StringView("}");
    } else {
        os << indent() << StringView("}");
        decIndent();
    }
}

auto RustPrinter::visit(ASTExprNodeIf& n) -> void {
    bool exprRoot = exprRoot;
    exprRoot = false;
    for (auto& arm : n.arms) {
        if (&arm != n.arms.data()) {
            if (exprRoot) {
                os << indent();
            }
            os << StringView("else ");
        }

        os << StringView("if ");
        visitIfletConditions(arm.conditions);

        bool isBlock = (cast<const ASTExprNodeBlock>(&*arm.body) != nullptr);
        if (!isBlock) {
            os << StringView("{ ");
        }
        ASTNodeVisitor::visit(arm.body);
        if (!isBlock) {
            os << StringView(" }");
        }
        if (exprRoot) {
            os << StringView("\n");
        }
    }
    if (n.elseNode) {
        if (exprRoot) {
            os << indent();
        }
        os << StringView("else");
        bool isBlock = (cast<const ASTExprNodeBlock>(&*n.elseNode) != nullptr);
        if (!isBlock) {
            os << StringView("{ ");
        }
        ASTNodeVisitor::visit(n.elseNode);
        if (!isBlock) {
            os << StringView(" }");
        }
    }
}

auto RustPrinter::visit(ASTExprNodeClosure& n) -> void {
    exprRoot = false;
    if (n.isMove) {
        os << StringView("move ");
    }
    if (n.isUse) {
        os << StringView("use ");
    }
    os << StringView("|");
    bool isFirst = true;
    for (const auto& arg : n.args) {
        if (!isFirst) {
            os << StringView(", ");
        }
        isFirst = false;
        printPattern(arg.first, false);
        os << StringView(": ");
        printType(arg.second);
    }
    os << StringView("| ->");
    printType(n.returnType);
    os << StringView(" { ");
    ASTNodeVisitor::visit(n.code);
    os << StringView(" }");
}

auto RustPrinter::visit(ASTExprNodeWildcardPattern& n) -> void {
    os << StringView("_");
}

auto RustPrinter::visit(ASTExprNodeInteger& n) -> void {
    exprRoot = false;
    switch (n.datatype) {
        case CORETYPE_INVAL:
            os << StringView("0x") << formatHex(n.value) << StringView("_/*INVAL*/");
            break;
        case CORETYPE_BOOL:
        case CORETYPE_STR:
            os << StringView("0x") << formatHex(n.value) << StringView("_/*bool/str*/");
            break;
        case CORETYPE_CHAR:
            if (n.value >= 0x20 && n.value < 128) {
                switch (n.value.truncateU64()) {
                    case '\'':
                        os << StringView("'\\''");
                        break;
                    case '\\':
                        os << StringView("'\\\\'");
                        break;
                    default:
                        os << StringView("'") << (char)n.value.truncateU64() << StringView("'");
                        break;
                }
            } else {
                os << StringView("'\\u{") << formatHex(n.value) << StringView("}'");
            }
            break;
        case CORETYPE_F16:
        case CORETYPE_F32:
        case CORETYPE_F64:
        case CORETYPE_F128:
            break;
        case CORETYPE_U8:
        case CORETYPE_U16:
        case CORETYPE_U32:
        case CORETYPE_U64:
        case CORETYPE_U128:
        case CORETYPE_UINT:
        case CORETYPE_ANY:
            os << StringView("0x") << formatHex(n.value);
            os << StringView("_") << coretypeName(n.datatype);
            break;
        case CORETYPE_I8:
        case CORETYPE_I16:
        case CORETYPE_I32:
        case CORETYPE_I64:
        case CORETYPE_I128:
        case CORETYPE_INT:
            os << n.value;
            os << StringView("_") << coretypeName(n.datatype);
            break;
    }
}

auto RustPrinter::visit(ASTExprNodeFloat& n) -> void {
    exprRoot = false;
    switch (n.datatype) {
        case CORETYPE_ANY:
            os << formatFloat128(n.value, FloatFormat::Default, std::numeric_limits<double>::max_digits10 + 1);
            break;
        case CORETYPE_F16:
        case CORETYPE_F32:
            os << formatFloat128(n.value, FloatFormat::Default, std::numeric_limits<float>::max_digits10 + 1);
            os << StringView("_") << coretypeName(n.datatype);
            break;
        case CORETYPE_F64:
            os << formatFloat128(n.value, FloatFormat::Default, std::numeric_limits<double>::max_digits10 + 1);
            os << StringView("_") << coretypeName(n.datatype);
            break;
        case CORETYPE_F128:
            os << formatFloat128(n.value, FloatFormat::Default, std::numeric_limits<double>::max_digits10 + 1);
            os << StringView("_") << coretypeName(n.datatype);
            break;
        default:
            break;
    }
}

auto RustPrinter::visit(ASTExprNodeBool& n) -> void {
    exprRoot = false;
    if (n.value) {
        os << StringView("true");
    } else {
        os << StringView("false");
    }
}

auto RustPrinter::visit(ASTExprNodeString& n) -> void {
    exprRoot = false;
    os << StringView("\"") << FmtEscaped(n.value) << StringView("\"");
}

auto RustPrinter::visit(ASTExprNodeByteString& n) -> void {
    exprRoot = false;
    os << StringView("b\"") << FmtEscaped(n.value) << StringView("\"");
}

auto RustPrinter::visit(ASTExprNodeCString& n) -> void {
    exprRoot = false;
    os << StringView("c\"") << FmtEscaped(n.value) << StringView("\"");
}

auto RustPrinter::visit(ASTExprNodeSuffixedLiteral& n) -> void {
    exprRoot = false;
    os << n.text;
}

auto RustPrinter::visit(ASTExprNodeStructLiteral& n) -> void {
    exprRoot = false;
    os << n.path << StringView(" {\n");
    incIndent();
    for (auto& i : n.values) {
        printAttrs(i.attrs);
        os << indent() << StringView("r#") << i.name << StringView(": ");
        ASTNodeVisitor::visit(i.value);
        os << StringView(",\n");
    }
    if (n.baseValue) {
        os << indent() << StringView(".. ");
        ASTNodeVisitor::visit(n.baseValue);
        os << StringView("\n");
    }
    os << indent() << StringView("}");
    decIndent();
}

auto RustPrinter::visit(ASTExprNodeStructLiteralPattern& n) -> void {
    exprRoot = false;
    os << n.path << StringView(" {\n");
    incIndent();
    for (auto& i : n.values) {
        printAttrs(i.attrs);
        os << indent() << StringView("r#") << i.name << StringView(": ");
        ASTNodeVisitor::visit(i.value);
        os << StringView(",\n");
    }
    os << indent() << StringView("..\n");
    os << indent() << StringView("}");
    decIndent();
}

auto RustPrinter::visit(ASTExprNodeArray& n) -> void {
    exprRoot = false;
    os << StringView("[");
    if (n.size) {
        ASTNodeVisitor::visit(n.values[0]);
        os << StringView("; ");
        ASTNodeVisitor::visit(n.size);
    } else {
        for (auto& item : n.values) {
            ASTNodeVisitor::visit(item);
            os << StringView(", ");
        }
    }
    os << StringView("]");
}

auto RustPrinter::visit(ASTExprNodeTuple& n) -> void {
    exprRoot = false;
    os << StringView("(");
    for (auto& item : n.values) {
        ASTNodeVisitor::visit(item);
        os << StringView(", ");
    }
    os << StringView(")");
}

auto RustPrinter::visit(ASTExprNodeNamedValue& n) -> void {
    exprRoot = false;
    os << n.path;
}

auto RustPrinter::visit(ASTExprNodeField& n) -> void {
    exprRoot = false;
    visitWithParensIf<ASTExprNodeDeref, ASTExprNodeUniOp, ASTExprNodeCast, ASTExprNodeBinOp, ASTExprNodeAssign, ASTExprNodeMatch, ASTExprNodeIf>(n.obj);
    os << StringView(".") << n.name;
}

auto RustPrinter::visit(ASTExprNodeIndex& n) -> void {
    exprRoot = false;
    visitWithParensIf<ASTExprNodeDeref, ASTExprNodeUniOp, ASTExprNodeCast, ASTExprNodeBinOp, ASTExprNodeAssign, ASTExprNodeMatch, ASTExprNodeIf>(n.obj);
    os << StringView("[");
    ASTNodeVisitor::visit(n.idx);
    os << StringView("]");
}

auto RustPrinter::visit(ASTExprNodeDeref& n) -> void {
    exprRoot = false;
    os << StringView("*(");
    ASTNodeVisitor::visit(n.value);
    os << StringView(")");
}

auto RustPrinter::visit(ASTExprNodeCast& n) -> void {
    exprRoot = false;
    os << StringView("(");
    ASTNodeVisitor::visit(n.value);
    os << StringView(") as ") << n.type;
}

auto RustPrinter::visit(ASTExprNodeTypeAnnotation& n) -> void {
    exprRoot = false;
    os << StringView("(");
    ASTNodeVisitor::visit(n.value);
    os << StringView(") : ") << n.type;
}

auto RustPrinter::visit(ASTExprNodeBinOp& n) -> void {
    exprRoot = false;
    auto* leftBinop = cast<ASTExprNodeBinOp>(n.left);
    if (!n.left) {
        os << StringView("/*null*/");
    } else if (leftBinop && leftBinop->type == n.type) {
        ASTNodeVisitor::visit(n.left);
    } else {
        visitWithParensIf<ASTExprNodeCast, ASTExprNodeBinOp>(n.left);
    }
    os << StringView(" ");
    switch (n.type) {
        case ASTExprNodeBinOp::CMPEQU:
            os << StringView("==");
            break;
        case ASTExprNodeBinOp::CMPNEQU:
            os << StringView("!=");
            break;
        case ASTExprNodeBinOp::CMPLT:
            os << StringView("<");
            break;
        case ASTExprNodeBinOp::CMPLTE:
            os << StringView("<=");
            break;
        case ASTExprNodeBinOp::CMPGT:
            os << StringView(">");
            break;
        case ASTExprNodeBinOp::CMPGTE:
            os << StringView(">=");
            break;
        case ASTExprNodeBinOp::BOOLAND:
            os << StringView("&&");
            break;
        case ASTExprNodeBinOp::BOOLOR:
            os << StringView("||");
            break;
        case ASTExprNodeBinOp::BITAND:
            os << StringView("&");
            break;
        case ASTExprNodeBinOp::BITOR:
            os << StringView("|");
            break;
        case ASTExprNodeBinOp::BITXOR:
            os << StringView("^");
            break;
        case ASTExprNodeBinOp::SHL:
            os << StringView("<<");
            break;
        case ASTExprNodeBinOp::SHR:
            os << StringView(">>");
            break;
        case ASTExprNodeBinOp::MULTIPLY:
            os << StringView("*");
            break;
        case ASTExprNodeBinOp::DIVIDE:
            os << StringView("/");
            break;
        case ASTExprNodeBinOp::MODULO:
            os << StringView("%");
            break;
        case ASTExprNodeBinOp::ADD:
            os << StringView("+");
            break;
        case ASTExprNodeBinOp::SUB:
            os << StringView("-");
            break;
        case ASTExprNodeBinOp::RANGE:
            os << StringView("..");
            break;
        case ASTExprNodeBinOp::RANGE_INC:
            os << StringView("...");
            break;
        case ASTExprNodeBinOp::PLACE_IN:
            os << StringView("<-");
            break;
    }
    os << StringView(" ");
    auto* rightBinop = cast<ASTExprNodeBinOp>(n.right);
    if (!n.right) {
        os << StringView("/*null*/");
    } else if (rightBinop && rightBinop->type != n.type) {
        parenWrap(n.right);
    } else {
        ASTNodeVisitor::visit(n.right);
    }
}

auto RustPrinter::visit(ASTExprNodeUniOp& n) -> void {
    exprRoot = false;
    switch (n.type) {
        case ASTExprNodeUniOp::NEGATE:
            os << StringView("-");
            break;
        case ASTExprNodeUniOp::INVERT:
            os << StringView("!");
            break;
        case ASTExprNodeUniOp::BOX:
            os << StringView("box ");
            break;
        case ASTExprNodeUniOp::REF:
            os << StringView("&");
            break;
        case ASTExprNodeUniOp::REFMUT:
            os << StringView("&mut ");
            break;
        case ASTExprNodeUniOp::RawBorrow:
            os << StringView("&raw const ");
            break;
        case ASTExprNodeUniOp::PinBorrow:
            os << StringView("&pin const ");
            break;
        case ASTExprNodeUniOp::PinBorrowMut:
            os << StringView("&pin mut ");
            break;
        case ASTExprNodeUniOp::RawBorrowMut:
            os << StringView("&raw mut ");
            break;
        case ASTExprNodeUniOp::QMARK:
            break;
        case ASTExprNodeUniOp::AWait:
        case ASTExprNodeUniOp::AWaitNext:
        case ASTExprNodeUniOp::USE:
            break;
    }

    bool wrap = isAny<ASTExprNodeBinOp, ASTExprNodeCast>(*n.value);
    if (wrap) {
        os << StringView("(");
    }
    ASTNodeVisitor::visit(n.value);
    if (wrap) {
        os << StringView(")");
    }
    switch (n.type) {
        case ASTExprNodeUniOp::QMARK:
            os << StringView("?");
            break;
        case ASTExprNodeUniOp::AWait:
            os << StringView(".await");
            break;
        case ASTExprNodeUniOp::AWaitNext:
            os << StringView(".await/*next*/");
            break;
        case ASTExprNodeUniOp::USE:
            os << StringView(".use");
            break;
        default:
            break;
    }
}

auto RustPrinter::visit(ASTExprNodeMacroDefinition& n) -> void {
    os << StringView("/* macro definition #") << n.definitionId << StringView(" */");
}

auto RustPrinter::parenWrap(ASTExprNode* node) -> void {
    os << StringView("(");
    ASTNodeVisitor::visit(node);
    os << StringView(")");
}
