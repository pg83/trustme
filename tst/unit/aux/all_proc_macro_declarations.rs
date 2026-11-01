extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro]
pub fn number(_: TokenStream) -> TokenStream { "37u16".parse().unwrap() }

#[proc_macro_attribute]
pub fn passthrough(_: TokenStream, item: TokenStream) -> TokenStream { item }

#[proc_macro_derive(Mark)]
pub fn mark(_: TokenStream) -> TokenStream {
    "impl Label { pub fn value() -> u16 { 11 } }".parse().unwrap()
}
