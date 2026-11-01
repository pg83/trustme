extern crate proc_macro;

use proc_macro::{Delimiter, TokenStream, TokenTree};

fn byte_literal_count(tokens: TokenStream) -> usize {
    tokens.into_iter().map(|token| match token {
        TokenTree::Literal(literal) => usize::from(literal.to_string().starts_with("b\"")),
        TokenTree::Group(group) => byte_literal_count(group.stream()),
        _ => 0,
    }).sum()
}

#[proc_macro_derive(InspectBytes, attributes(bytes))]
pub fn inspect_bytes(input: TokenStream) -> TokenStream {
    let mut tokens = input.clone().into_iter();
    while let Some(token) = tokens.next() {
        if matches!(token, TokenTree::Ident(ref ident) if ident.to_string() == "pub") {
            match tokens.next().unwrap() {
                TokenTree::Group(group) => {
                    assert_eq!(group.delimiter(), Delimiter::Parenthesis);
                    let names: Vec<_> = group.stream().into_iter().filter_map(|token| match token {
                        TokenTree::Ident(ident) => Some(ident.to_string()),
                        _ => None,
                    }).collect();
                    assert_eq!(names, ["in", "crate", "outer"]);
                }
                _ => panic!("missing restricted visibility"),
            }
            assert_eq!(byte_literal_count(input), 1);
            return TokenStream::new();
        }
    }
    panic!("missing visibility");
}
