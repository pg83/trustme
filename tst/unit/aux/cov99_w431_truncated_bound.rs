extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn truncated_bound(_input: TokenStream) -> TokenStream {
    let mut out = std::io::stdout();
    out.write_all(b"\x02\x06struct\x02\x06Broken\x01\x01<\x02\x01T\x01\x02:(\x02\x04Send\x01\x00").unwrap();
    out.flush().unwrap();
    "struct Broken<T: (Send)>".parse().unwrap()
}
