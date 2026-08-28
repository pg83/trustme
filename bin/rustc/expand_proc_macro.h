#pragma once

#include "slice.h"
#include "parse_tokenstream.h"

class ASTAttribute;
struct WireBoard;
class ASTCrate;
class ASTEnum;
class ASTImplDef;
class ASTItem;
class ASTStruct;
class ASTUnion;
class ASTVisibility;

std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& name, const ASTStruct& i);
std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& name, const ASTEnum& i);
std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& name, const ASTUnion& i);

std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath, const TokenTree& tt, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& itemName, const ASTItem& i);
std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath, const TokenTree& tt, slice<const ASTAttribute> attrs, const ASTImplDef& i);

std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath, const TokenTree& tt);
