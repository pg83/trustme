//@ test-harness

extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn passthrough(_: TokenStream, input: TokenStream) -> TokenStream {
    input
}

#[test]
fn proc_macro_definitions_are_not_exported_by_the_test_crate() {}
