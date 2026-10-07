//@ edition: 2018
// darling_macro 0.14 (edition 2018) emits `use ::darling::ToTokens;` into the
// item it derives on, and derive_builder_core_fork_arti is edition 2015.
extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro]
pub fn writer_module(_input: TokenStream) -> TokenStream {
    "mod writer { use ::std::fmt::Write as _; pub fn hello() -> String { let mut out = String::new(); write!(out, \"hello\").unwrap(); out } }"
        .parse()
        .unwrap()
}
