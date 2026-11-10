extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro]
pub fn observe(input: TokenStream) -> TokenStream {
    let text = input.to_string();
    assert!(text.contains("kept"));
    assert!(text.contains("original"));
    assert!(text.contains("59"));
    "fn observed() -> u16 { 59 }".parse().unwrap()
}
