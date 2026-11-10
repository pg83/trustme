extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro]
pub fn receive(input: TokenStream) -> TokenStream {
    assert!(input.to_string().contains("Mark"));
    "43u16".parse().unwrap()
}
