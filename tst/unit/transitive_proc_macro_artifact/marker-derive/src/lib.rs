extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_derive(Marker)]
pub fn derive_marker(_input: TokenStream) -> TokenStream {
    "impl Derived { pub fn value() -> u32 { 42 } }".parse().unwrap()
}
