extern crate proc_macro;

#[proc_macro_attribute]
pub fn keep(
    _args: proc_macro::TokenStream,
    input: proc_macro::TokenStream,
) -> proc_macro::TokenStream {
    input
}
