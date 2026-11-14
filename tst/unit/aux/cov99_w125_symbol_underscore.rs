extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn wildcard(_: TokenStream) -> TokenStream {
    std::io::stdout().write_all(b"\x01\x02_|").unwrap();
    "_".parse().unwrap()
}
