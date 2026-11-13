extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro]
pub fn echo(input: TokenStream) -> TokenStream {
    input
}

#[proc_macro_attribute]
pub fn keep(_: TokenStream, item: TokenStream) -> TokenStream {
    item
}

#[proc_macro_derive(Total)]
pub fn total(_: TokenStream) -> TokenStream {
    "impl Derived { fn total(&self) -> u16 { self.0 + 433 } }".parse().unwrap()
}
