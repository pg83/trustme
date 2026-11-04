extern crate proc_macro;

#[proc_macro]
pub fn render(input: proc_macro::TokenStream) -> proc_macro::TokenStream {
    proc_macro::TokenTree::Literal(proc_macro::Literal::string(&input.to_string())).into()
}
