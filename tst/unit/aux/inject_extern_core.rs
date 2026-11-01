extern crate proc_macro;
use proc_macro::TokenStream;

#[proc_macro_attribute]
pub fn import_core(_: TokenStream, item: TokenStream) -> TokenStream {
    let mut output: TokenStream = "extern crate core as injected_core;".parse().unwrap();
    output.extend(item);
    output
}
