extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_derive(ObserveAssignment)]
pub fn observe(input: TokenStream) -> TokenStream {
    let text = input.to_string();
    assert!(text.contains("first") && text.contains("second"));
    TokenStream::new()
}
