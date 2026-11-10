extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn identity(_attribute: TokenStream, input: TokenStream) -> TokenStream {
    input
}
