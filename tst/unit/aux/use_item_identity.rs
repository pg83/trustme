extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro]
pub fn echo(input: TokenStream) -> TokenStream { input }
