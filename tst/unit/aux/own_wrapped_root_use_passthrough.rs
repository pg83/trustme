extern crate proc_macro;

use proc_macro::{Delimiter, TokenStream, TokenTree};

#[proc_macro]
pub fn preserve(input: TokenStream) -> TokenStream {
    let mut outer = input.clone().into_iter();
    let item = match outer.next().unwrap() {
        TokenTree::Group(group) if matches!(group.delimiter(), Delimiter::None) => group,
        _ => panic!("item fragment lost its invisible group"),
    };
    assert!(outer.next().is_none());
    let mut tokens = item.stream().into_iter();
    assert_eq!(tokens.next().unwrap().to_string(), "use");
    match tokens.next().unwrap() {
        TokenTree::Group(group) => assert!(matches!(group.delimiter(), Delimiter::Brace)),
        _ => panic!("root import group was lost"),
    }
    input
}
