//@ edition: 2018
// derive_builder_macro_fork_arti (edition 2015) writes `ref` bindings in patterns
// that match through a reference, and arti's crates are edition 2024.
extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro]
pub fn first_len(_input: TokenStream) -> TokenStream {
    "fn first_len(v: &Option<String>) -> usize { match v { Some(ref s) => s.len(), None => 0 } }"
        .parse()
        .unwrap()
}
