extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn keep_tree(_: TokenStream, input: TokenStream) -> TokenStream { input }
