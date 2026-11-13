extern crate proc_macro;

use proc_macro::{Delimiter, TokenStream, TokenTree};

#[proc_macro_derive(KeepField)]
pub fn keep_field(input: TokenStream) -> TokenStream {
    let field = input.into_iter().find_map(|token| match token {
        TokenTree::Group(group) if group.delimiter() == Delimiter::Parenthesis => {
            Some(group.stream().to_string())
        }
        _ => None,
    }).unwrap();
    format!(
        "type KeptField = {}; impl Bounded {{ fn into_field(self) -> KeptField {{ self.0 }} }}",
        field
    ).parse().unwrap()
}
