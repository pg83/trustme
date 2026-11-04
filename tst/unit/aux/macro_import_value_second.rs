extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro]
pub fn parcel(_: TokenStream) -> TokenStream { "67u32".parse().unwrap() }
