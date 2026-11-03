extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro_derive(ObserveArray)]
pub fn observe_array(input: TokenStream) -> TokenStream {
    assert!(input.to_string().contains("bytes"));
    TokenStream::new()
}
