extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro]
pub fn make_value(_: TokenStream) -> TokenStream { "11u16".parse().unwrap() }

#[proc_macro_attribute]
pub fn replace_function(_: TokenStream, _: TokenStream) -> TokenStream {
    "fn attributed() -> u16 { 17 }".parse().unwrap()
}

#[proc_macro_derive(DeriveMark)]
pub fn derive_mark(_: TokenStream) -> TokenStream {
    "impl crate::Mark for crate::Carrier { const VALUE: u16 = 31; }".parse().unwrap()
}
