extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn expand_to(attribute: TokenStream, _: TokenStream) -> TokenStream { attribute }

#[proc_macro_attribute]
pub fn discard(_: TokenStream, _: TokenStream) -> TokenStream { TokenStream::new() }

#[proc_macro_attribute]
pub fn passthrough(_: TokenStream, item: TokenStream) -> TokenStream { item }
