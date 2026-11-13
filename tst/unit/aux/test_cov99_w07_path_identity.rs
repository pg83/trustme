//@ edition: 2021

extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn identity(_args: TokenStream, item: TokenStream) -> TokenStream {
    item
}
