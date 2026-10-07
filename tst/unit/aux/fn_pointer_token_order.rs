// pwd-grp's `#[derive(Deftly)]` struct has fields like
// `unsafe extern "C" fn(c_int) -> c_long`, and derive-deftly reads them with syn,
// which wants `unsafe`, then `extern "C"`, then `fn`.
extern crate proc_macro;
use proc_macro::{TokenStream, TokenTree};

#[proc_macro_derive(FieldOrder)]
pub fn field_order(input: TokenStream) -> TokenStream {
    let words: Vec<String> = input
        .into_iter()
        .filter_map(|tree| match tree {
            TokenTree::Group(group) => Some(group.stream()),
            _ => None,
        })
        .last()
        .unwrap()
        .into_iter()
        .map(|tree| tree.to_string())
        .collect();
    let at = |word: &str| words.iter().position(|w| w == word).unwrap();
    let ordered = at("unsafe") < at("extern") && at("extern") < at("fn") && words.iter().any(|w| w.starts_with('(') && w.contains("..."));
    format!("const FIELD_ORDER: bool = {ordered};").parse().unwrap()
}
