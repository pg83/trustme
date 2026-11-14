extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn erase(_: TokenStream, item: TokenStream) -> TokenStream {
    assert_eq!(item.into_iter().next().unwrap().to_string(), "crate");
    TokenStream::new()
}
