extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro]
pub fn declare_reader(_: TokenStream) -> TokenStream {
    "macro own_reader() { own_source() }".parse().unwrap()
}
