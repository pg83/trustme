#pragma once

#include "parse_tokenstream.h"
#include "slice.h"

    class ASTAttribute;
    class ASTCrate;
    class ASTEnum;
    class ASTImplDef;
    class ASTItem;
    class ASTStruct;
    class ASTUnion;
    class ASTVisibility;

// Derive macros
extern ::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& name, const ASTStruct& i);
extern ::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& name, const ASTEnum& i);
extern ::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& name, const ASTUnion& i);

// Attribute macros
extern ::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath, const TokenTree& tt, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& itemName, const ASTItem& i);
extern ::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath, const TokenTree& tt, slice<const ASTAttribute> attrs, const ASTImplDef& i);

// Function-like macros
extern ::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath, const TokenTree& tt);
