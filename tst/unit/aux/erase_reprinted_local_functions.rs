extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn erase(_: TokenStream, input: TokenStream) -> TokenStream {
    assert!(input.to_string().contains("selected"));
    "0u8".parse().unwrap()
}
