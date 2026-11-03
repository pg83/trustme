extern crate proc_macro;
use proc_macro::{Literal, TokenStream, TokenTree};
#[proc_macro]
pub fn spelling(input: TokenStream) -> TokenStream {
    TokenTree::Literal(Literal::string(&input.to_string())).into()
}
