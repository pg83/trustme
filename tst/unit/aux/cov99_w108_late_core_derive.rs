extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_derive(W108LateCore)]
pub fn late_core(_: TokenStream) -> TokenStream {
    "extern crate core as w108_late_core; use ::w108_late_core::marker::Copy as W108Copy;"
        .parse()
        .unwrap()
}
