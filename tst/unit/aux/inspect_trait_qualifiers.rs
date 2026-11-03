extern crate proc_macro;
use proc_macro::{TokenStream, TokenTree};
#[proc_macro_attribute]
pub fn qualifiers(_: TokenStream, input: TokenStream) -> TokenStream {
    let mut mask = 0u8;
    for token in input {
        if let TokenTree::Ident(name) = token {
            match name.to_string().as_str() { "unsafe" => mask |= 1, "auto" => mask |= 2, _ => {} }
        }
    }
    format!("const QUALIFIERS: u8 = {};", mask).parse().unwrap()
}
