extern crate proc_macro;

use proc_macro::{Delimiter, Spacing, TokenStream, TokenTree};
use std::io::Write;

fn record(bytes: &mut Vec<u8>, tag: u8, text: &str) {
    assert!(text.len() < 128);
    bytes.push(tag);
    bytes.push(text.len() as u8);
    bytes.extend_from_slice(text.as_bytes());
}

fn emit(bytes: &mut Vec<u8>, stream: TokenStream) {
    let mut tokens = stream.into_iter().peekable();
    while let Some(token) = tokens.next() {
        match token {
            TokenTree::Group(group) => {
                let delimiters = match group.delimiter() {
                    Delimiter::Parenthesis => ("(", ")"),
                    Delimiter::Brace => ("{", "}"),
                    Delimiter::Bracket => ("[", "]"),
                    Delimiter::None => ("", ""),
                };
                if !delimiters.0.is_empty() {
                    record(bytes, 1, delimiters.0);
                }
                emit(bytes, group.stream());
                if !delimiters.1.is_empty() {
                    record(bytes, 1, delimiters.1);
                }
            }
            TokenTree::Ident(ident) => {
                let name = ident.to_string();
                record(bytes, 2, if name == "Placeholder" { "future#w1419_cleanup" } else { &name });
            }
            TokenTree::Punct(punct) => {
                let mut text = punct.as_char().to_string();
                let mut joint = punct.spacing() == Spacing::Joint;
                while joint {
                    if let Some(TokenTree::Punct(next)) = tokens.peek() {
                        text.push(next.as_char());
                        joint = next.spacing() == Spacing::Joint;
                        tokens.next();
                    } else {
                        break;
                    }
                }
                record(bytes, 1, &text);
            }
            TokenTree::Literal(literal) => record(bytes, 12, &literal.to_string()),
        }
    }
}

#[proc_macro]
pub fn future_name(input: TokenStream) -> TokenStream {
    let mut bytes = vec![10, 1];
    emit(&mut bytes, input);
    let mut output = std::io::stdout().lock();
    output.write_all(&bytes).unwrap();
    output.flush().unwrap();
    TokenStream::new()
}
