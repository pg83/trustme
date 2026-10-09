//@ edition: 2015
// devise_codegen 0.4, an edition-2015 proc-macro crate, writes
// `use ::devise::ext::SpanDiagnosticExt;` into the items it derives.
extern crate proc_macro;

use proc_macro::TokenStream;

#[proc_macro]
pub fn root_path_call(_input: TokenStream) -> TokenStream {
    "{ use ::root_path_target::ext::seven; seven() + ::root_path_target::ext::seven() }".parse().unwrap()
}
