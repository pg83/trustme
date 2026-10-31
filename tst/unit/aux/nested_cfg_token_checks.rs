extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn raw_shape(_: TokenStream, input: TokenStream) -> TokenStream {
    let text = input.to_string();
    assert!(text.contains("cfg_attr"));
    assert!(text.contains("removed"));
    input
}

#[proc_macro_derive(OnlyLive)]
pub fn only_live(input: TokenStream) -> TokenStream {
    let text = input.to_string();
    assert!(!text.contains("removed"));
    assert!(!text.contains("Missing"));
    TokenStream::new()
}
