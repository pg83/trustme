extern crate proc_macro;

use proc_macro::{Delimiter, Group, TokenStream, TokenTree};

#[proc_macro]
pub fn log_tokens(input: TokenStream) -> TokenStream {
    let body = match input.into_iter().next() {
        Some(TokenTree::Group(group)) => group.stream(),
        _ => panic!("expected an expression group"),
    };
    let mut block: TokenStream = "log_syntax!".parse().unwrap();
    block.extend(TokenStream::from(TokenTree::Group(Group::new(
        Delimiter::Parenthesis,
        body,
    ))));
    block.extend("; 7usize".parse::<TokenStream>().unwrap());
    TokenStream::from(TokenTree::Group(Group::new(Delimiter::Brace, block)))
}
