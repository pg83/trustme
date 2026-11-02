extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro_derive(ObserveFields)]
pub fn observe_fields(input: TokenStream) -> TokenStream {
    let text = input.to_string();
    assert!(text.contains("small"));
    assert!(text.contains("callback"));
    "impl Shape { const OBSERVED: usize = core::mem::size_of::<Self>(); }".parse().unwrap()
}
