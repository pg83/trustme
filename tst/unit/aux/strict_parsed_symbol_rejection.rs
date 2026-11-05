extern crate proc_macro;
use proc_macro::{Delimiter, Group, Ident, Punct, Spacing, Span, TokenStream, TokenTree};

fn reject(source: &str) -> TokenStream {
    match source.parse::<TokenStream>() {
        Err(_) => "compile_error!(\"invalid parsed punctuation rejected\");".parse().unwrap(),
        Ok(tokens) => [
            TokenTree::Ident(Ident::new("stringify", Span::call_site())),
            TokenTree::Punct(Punct::new('!', Spacing::Alone)),
            TokenTree::Group(Group::new(Delimiter::Parenthesis, tokens)),
        ].into_iter().collect(),
    }
}

#[proc_macro]
pub fn backslash(_: TokenStream) -> TokenStream { reject("\\") }

#[proc_macro]
pub fn backtick(_: TokenStream) -> TokenStream { reject("`") }
