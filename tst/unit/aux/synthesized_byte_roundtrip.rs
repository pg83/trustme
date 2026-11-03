extern crate proc_macro;
use proc_macro::{Delimiter, Group, Ident, Literal, Punct, Spacing, Span, TokenStream, TokenTree};

#[proc_macro]
pub fn produce(_: TokenStream) -> TokenStream {
    let literal = TokenTree::Literal(Literal::byte_string(&[0, 34, 92, 255]));
    let payload = TokenStream::from(literal);
    [
        TokenTree::Ident(Ident::new("echo", Span::call_site())),
        TokenTree::Punct(Punct::new('!', Spacing::Alone)),
        TokenTree::Group(Group::new(Delimiter::Parenthesis, payload)),
    ].into_iter().collect()
}

#[proc_macro]
pub fn echo(input: TokenStream) -> TokenStream { input }
