extern crate proc_macro;

use proc_macro::{Delimiter, TokenStream, TokenTree};

#[proc_macro_attribute]
pub fn check(_attr: TokenStream, input: TokenStream) -> TokenStream {
    let group = match input.into_iter().last() {
        Some(TokenTree::Group(group)) => group,
        _ => panic!("expected an impl body"),
    };
    assert_eq!(group.delimiter(), Delimiter::Brace);
    let mut body = group.stream().into_iter();
    assert!(matches!(body.next(), Some(TokenTree::Ident(name)) if name.to_string() == "default"));
    assert!(matches!(body.next(), Some(TokenTree::Ident(name)) if name.to_string() == "fn"));
    TokenStream::new()
}
