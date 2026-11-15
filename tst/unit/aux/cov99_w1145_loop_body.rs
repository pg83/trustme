extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn without_block(_attr: TokenStream, _body: TokenStream) -> TokenStream {
    "Slot { 0: super let _retained = 7u32 }.0".parse().unwrap()
}
