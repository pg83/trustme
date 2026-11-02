extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro]
pub fn emit(input: TokenStream) -> TokenStream { input }

#[proc_macro_attribute]
pub fn passthrough(_: TokenStream, item: TokenStream) -> TokenStream { item }

#[proc_macro_derive(Seen)]
pub fn seen(input: TokenStream) -> TokenStream {
    assert!(input.to_string().contains("callback"));
    TokenStream::new()
}
