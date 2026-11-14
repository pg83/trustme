extern crate proc_macro;

use proc_macro::{Delimiter, Group, Ident, Punct, Spacing, Span, TokenStream, TokenTree};

#[proc_macro]
pub fn adjacent_slashes(_: TokenStream) -> TokenStream {
    let slashes = TokenStream::from_iter([
        TokenTree::Punct(Punct::new('/', Spacing::Joint)),
        TokenTree::Punct(Punct::new('/', Spacing::Alone)),
    ]);
    TokenStream::from_iter([
        TokenTree::Ident(Ident::new("accept_slashes", Span::call_site())),
        TokenTree::Punct(Punct::new('!', Spacing::Alone)),
        TokenTree::Group(Group::new(Delimiter::Parenthesis, slashes)),
    ])
}
