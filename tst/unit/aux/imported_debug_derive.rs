// A derive macro named like a built-in one, as derive_more's `Debug`, `Eq` and
// `PartialEq` are.
extern crate proc_macro;

use proc_macro::{TokenStream, TokenTree};

#[proc_macro_derive(Debug)]
pub fn debug(input: TokenStream) -> TokenStream {
    let tokens: Vec<TokenTree> = input.into_iter().collect();
    let mut name = None;
    for (i, token) in tokens.iter().enumerate() {
        if let TokenTree::Ident(keyword) = token {
            if keyword.to_string() == "struct" {
                name = Some(tokens[i + 1].to_string());
            }
        }
    }
    format!(
        "impl {} {{ fn derived_by() -> &'static str {{ \"imported\" }} }}",
        name.expect("a struct")
    )
    .parse()
    .unwrap()
}
