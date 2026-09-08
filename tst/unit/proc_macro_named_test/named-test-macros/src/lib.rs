// A procedural-macro crate defining attributes under the names of builtin
// attributes, as tokio-macros defines `main` and `test`.
use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn main(_args: TokenStream, item: TokenStream) -> TokenStream {
    item
}

#[proc_macro_attribute]
pub fn test(_args: TokenStream, item: TokenStream) -> TokenStream {
    item
}
