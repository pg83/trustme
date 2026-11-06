extern crate proc_macro;
use proc_macro::{Ident, Span, TokenStream, TokenTree};

#[proc_macro]
pub fn reject_legacy_name(_: TokenStream) -> TokenStream {
    let identifier = Ident::new("#type_id", Span::call_site());
    TokenStream::from(TokenTree::Ident(identifier))
}
