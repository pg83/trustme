// pin-project's shape: an attribute macro re-emits its item with a derive
// named by an absolute path and its own arguments in a helper attribute; the
// derive spans a parameter with an argument's token and uses it at its call
// site.
extern crate proc_macro;

use proc_macro::{Delimiter, Group, Ident, Span, TokenStream, TokenTree};

#[proc_macro_attribute]
pub fn with_replace(arguments: TokenStream, item: TokenStream) -> TokenStream {
    let mut output: TokenStream = "#[derive(::replace_derive::Replace)]".parse().unwrap();
    output.extend("#".parse::<TokenStream>().unwrap());
    let mut helper: TokenStream = "replace".parse().unwrap();
    helper.extend([TokenTree::Group(Group::new(Delimiter::Parenthesis, arguments))]);
    output.extend([TokenTree::Group(Group::new(Delimiter::Bracket, helper))]);
    output.extend(item);
    output
}

#[proc_macro_derive(Replace, attributes(replace))]
pub fn derive_replace(item: TokenStream) -> TokenStream {
    let tokens: Vec<TokenTree> = item.into_iter().collect();
    let mut argument_span = None;
    let mut name = None;
    for (i, token) in tokens.iter().enumerate() {
        if let TokenTree::Group(group) = token {
            let inner: Vec<TokenTree> = group.stream().into_iter().collect();
            if let (Some(TokenTree::Ident(word)), Some(TokenTree::Group(arguments))) = (inner.first(), inner.get(1)) {
                if word.to_string() == "replace" {
                    argument_span = arguments.stream().into_iter().next().map(|token| token.span());
                }
            }
        }
        if let TokenTree::Ident(word) = token {
            if word.to_string() == "struct" {
                name = tokens.get(i + 1).map(|token| token.to_string());
            }
        }
    }
    let mut parameters: TokenStream = "self,".parse().unwrap();
    parameters.extend([TokenTree::Ident(Ident::new("__replacement", argument_span.unwrap()))]);
    parameters.extend(": u32".parse::<TokenStream>().unwrap());
    let body = TokenStream::from(TokenTree::Ident(Ident::new("__replacement", Span::call_site())));
    let mut function: TokenStream = "pub fn replace".parse().unwrap();
    function.extend([TokenTree::Group(Group::new(Delimiter::Parenthesis, parameters))]);
    function.extend("-> u32".parse::<TokenStream>().unwrap());
    function.extend([TokenTree::Group(Group::new(Delimiter::Brace, body))]);
    let mut output: TokenStream = format!("impl {}", name.unwrap()).parse().unwrap();
    output.extend([TokenTree::Group(Group::new(Delimiter::Brace, function))]);
    output
}
