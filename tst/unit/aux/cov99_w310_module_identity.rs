extern crate proc_macro;

#[proc_macro_attribute]
pub fn identity(_: proc_macro::TokenStream, item: proc_macro::TokenStream) -> proc_macro::TokenStream {
    item
}
