extern crate proc_macro;

use proc_macro::{Literal, Span, TokenStream, TokenTree};

#[proc_macro]
pub fn call_site_debug(_input: TokenStream) -> TokenStream {
    TokenTree::from(Literal::string(&format!("{:?}", Span::call_site()))).into()
}
