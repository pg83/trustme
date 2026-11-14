extern crate proc_macro;

use proc_macro::{Delimiter, TokenStream, TokenTree};
use std::io::Write;

fn token(output: &mut Vec<u8>, kind: u8, text: &str) {
    assert!(text.len() < 128);
    output.push(kind);
    output.push(text.len() as u8);
    output.extend_from_slice(text.as_bytes());
}

fn emit(output: &mut Vec<u8>, input: TokenStream) {
    for tree in input {
        match tree {
            TokenTree::Group(group) => {
                let (open, close) = match group.delimiter() {
                    Delimiter::Parenthesis => ("(", ")"),
                    Delimiter::Brace => ("{", "}"),
                    Delimiter::Bracket => ("[", "]"),
                    Delimiter::None => ("", ""),
                };
                if !open.is_empty() {
                    token(output, 1, open);
                }
                emit(output, group.stream());
                if !close.is_empty() {
                    token(output, 1, close);
                }
            }
            TokenTree::Ident(ident) => {
                let text = ident.to_string();
                token(output, 2, if text == "EmptyName" { "" } else { &text });
            }
            TokenTree::Punct(punct) => token(output, 1, &punct.as_char().to_string()),
            TokenTree::Literal(literal) => token(output, 12, &literal.to_string()),
        }
    }
}

#[proc_macro]
pub fn empty_assoc(input: TokenStream) -> TokenStream {
    let mut output = vec![10, 1];
    emit(&mut output, input);
    let mut stdout = std::io::stdout().lock();
    stdout.write_all(&output).unwrap();
    stdout.flush().unwrap();
    TokenStream::new()
}
