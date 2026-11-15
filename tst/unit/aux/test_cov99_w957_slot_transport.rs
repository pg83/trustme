extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn slot_witness(_: TokenStream) -> TokenStream {
    let tokens = "pub trait W957#slot { fn method ( & self ) ; }
                  pub struct W957#slot#vtable ;
                  pub fn witness ( ) -> fn ( & dyn W957#slot ) {
                      < dyn W957#slot as W957#slot > :: method
                  }";
    let mut bytes = Vec::new();
    for token in tokens.split_whitespace() {
        let class = if token.as_bytes()[0].is_ascii_alphabetic() { 2 } else { 1 };
        bytes.push(class);
        bytes.push(token.len() as u8);
        bytes.extend_from_slice(token.as_bytes());
    }
    let mut output = std::io::stdout();
    output.write_all(&bytes).unwrap();
    output.flush().unwrap();
    TokenStream::new()
}
