extern crate proc_macro;

use proc_macro::{TokenStream, TokenTree};

#[proc_macro_derive(Refused)]
pub fn refused(input: TokenStream) -> TokenStream {
    let name = input
        .into_iter()
        .filter_map(|tree| match tree {
            TokenTree::Ident(ident) if ident.to_string() != "struct" => Some(ident.to_string()),
            _ => None,
        })
        .next()
        .unwrap_or_default();
    panic!("{name} cannot derive Refused")
}
