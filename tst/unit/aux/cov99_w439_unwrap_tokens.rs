extern crate proc_macro;

use proc_macro::{Delimiter, Group, TokenStream, TokenTree};

#[proc_macro]
pub fn unwrap_tokens(input: TokenStream) -> TokenStream {
    let body = match input.into_iter().next() {
        Some(TokenTree::Group(group)) => group.stream(),
        _ => panic!("expected an expression group"),
    };
    let mut output: TokenStream = "stringify!".parse().unwrap();
    output.extend(TokenStream::from(TokenTree::Group(Group::new(
        Delimiter::Parenthesis,
        body,
    ))));
    output
}
