extern crate proc_macro;

use proc_macro::TokenStream;
use std::io::Write;

#[proc_macro]
pub fn write_end_of_stream(_: TokenStream) -> TokenStream {
    let mut out = std::io::stdout();
    out.write_all(&[0]).unwrap();
    out.flush().unwrap();
    TokenStream::new()
}
