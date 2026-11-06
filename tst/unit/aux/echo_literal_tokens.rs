extern crate proc_macro;

#[proc_macro]
pub fn echo(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    input
}
