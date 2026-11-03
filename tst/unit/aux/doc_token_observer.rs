extern crate proc_macro;
use proc_macro::{TokenStream, TokenTree};

fn docs(input: TokenStream, found: &mut Vec<String>) {
    for token in input {
        if let TokenTree::Group(group) = token {
            let mut tokens = group.stream().into_iter();
            if let Some(TokenTree::Ident(name)) = tokens.next() {
                if name.to_string() == "doc" {
                    if let Some(TokenTree::Punct(equal)) = tokens.next() {
                        if equal.as_char() == '=' {
                            if let Some(TokenTree::Literal(value)) = tokens.next() {
                                found.push(value.to_string());
                            }
                        }
                    }
                }
            }
            docs(group.stream(), found);
        }
    }
}

#[proc_macro_attribute]
pub fn observed_docs(_: TokenStream, item: TokenStream) -> TokenStream {
    let mut found = Vec::new();
    docs(item.clone(), &mut found);
    for marker in ["outer orchard", "inner lantern", "field river", "method mountain"] {
        assert!(found.iter().any(|text| text.contains(marker)), "missing {} in {:?}", marker, found);
    }
    item
}

#[proc_macro]
pub fn discard(_: TokenStream) -> TokenStream {
    "73u32".parse().unwrap()
}
