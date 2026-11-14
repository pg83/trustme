extern crate proc_macro;
use proc_macro::{Delimiter, TokenStream, TokenTree};

#[proc_macro_attribute]
pub fn inspect(_attribute: TokenStream, item: TokenStream) -> TokenStream {
    let mut tokens = item.into_iter();
    assert!(matches!(tokens.next(), Some(TokenTree::Punct(p)) if p.as_char() == '#'));
    let group = match tokens.next() {
        Some(TokenTree::Group(group)) => group,
        _ => panic!("expected the retained attribute"),
    };
    assert_eq!(group.delimiter(), Delimiter::Bracket);
    let mut path = group.stream().into_iter();
    for expected in [':', ':'] {
        assert!(matches!(path.next(), Some(TokenTree::Punct(p)) if p.as_char() == expected));
    }
    "fn leading_attribute_seen() -> u8 { 31 }".parse().unwrap()
}
