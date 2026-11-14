extern crate proc_macro;

use proc_macro::{Delimiter, Group, Literal, TokenStream, TokenTree};

#[proc_macro]
pub fn cstring(_: TokenStream) -> TokenStream {
    let mut output: TokenStream = "cov99_w430_cstring_aux::discard!".parse().unwrap();
    let literal = TokenStream::from(TokenTree::Literal(Literal::c_string(c"w430")));
    output.extend([TokenTree::Group(Group::new(Delimiter::Brace, literal))]);
    output
}

#[proc_macro]
pub fn discard(input: TokenStream) -> TokenStream {
    assert_eq!(input.to_string(), "c\"w430\"");
    TokenStream::new()
}
