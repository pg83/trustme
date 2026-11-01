extern crate proc_macro;
use proc_macro::TokenStream;
#[proc_macro_derive(Mark, attributes(note))]
pub fn mark(_: TokenStream) -> TokenStream {
    "impl crate::Marked for crate::Tagged { fn number() -> u16 { 47 } }".parse().unwrap()
}
