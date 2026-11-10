extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn identity(arguments: TokenStream, expression: TokenStream) -> TokenStream {
    assert!(arguments.is_empty());
    expression
}
