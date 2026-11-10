extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro]
pub fn preserve(input: TokenStream) -> TokenStream {
    input.to_string().parse().unwrap()
}
