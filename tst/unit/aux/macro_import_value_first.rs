extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro]
pub fn parcel(_: TokenStream) -> TokenStream { "61u32".parse().unwrap() }

#[proc_macro]
pub fn first_only(_: TokenStream) -> TokenStream { "13u32".parse().unwrap() }
