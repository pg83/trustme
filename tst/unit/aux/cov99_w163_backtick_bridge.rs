extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn seed(_: TokenStream) -> TokenStream {
    let mut output = std::io::stdout().lock();
    output
        .write_all(b"\x02\x07consume\x01\x01!\x01\x01{\x01\x01`\x01\x01}")
        .unwrap();
    output.flush().unwrap();
    TokenStream::new()
}

#[proc_macro]
pub fn consume(input: TokenStream) -> TokenStream {
    assert_eq!(input.to_string(), "`");
    TokenStream::new()
}
