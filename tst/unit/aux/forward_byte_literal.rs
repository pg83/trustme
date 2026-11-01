extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro]
pub fn keep_literal(input: TokenStream) -> TokenStream { input }
