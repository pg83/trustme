extern crate proc_macro;
use proc_macro::{Delimiter, Group, Ident, Literal, Punct, Spacing, Span, TokenStream, TokenTree};

fn classify(source: &str) -> TokenStream {
    match source.parse::<TokenStream>() {
        Err(_) => TokenTree::Literal(Literal::string("rejected")).into(),
        Ok(tokens) => [
            TokenTree::Ident(Ident::new("stringify", Span::call_site())),
            TokenTree::Punct(Punct::new('!', Spacing::Alone)),
            TokenTree::Group(Group::new(Delimiter::Parenthesis, tokens)),
        ].into_iter().collect(),
    }
}

#[proc_macro]
pub fn backslash(_: TokenStream) -> TokenStream { classify("\\") }

#[proc_macro]
pub fn backtick(_: TokenStream) -> TokenStream { classify("`") }

#[proc_macro]
pub fn legal(_: TokenStream) -> TokenStream { classify("+") }
