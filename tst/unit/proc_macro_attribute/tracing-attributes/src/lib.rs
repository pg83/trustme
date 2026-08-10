extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn instrument(_attribute: TokenStream, _item: TokenStream) -> TokenStream {
    "fn instrument_was_invoked() -> u32 { 42 }".parse().unwrap()
}
