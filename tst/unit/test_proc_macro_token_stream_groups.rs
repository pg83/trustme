extern crate proc_macro;

use proc_macro::{Delimiter, TokenStream, TokenTree};
use std::str::FromStr;

fn expect_ident(token: Option<TokenTree>, expected: &str) {
    match token {
        Some(TokenTree::Ident(ident)) => assert_eq!(ident.to_string(), expected),
        other => panic!("expected ident {:?}, got {:?}", expected, other),
    }
}

fn expect_empty_group(token: Option<TokenTree>, expected: Delimiter) {
    match token {
        Some(TokenTree::Group(group)) => {
            assert_eq!(group.delimiter(), expected);
            assert!(group.stream().is_empty());
        },
        other => panic!("expected {:?} group, got {:?}", expected, other),
    }
}

fn main() {
    let function = TokenStream::from_str("async fn process() {}").unwrap();
    assert_eq!(function.to_string(), "async fn process ( ) { }");
    let mut function = function.into_iter();
    expect_ident(function.next(), "async");
    expect_ident(function.next(), "fn");
    expect_ident(function.next(), "process");
    expect_empty_group(function.next(), Delimiter::Parenthesis);
    expect_empty_group(function.next(), Delimiter::Brace);
    assert!(function.next().is_none());

    let closure = TokenStream::from_str("async || { [] }").unwrap();
    assert_eq!(closure.to_string(), "async || { [ ] }");
    let mut closure = closure.into_iter();
    expect_ident(closure.next(), "async");
    assert!(matches!(closure.next(), Some(TokenTree::Punct(_))));
    assert!(matches!(closure.next(), Some(TokenTree::Punct(_))));
    match closure.next() {
        Some(TokenTree::Group(body)) => {
            assert_eq!(body.delimiter(), Delimiter::Brace);
            let mut body = body.stream().into_iter();
            expect_empty_group(body.next(), Delimiter::Bracket);
            assert!(body.next().is_none());
        },
        other => panic!("expected closure body group, got {:?}", other),
    }
    assert!(closure.next().is_none());

    assert_eq!(
        TokenStream::from_str("fn main() { assert_eq!(foo(), \"Hello, world!\"); }")
            .unwrap()
            .to_string(),
        "fn main ( ) { assert_eq ! ( foo ( ) , \"Hello, world!\" ) ; }",
    );
    assert_eq!(TokenStream::from_str("<T>").unwrap().to_string(), "< T >");

    assert!(TokenStream::from_str("(]").is_err());
    assert!(TokenStream::from_str("{").is_err());
}
