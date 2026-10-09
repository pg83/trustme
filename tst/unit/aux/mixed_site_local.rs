// A function-like macro whose expansion declares a local with
// `Span::mixed_site()`, the way multiversion 0.9's dispatcher declares
// `current_fn`, and then uses the caller's tokens beside it.
extern crate proc_macro;

use proc_macro::{Delimiter, Group, Ident, Span, TokenStream, TokenTree};

fn mixed(stream: TokenStream, name: &str) -> TokenStream {
    stream
        .into_iter()
        .map(|token| match token {
            TokenTree::Ident(ident) if ident.to_string() == name => TokenTree::Ident(Ident::new(name, Span::mixed_site())),
            other => other,
        })
        .collect()
}

#[proc_macro]
pub fn add_hidden(input: TokenStream) -> TokenStream {
    let mut body = mixed("let value = 100u32; value +".parse().unwrap(), "value");
    body.extend([TokenTree::Group(Group::new(Delimiter::Parenthesis, input))]);
    TokenTree::Group(Group::new(Delimiter::Brace, body)).into()
}

#[proc_macro]
pub fn hidden_located_at_input(input: TokenStream) -> TokenStream {
    let at = input.into_iter().next().unwrap().span();
    let local = TokenTree::Ident(Ident::new("value", Span::call_site().located_at(at)));
    let mut body: TokenStream = "let value = 7u32;".parse().unwrap();
    body.extend([local]);
    TokenTree::Group(Group::new(Delimiter::Brace, body)).into()
}
