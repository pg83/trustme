extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn stop(_: TokenStream, _: TokenStream) -> TokenStream {
    std::panic::panic_any(37u16)
}
