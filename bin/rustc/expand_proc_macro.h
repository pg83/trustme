#pragma once
#include "parse_tokenstream.h"
#include "slice.h"

namespace AST {
    class Attribute;
    class Crate;
    class Enum;
    class ImplDef;
    class Item;
    class Struct;
    class Union;
    class Visibility;
}

// Derive macros
extern ::std::unique_ptr<TokenStream> ProcMacro_Invoke(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& mac_path, slice<const AST::Attribute> attrs, const AST::Visibility& vis, const RcString& name, const ::AST::Struct& i);
extern ::std::unique_ptr<TokenStream> ProcMacro_Invoke(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& mac_path, slice<const AST::Attribute> attrs, const AST::Visibility& vis, const RcString& name, const ::AST::Enum& i);
extern ::std::unique_ptr<TokenStream> ProcMacro_Invoke(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& mac_path, slice<const AST::Attribute> attrs, const AST::Visibility& vis, const RcString& name, const ::AST::Union& i);

// Attribute macros
extern ::std::unique_ptr<TokenStream> ProcMacro_Invoke(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& mac_path, const TokenTree& tt, slice<const AST::Attribute> attrs, const AST::Visibility& vis, const RcString& item_name, const ::AST::Item& i);
extern ::std::unique_ptr<TokenStream> ProcMacro_Invoke(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& mac_path, const TokenTree& tt, slice<const AST::Attribute> attrs, const ::AST::ImplDef& i);

// Function-like macros
extern ::std::unique_ptr<TokenStream> ProcMacro_Invoke(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& mac_path, const TokenTree& tt);
