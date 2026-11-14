extern crate proc_macro;

use proc_macro::{TokenStream, TokenTree};
use std::io::Write;

#[proc_macro]
pub fn relay(input: TokenStream) -> TokenStream {
    if input.is_empty() {
        let mut output = std::io::stdout().lock();
        output
            .write_all(b"\x02\x05relay\x01\x01!\x01\x01{\x01\x01\\\x01\x01}")
            .unwrap();
        output.flush().unwrap();
    } else {
        let mut tokens = input.into_iter();
        match tokens.next().unwrap() {
            TokenTree::Punct(p) => assert_eq!(p.as_char(), '\\'),
            _ => panic!("expected backslash punctuation"),
        }
        assert!(tokens.next().is_none());
    }
    TokenStream::new()
}
