extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro]
pub fn number(_: TokenStream) -> TokenStream { "83u32".parse().unwrap() }

#[proc_macro_attribute]
pub fn passthrough(_: TokenStream, item: TokenStream) -> TokenStream { item }

#[proc_macro_derive(Marked)]
pub fn marked(_: TokenStream) -> TokenStream { TokenStream::new() }
