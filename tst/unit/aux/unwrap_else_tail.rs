extern crate proc_macro;

use proc_macro::{Delimiter, TokenStream, TokenTree};

#[proc_macro_attribute]
pub fn tail(_: TokenStream, input: TokenStream) -> TokenStream {
    let mut tokens = input.into_iter();
    let TokenTree::Group(group) = tokens.next().unwrap() else { panic!("block required") };
    assert_eq!(group.delimiter(), Delimiter::Brace);
    assert!(tokens.next().is_none());
    group.stream()
}
