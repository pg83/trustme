extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn replace_assignment(_attribute: TokenStream, _input: TokenStream) -> TokenStream {
    "{ (first, second) = (17u8, 19u8); }".parse().unwrap()
}
