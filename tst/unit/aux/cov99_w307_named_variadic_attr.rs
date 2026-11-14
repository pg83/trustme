extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn erase(_: TokenStream, _: TokenStream) -> TokenStream {
    TokenStream::new()
}
