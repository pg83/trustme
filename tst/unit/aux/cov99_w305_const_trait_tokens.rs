extern crate proc_macro;

use proc_macro::{Delimiter, TokenStream, TokenTree};

#[proc_macro_attribute]
pub fn check_const_trait(attribute: TokenStream, item: TokenStream) -> TokenStream {
    assert!(attribute.is_empty());
    let mut tokens = item.into_iter();
    assert_eq!(tokens.next().unwrap().to_string(), "const");
    assert_eq!(tokens.next().unwrap().to_string(), "trait");
    assert_eq!(tokens.next().unwrap().to_string(), "SerializedConstTrait");
    assert!(matches!(tokens.next(), Some(TokenTree::Group(group))
        if group.delimiter() == Delimiter::Brace && group.stream().is_empty()));
    assert!(tokens.next().is_none());
    TokenStream::new()
}
