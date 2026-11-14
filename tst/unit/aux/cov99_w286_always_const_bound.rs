extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn consume(_: TokenStream, item: TokenStream) -> TokenStream {
    assert!(item.to_string().contains("const Clone"));
    TokenStream::new()
}
