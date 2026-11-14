extern crate proc_macro;

use proc_macro::{Delimiter, TokenStream, TokenTree};

#[proc_macro_attribute]
pub fn check_const_impl(attribute: TokenStream, item: TokenStream) -> TokenStream {
    assert!(attribute.is_empty());
    let mut tokens = item.into_iter();
    for expected in ["impl", "const", "Marker", "for", "Subject"] {
        match tokens.next().unwrap() {
            TokenTree::Ident(word) => assert_eq!(word.to_string(), expected),
            _ => panic!("expected an identifier in the const impl header"),
        }
    }
    match tokens.next().unwrap() {
        TokenTree::Group(body) => {
            assert_eq!(body.delimiter(), Delimiter::Brace);
            assert!(body.stream().is_empty());
        }
        _ => panic!("expected the impl body"),
    }
    assert!(tokens.next().is_none());
    TokenStream::new()
}
